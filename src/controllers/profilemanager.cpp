#include <optional>
#include "core/settings_app.h"
#include "profilemanager.h"
#include "steamheaterpolicy.h"
#include "../core/drinktypes.h"
#include "../core/settings.h"
#include "../core/settings_brew.h"
#include "../core/settings_dye.h"
#include "../core/settings_calibration.h"
#include "../core/profilestorage.h"
#include "../history/coffeebagstorage.h"
#include "../core/yieldspec.h"
#include "../ble/de1device.h"
#include "../ble/protocol/de1characteristics.h"
#include "../machine/machinestate.h"
#include "../profile/recipegenerator.h"
#include "../profile/recipeanalyzer.h"
#include "../profile/profilesavehelper.h"
#include "../profile/temperaturedisplay.h"
#include "../ai/shotsummarizer.h"
#include "../ai/profileshapeindex.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QQmlEngine>
#include <QJSEngine>
#include <algorithm>
#include <cmath>
#include <tuple>

#ifndef Q_OS_WIN
#include <dlfcn.h>   // For dladdr() to resolve caller symbols
#include <unwind.h>  // For _Unwind_Backtrace stack walking

// Helper for stack unwinding
struct BacktraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg) {
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        }
        *state->current++ = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
}

static size_t captureBacktrace(void** buffer, size_t maxFrames) {
    BacktraceState state = {buffer, buffer + maxFrames};
    _Unwind_Backtrace(unwindCallback, &state);
    return state.current - buffer;
}
#endif

// Auto-retry constants for failed profile uploads (see profilemanager.h for
// the full design note). The backoff is 1s, 2s, 4s, 8s between the 4 retries,
// then give up after the 5th total attempt and surface the communication
// failure to the user. The cap of 8s keeps the total wall-clock before the
// dialog shows down to ~15s of transient BLE trouble; past that, the DE1
// very likely needs to be power-cycled.
static constexpr int kUploadRetryBaseMs = 1000;
static constexpr int kUploadRetryMaxMs = 8000;
static constexpr int kMaxUploadRetryAttempts = 5;

// Reasons returned by DE1Device::profileUploaded(false, reason) that should
// NOT trigger an auto-retry. The rest (frame sequence mismatch, ACK timeout)
// are treated as retryable.
//
// We use startsWith() rather than exact equality so DE1Device can include
// variable details after a stable prefix — for example "frame sequence
// mismatch (expected [0x00, 0x01], got [0x00, 0x00])" carries the hex
// payload in the same string. The exact retryable/non-retryable prefix
// text is locked down by tst_profileupload.cpp's `.at(1).toString()`
// assertions, so any future rename of a reason string in
// finishProfileUpload() will break those tests loudly before it can
// silently flip classification here.
static bool isRetryableUploadFailure(const QString& reason) {
    // Superseded: a newer upload is already in flight — let it own the outcome.
    if (reason.startsWith(QStringLiteral("superseded"))) return false;
    // Queue cleared: a shot/steam/hot-water just started, clearing the queue
    // intentionally. The next uploadCurrentProfile() will re-arm.
    if (reason.startsWith(QStringLiteral("command queue cleared"))) return false;
    // BLE disconnect: the reconnect path (initialSettingsComplete ->
    // applyAllSettings -> uploadCurrentProfile) already re-uploads when the
    // link comes back. Retrying on a timer would race with that.
    if (reason.startsWith(QStringLiteral("BLE disconnect"))) return false;
    // Firmware flash: DE1Device dropped the call because a firmware update is in
    // progress. The reconnect path re-uploads once the flash completes and the
    // DE1 reconnects — no timer retry needed, and it would just flood the log.
    if (reason.startsWith(QStringLiteral("firmware flash"))) return false;
    return true;
}


ProfileManager *ProfileManager::s_qmlInstance = nullptr;

void ProfileManager::setQmlInstance(ProfileManager *instance)
{
    s_qmlInstance = instance;
}

ProfileManager *ProfileManager::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    if (!s_qmlInstance) {
        // Reached only if QML resolves the singleton before main.cpp published the instance.
        // Name the missing call: the symptom otherwise is every profile-related binding in the
        // UI reading as undefined, which looks like a dozen unrelated bugs rather than one
        // missing line.
        qCritical("ProfileManager: QML asked for the singleton before "
                  "ProfileManager::setQmlInstance() was called. Publish the instance before "
                  "QQmlEngine::load().");
        return nullptr;
    }
    // No per-engine state here, so no second-engine guard — same reasoning as MainController.
    // The engine would otherwise take ownership of an object MainController owns and delete it.
    QJSEngine::setObjectOwnership(s_qmlInstance, QJSEngine::CppOwnership);
    return s_qmlInstance;
}

ProfileManager::ProfileManager(Settings* settings, DE1Device* device,
                               MachineState* machineState,
                               ProfileStorage* profileStorage,
                               SteamHeaterPolicy* steamHeaterPolicy,
                               QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_device(device)
    , m_machineState(machineState)
    , m_steamHeaterPolicy(steamHeaterPolicy)
    , m_profileStorage(profileStorage)
{
    // Retry pending profile upload when machine reaches Idle, Ready, Sleep, or
    // Heating — phases where it's safe to write a new profile to the DE1.
    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged, this, [this]() {
            if (!m_profileUploadPending) return;
            auto phase = m_machineState->phase();
            if (phase == MachineState::Phase::Disconnected) {
                qDebug() << "Clearing pending profile upload: device disconnected";
                m_profileUploadPending = false;
                return;
            }
            if (phase == MachineState::Phase::Idle || phase == MachineState::Phase::Ready ||
                phase == MachineState::Phase::Sleep || phase == MachineState::Phase::Heating) {
                qDebug() << "Retrying pending profile upload now that phase is" << m_machineState->phaseString();
                uploadCurrentProfile();
            }
        });
    }

    // Auto-retry on failed profile uploads. When DE1Device emits a failure
    // with a retryable reason, arm m_profileUploadRetryTimer with exponential
    // backoff (capped at 8s). After kMaxUploadRetryAttempts consecutive
    // failures, give up and set m_de1CommunicationFailure so the UI surfaces
    // a "power-cycle the DE1" dialog. Success — or any non-retryable reason
    // like "superseded" — resets the counter.
    m_profileUploadRetryTimer.setSingleShot(true);
    connect(&m_profileUploadRetryTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "ProfileManager: retrying failed profile upload (attempt"
                 << m_profileUploadRetryAttempts << "of" << kMaxUploadRetryAttempts
                 << "— last failure:" << m_lastUploadFailureReason << ")";
        // Timer is no longer active (it's firing) — the retry indicator
        // should clear for this instant and re-arm only if the retry also
        // fails with a retryable reason.
        updateProfileUploadRetrying();
        uploadCurrentProfile();
    });

    if (m_device) {
        connect(m_device, &DE1Device::profileUploaded, this,
                [this](bool success, const QString& reason) {
            // Clear the in-flight gate so the next uploadCurrentProfile() call can proceed.
            // If a newer profile change arrived while the upload was in flight, trigger it now.
            m_uploadInFlight = false;
            bool hadPending = m_uploadPendingAfterInFlight;
            m_uploadPendingAfterInFlight = false;

            if (success) {
                // A successful upload clears all retry state. If a prior
                // communication-failure dialog is still up, leave it — the
                // user needs to explicitly acknowledge — but restore the
                // ability to arm a fresh retry if this succeeding upload is
                // followed by another failure.
                m_profileUploadRetryTimer.stop();
                m_profileUploadRetryAttempts = 0;
                m_lastUploadFailureReason.clear();
                updateProfileUploadRetrying();
                if (hadPending) {
                    uploadCurrentProfile();
                }
                return;
            }
            if (!isRetryableUploadFailure(reason)) {
                // Not a retry condition — don't bump the counter. The
                // existing m_profileUploadPending / phaseChanged machinery
                // handles queue-clear and supersede cases on its own.
                // The pending upload (if any) will ride the reconnect path.
                if (hadPending) {
                    m_profileUploadPending = true;
                }
                return;
            }
            m_lastUploadFailureReason = reason;
            m_profileUploadRetryAttempts++;
            if (m_profileUploadRetryAttempts >= kMaxUploadRetryAttempts) {
                qWarning().noquote() << QStringLiteral(
                    "ProfileManager: profile upload failed %1 consecutive times — "
                    "giving up and asking the user to power-cycle the DE1. "
                    "Last reason: %2")
                    .arg(m_profileUploadRetryAttempts)
                    .arg(m_lastUploadFailureReason);
                m_profileUploadRetryTimer.stop();
                if (hadPending) {
                    // Preserve intent so reconnect path re-uploads the latest profile.
                    m_profileUploadPending = true;
                }
                if (!m_de1CommunicationFailure) {
                    m_de1CommunicationFailure = true;
                    emit de1CommunicationFailureChanged();
                }
                updateProfileUploadRetrying();
                return;
            }
            // Exponential backoff capped at kUploadRetryMaxMs.
            // attempts=1 -> 1000ms, 2 -> 2000ms, 3 -> 4000ms, 4 -> 8000ms.
            const int shift = qMin(m_profileUploadRetryAttempts - 1, 20);
            const int delayMs = qMin(kUploadRetryBaseMs * (1 << shift), kUploadRetryMaxMs);
            qDebug().noquote() << QStringLiteral(
                "ProfileManager: profile upload failed (%1); retrying in %2 ms "
                "(attempt %3 of %4)")
                .arg(reason)
                .arg(delayMs)
                .arg(m_profileUploadRetryAttempts)
                .arg(kMaxUploadRetryAttempts);
            m_profileUploadRetryTimer.start(delayMs);
            updateProfileUploadRetrying();
            // hadPending was already consumed and m_uploadPendingAfterInFlight cleared at the top
            // of this handler. The retry timer calls uploadCurrentProfile(), which re-reads
            // m_currentProfile at fire time and thus naturally uploads the latest selection.

            // Safety: if a shot is already running on the DE1 (started via the
            // group-head button before the new profile landed), the DE1 is
            // extracting with whatever was previously in memory. Rather than
            // let the user brew on a stale or half-uploaded profile, stop the
            // shot immediately — same behaviour as aborting when the saved
            // scale is not connected. The user can restart once the retry
            // loop either succeeds or surfaces the communication-failure
            // dialog.
            if (m_machineState && m_device) {
                auto phase = m_machineState->phase();
                const bool isEspressoShot =
                    (phase == MachineState::Phase::EspressoPreheating ||
                     phase == MachineState::Phase::Preinfusion ||
                     phase == MachineState::Phase::Pouring ||
                     phase == MachineState::Phase::Ending);
                if (isEspressoShot) {
                    qWarning() << "ProfileManager: aborting in-progress shot "
                                  "because profile upload is retrying (DE1 may "
                                  "be running stale frames). Phase was:"
                               << m_machineState->phaseString();
                    m_device->requestState(DE1::State::Idle);
                    emit shotAbortedProfileUploadRetrying();
                }
            }
        });

        // On BLE disconnect, stop retrying — the reconnect path
        // (initialSettingsComplete -> applyAllSettings -> uploadCurrentProfile)
        // will re-upload once the link is back, and that fresh upload should
        // start from attempt 1.
        connect(m_device, &DE1Device::connectedChanged, this, [this]() {
            if (m_device && !m_device->isConnected()) {
                // Safety reset: clear in-flight gate in case profileUploaded(false, "BLE disconnect")
                // fires after this signal (signal ordering is not guaranteed).
                if (m_uploadPendingAfterInFlight) {
                    // A deferred profile change was queued mid-upload. Preserve the intent
                    // so the reconnect path (initialSettingsComplete → uploadCurrentProfile)
                    // re-uploads the latest profile when the link comes back.
                    m_profileUploadPending = true;
                }
                m_uploadInFlight = false;
                m_uploadPendingAfterInFlight = false;
                if (m_profileUploadRetryTimer.isActive()
                    || m_profileUploadRetryAttempts > 0) {
                    qDebug() << "ProfileManager: resetting upload-retry state "
                                "because DE1 disconnected";
                    m_profileUploadRetryTimer.stop();
                    m_profileUploadRetryAttempts = 0;
                    m_lastUploadFailureReason.clear();
                    updateProfileUploadRetrying();
                }
            }
        });
    }

    // Refresh profiles when storage permission changes (Android)
    if (m_profileStorage) {
        connect(m_profileStorage, &ProfileStorage::configuredChanged, this, [this]() {
            if (m_profileStorage->isConfigured()) {
                qDebug() << "[ProfileManager] Storage configured, refreshing profiles";
                refreshProfiles();
            }
        });
    }

    // Migrate profile folders (one-time migration for existing users)
    migrateProfileFolders();

    // Load initial profile
    refreshProfiles();

    // One-time upgrade: strip stored recipe blocks. Runs BEFORE migrateProfileFormat()
    // so a profile carrying a block is brought to the canonical shape once, by the pass
    // that understands the block, rather than being rewritten twice in one startup.
    //
    // (An earlier revision justified the ordering by claiming migrateProfileFormat's
    // parity gate would otherwise refuse these profiles. It would not:
    // collectParityErrors skips deliberatelyDroppedKeys() for every caller, so a
    // dropped `recipe` is never reported to it either. The ordering is about doing the
    // work once, not about unblocking a gate.)
    stripStoredRecipeBlocks();

    // One-time migration: resave profiles in unified de1app-compatible format
    migrateProfileFormat();

    // One-time migration: rename user overrides of built-in profiles, fix broken D-Flow/A-Flow
    migrateReadOnlyProfiles();

    // Check for temp file (modified profile from previous session)
    QString tempPath = profilesPath() + "/_current.json";
    if (QFile::exists(tempPath)) {
        qDebug() << "Loading modified profile from temp file:" << tempPath;
        m_currentProfile = Profile::loadFromFile(tempPath);
        m_profileModified = true;
        // Get the base profile name from settings
        if (m_settings) {
            m_baseProfileName = m_settings->app()->currentProfile();
            // Sync selectedFavoriteProfile so UI shows correct pill
            int favoriteIndex = m_settings->app()->findFavoriteIndexByFilename(m_baseProfileName);
            m_settings->app()->setSelectedFavoriteProfile(favoriteIndex);
        }
        // Persisted brew overrides survive the restart; same-as-default noise is dropped.
        resetBrewOverridesForLoadedProfile();
        if (m_machineState) {
            m_machineState->setTargetWeight(targetWeight());
        }
        // Upload to machine if connected
        if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
            uploadCurrentProfile();
        }
    } else if (m_settings) {
        loadProfile(m_settings->app()->currentProfile());
    } else {
        loadDefaultProfile();
    }
    m_profileJsonCache.clear();  // Free cached JSON after startup profile load
    m_startupLoadDone = true;

    // Keep MachineState in sync when yield override changes in Settings
    if (m_settings) {
        connect(m_settings->brew(), &SettingsBrew::brewOverridesChanged, this, [this]() {
            if (m_machineState) {
                m_machineState->setTargetWeight(targetWeight());
            }
            emit targetWeightChanged();
        });
        connect(m_settings->dye(), &SettingsDye::dyeBeanWeightChanged, this, [this]() {
            // Under a ratio anchor a dose write re-derives the gram target
            // (add-yield-ratio-anchor): push the resolved value to
            // MachineState like the brewOverridesChanged handler above.
            // Mid-shot this is inert by construction — targetWeight()
            // answers with the target latched at cycle start, so the write
            // is a no-op (Decision 9). Under an absolute or no anchor the
            // resolved value doesn't depend on the dose, so this is a no-op
            // there too.
            if (m_machineState) {
                m_machineState->setTargetWeight(targetWeight());
            }
            emit targetWeightChanged();
        });

        // Update profile lists when selection/hidden state changes
        connect(m_settings->app(), &SettingsApp::selectedBuiltInProfilesChanged, this, &ProfileManager::profilesChanged);
        connect(m_settings->app(), &SettingsApp::hiddenProfilesChanged, this, &ProfileManager::profilesChanged);
    }
}


// === Profile state ===

QString ProfileManager::currentProfileName() const {
    if (m_profileModified) {
        // Read-only profiles get "(modified)" suffix so Visualizer uploads
        // don't confuse people seeing an altered version of a stock profile.
        if (isCurrentProfileReadOnly())
            return m_currentProfile.title() + " (modified)";
        return "*" + m_currentProfile.title();
    }
    return m_currentProfile.title();
}

bool ProfileManager::isDFlowTitle(const QString& title) {
    // Check if title indicates a D-Flow profile (matching de1app behavior)
    // Ignores leading * (modified indicator that may come from imports)
    QString t = title.startsWith(QLatin1Char('*')) ? title.mid(1) : title;
    return t.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive);
}

bool ProfileManager::isAFlowTitle(const QString& title) {
    // Check if title indicates an A-Flow profile (matching de1app behavior)
    // Ignores leading * (modified indicator that may come from imports)
    QString t = title.startsWith(QLatin1Char('*')) ? title.mid(1) : title;
    return t.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive);
}

bool ProfileManager::isCurrentProfileRecipe() const {
    return m_currentProfile.editorType() != QLatin1String("advanced");
}

QString ProfileManager::currentEditorType() const {
    // Title overrides stored editorType for D-Flow/A-Flow (safety net for imports)
    if (isDFlowTitle(m_currentProfile.title())) return QStringLiteral("dflow");
    if (isAFlowTitle(m_currentProfile.title())) return QStringLiteral("aflow");

    return m_currentProfile.editorType();
}


// === Target weight / brew-by-ratio ===

double ProfileManager::targetWeight() const {
    // MID-SHOT THE RESOLVED TARGET IS FROZEN (add-yield-ratio-anchor
    // Decision 9). Latching only the DOSE was not enough: every OTHER input
    // to the resolution stays live during a shot — a bean switch's
    // clearBrewOverrides, a recipe activation, an MCP/web anchor write, a
    // profile load — and each one re-resolves and pushes the new value at
    // MachineState, which main.cpp's forwarder hands to the WeightProcessor
    // with no phase gate. That MOVES THE LIVE SAW TARGET: a bean switch
    // during a pour dropped a 45 g target to 36 g and cut the shot short.
    // Freezing the resolved value kills every path that RESOLVES THROUGH
    // HERE at once, and keeps the display, the machine, and the shot record
    // agreeing on the target that actually ran. It is not a guarantee about
    // MachineState's target in general: a caller that writes
    // setTargetWeight() with a value of its own bypasses this, latch and
    // all. That is exactly what the deliberate +10 g bump does — so any new
    // caller must route through targetWeight() unless it means to override a
    // shot in flight.
    //
    // The deliberate mid-shot +10 g bump is unaffected — it writes
    // MachineState::setTargetWeight() directly and never comes through here.
    if (m_shotLatched)
        return m_latchedTargetG;

    // The ladder's single evaluation point: resolve the session anchor
    // {value, mode} to grams. A ratio multiplies the effective dose; a ratio
    // with no dose — and mode "none" — fall back to the profile's own
    // target_weight. Nothing downstream (MachineState, WeightProcessor,
    // MQTT, shots.yield_override) ever sees a ratio.
    if (m_settings) {
        SettingsBrew* brew = m_settings->brew();
        return YieldSpec::resolveGrams(brew->brewYieldMode(), brew->brewYieldOverride(),
                                       brewByRatioDose(), m_currentProfile.targetWeight());
    }
    return m_currentProfile.targetWeight();
}

bool ProfileManager::brewByRatioActive() const {
    // The stored mode is the only definition of "ratio-anchored". The old
    // qAbs(override − profileTarget) > 0.1 inference silently dropped a
    // ratio whose derived target happened to equal the profile's (Bug A).
    return m_settings
        && m_settings->brew()->brewYieldMode() == YieldSpec::modeRatio();
}

double ProfileManager::brewByRatioDose() const {
    if (m_shotLatched)
        return m_latchedDoseG;
    return m_settings ? m_settings->dye()->dyeBeanWeight() : 0.0;
}

double ProfileManager::brewByRatio() const {
    // Ratio mode: the stored anchor, verbatim — never re-derived through the
    // dose (deriving would drift the displayed ratio the moment the resolved
    // grams round). Absolute mode: the derived display ratio. None: 0.
    if (!m_settings) return 0.0;
    SettingsBrew* brew = m_settings->brew();
    if (brew->brewYieldMode() == YieldSpec::modeRatio())
        return brew->brewYieldOverride();
    if (brew->brewYieldMode() == YieldSpec::modeAbsolute()) {
        const double dose = brewByRatioDose();
        return dose > 0 ? brew->brewYieldOverride() / dose : 0.0;
    }
    return 0.0;
}

void ProfileManager::latchForShot() {
    // Snapshot the dose, the resolved target, AND the anchor that produced
    // it. The dose keeps the ratio display stable; the target is the
    // load-bearing one — see targetWeight() for why latching the dose alone
    // was insufficient; the anchor is what the shot record stores as intent
    // (see hasShotSnapshot()).
    // Order matters: resolve the target BEFORE arming the flag, or
    // targetWeight() would read the not-yet-written m_latchedTargetG.
    // Clearing first makes that ordering hold even if a previous latch was
    // somehow never released: resolving through a still-armed latch would
    // self-assign (targetWeight() would answer with m_latchedTargetG), so a
    // stale target would re-latch itself on every shot and never re-resolve.
    // The cycle-ended release makes that unreachable — this keeps a future
    // arm/release mismatch a one-shot bug rather than a permanent one.
    m_shotLatched = false;
    m_latchedDoseG = m_settings ? m_settings->dye()->dyeBeanWeight() : 0.0;
    m_latchedTargetG = targetWeight();
    if (m_settings) {
        m_latchedYieldMode = m_settings->brew()->brewYieldMode();
        m_latchedYieldAnchorValue = m_settings->brew()->brewYieldOverride();
    }
    m_shotSnapshotValid = true;
    m_shotLatched = true;
    // Push the latched target so the machine and the snapshot agree BY
    // CONSTRUCTION. main.cpp reads machineState.targetWeight() (not this
    // value) to configure the WeightProcessor, while the shot record reads
    // latchedTargetG() — so if MachineState were out of sync at cycle start
    // for any reason, the shot would STOP at one number and be RECORDED at
    // another, with nothing logged. Today the ladder's pushes keep them
    // level and this is a no-op; it costs one comparison to stop that from
    // being a standing assumption.
    if (m_machineState)
        m_machineState->setTargetWeight(m_latchedTargetG);
    // The latch is silent machinery that decides what the machine stops at,
    // and it exists because a target once moved mid-pour and cut a shot
    // short. Log the resolution so a debug log can answer "what did this
    // shot actually target, and why" — the field diagnoses these through the
    // log, and every latch bug so far has been invisible in it.
    qDebug().noquote() << QString("[Yield] latched target=%1g dose=%2g anchor=%3:%4")
        .arg(m_latchedTargetG, 0, 'f', 1).arg(m_latchedDoseG, 0, 'f', 1)
        .arg(m_latchedYieldMode).arg(m_latchedYieldAnchorValue, 0, 'f', 2);
}

void ProfileManager::releaseShotLatch() {
    if (!m_shotLatched)
        return;
    m_shotLatched = false;
    // Re-resolve against the live state so the NEXT shot picks up anything
    // written while the latch held (a dose capture, a bean switch, an anchor
    // edit) — all of which were deliberately inert on the running shot.
    const double resolved = targetWeight();
    // This write reaches the WeightProcessor through main.cpp's ungated
    // forwarder, so a release arriving EARLY (a BLE glitch bouncing the phase
    // out of the espresso set mid-pour) would move the live SAW target rather
    // than merely unlatch. Log the value whenever it actually changes: if that
    // ever happens mid-shot, this line is the evidence, and its absence on a
    // normal shot end is free.
    if (!qFuzzyCompare(resolved, m_latchedTargetG))
        qDebug().noquote() << QString("[Yield] latch released, re-resolved %1g -> %2g")
            .arg(m_latchedTargetG, 0, 'f', 1).arg(resolved, 0, 'f', 1);
    if (m_machineState)
        m_machineState->setTargetWeight(resolved);
    emit targetWeightChanged();
}

void ProfileManager::setTargetWeight(double weight) {
    if (m_currentProfile.targetWeight() != weight) {
        m_currentProfile.setTargetWeight(weight);
        if (m_machineState) {
            m_machineState->setTargetWeight(weight);
        }
        emit targetWeightChanged();
    }
}

void ProfileManager::activateBrewWithOverrides(double dose, double yieldValue,
                                               const QString& yieldMode,
                                               double temperature, const QString& grind,
                                               int rpm) {
    if (m_settings) {
        // The dose ladder (dose-source-precedence) gains NO profile write
        // target here, deliberately. The recipe stamp and the bag write-through
        // already ride on setDyeBeanWeight, so an edit reaches whichever of the
        // top two rungs is active. The profile is left out because the only way
        // to write it is setCurrentProfileRecommendedDose, which marks the
        // profile MODIFIED — so nudging the dose by 0.2 g in a dial-in dialog
        // would dirty the loaded profile and ask to be saved. A profile's
        // recommended dose is stored design, edited in the profile editors;
        // Brew Settings dials the session. See the change's design notes.
        m_settings->dye()->setDyeBeanWeight(dose);
        m_settings->dye()->setDyeGrinderSetting(grind);
        // RPM is the second half of the dial-in; set it only when the caller
        // supplied one (rpm >= 0), leaving it untouched otherwise.
        if (rpm >= 0)
            m_settings->dye()->setDyeGrinderRpm(rpm);
        const QString mode = YieldSpec::normalizedMode(yieldMode);
        if (mode == YieldSpec::modeRatio()) {
            // A ratio is always a deliberate anchor — even when it derives
            // exactly the profile's target. No gram comparison here.
            m_settings->brew()->setBrewRatioAnchor(yieldValue);
        } else if (mode == YieldSpec::modeAbsolute()
                   && qAbs(yieldValue - m_currentProfile.targetWeight()) > 0.1) {
            m_settings->brew()->setBrewYieldOverride(yieldValue);
        } else {
            // Absolute matching the profile default is not an override
            // (Bug A rule — survives for absolute only); mode "none" clears.
            m_settings->brew()->setBrewYieldOverride(0);
        }
        if (qAbs(temperature - m_currentProfile.espressoTemperature()) > 0.1)
            m_settings->brew()->setTemperatureOverride(temperature);
        else
            m_settings->brew()->clearTemperatureOverride();

        // NOTE (add-yield-ratio-anchor): the bag yield write-through that
        // lived here is gone. The bag's yield spec is button-protected —
        // it changes only via "Update Bag" in Brew Settings, never as a
        // side effect of committing the dialog.
    }

    qDebug() << "Brew overrides activated: dose=" << dose << "g, yield ="
             << yieldValue << YieldSpec::normalizedMode(yieldMode)
             << "-> target=" << targetWeight() << "g";

    // MachineState sync happens via brewOverridesChanged signal connection

    // Re-upload profile with temperature applied to machine frames
    uploadCurrentProfile();
}

void ProfileManager::activateBrewWithOverrides(double dose, double yield, double temperature, const QString& grind) {
    activateBrewWithOverrides(dose, yield, YieldSpec::modeAbsolute(), temperature, grind);
}

void ProfileManager::applyTemperatureOverride(double temperatureC) {
    // The temperature branch of activateBrewWithOverrides, on its own: set-or-clear
    // against the profile baseline so tapping the profile's own temperature disarms
    // the override instead of pinning a redundant one. The m_settings guard mirrors
    // the sibling; uploadCurrentProfile() runs regardless, exactly as there.
    if (m_settings) {
        if (qAbs(temperatureC - m_currentProfile.espressoTemperature()) > 0.1)
            m_settings->brew()->setTemperatureOverride(temperatureC);
        else
            m_settings->brew()->clearTemperatureOverride();
    }
    // Re-upload profile with the temperature applied to the machine frames.
    uploadCurrentProfile();
}

void ProfileManager::clearBrewOverrides() {
    if (m_settings) {
        m_settings->brew()->clearAllBrewOverrides();
    }
    // MachineState sync happens via brewOverridesChanged signal connection
    qDebug() << "Brew overrides cleared, profile defaults apply, target=" << m_currentProfile.targetWeight() << "g";
}

void ProfileManager::resetBrewOverridesForLoadedProfile() {
    if (!m_settings)
        return;
    SettingsBrew* brew = m_settings->brew();
    if (m_startupLoadDone) {
        // Every normal runtime profile load takes this branch. Clear what
        // the outgoing profile owned — temperature and an ABSOLUTE yield
        // anchor — but keep a ratio anchor: 1:2 is 1:2 on any profile
        // (add-yield-ratio-anchor Decision 8).
        brew->clearProfileScopedBrewOverrides();
        return;
    }
    // Startup only: persisted overrides survive the launch load, except a
    // frozen value that happens to equal the loaded profile's own default —
    // that is not an override. The gram comparison applies to ABSOLUTE
    // anchors only; a persisted ratio is kept verbatim (comparing its
    // derived grams against the profile is exactly the Bug-A inference the
    // stored mode retires).
    if (brew->hasTemperatureOverride()
        && qAbs(brew->temperatureOverride() - m_currentProfile.espressoTemperature()) <= 0.1)
        brew->clearTemperatureOverride();
    if (brew->brewYieldMode() == YieldSpec::modeAbsolute()
        && qAbs(brew->brewYieldOverride() - m_currentProfile.targetWeight()) <= 0.1)
        brew->setBrewYieldOverride(0);
}

void ProfileManager::applyRecommendedDoseIfProfileOwnsIt() {
    if (!m_settings)
        return;
    if (!m_currentProfile.hasRecommendedDose() || m_currentProfile.recommendedDose() <= 0)
        return;
    // The profile is the LAST rung of the dose ladder (dose-source-precedence):
    // an active recipe or bag that supplies a dose outranks it. This write used
    // to be unconditional, and precedence was "whichever queued write lands
    // last" — which only ever held for a load that a recipe activation had
    // itself triggered. With a BAG active the damage was persistent: the write
    // lands, setDyeBeanWeight's writeThroughToBag rewrites the bag's stored
    // doseWeightG, and the bean's remembered dose is gone.
    //
    // The ladder is consulted in the queued lambda below rather than here, so
    // deciding late is the whole point — see the comment at the check. It also
    // means this function reads the ladder AFTER MainController's title-mismatch
    // watcher has had its turn: switching to a different profile deactivates the
    // recipe during currentProfileChanged, and the write then correctly lands
    // for a profile that really does own the dose.
    //
    // Startup is not a resolution point. The live dose is already persisted
    // from the last session — whichever source won it then — while the bag and
    // recipe rows are still loading asynchronously. Same rule
    // resetBrewOverridesForLoadedProfile applies to the yield: persisted values
    // survive the launch load.
    if (!m_startupLoadDone)
        return;
    // Deferred to next event loop to avoid a QML signal cascade during load.
    const double dose = m_currentProfile.recommendedDose();
    const QString title = m_currentProfile.title();
    QMetaObject::invokeMethod(this, [this, dose, title]() {
        if (!m_settings)
            return;
        auto* dye = m_settings->dye();
        // The ladder is resolved HERE, at delivery, not where the write was
        // armed. Both higher rungs load their row on a storage worker, and a
        // profile load can be issued in the window between selecting a bag or
        // recipe and its row arriving — an arm-time check reads that window as
        // "nobody else supplies a dose" and this write then lands on top of the
        // source that does. It is the destructive one: setDyeBeanWeight writes
        // through to the active bag's stored doseWeightG and stamps the active
        // recipe's doseG, so a mistimed profile load does not merely show the
        // wrong number, it erases what the bean or the recipe remembered.
        if (!dye->doseLadderResolved()) {
            qDebug().noquote()
                << QStringLiteral("[dose] '%1' recommends %2 g but the bag/recipe rows have not "
                                  "arrived yet, so the dose ladder cannot be resolved "
                                  "(dose-source-precedence) — live dose stays %3 g. Re-select the "
                                  "profile once loading settles if you want its dose.")
                       .arg(title).arg(dose).arg(dye->dyeBeanWeight());
            return;
        }
        if (dye->doseOwner() != SettingsDye::DoseOwner::Profile) {
            // Deliberately suppressed and genuinely surprising, so it says so
            // in the log: a user reporting "I loaded the 18 g profile and the
            // dose stayed at 20" otherwise hands their AI a log with no
            // evidence the ladder ran at all, and the likely reading is that
            // the profile's recommendation is unset.
            const bool recipeOwns = dye->doseOwner() == SettingsDye::DoseOwner::Recipe;
            qDebug().noquote()
                << QStringLiteral("[dose] '%1' recommends %2 g but the active %3 owns the dose "
                                  "and outranks the profile (dose-source-precedence) — live dose "
                                  "stays %4 g. Change it with MCP %5, or clear the %3; loading a "
                                  "profile will not override it.")
                       .arg(title).arg(dose)
                       .arg(recipeOwns ? QStringLiteral("recipe") : QStringLiteral("bag"))
                       .arg(dye->dyeBeanWeight())
                       .arg(recipeOwns ? QStringLiteral("recipe_update") : QStringLiteral("bag_update"));
            return;
        }
        dye->setDyeBeanWeight(dose);
    }, Qt::QueuedConnection);
}


// === Profile catalog ===

// The ONE ProfileInfo -> QVariantMap projection every catalog accessor returns.
//
// There were six hand-written copies of this. Adding one field meant six
// identical edits, and they had already drifted: the allProfilesList copy
// emitted espressoTemperature and targetWeight while the other five did not,
// so which fields a QML consumer could read depended on which accessor it had
// happened to call. Nothing failed, because no screen read both. The two extra
// fields are included here for everyone — a consumer that ignores them is
// unaffected, and the alternative is keeping the drift as a feature.
static QVariantMap profileInfoToVariantMap(const ProfileInfo& info)
{
    QVariantMap profile;
    profile["name"] = info.filename;
    profile["title"] = info.title;
    profile["beverageType"] = info.beverageType;
    profile["espressoTemperature"] = info.espressoTemperature;
    profile["targetWeight"] = info.targetWeight;
    profile["source"] = static_cast<int>(info.source);
    profile["hasKnowledgeBase"] = info.hasKnowledgeBase;
    profile["kbDerivedFrom"] = info.kbDerivedFrom;
    profile["readOnly"] = info.readOnly;
    return profile;
}

QVariantList ProfileManager::availableProfiles() const {
    QVariantList result;
    for (const ProfileInfo& info : m_allProfiles) {
        QVariantMap profile;
        profile["name"] = info.filename;  // filename for loading
        profile["title"] = info.title;    // display title
        profile["editorType"] = info.editorType;
        result.append(profile);
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList ProfileManager::selectedProfiles() const {
    QVariantList result;

    // Get selected built-in profile names from settings
    QStringList selectedBuiltIns = m_settings ? m_settings->app()->selectedBuiltInProfiles() : QStringList();
    QStringList hiddenProfiles = m_settings ? m_settings->app()->hiddenProfiles() : QStringList();

    for (const ProfileInfo& info : m_allProfiles) {
        bool include = false;

        switch (info.source) {
        case ProfileSource::BuiltIn:
            // Only include if selected
            include = selectedBuiltIns.contains(info.filename);
            break;
        case ProfileSource::Downloaded:
        case ProfileSource::UserCreated:
            // Include unless explicitly hidden
            include = !hiddenProfiles.contains(info.filename);
            break;
        }

        if (include) {
            result.append(profileInfoToVariantMap(info));
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList ProfileManager::allBuiltInProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.source == ProfileSource::BuiltIn) {
            result.append(profileInfoToVariantMap(info));
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList ProfileManager::cleaningProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        // Include both cleaning and descale profiles in this category
        if (info.beverageType == "cleaning" || info.beverageType == "descale") {
            result.append(profileInfoToVariantMap(info));
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList ProfileManager::downloadedProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.source == ProfileSource::Downloaded) {
            result.append(profileInfoToVariantMap(info));
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList ProfileManager::userCreatedProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.source == ProfileSource::UserCreated) {
            result.append(profileInfoToVariantMap(info));
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList ProfileManager::allProfilesList() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        result.append(profileInfoToVariantMap(info));
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}


// === Profile CRUD ===

QVariantMap ProfileManager::getCurrentProfile() const {
    QVariantMap profile;
    profile["title"] = m_currentProfile.title();
    profile["author"] = m_currentProfile.author();
    profile["profile_notes"] = m_currentProfile.profileNotes();
    profile["target_weight"] = m_currentProfile.targetWeight();
    profile["target_volume"] = m_currentProfile.targetVolume();
    profile["espresso_temperature"] = m_currentProfile.espressoTemperature();
    profile["mode"] = m_currentProfile.mode() == Profile::Mode::FrameBased ? "frame_based" : "direct";
    profile["has_recommended_dose"] = m_currentProfile.hasRecommendedDose();
    profile["recommended_dose"] = m_currentProfile.recommendedDose();
    profile["tank_desired_water_temperature"] = m_currentProfile.tankDesiredWaterTemperature();
    profile["maximum_flow_range_advanced"] = m_currentProfile.maximumFlowRangeAdvanced();
    profile["maximum_pressure_range_advanced"] = m_currentProfile.maximumPressureRangeAdvanced();
    profile["maximum_pressure"] = m_currentProfile.maximumPressure();
    profile["maximum_flow"] = m_currentProfile.maximumFlow();
    profile["preinfuse_frame_count"] = m_currentProfile.preinfuseFrameCount();

    QVariantList steps;
    for (const auto& frame : m_currentProfile.steps()) {
        QVariantMap step;
        step["name"] = frame.name;
        step["temperature"] = frame.temperature;
        step["sensor"] = frame.sensor;
        step["pump"] = frame.pump;
        step["transition"] = frame.transition;
        step["pressure"] = frame.pressure;
        step["flow"] = frame.flow;
        step["seconds"] = frame.seconds;
        step["volume"] = frame.volume;
        step["exit_if"] = frame.exitIf;
        step["exit_type"] = frame.exitType;
        step["exit_pressure_over"] = frame.exitPressureOver;
        step["exit_pressure_under"] = frame.exitPressureUnder;
        step["exit_flow_over"] = frame.exitFlowOver;
        step["exit_flow_under"] = frame.exitFlowUnder;
        step["exit_weight"] = frame.exitWeight;
        step["popup"] = frame.popup;
        step["max_flow_or_pressure"] = frame.maxFlowOrPressure;
        step["max_flow_or_pressure_range"] = frame.maxFlowOrPressureRange;
        steps.append(step);
    }
    profile["steps"] = steps;

    return profile;
}

void ProfileManager::markProfileClean() {
    if (m_profileModified) {
        m_profileModified = false;
        emit profileModifiedChanged();
        emit currentProfileChanged();  // Update the name (remove * prefix)

        // Remove temp file since we're now clean
        QString tempPath = profilesPath() + "/_current.json";
        QFile::remove(tempPath);
        qDebug() << "Profile marked clean, removed temp file";
    }
}

QString ProfileManager::titleToFilename(const QString& title) const {
    // Implementation lives on Profile so the profile_sync tool and the parity
    // tests derive the same filename the app does. This used to be its own copy
    // with a 22-entry accent table; the tool's copy used NFD decomposition and a
    // 50-character cap. They agreed on every shipped title and would not have
    // agreed on the next one.
    return Profile::titleToFilename(title);
}

QString ProfileManager::findProfileByTitle(const QString& title) const {
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.title == title) {
            return info.filename;
        }
    }
    return QString();
}

QVariantMap ProfileManager::profileCatalogInfoForTitle(const QString& title) const {
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.title != title)
            continue;
        QVariantMap m;
        m["filename"] = info.filename;
        m["title"] = info.title;
        m["editorType"] = info.editorType;
        m["beverageType"] = info.beverageType;
        m["hasKnowledgeBase"] = info.hasKnowledgeBase;
        m["kbDerivedFrom"] = info.kbDerivedFrom;
        m["espressoTemperatureC"] = info.espressoTemperature;
        m["targetWeightG"] = info.targetWeight;
        return m;
    }
    return QVariantMap();
}

QString ProfileManager::profileKnowledgeContent(const QString& profileTitle) const {
    // Prefer the catalog's already-resolved id. The catalog resolves by title
    // AND, on a miss, by profile shape; going straight to findProfileSection
    // would redo only the title half, so a shape-resolved profile would show a
    // lit knowledge indicator and then an empty dialog. The indicator and this
    // lookup must agree, and they agree by reading the same resolution.
    if (const ProfileInfo* found = findProfileByTitleForKb(profileTitle)) {
        const ProfileInfo& info = *found;
        if (info.kbIds.isEmpty()) return QString();
        if (info.kbIds.size() == 1)
            return ShotSummarizer::profileKnowledgeForKbId(info.kbIds.first());

        // Ambiguous shape: several documented profiles share this frame
        // structure. Show ALL of them rather than picking one — the badges
        // were shaped by what these entries agree on, and naming a single
        // member would assert an identity the shape never established.
        //
        // Each body is preceded by its canonical name on its own line, and
        // nothing else is inserted. No English connective belongs here: the KB
        // prose is untranslated by nature, but a sentence this code invented
        // would be UI text with no route to TranslationManager. The dialog
        // adds that line, driven by profileKbCandidateNames().
        QStringList sections;
        sections.reserve(info.kbIds.size());
        for (const QString& id : info.kbIds) {
            const QString body = ShotSummarizer::profileKnowledgeForKbId(id);
            if (body.isEmpty()) continue;
            sections << ShotSummarizer::canonicalNameForKbId(id)
                            + QStringLiteral("\n") + body;
        }
        return sections.join(QStringLiteral("\n\n"));
    }
    // Not in the catalog — a shot's stored profile name, which may be a
    // profile the user has since deleted. Title resolution is all that is
    // available for it.
    return ShotSummarizer::findProfileSection(profileTitle);
}

// Catalog lookup by TITLE, for the three KB surfaces that only have a title to
// go on (a shot's stored profile name, a selector delegate's display title).
//
// Title is not a key: refreshProfiles de-duplicates by FILENAME, so a
// downloaded copy and a user "Save As" can legitimately share one. Returning
// the first hit would let the sparkle, the "Based on X" line and the dialog
// body each answer from whichever entry the scan happened to reach first —
// and those three sit next to each other on the same screen. When two entries
// with one title disagree about their KB resolution there is no right answer,
// so say so and return none rather than pick.
const ProfileInfo* ProfileManager::findProfileByTitleForKb(const QString& profileTitle) const
{
    const ProfileInfo* hit = nullptr;
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.title != profileTitle) continue;
        if (!hit) { hit = &info; continue; }
        if (hit->kbIds != info.kbIds || hit->kbDerivedFrom != info.kbDerivedFrom) {
            qWarning() << "ProfileManager: profiles" << hit->filename << "and"
                       << info.filename << "share the title" << profileTitle
                       << "but resolve to different KB entries - showing neither";
            return nullptr;
        }
    }
    return hit;
}

bool ProfileManager::profileHasKnowledge(const QString& profileTitle) const {
    return !profileKnowledgeContent(profileTitle).isEmpty();
}

QStringList ProfileManager::profileKbCandidateNames(const QString& profileTitle) const {
    const ProfileInfo* found = findProfileByTitleForKb(profileTitle);
    // Single match: the dialog is already titled with the profile, so naming
    // one entry beside it would be noise. Only ambiguity needs explaining.
    if (!found || found->kbIds.size() < 2) return {};
    QStringList names;
    names.reserve(found->kbIds.size());
    for (const QString& id : found->kbIds)
        names << ShotSummarizer::canonicalNameForKbId(id);
    return names;
}

QString ProfileManager::profileKbDerivedFrom(const QString& profileTitle) const {
    const ProfileInfo* found = findProfileByTitleForKb(profileTitle);
    return found ? found->kbDerivedFrom : QString();
}

QVariantMap ProfileManager::dialInDiffFor(const Profile& p) {
    QVariantMap out;
    out[QStringLiteral("hasBase")] = false;
    out[QStringLiteral("unchanged")] = false;
    out[QStringLiteral("rows")] = QVariantList();
    if (!p.isValid()) return out;

    const DialInComparison cmp = compareWithBundledBase(p, resolveProfileKb(p));
    if (!cmp.hasBase()) return out;

    QVariantList rows;
    rows.reserve(cmp.deltas.size());
    for (const ProfileFieldDelta& d : cmp.deltas) {
        rows.append(QVariantMap{
            { QStringLiteral("kind"),       d.kind },
            { QStringLiteral("unit"),       d.unit },
            { QStringLiteral("frameIndex"), d.frameIndex },
            { QStringLiteral("frameName"),  d.frameName },
            { QStringLiteral("numeric"),    d.numeric },
            { QStringLiteral("oldValue"),   d.oldValue },
            { QStringLiteral("newValue"),   d.newValue },
            { QStringLiteral("oldText"),    d.oldText },
            { QStringLiteral("newText"),    d.newText },
        });
    }

    out[QStringLiteral("hasBase")]   = true;
    out[QStringLiteral("baseTitle")] = cmp.baseTitle;
    out[QStringLiteral("baseKbId")]  = cmp.baseKbId;
    out[QStringLiteral("unchanged")] = rows.isEmpty();
    out[QStringLiteral("rows")]      = rows;
    return out;
}

QVariantMap ProfileManager::profileDialInDiff(const QString& profileTitle) const {
    // Through findProfileByTitleForKb rather than a bare title scan, so this
    // answers from the same catalog entry the sparkle and the "Based on X" line
    // do. Two same-titled profiles that disagree return nothing there, and
    // showing a difference block for one of them would contradict that.
    const ProfileInfo* found = findProfileByTitleForKb(profileTitle);
    if (!found) return dialInDiffFor(Profile());

    // NOT MEASURED, and deliberately not given a number here. The work is one
    // Profile load from disk plus one to four from the (in-memory) resource
    // system, each followed by a linear field walk — bounded by the shape
    // bucket, whose largest member over the shipped set is two. It runs on
    // knowledge-dialog OPEN, a discrete user action, and the dialog assigns it
    // to a plain property rather than binding it, so it evaluates once per open
    // and never re-evaluates on an unrelated change. That is what makes it an
    // acceptable inline read; a stopwatch figure would not change the decision,
    // and inventing one would be worse than admitting there isn't one.
    //
    // A cold shape index adds the 48 ms documented in profileshapeindex.cpp —
    // that one IS measured, and the catalog scan normally pays it first.
    return dialInDiffFor(loadProfileByFilename(found->filename));
}

QVariantMap ProfileManager::profileDialInDiffForJson(const QString& profileJson) const {
    // Parsed here rather than through Profile::loadFromJsonString, which warns.
    // A shot row with unreadable profile JSON is already reported where it is
    // READ (shothistorystorage_internal.cpp warns, and the shot-detail page
    // parses the same string for its own header). Warning again here would
    // re-report one defect on every knowledge-dialog open, for a user who can
    // do nothing about it, and this path has a perfectly good answer for the
    // case: nothing can be compared, so there is no base.
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(profileJson.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError)
        return dialInDiffFor(Profile());

    return dialInDiffFor(Profile::fromJson(doc));
}

bool ProfileManager::profileExists(const QString& filename) const {
    if (m_availableProfiles.contains(filename))
        return true;
    // Fallback: check disk (for profiles loaded after initial scan)
    QString path = profilesPath() + "/" + filename + ".json";
    return QFile::exists(path);
}

bool ProfileManager::isProfileInSelectedList(const QString& filename) const {
    if (filename.isEmpty() || !m_settings) return false;

    const QStringList selectedBuiltIns = m_settings->app()->selectedBuiltInProfiles();
    const QStringList hiddenProfiles = m_settings->app()->hiddenProfiles();

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.filename != filename) continue;
        switch (info.source) {
        case ProfileSource::BuiltIn:
            return selectedBuiltIns.contains(filename);
        case ProfileSource::Downloaded:
        case ProfileSource::UserCreated:
            return !hiddenProfiles.contains(filename);
        }
        // Defensive: an unknown ProfileSource (added later without updating
        // this switch) should default to "not selectable" so auto-load doesn't
        // silently pin a profile whose eligibility rules haven't been defined.
        qWarning() << "isProfileInSelectedList: unhandled ProfileSource for"
                   << filename << "— treating as not selected";
        return false;
    }
    return false;
}

void ProfileManager::loadAutoLoadProfileIfNeeded() {
    if (!m_settings) return;

    const QString filename = m_settings->app()->autoLoadProfileFilename();
    if (filename.isEmpty()) return;

    if (!isProfileInSelectedList(filename)) {
        qDebug() << "ProfileManager: auto-load filename" << filename
                 << "no longer in Selected list — clearing";
        m_settings->app()->setAutoLoadProfileFilename("");
        emit autoLoadStaleCleared();
        return;
    }

    if (filename == m_baseProfileName) {
        return; // Already active
    }

    qDebug() << "ProfileManager: loading auto-load profile" << filename;
    loadProfile(filename);
}

bool ProfileManager::deleteProfile(const QString& filename) {
    // Find the profile info. The title is captured HERE, while the catalog
    // still holds the row — everything that references a profile does so by
    // title, and by the time the delete has run and refreshProfiles() has
    // rebuilt the catalog there is nothing left to resolve the filename
    // against.
    ProfileSource source = ProfileSource::BuiltIn;
    QString deletedTitle;
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.filename == filename) {
            source = info.source;
            deletedTitle = info.title;
            break;
        }
    }

    bool deleted = false;

    // Always try to clean up ProfileStorage copies (even for built-in profiles,
    // since imported copies can shadow the built-in version)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        if (m_profileStorage->deleteProfile(filename)) {
            qDebug() << "Deleted profile from ProfileStorage:" << filename;
            deleted = true;
        }
    }

    // Always try to clean up local folder copies
    QString userPath = userProfilesPath() + "/" + filename + ".json";
    if (QFile::remove(userPath)) {
        qDebug() << "Deleted profile from user storage:" << userPath;
        deleted = true;
    }
    QString downloadedPath = downloadedProfilesPath() + "/" + filename + ".json";
    if (QFile::remove(downloadedPath)) {
        qDebug() << "Deleted profile from downloaded storage:" << downloadedPath;
        deleted = true;
    }

    // Built-in profiles can't be fully deleted (they'll still show from QRC),
    // but we cleaned up any local overrides above
    if (source == ProfileSource::BuiltIn) {
        if (deleted) {
            // `source` defaults to BuiltIn, so a filename that was NOT in the
            // catalog lands here too and gets diagnosed as an override cleanup
            // it never was. Say so, or the log claims a built-in was tidied when
            // a real profile was removed and nothing announced it.
            if (deletedTitle.isEmpty())
                qWarning() << "Deleted" << filename
                           << "but it was not in the profile catalog — cannot resolve its title,"
                           << "so nothing holding a title reference to it will be told";
            qDebug() << "Cleaned up local override for built-in profile:" << filename;
            refreshProfiles();
        }
        return false;
    }

    if (deleted) {
        // Remove from favorites if it was a favorite
        if (m_settings && m_settings->app()->isFavoriteProfile(filename)) {
            // Find index and remove
            QVariantList favorites = m_settings->app()->favoriteProfiles();
            for (qsizetype i = 0; i < favorites.size(); ++i) {
                if (favorites[i].toMap()["filename"].toString() == filename) {
                    m_settings->app()->removeFavoriteProfile(static_cast<int>(i));
                    break;
                }
            }
        }

        // Eager-clear: deleting the auto-load profile makes it ineligible
        if (m_settings && m_settings->app()->autoLoadProfileFilename() == filename) {
            m_settings->app()->setAutoLoadProfileFilename("");
        }

        // Refresh the profile list
        refreshProfiles();
        // Announce the deletion AFTER the catalog is rebuilt, and only when the
        // TITLE no longer resolves — which is the only thing any listener
        // actually cares about, since recipes and shots reference profiles by
        // title.
        //
        // The re-resolve is not belt-and-braces, it is the condition. Relying on
        // the built-in early return above instead was wrong: refreshProfiles()
        // classifies everything ProfileStorage lists as UserCreated and replaces
        // the built-in catalog row with it, so deleting a file that SHADOWS a
        // built-in never reaches that return — it would emit, MainController
        // would deactivate the recipe, and the title would still resolve to the
        // restored built-in. The same holds wherever two catalog rows share a
        // title. Asking findProfileByTitle makes this signal agree with
        // installedProfileTitles, which is what the recipe cards bind to.
        //
        // An empty title means the filename was not in the catalog at all —
        // nothing could have referenced it by title, so nothing to announce.
        if (!deletedTitle.isEmpty() && findProfileByTitle(deletedTitle).isEmpty())
            emit profileDeleted(deletedTitle);
        else if (!deletedTitle.isEmpty())
            qDebug() << "Deleted" << filename << "but title" << deletedTitle
                     << "still resolves — not announcing a deletion";
        return true;
    }

    qWarning() << "Failed to delete profile:" << filename;
    return false;
}

// === Read-only protection ===

bool ProfileManager::isCurrentProfileReadOnly() const {
    // A profile is read-only if its own read_only flag is set (from TCL/JSON)
    if (m_currentProfile.isReadOnly()) return true;
    // Or if it was loaded from built-in resources without a user override
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.filename == m_baseProfileName)
            return info.source == ProfileSource::BuiltIn;
    }
    return false;
}

bool ProfileManager::isBuiltInFilename(const QString& filename) const {
    return QFile::exists(QStringLiteral(":/profiles/") + filename + QStringLiteral(".json"));
}

bool ProfileManager::resetProfileToDefault(const QString& filename) {
    if (!isBuiltInFilename(filename)) return false;

    // Remove user copies from all storage locations
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        m_profileStorage->deleteProfile(filename);
    }
    QFile::remove(userProfilesPath() + "/" + filename + ".json");
    QFile::remove(downloadedProfilesPath() + "/" + filename + ".json");

    // Refresh and reload from the built-in QRC resource
    refreshProfiles();
    loadProfile(filename);
    qDebug() << "Reset built-in profile to default:" << filename;
    return true;
}

Profile ProfileManager::loadProfileByFilename(const QString& filename, bool* found) const {
    if (found) *found = true;
    // 1. ProfileStorage (SAF folder on Android). Reads WITHOUT consuming
    //    m_profileJsonCache — see the declaration for why loadProfile() keeps
    //    its own walk instead of calling this.
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        const QString jsonContent = m_profileStorage->readProfile(filename);
        if (!jsonContent.isEmpty())
            return Profile::loadFromJsonString(jsonContent);
    }

    // 2-4. User folder, downloaded folder, built-ins, in precedence order: a
    //      local copy shadows the bundled one of the same name.
    const QStringList paths{
        userProfilesPath() + "/" + filename + ".json",
        downloadedProfilesPath() + "/" + filename + ".json",
        ":/profiles/" + filename + ".json",
    };
    for (const QString& path : paths)
        if (QFile::exists(path))
            return Profile::loadFromFile(path);

    if (found) *found = false;
    return Profile();   // invalid: no steps
}

QVariantMap ProfileManager::getProfileByFilename(const QString& filename) const {
    // `found`, not isValid(): a file that exists but parses to a step-less
    // profile still returned a (partly empty) map before this helper existed,
    // and some caller may rely on that. Extracting the lookup must not quietly
    // change what a malformed profile does.
    bool found = false;
    Profile profile = loadProfileByFilename(filename, &found);
    if (!found) {
        return QVariantMap();  // Return empty map if not found
    }

    // Backfill empty notes from built-in profile (handles imported copies from before notes were added)
    if (profile.profileNotes().isEmpty()) {
        QString builtInPath = ":/profiles/" + filename + ".json";
        if (QFile::exists(builtInPath)) {
            Profile builtIn = Profile::loadFromFile(builtInPath);
            if (!builtIn.profileNotes().isEmpty()) {
                profile.setProfileNotes(builtIn.profileNotes());
            }
        }
    }

    // Build result map (same format as getCurrentProfile)
    QVariantMap result;
    result["title"] = profile.title();
    result["author"] = profile.author();
    result["profile_notes"] = profile.profileNotes();
    result["target_weight"] = profile.targetWeight();
    result["target_volume"] = profile.targetVolume();
    result["espresso_temperature"] = profile.espressoTemperature();
    result["mode"] = profile.mode() == Profile::Mode::FrameBased ? "frame_based" : "direct";
    result["has_recommended_dose"] = profile.hasRecommendedDose();
    result["recommended_dose"] = profile.recommendedDose();
    result["tank_desired_water_temperature"] = profile.tankDesiredWaterTemperature();
    result["maximum_flow_range_advanced"] = profile.maximumFlowRangeAdvanced();
    result["maximum_pressure_range_advanced"] = profile.maximumPressureRangeAdvanced();
    result["maximum_pressure"] = profile.maximumPressure();
    result["maximum_flow"] = profile.maximumFlow();
    result["preinfuse_frame_count"] = profile.preinfuseFrameCount();

    QVariantList steps;
    for (const auto& frame : profile.steps()) {
        QVariantMap step;
        step["name"] = frame.name;
        step["temperature"] = frame.temperature;
        step["sensor"] = frame.sensor;
        step["pump"] = frame.pump;
        step["transition"] = frame.transition;
        step["pressure"] = frame.pressure;
        step["flow"] = frame.flow;
        step["seconds"] = frame.seconds;
        step["volume"] = frame.volume;
        step["exit_if"] = frame.exitIf;
        step["exit_type"] = frame.exitType;
        step["exit_pressure_over"] = frame.exitPressureOver;
        step["exit_pressure_under"] = frame.exitPressureUnder;
        step["exit_flow_over"] = frame.exitFlowOver;
        step["exit_flow_under"] = frame.exitFlowUnder;
        step["exit_weight"] = frame.exitWeight;
        step["popup"] = frame.popup;
        step["max_flow_or_pressure"] = frame.maxFlowOrPressure;
        step["max_flow_or_pressure_range"] = frame.maxFlowOrPressureRange;
        steps.append(step);
    }
    result["steps"] = steps;

    return result;
}

bool ProfileManager::teaProfileMatchesType(const QString& profileTitle, const QString& teaType) const {
    return DrinkTypes::teaProfileMatchesType(profileTitle, teaType);
}

double ProfileManager::defaultTeaTempC(const QString& teaType) const {
    return DrinkTypes::defaultTeaTempC(teaType);
}

QString ProfileManager::grindDirectionBetween(const QString& sourceProfileTitle,
                                              const QString& targetProfileTitle) const {
    return ShotSummarizer::grindDirectionBetween(sourceProfileTitle, targetProfileTitle);
}

QString ProfileManager::beverageTypeForTitle(const QString& profileTitle) const {
    const QString needle = profileTitle.trimmed().toLower();
    if (needle.isEmpty())
        return {};
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.title.trimmed().toLower() == needle)
            return info.beverageType.trimmed().toLower();
    }
    return {};
}

QHash<QString, QString> ProfileManager::beverageTypeByTitleSnapshot() const {
    QHash<QString, QString> map;
    map.reserve(m_allProfiles.size());
    for (const ProfileInfo& info : m_allProfiles)
        map.insert(info.title.trimmed().toLower(), info.beverageType.trimmed().toLower());
    return map;
}

QHash<QString, double> ProfileManager::espressoTempByTitleSnapshot() const {
    QHash<QString, double> map;
    map.reserve(m_allProfiles.size());
    for (const ProfileInfo& info : m_allProfiles)
        map.insert(info.title.trimmed().toLower(), info.espressoTemperature);
    return map;
}

bool ProfileManager::kbProfileSuitsRoast(const QString& profileTitle, const QString& roastLevel) const {
    const QString normalized = roastLevel.trimmed().toLower().replace(QLatin1Char(' '), QLatin1Char('-'));
    if (normalized.isEmpty())
        return false;
    return ShotSummarizer::roastAffinityForTitle(profileTitle).contains(normalized);
}


// === Profile loading ===

// Rewrite a stored profile in the canonical encoding, but only when doing so
// provably loses nothing.
//
// Why on load rather than in a one-time migration: the set of files is not fixed.
// A user drops profiles into the folder whenever they like — sideloaded, restored
// from a backup, synced from another device — so a pass that marks itself complete
// converts whatever happened to be present that day and ignores every later
// arrival, which is exactly the population most likely to be legacy-encoded.
//
// Why it matters at all: DatabaseBackupManager copies the profile directory
// verbatim, so a legacy-encoded file travels byte-for-byte into a backup and onto
// another device, where a stricter reader (Decaid) rejects it outright for the
// missing tank_temperature / target_volume_count_start.
//
// `filePath` empty means the profile came from ProfileStorage; the concrete file
// is resolved below.
//
// The I/O here is synchronous on the main thread, against the project rule that
// disk I/O belongs on a worker. Deliberate, and narrow: it is one read and at most
// one write of a single profile (a few KB), in a function that already does
// synchronous QFile::exists and Profile::loadFromFile on the same thread, and
// saveProfile() likewise writes synchronously straight from QML. Moving only this
// write to a worker would add a race between two loads of the same profile for no
// measurable gain. If this ever needs to change, move the whole resolve off-thread
// rather than this write alone.
void ProfileManager::upgradeStoredEncoding(const QString& resolvedName,
                                           const QString& filePath,
                                           const Profile& loaded) {
    // Resolve ONE concrete file and both read and write it.
    //
    // ProfileStorage cannot be used as a read/write pair here: readProfile() tries
    // external then falls back to app-internal, while writeProfile() tries external
    // FIRST and creates the directory if absent. So a profile living only in the
    // fallback would be read from the fallback and written to the user's shared
    // de1plus/profiles folder — a file that was never there — leaving the copy we
    // actually examined untouched and now divergent. Both of ProfileStorage's tiers
    // are ordinary filesystem paths (readProfile opens them with QFile), so the
    // resolution is done here instead.
    QString target = filePath;
    if (target.isEmpty()) {
        if (!m_profileStorage)
            return;
        const QString ext = m_profileStorage->externalProfilesPath();
        if (!ext.isEmpty() && QFile::exists(ext + "/" + resolvedName + ".json"))
            target = ext + "/" + resolvedName + ".json";
        else if (QFile::exists(m_profileStorage->fallbackPath() + "/" + resolvedName + ".json"))
            target = m_profileStorage->fallbackPath() + "/" + resolvedName + ".json";
        else
            return;
    }

    // Read the stored bytes back rather than trusting anything in memory. The
    // parity check below is only meaningful against what is actually on disk.
    QJsonObject original;
    {
        QFile f(target);
        if (!f.open(QIODevice::ReadOnly))
            return;
        original = QJsonDocument::fromJson(f.readAll()).object();
    }
    if (original.isEmpty())
        return;

    const QJsonObject canonical = loaded.toJsonObject();

    // Already canonical — the overwhelmingly common case once a profile has been
    // converted. Compare the parsed objects, not the bytes: whitespace that
    // survives a round trip would otherwise rewrite the file, and bump its mtime,
    // on every single activation.
    if (original == canonical)
        return;

    // Audit BEFORE writing, never after. profile_sync's --rewrite-format path
    // records why in full: an earlier revision wrote first and audited the file it
    // had just clobbered, so by the time "DATA LOSS" appeared the original existed
    // only in git. Here there is no git — it is the user's profile.
    //
    // This check is ONE-DIRECTIONAL by design: it walks the keys of `original` and
    // reports what was lost or altered. It says nothing about keys the canonical
    // form ADDS, and it must not — canonicalising a legacy file legitimately adds
    // tank_temperature, target_volume_count_start and the simple-editor scalars, so
    // a symmetric check would refuse every conversion. Content ADDED by the reader
    // is therefore not caught here and has to be excluded by the caller instead;
    // see the espressoTemperatureHealed() guard at the call site.
    const QStringList parity = Profile::jsonParityErrors(original, canonical);
    if (!parity.isEmpty()) {
        qWarning() << "ProfileManager: leaving" << resolvedName
                   << "in its stored encoding — converting it would not be lossless:"
                   << parity.join(QStringLiteral("; "));
        return;
    }

    if (loaded.saveToFile(target))   // QSaveFile: temp + atomic rename
        qDebug() << "ProfileManager: upgraded stored encoding for" << resolvedName;
    else
        qWarning() << "ProfileManager: failed to upgrade stored encoding for" << resolvedName
                   << "- the profile loaded fine and is unchanged on disk";
}

bool ProfileManager::loadProfile(const QString& profileName) {
    QString path;
    bool found = false;
    // Which tier satisfied the load. Only the writable ones may have their
    // encoding upgraded; `:/profiles/` is a Qt resource and cannot be written
    // at all. Tracked explicitly rather than inferred from `path`, because the
    // storage tier never sets `path` and an empty path would read as "resource".
    enum class Origin { None, Storage, LocalFile, BuiltIn } origin = Origin::None;

    // Loaded into a candidate rather than straight into m_currentProfile so the
    // profile can be REFUSED without having already replaced the active one.
    // Assigning first and validating after would leave a profile we just decided
    // must not brew sitting in m_currentProfile, which is the whole failure this
    // guards against.
    Profile candidate;

    // Normalize: strip .json extension if present (legacy settings entries may include it)
    QString resolvedName = profileName;
    if (resolvedName.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        resolvedName = resolvedName.chopped(5);

    // User-initiated profile switch: reset any in-flight retry state so a
    // stale retry from the previous profile doesn't count toward the new
    // profile's attempt budget. We do NOT reset in uploadCurrentProfile()
    // itself because the retry timer calls that function — resetting there
    // would make kMaxUploadRetryAttempts unreachable.
    m_profileUploadRetryTimer.stop();
    m_profileUploadRetryAttempts = 0;
    m_lastUploadFailureReason.clear();
    updateProfileUploadRetrying();

    // Resolve profile name: could be title or filename (MQTT publishes titles)

    // First, check if it's a title (most common case from MQTT)
    for (auto it = m_profileTitles.begin(); it != m_profileTitles.end(); ++it) {
        if (it.value() == profileName) {
            resolvedName = it.key();  // Found filename for this title
            qDebug() << "ProfileManager::loadProfile: Resolved title" << profileName << "to filename" << resolvedName;
            break;
        }
    }

    // If not found as title, assume it's already a filename (fallback)

    // 1. Check ProfileStorage first (SAF folder on Android)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        // Use cached JSON from refreshProfiles() if available (avoids double-read at startup)
        QString jsonContent = m_profileJsonCache.take(resolvedName);
        if (jsonContent.isEmpty()) {
            jsonContent = m_profileStorage->readProfile(resolvedName);
        }
        if (!jsonContent.isEmpty()) {
            candidate = Profile::loadFromJsonString(jsonContent);
            found = true;
            origin = Origin::Storage;
            qDebug() << "Loaded profile from ProfileStorage:" << resolvedName;
        }
    }

    // 2. Check user profiles (local fallback)
    if (!found) {
        path = userProfilesPath() + "/" + resolvedName + ".json";
        if (QFile::exists(path)) {
            candidate = Profile::loadFromFile(path);
            found = true;
            origin = Origin::LocalFile;
        }
    }

    // 3. Check downloaded profiles (local fallback)
    if (!found) {
        path = downloadedProfilesPath() + "/" + resolvedName + ".json";
        if (QFile::exists(path)) {
            candidate = Profile::loadFromFile(path);
            found = true;
            origin = Origin::LocalFile;
        }
    }

    // 4. Check built-in profiles
    if (!found) {
        path = ":/profiles/" + resolvedName + ".json";
        if (QFile::exists(path)) {
            candidate = Profile::loadFromFile(path);
            found = true;
            origin = Origin::BuiltIn;
        }
    }

    // 5. Fall back to real default — this should not happen in normal operation after
    // startup validation removes stale references from favorites and currentProfile.
    if (!found) {
        qWarning() << "ProfileManager::loadProfile: Profile not found:" << profileName
                   << "(resolved:" << resolvedName << ")";
        emit profileLoadFailed(resolvedName);
        loadDefaultProfile();
        resolvedName = QStringLiteral("default");  // Track real default, not stale name
    }

    // Every path that brings a profile INTO the app already refuses an invalid
    // one (ProfileImporter, DataMigrationClient). This is the path that loads one
    // already on disk, and it did not check — so a profile stored before the
    // strict-import rule shipped, or dropped into the profile folder by another
    // app or a file sync, reached the DE1 unchecked.
    //
    // Refusing means keeping the CURRENTLY active profile rather than falling back
    // to default: the user asked to switch to this one, and silently brewing a
    // different profile is the same class of surprise as brewing the bad one. The
    // dialog names what we could not read so it can become a bug report.
    if (found && !candidate.isValid()) {
        qWarning() << "ProfileManager::loadProfile: refusing" << resolvedName
                   << "-" << candidate.validationErrors().join(QStringLiteral("; "));
        emit profileRefusedUnreadable(resolvedName, candidate.title(),
                                      candidate.unsupportedStepKeys(),
                                      candidate.malformedValues());
        return false;
    }

    // Upgrade the STORED encoding to canonical, if that is lossless.
    //
    // Deliberately on `candidate`, before any of the mutations below, and with two
    // exclusions that the parity check inside cannot make for itself:
    //
    //  - `espressoTemperatureHealed()`: fromJson DERIVES a missing or stale-default
    //    espresso_temperature from the frames, and jsonParityErrors only walks keys
    //    present in the ORIGINAL, so an added key is invisible to it. Without this
    //    guard the upgrade would write derived content under an "encoding" label —
    //    exactly what this pass must never do. The repair below owns that case and
    //    logs it as a repair.
    //  - `isReadOnly()`: the repair below skips read-only profiles and so does this,
    //    rather than one honouring the flag and the other quietly ignoring it.
    //
    // This pass converts ENCODING and must never change content, which is also what
    // keeps it clear of the rule against retro-rewriting user-set data.
    if (found && origin != Origin::BuiltIn && origin != Origin::None
        && !candidate.isReadOnly() && !candidate.espressoTemperatureHealed()) {
        upgradeStoredEncoding(resolvedName,
                              origin == Origin::LocalFile ? path : QString(),
                              candidate);
    }

    if (found)
        m_currentProfile = candidate;

    // Persist the removal of a stored `recipe` block, so it does not survive on disk
    // on a profile the user never re-saves.
    //
    // fromJson has already dropped it in memory and promoted any set dose; this is
    // only about the copy on disk. Same shape as the espresso_temperature repair
    // below — write once, gated on losing nothing else, in-memory-only if the
    // profile cannot be written (qrc built-ins, read-only stores).
    //
    // The parity gate is what makes this work at all: without `recipe` being excused
    // in deliberatelyDroppedKeys(), the check would report it as a lost key and
    // refuse the very write that removes it.
    if (found && m_currentProfile.recipeBlockStripped() && !m_currentProfile.isReadOnly()) {
        QStringList parity;
        switch (writeProfileBackIfLossless(resolvedName, path, origin == Origin::Storage,
                                           m_currentProfile, QString(), &parity)) {
        case WriteBack::Written:
            qInfo() << "ProfileManager::loadProfile: removed stored recipe block from"
                    << resolvedName;
            break;
        case WriteBack::Refused:
            // qInfo, not qWarning: a profile stored in a non-canonical encoding can
            // never be rewritten losslessly, so this would fire on every load of that
            // profile forever. The block staying on disk is harmless — nothing reads
            // one — and it is already gone in memory.
            qInfo() << "ProfileManager::loadProfile: leaving the recipe block on disk for"
                    << resolvedName << "-" << parity.join(QStringLiteral("; "))
                    << "- dropped in memory only";
            break;
        case WriteBack::Failed:
            // Loud: unlike a refusal this CAN succeed later, and a permanently failing
            // store (revoked SAF grant, read-only volume, full disk) would otherwise
            // retry in silence on every load forever.
            qWarning() << "ProfileManager::loadProfile: failed to persist the recipe-block"
                       << "removal for" << resolvedName
                       << "- dropped in memory only, will retry on next load";
            break;
        case WriteBack::NotWritable:
            break;   // qrc built-in or no store; in-memory removal is all there is
        }
        // Cleared either way: the retry driver is the block still being on disk, read
        // afresh by fromJson on the next load, not this transient flag.
        m_currentProfile.clearRecipeBlockStripped();
    }

    // One-time on-disk repair: if fromJson had to re-derive a missing or
    // stale-default espresso_temperature from the frames (e.g. the 93.0 default
    // baked in by an older Visualizer import — see Profile::fromJson), persist the
    // corrected value so the profile is repaired exactly once instead of re-healed
    // on every load. Skip read-only/built-in profiles (qrc, can't and needn't be
    // rewritten).
    if (found && m_currentProfile.espressoTemperatureHealed() && !m_currentProfile.isReadOnly()) {
        // Gated like every write here. It re-serializes the WHOLE profile, so it can
        // carry unrelated losses — a value that does not survive the writer's
        // precision, say — into the user's file under the banner of a temperature
        // repair. espresso_temperature is excluded from the comparison because
        // changing it is the entire point; everything else must be unchanged.
        //
        // This block used to read its "before" bytes from `path` while writing to
        // ProfileStorage, so the storage tier was rewritten unaudited. Routing it
        // through the shared helper is what makes the read and the write agree on
        // where the profile actually lives.
        QStringList parity;
        switch (writeProfileBackIfLossless(resolvedName, path, origin == Origin::Storage,
                                           m_currentProfile,
                                           QStringLiteral("espresso_temperature"), &parity)) {
        case WriteBack::Written:
            qInfo() << "ProfileManager::loadProfile: repaired stale espresso_temperature"
                    << "on disk for" << resolvedName;
            break;
        case WriteBack::Refused:
            qWarning() << "ProfileManager::loadProfile: NOT persisting the"
                       << "espresso_temperature repair for" << resolvedName << "-"
                       << parity.join(QStringLiteral("; ")) << "- corrected in memory only";
            break;
        case WriteBack::Failed:
            qWarning() << "ProfileManager::loadProfile: failed to persist espresso_temperature"
                       << "repair for" << resolvedName
                       << "- corrected in memory only, will retry on next load";
            break;
        case WriteBack::NotWritable:
            break;
        }
    }

    // Backfill empty notes from the built-in (handles imported copies from before
    // notes were added).
    //
    // LAST, after every on-disk write above, and in memory only. This injects text
    // the user's file does not contain; when it ran before the repair above, that
    // repair serialized the whole profile and persisted the injected notes into the
    // user's file. Any future write added to this function belongs ABOVE this block.
    if (found && m_currentProfile.profileNotes().isEmpty()) {
        QString builtInPath = ":/profiles/" + resolvedName + ".json";
        if (QFile::exists(builtInPath)) {
            Profile builtIn = Profile::loadFromFile(builtInPath);
            if (!builtIn.profileNotes().isEmpty()) {
                m_currentProfile.setProfileNotes(builtIn.profileNotes());
            }
        }
    }

    // Save current profile as previous before switching (only if new profile was found)
    if (found && !m_baseProfileName.isEmpty() && m_baseProfileName != resolvedName)
        m_previousProfileName = m_baseProfileName;

    // Track the base profile name (filename without extension)
    m_baseProfileName = resolvedName;
    bool wasModified = m_profileModified;
    m_profileModified = false;

    // Remove stale temp file so next startup loads the correct profile
    if (wasModified) {
        QString tempPath = profilesPath() + "/_current.json";
        QFile::remove(tempPath);
    }

    if (m_settings) {
        m_settings->app()->setCurrentProfile(resolvedName);
        // Sync selectedFavoriteProfile with the loaded profile
        // This ensures the UI shows the correct pill as selected, or -1 if not a favorite
        int favoriteIndex = m_settings->app()->findFavoriteIndexByFilename(resolvedName);
        qDebug() << "loadProfile:" << resolvedName << "favoriteIndex=" << favoriteIndex;
        m_settings->app()->setSelectedFavoriteProfile(favoriteIndex);
    }

    // Reset brew overrides for the new profile: a profile switch clears them
    // (flags genuinely go false); the startup load preserves persisted ones.
    resetBrewOverridesForLoadedProfile();
    applyRecommendedDoseIfProfileOwnsIt();

    if (m_machineState) {
        m_machineState->setTargetWeight(targetWeight());
        m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        m_machineState->setProfileType(m_currentProfile.profileType());
    }

    // Upload to machine if connected (for frame-based mode)
    if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
        uploadCurrentProfile();
    }

    // Apply per-profile flow calibration if auto-cal is enabled
    applyFlowCalibration();

    emit currentProfileChanged();
    emit targetWeightChanged();
    if (wasModified) {
        emit profileModifiedChanged();
    }

    // `found` and not refused — the requested profile is the active one. When it
    // was not found we loaded the default above and carried on, which is a load
    // but not the one that was asked for.
    return found;
}

bool ProfileManager::loadProfileFromJson(const QString& jsonContent) {
    if (jsonContent.isEmpty()) {
        qWarning() << "loadProfileFromJson: Empty JSON content";
        return false;
    }

    // Fresh profile load = fresh retry budget (see loadProfile() for the
    // full rationale).
    m_profileUploadRetryTimer.stop();
    m_profileUploadRetryAttempts = 0;
    m_lastUploadFailureReason.clear();
    updateProfileUploadRetrying();

    m_currentProfile = Profile::loadFromJsonString(jsonContent);

    if (m_currentProfile.title().isEmpty() || m_currentProfile.steps().isEmpty()) {
        qWarning() << "loadProfileFromJson: Failed to parse profile JSON";
        return false;
    }

    // Use title as base name since this profile isn't from a file
    m_baseProfileName = m_currentProfile.title();
    m_profileModified = false;

    if (m_settings) {
        // Set selectedFavoriteProfile to -1 to show non-favorite pill
        // Profiles loaded from JSON (e.g., shot history) are typically not in favorites
        m_settings->app()->setSelectedFavoriteProfile(-1);
    }
    // New profile, no overrides: the caller (e.g. shot load) re-applies its own.
    resetBrewOverridesForLoadedProfile();

    if (m_machineState) {
        m_machineState->setTargetWeight(targetWeight());
        m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        m_machineState->setProfileType(m_currentProfile.profileType());
    }

    // Upload to machine if connected (for frame-based mode)
    if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
        uploadCurrentProfile();
    }

    qDebug() << "Loaded profile from JSON:" << m_currentProfile.title()
             << "with" << m_currentProfile.steps().size() << "steps";

    emit currentProfileChanged();
    emit targetWeightChanged();
    return true;
}

bool ProfileManager::persistCurrentProfile() {
    if (m_currentProfile.title().isEmpty() || m_currentProfile.steps().isEmpty())
        return false;
    // Skip if already installed — don't shadow an existing profile
    if (!findProfileByTitle(m_currentProfile.title()).isEmpty())
        return false;

    QString filename = titleToFilename(m_currentProfile.title());
    QString path = downloadedProfilesPath() + "/" + filename + ".json";
    if (!m_currentProfile.saveToFile(path)) {
        qWarning() << "ProfileManager::persistCurrentProfile: failed to save" << m_currentProfile.title();
        return false;
    }
    qDebug() << "ProfileManager: persisted shot profile" << m_currentProfile.title() << "to downloaded folder";
    refreshProfiles();
    return true;
}

void ProfileManager::refreshProfiles() {
    m_availableProfiles.clear();
    m_profileTitles.clear();
    m_allProfiles.clear();
    m_profileJsonCache.clear();

    // Helper to extract profile metadata from a JSON object
    // Named rather than a tuple: this carried seven positional fields
    // destructured at four call sites, and the shape-resolution work needed
    // two more. A nine-element structured binding is unreadable and mis-orders
    // silently, so it is a struct — and for the same reason it is populated by
    // NAME below rather than by a positional brace-literal, which would have
    // reintroduced exactly the hazard (two adjacent QStrings and two adjacent
    // doubles, each pair silently swappable).
    struct ProfileMeta {
        QString title;
        QString beverageType;
        bool    hasKnowledgeBase = false;
        // "advanced" is the editor a profile with no recognisable type falls
        // back to, and it is what the unreadable-file case below wants, so it
        // is the member's default rather than a literal repeated at that site.
        QString editorType = QStringLiteral("advanced");
        bool    readOnly = false;
        double  espressoTemperature = 0;
        double  targetWeight = 0;
        // Canonical display name of the entry a SHAPE match landed on; empty
        // for a title match, a miss, or an ambiguous shape. See
        // ProfileInfo::kbDerivedFrom.
        QString kbDerivedFrom;
        QStringList kbIds;
    };

    auto extractProfileMeta = [](const QJsonObject& obj) -> ProfileMeta {
        ProfileMeta meta;
        meta.title = obj["title"].toString();
        meta.beverageType = obj["beverage_type"].toString();
        meta.readOnly = (obj["read_only"].toInt(0) == 1);
        // Tolerant parse: Visualizer-format profile JSON stores these as
        // STRINGS — a raw toDouble() would cache 0 ("unstated") for them.
        meta.espressoTemperature = profileJsonToDouble(obj["espresso_temperature"]);
        meta.targetWeight = profileJsonToDouble(obj["target_weight"]);

        // Derive editor type from title + profileType (matching Profile::editorType())
        const QString t = meta.title.startsWith(QLatin1Char('*'))
                              ? meta.title.mid(1) : meta.title;
        if (t.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive))
            meta.editorType = QStringLiteral("dflow");
        else if (t.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive))
            meta.editorType = QStringLiteral("aflow");
        else {
            QString profileType = obj["legacy_profile_type"].toString();
            if (profileType.isEmpty()) profileType = obj["profile_type"].toString();
            if (profileType == QLatin1String("settings_2a"))
                meta.editorType = QStringLiteral("pressure");
            else if (profileType == QLatin1String("settings_2b"))
                meta.editorType = QStringLiteral("flow");
            else
                meta.editorType = QStringLiteral("advanced");
        }

        // Title resolution first — cheap, and the answer for every built-in
        // and every profile whose name still carries its origin.
        const QString titleKbId =
            ShotSummarizer::computeProfileKbId(meta.title, meta.editorType);

        // Only a title MISS pays for shape resolution, which needs a full
        // Profile::fromJson rather than the handful of fields read above. The
        // catalog scan visits every profile, so making this unconditional
        // would add that parse to all of them for the benefit of a few.
        if (!titleKbId.isEmpty()) {
            meta.kbIds << titleKbId;
        } else {
            const Profile parsed = Profile::fromJson(QJsonDocument(obj));
            if (!parsed.steps().isEmpty()) {
                const KbResolution r = resolveProfileKb(parsed);
                meta.kbIds = r.ids;
                // IDENTITY is the stricter claim and needs a single candidate.
                // An ambiguous shape still lends its suppression flags to the
                // analysis — so the whole SET is kept, and the indicator is
                // driven by that — but there is no one entry to name, so
                // kbDerivedFrom stays empty.
                if (r.hasIdentity() && r.origin == KbResolution::Origin::Shape)
                    meta.kbDerivedFrom =
                        ShotSummarizer::canonicalNameForKbId(r.ids.first());
            }
        }
        // The indicator means "the badges and summary were shaped by KB
        // knowledge", which an ambiguous match does as much as a unique one.
        // Derived from the same set the content lookup uses, so the two agree
        // about whether there is anything to show — as long as every entry in
        // the KB actually carries prose, since profileKnowledgeContent() drops
        // a candidate whose body is empty. All 47 shipped entries do (checked);
        // this is a data property, not a structural guarantee, so an entry
        // added with an empty body would light a sparkle over an empty dialog.
        meta.hasKnowledgeBase = !meta.kbIds.isEmpty();
        return meta;
    };

    // Helper to load profile metadata from file path
    auto loadProfileMeta = [&extractProfileMeta](const QString& path) -> ProfileMeta {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            return extractProfileMeta(doc.object());
        }
        return {};   // unreadable file: every field at its default
    };

    // Helper to load profile metadata from JSON string
    auto loadProfileMetaFromJson = [&extractProfileMeta](const QString& jsonContent) -> ProfileMeta {
        QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8());
        return extractProfileMeta(doc.object());
    };

    // The ONE ProfileMeta -> ProfileInfo projection the four scan passes below
    // share. It was written out four times, and this change had to add the two
    // shape fields to each of them by hand — the fourth copy was one edit away
    // from silently shipping without kbIds, which reads as "this profile has
    // no knowledge" rather than as a missing assignment. Only `source` and the
    // built-in read-only override actually differ between the passes, so those
    // are the parameters and everything else is written once.
    auto makeProfileInfo = [](const QString& name, const ProfileMeta& meta,
                              ProfileSource source) {
        ProfileInfo info;
        info.filename = name;
        info.title = meta.title.isEmpty() ? name : meta.title;
        info.beverageType = meta.beverageType;
        info.editorType = meta.editorType;
        info.espressoTemperature = meta.espressoTemperature;
        info.targetWeight = meta.targetWeight;
        info.source = source;
        info.hasKnowledgeBase = meta.hasKnowledgeBase;
        info.kbDerivedFrom = meta.kbDerivedFrom;
        info.kbIds = meta.kbIds;
        // Built-in profiles are always read-only, whatever their file says.
        info.readOnly = (source == ProfileSource::BuiltIn) ? true : meta.readOnly;
        return info;
    };

    QStringList filters;
    filters << "*.json";

    // 1. Load built-in profiles (always available)
    QDir builtInDir(":/profiles");
    QStringList files = builtInDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);  // Remove .json
        const ProfileMeta meta = loadProfileMeta(":/profiles/" + file);

        const ProfileInfo info = makeProfileInfo(name, meta, ProfileSource::BuiltIn);
        m_allProfiles.append(info);

        m_availableProfiles.append(name);
        m_profileTitles[name] = info.title;
    }

    // 2. Load profiles from ProfileStorage (SAF folder or fallback)
    // ProfileStorage takes loading priority over built-in (loadProfile checks it first),
    // so if a copy exists here it should override the built-in entry in the list too.
    if (m_profileStorage) {
        QStringList storageProfiles = m_profileStorage->listProfiles();
        for (const QString& name : storageProfiles) {
            QString jsonContent = m_profileStorage->readProfile(name);
            if (jsonContent.isEmpty()) {
                continue;
            }

            // Cache for loadProfile() to avoid re-reading from storage
            m_profileJsonCache[name] = jsonContent;

            const ProfileMeta meta = loadProfileMetaFromJson(jsonContent);

            const ProfileInfo info =
                makeProfileInfo(name, meta, ProfileSource::UserCreated);

            if (m_availableProfiles.contains(name)) {
                // Override built-in entry so list matches what loadProfile() actually loads
                for (qsizetype i = 0; i < m_allProfiles.size(); ++i) {
                    if (m_allProfiles[i].filename == name) {
                        m_allProfiles[i] = info;
                        break;
                    }
                }
            } else {
                m_allProfiles.append(info);
                m_availableProfiles.append(name);
            }
            m_profileTitles[name] = info.title;
        }
    }

    // 3. Load downloaded profiles (legacy local folder)
    QDir downloadedDir(downloadedProfilesPath());
    files = downloadedDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        if (m_availableProfiles.contains(name)) {
            continue;  // Skip if already loaded from ProfileStorage
        }

        const ProfileMeta meta = loadProfileMeta(downloadedDir.filePath(file));

        const ProfileInfo info = makeProfileInfo(name, meta, ProfileSource::Downloaded);
        m_allProfiles.append(info);

        m_availableProfiles.append(name);
        m_profileTitles[name] = info.title;
    }

    // 4. Load user-created profiles (legacy local folder)
    QDir userDir(userProfilesPath());
    files = userDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        if (m_availableProfiles.contains(name)) {
            continue;  // Skip if already loaded from ProfileStorage
        }

        const ProfileMeta meta = loadProfileMeta(userDir.filePath(file));

        const ProfileInfo info = makeProfileInfo(name, meta, ProfileSource::UserCreated);
        m_allProfiles.append(info);

        m_availableProfiles.append(name);
        m_profileTitles[name] = info.title;
    }

    // Validate favorites and currentProfile against the known profile set.
    // Removes favorites that reference profiles not found in any directory, and resets
    // a stale currentProfile to the first remaining valid favorite (or "default").
    // Runs every time refreshProfiles() is called so stale references are cleaned up at startup.
    if (m_settings) {
        QSet<QString> known(m_availableProfiles.begin(), m_availableProfiles.end());

        QVariantList favorites = m_settings->app()->favoriteProfiles();
        for (qsizetype i = favorites.size() - 1; i >= 0; --i) {
            QString fn = favorites.at(i).toMap()[QStringLiteral("filename")].toString();
            if (!known.contains(fn)) {
                qWarning() << "refreshProfiles: removing stale favorite" << fn << "(profile not found)";
                m_settings->app()->removeFavoriteProfile(static_cast<int>(i));
            }
        }

        QString cp = m_settings->app()->currentProfile();
        if (cp.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
            cp = cp.chopped(5);
        if (!cp.isEmpty() && !known.contains(cp)) {
            QString replacement = QStringLiteral("default");
            QVariantList updatedFavs = m_settings->app()->favoriteProfiles();
            if (!updatedFavs.isEmpty())
                replacement = updatedFavs.first().toMap()[QStringLiteral("filename")].toString();
            qWarning() << "refreshProfiles: stale currentProfile" << cp
                       << "-> replacing with" << replacement;
            m_settings->app()->setCurrentProfile(replacement);
        }
    }

    emit profilesChanged();
    emit allBuiltInProfileListChanged();
}


// === Profile upload ===

void ProfileManager::updateProfileUploadRetrying() {
    const bool retrying = m_profileUploadRetryAttempts > 0
                          && m_profileUploadRetryTimer.isActive();
    if (retrying == m_profileUploadRetrying) return;
    m_profileUploadRetrying = retrying;
    emit profileUploadRetryingChanged();
}

void ProfileManager::acknowledgeDe1CommunicationFailure() {
    // The user dismissed the communication-failure dialog. Clear the flag
    // (so the dialog goes away) and reset retry state so the next
    // uploadCurrentProfile() starts from a clean attempt counter. We do NOT
    // auto-trigger a new upload here; the user is expected to power-cycle
    // the DE1, after which the normal reconnect path re-uploads.
    if (m_de1CommunicationFailure) {
        m_de1CommunicationFailure = false;
        emit de1CommunicationFailureChanged();
    }
    m_profileUploadRetryTimer.stop();
    m_profileUploadRetryAttempts = 0;
    m_lastUploadFailureReason.clear();
    updateProfileUploadRetrying();
}

void ProfileManager::uploadCurrentProfile() {
    // Guard: Don't upload profile during active operations - this corrupts the running shot!
    if (m_machineState) {
        auto phase = m_machineState->phase();
        bool isActivePhase = (phase == MachineState::Phase::EspressoPreheating ||
                              phase == MachineState::Phase::Preinfusion ||
                              phase == MachineState::Phase::Pouring ||
                              phase == MachineState::Phase::Ending ||
                              phase == MachineState::Phase::Steaming ||
                              phase == MachineState::Phase::HotWater ||
                              phase == MachineState::Phase::Flushing ||
                              phase == MachineState::Phase::Descaling ||
                              phase == MachineState::Phase::Cleaning ||
                              phase == MachineState::Phase::Transport);

        if (isActivePhase) {
            qWarning() << "uploadCurrentProfile() BLOCKED during active phase:"
                       << m_machineState->phaseString();

            QString stackTrace = "Stack trace:\n";
#ifndef Q_OS_WIN
            void* stack[15];
            size_t frameCount = captureBacktrace(stack, 15);
            for (size_t i = 0; i < frameCount; i++) {
                Dl_info info;
                QString frameLine;
                if (dladdr(stack[i], &info) && info.dli_sname) {
                    frameLine = QString("  #%1: %2 (+%3)")
                        .arg(i)
                        .arg(info.dli_sname)
                        .arg(reinterpret_cast<quintptr>(stack[i]) - reinterpret_cast<quintptr>(info.dli_saddr));
                } else {
                    frameLine = QString("  #%1: 0x%2")
                        .arg(i)
                        .arg(reinterpret_cast<quintptr>(stack[i]), 0, 16);
                }
                stackTrace += frameLine + "\n";
                qWarning().noquote() << frameLine;
            }
#else
            stackTrace += "  (not available on Windows)\n";
#endif
            emit profileUploadBlocked(m_machineState->phaseString(), stackTrace);
            m_profileUploadPending = true;
            return;  // Don't upload!
        }
    }

    if (m_device && m_device->isConnected()) {
        // If a profile upload is already in flight, defer this one rather than flooding the
        // Android BLE GATT write queue. The profileUploaded signal will trigger a follow-up
        // upload when the current one completes, carrying the latest m_currentProfile.
        if (m_uploadInFlight) {
            qDebug() << "ProfileManager: uploadCurrentProfile deferred — "
                        "upload in flight, will retry on profileUploaded";
            m_uploadPendingAfterInFlight = true;
            return;
        }

        m_profileUploadPending = false;
        m_uploadInFlight = true;
        double groupTemp;

        // Apply temperature override as delta offset (preserves per-frame differences)
        if (m_settings && m_settings->brew()->hasTemperatureOverride()) {
            Profile modifiedProfile = m_currentProfile;
            double overrideTemp = m_settings->brew()->temperatureOverride();
            modifiedProfile.setSteps(framesShiftedToTemperature(overrideTemp));
            modifiedProfile.setEspressoTemperature(overrideTemp);
            qDebug() << "Uploading profile with temperature override:" << overrideTemp
                     << "C (delta:" << (overrideTemp - m_currentProfile.espressoTemperature()) << "C)";
            m_device->uploadProfile(modifiedProfile);
            groupTemp = overrideTemp;
        } else {
            m_device->uploadProfile(m_currentProfile);
            groupTemp = m_currentProfile.espressoTemperature();
        }

        // Update shot settings with the profile's target temperature
        // This controls what temperature the machine heats to in Ready state
        if (m_settings) {
            // Resolved by SteamHeaterPolicy, never derived here. This used to be a
            // private copy of the rule that knew only steamDisabled and
            // keepSteamHeaterOn — not the active recipe — so every profile upload
            // re-sent steam = 0 for a keep-heater-off user. Recipe activation
            // triggers an upload, and that upload is deferred behind
            // m_uploadInFlight, so it landed AFTER startSteamHeating() and undid
            // it: picking a milk recipe left the steam heater cold.
            // No policy means no opinion about the steam target — send what the
            // machine already holds rather than a second, differently-derived
            // answer. Every production and test construction passes one.
            if (!m_steamHeaterPolicy) {
                qWarning() << "ProfileManager: no steam heater policy — skipping the profile"
                              " upload's ShotSettings write rather than guessing a steam target";
                return;
            }
            double steamTemp = m_steamHeaterPolicy->commandedTemperatureC();
            m_device->setShotSettings(
                steamTemp,
                m_settings->brew()->steamTimeout(),
                m_settings->brew()->waterTemperature(),
                m_settings->brew()->effectiveHotWaterVolume(),
                groupTemp,
                QStringLiteral("uploadCurrentProfile")
            );
            qDebug() << "Set group temp to" << groupTemp << "C for profile" << m_currentProfile.title();
        }
    } else if (m_profileUploadPending) {
        qDebug() << "uploadCurrentProfile: device not connected, keeping pending flag for later retry";
    }
}

void ProfileManager::uploadProfile(const QVariantMap& profileData) {
    // Update current profile from QML data
    if (profileData.contains("title")) {
        m_currentProfile.setTitle(profileData["title"].toString());
    }
    if (profileData.contains("author")) {
        m_currentProfile.setAuthor(profileData["author"].toString());
    }
    if (profileData.contains("notes")) {
        m_currentProfile.setProfileNotes(profileData["notes"].toString());
    } else if (profileData.contains("profile_notes")) {
        m_currentProfile.setProfileNotes(profileData["profile_notes"].toString());
    }
    if (profileData.contains("espresso_temperature")) {
        double newTemp = profileData["espresso_temperature"].toDouble();
        m_currentProfile.setEspressoTemperature(newTemp);
        // The edited temperature IS the new profile default — any live override
        // is now stale, so clear it (uploadCurrentProfile must not re-delta it).
        if (m_settings) {
            m_settings->brew()->clearTemperatureOverride();
        }
    }
    if (profileData.contains("target_weight")) {
        double newWeight = profileData["target_weight"].toDouble();
        m_currentProfile.setTargetWeight(newWeight);
        if (m_machineState) {
            m_machineState->setTargetWeight(newWeight);
        }
        // The edited weight IS the new profile default — clear any live yield
        // override so the plan follows it instead of a stale number.
        if (m_settings) {
            m_settings->brew()->setBrewYieldOverride(0);
        }
    }
    if (profileData.contains("target_volume")) {
        m_currentProfile.setTargetVolume(profileData["target_volume"].toDouble());
        if (m_machineState) {
            m_machineState->setTargetVolume(m_currentProfile.targetVolume());
            m_machineState->setProfileType(m_currentProfile.profileType());
        }
    }
    if (profileData.contains("has_recommended_dose")) {
        m_currentProfile.setHasRecommendedDose(profileData["has_recommended_dose"].toBool());
    }
    if (profileData.contains("recommended_dose")) {
        m_currentProfile.setRecommendedDose(profileData["recommended_dose"].toDouble());
    }
    if (profileData.contains("tank_desired_water_temperature")) {
        m_currentProfile.setTankDesiredWaterTemperature(profileData["tank_desired_water_temperature"].toDouble());
    }
    if (profileData.contains("maximum_flow_range_advanced")) {
        m_currentProfile.setMaximumFlowRangeAdvanced(profileData["maximum_flow_range_advanced"].toDouble());
    }
    if (profileData.contains("maximum_pressure_range_advanced")) {
        m_currentProfile.setMaximumPressureRangeAdvanced(profileData["maximum_pressure_range_advanced"].toDouble());
    }
    if (profileData.contains("preinfuse_frame_count")) {
        m_currentProfile.setPreinfuseFrameCount(profileData["preinfuse_frame_count"].toInt());
    }

    // Update steps/frames - build new list atomically to avoid any reference issues
    if (profileData.contains("steps")) {
        QList<ProfileFrame> newSteps;
        QVariantList steps = profileData["steps"].toList();
        newSteps.reserve(steps.size());
        for (const QVariant& stepVar : steps) {
            QVariantMap step = stepVar.toMap();
            ProfileFrame frame;
            frame.name = step["name"].toString();
            frame.temperature = step["temperature"].toDouble();
            frame.sensor = step["sensor"].toString();
            frame.pump = step["pump"].toString();
            frame.transition = step["transition"].toString();
            frame.pressure = step["pressure"].toDouble();
            frame.flow = step["flow"].toDouble();
            frame.seconds = step["seconds"].toDouble();
            frame.volume = step["volume"].toDouble();
            frame.exitIf = step["exit_if"].toBool();
            frame.exitType = step["exit_type"].toString();
            frame.exitPressureOver = step["exit_pressure_over"].toDouble();
            frame.exitPressureUnder = step["exit_pressure_under"].toDouble();
            frame.exitFlowOver = step["exit_flow_over"].toDouble();
            frame.exitFlowUnder = step["exit_flow_under"].toDouble();
            frame.exitWeight = step["exit_weight"].toDouble();
            frame.popup = step["popup"].toString();
            frame.maxFlowOrPressure = step["max_flow_or_pressure"].toDouble();
            frame.maxFlowOrPressureRange = step["max_flow_or_pressure_range"].toDouble();
            newSteps.append(frame);
        }
        // Replace all steps atomically
        m_currentProfile.setSteps(newSteps);

        qDebug() << "uploadProfile: Updated" << newSteps.size() << "steps";
        for (int i = 0; i < newSteps.size(); i++) {
            qDebug() << "  Frame" << i << ":" << newSteps[i].name << "temp=" << newSteps[i].temperature;
        }
    }

    // Mark as modified
    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }

    // Save to temp file for persistence across restarts
    QString tempPath = profilesPath() + "/_current.json";
    if (!m_currentProfile.saveToFile(tempPath)) {
        qWarning() << "Failed to save modified profile to temp file:" << tempPath;
    }

    // NOTE: BLE upload deferred to editor exit (QML calls uploadCurrentProfile() explicitly).
    // This avoids flooding the DE1 with BLE writes on every slider tick. See #557.

    emit currentProfileChanged();
}

bool ProfileManager::saveProfile(const QString& filename) {
    // Prevent saving over read-only profiles
    if (isCurrentProfileReadOnly()) {
        qWarning() << "ProfileManager::saveProfile: Cannot save read-only profile in place:" << filename;
        return false;
    }

    bool success = false;

    // Try ProfileStorage first (SAF on Android), then fall back to local file
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        success = m_profileStorage->writeProfile(filename, m_currentProfile.toJsonString());
        if (success) {
            qDebug() << "Saved profile to ProfileStorage:" << filename;
        }
    }

    if (!success) {
        // Fall back to local file
        QString path = userProfilesPath() + "/" + filename + ".json";
        success = m_currentProfile.saveToFile(path);
        if (success) {
            qDebug() << "Saved profile to local file:" << path;
        } else {
            qWarning() << "Failed to save profile to:" << path;
        }
    }

    if (success) {
        // If saving a built-in profile, auto-select it and update favorites
        if (m_settings) {
            // Check if this was originally a built-in
            bool wasBuiltIn = false;
            for (const ProfileInfo& info : m_allProfiles) {
                if (info.filename == m_baseProfileName && info.source == ProfileSource::BuiltIn) {
                    wasBuiltIn = true;
                    break;
                }
            }

            // If it was a built-in and is in favorites, the favorite now points to user copy
            if (wasBuiltIn && m_settings->app()->isFavoriteProfile(m_baseProfileName)) {
                m_settings->app()->updateFavoriteProfile(m_baseProfileName, filename, m_currentProfile.title());
            }
        }

        m_baseProfileName = filename;
        markProfileClean();
        refreshProfiles();

        // Re-upload profile to machine to ensure it's synced after save
        if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
            uploadCurrentProfile();
        }
    }
    return success;
}

bool ProfileManager::saveProfileAs(const QString& filename, const QString& title) {
    // Prevent saving with a built-in profile filename
    if (isBuiltInFilename(filename)) {
        qWarning() << "ProfileManager::saveProfileAs: Cannot overwrite built-in profile filename:" << filename;
        return false;
    }

    // Remember old filename for favorite update
    QString oldFilename = m_baseProfileName;

    // Clear read-only flag on Save As copies — user copies are always editable
    m_currentProfile.setReadOnly(0);

    // Update the profile title
    m_currentProfile.setTitle(title);

    bool success = false;

    // Try ProfileStorage first (SAF on Android), then fall back to local file
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        success = m_profileStorage->writeProfile(filename, m_currentProfile.toJsonString());
        if (success) {
            qDebug() << "Saved profile as to ProfileStorage:" << filename;
        }
    }

    if (!success) {
        // Fall back to local file
        QString path = userProfilesPath() + "/" + filename + ".json";
        success = m_currentProfile.saveToFile(path);
        if (success) {
            qDebug() << "Saved profile as to local file:" << path;
        } else {
            qWarning() << "Failed to save profile to:" << path;
        }
    }

    if (success) {
        m_baseProfileName = filename;
        if (m_settings) {
            m_settings->app()->setCurrentProfile(filename);

            // Handle favorites based on whether this is a true "Save As" or just "Save"
            if (!oldFilename.isEmpty() && oldFilename != filename) {
                // True "Save As" - keep original favorite, add new profile to favorites
                m_settings->app()->addFavoriteProfile(title, filename);
            } else if (!oldFilename.isEmpty()) {
                // Same filename - just update the title if it changed
                m_settings->app()->updateFavoriteProfile(oldFilename, filename, title);
            } else {
                // New profile (no old filename) - add to favorites
                m_settings->app()->addFavoriteProfile(title, filename);
            }
        }
        markProfileClean();
        refreshProfiles();

        // Re-upload profile to machine to ensure it's synced after save
        // This catches edge cases where the previous upload may not have completed
        if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
            uploadCurrentProfile();
        }

        emit currentProfileChanged();
    }
    return success;
}

bool ProfileManager::duplicateProfile(const QString& sourceFilename, const QString& newTitle) {
    // Generate a unique filename from the title
    QString newFilename = titleToFilename(newTitle);

    // titleToFilename transliterates accented Latin, then replaces remaining non-alphanumerics
    // with underscores. Only emoji/punctuation/symbol-only titles (no Unicode letters or digits)
    // reduce to an empty string; CJK and Cyrillic titles produce a non-empty Unicode filename.
    if (newFilename.isEmpty()) {
        qWarning() << "ProfileManager::duplicateProfile: title sanitises to empty filename:" << newTitle;
        return false;
    }

    // Prevent duplicating with a built-in profile filename
    if (isBuiltInFilename(newFilename)) {
        qWarning() << "ProfileManager::duplicateProfile: Cannot use built-in profile filename:" << newFilename;
        return false;
    }

    // Check if the new filename already exists in any of the locations we might write to.
    // profileExists() only checks m_availableProfiles + profilesPath() root, so we also
    // probe ProfileStorage and the user/downloaded folders to avoid silent overwrite when
    // m_availableProfiles is stale.
    auto collides = [this](const QString& fn) {
        if (profileExists(fn))
            return true;
        if (m_profileStorage && m_profileStorage->isConfigured()
            && m_profileStorage->profileExists(fn))
            return true;
        if (QFile::exists(userProfilesPath() + "/" + fn + ".json"))
            return true;
        if (QFile::exists(downloadedProfilesPath() + "/" + fn + ".json"))
            return true;
        return false;
    };
    if (collides(newFilename)) {
        qWarning() << "ProfileManager::duplicateProfile: Profile already exists:" << newFilename;
        return false;
    }

    // Load the source profile JSON. Order mirrors loadProfile() so a user-edited copy
    // of a built-in profile is preferred over the pristine resource.
    QString jsonContent;

    // 1. ProfileStorage (SAF on Android)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        jsonContent = m_profileStorage->readProfile(sourceFilename);
    }

    // 2. User profiles (local fallback)
    if (jsonContent.isEmpty()) {
        QString userPath = userProfilesPath() + "/" + sourceFilename + ".json";
        QFile userFile(userPath);
        if (userFile.open(QIODevice::ReadOnly)) {
            jsonContent = QString::fromUtf8(userFile.readAll());
        }
    }

    // 3. Downloaded profiles (local fallback)
    if (jsonContent.isEmpty()) {
        QString downloadedPath = downloadedProfilesPath() + "/" + sourceFilename + ".json";
        QFile downloadedFile(downloadedPath);
        if (downloadedFile.open(QIODevice::ReadOnly)) {
            jsonContent = QString::fromUtf8(downloadedFile.readAll());
        }
    }

    // 4. Built-in profiles
    if (jsonContent.isEmpty()) {
        QString builtInPath = ":/profiles/" + sourceFilename + ".json";
        QFile builtInFile(builtInPath);
        if (builtInFile.open(QIODevice::ReadOnly)) {
            jsonContent = QString::fromUtf8(builtInFile.readAll());
        }
    }

    if (jsonContent.isEmpty()) {
        qWarning() << "ProfileManager::duplicateProfile: Could not load source profile:" << sourceFilename;
        return false;
    }

    // Verify the JSON is well-formed before handing it to Profile::loadFromJsonString,
    // which returns a default-constructed Profile (title="Default") on parse failure —
    // so a checking-the-title guard alone would silently save a near-empty profile.
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "ProfileManager::duplicateProfile: source JSON is malformed:" << parseErr.errorString();
        return false;
    }

    Profile duplicatedProfile = Profile::loadFromJsonString(jsonContent);

    // Clear read-only so the copy is editable even when duplicated from a built-in profile
    duplicatedProfile.setTitle(newTitle);
    duplicatedProfile.setReadOnly(0);

    bool success = false;
    
    // Try ProfileStorage first (SAF on Android)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        success = m_profileStorage->writeProfile(newFilename, duplicatedProfile.toJsonString());
        if (success) {
            qDebug() << "Duplicated profile to ProfileStorage:" << newFilename;
        }
    }

    if (!success) {
        // Fall back to local file
        QString path = userProfilesPath() + "/" + newFilename + ".json";
        success = duplicatedProfile.saveToFile(path);
        if (success) {
            qDebug() << "Duplicated profile to local file:" << path;
        } else {
            qWarning() << "Failed to save duplicated profile to:" << path;
        }
    }

    if (success) {
        if (m_settings) {
            m_settings->app()->addSelectedBuiltInProfile(newFilename);
        }
        refreshProfiles();
    }

    return success;
}

bool ProfileManager::renameProfile(const QString& filename, const QString& newTitle) {
    const QString trimmedTitle = newTitle.trimmed();
    if (trimmedTitle.isEmpty()) {
        qWarning() << "ProfileManager::renameProfile: empty title for" << filename;
        return false;
    }

    // Renaming changes only the title and keeps the filename (matching the
    // advanced editor's in-place rename), so all filename-keyed references stay
    // valid. Pure built-in profiles are read-only resources — refuse them here
    // and gate the menu item to non-built-in profiles, like Delete.
    ProfileSource source = ProfileSource::BuiltIn;
    bool found = false;
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.filename == filename) {
            source = info.source;
            found = true;
            break;
        }
    }
    if (!found) {
        qWarning() << "ProfileManager::renameProfile: unknown profile" << filename;
        return false;
    }
    if (source == ProfileSource::BuiltIn) {
        qWarning() << "ProfileManager::renameProfile: cannot rename built-in profile" << filename;
        return false;
    }

    // Load the profile JSON (ProfileStorage → user → downloaded), mirroring loadProfile().
    QString jsonContent;
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        jsonContent = m_profileStorage->readProfile(filename);
    }
    if (jsonContent.isEmpty()) {
        QFile userFile(userProfilesPath() + "/" + filename + ".json");
        if (userFile.open(QIODevice::ReadOnly))
            jsonContent = QString::fromUtf8(userFile.readAll());
    }
    if (jsonContent.isEmpty()) {
        QFile downloadedFile(downloadedProfilesPath() + "/" + filename + ".json");
        if (downloadedFile.open(QIODevice::ReadOnly))
            jsonContent = QString::fromUtf8(downloadedFile.readAll());
    }
    if (jsonContent.isEmpty()) {
        qWarning() << "ProfileManager::renameProfile: could not load profile" << filename;
        return false;
    }

    // Profile::loadFromJsonString returns a default profile on parse failure, so
    // verify the JSON is well-formed before trusting it (matches duplicateProfile).
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "ProfileManager::renameProfile: malformed JSON for" << filename
                   << parseErr.errorString();
        return false;
    }

    Profile renamed = Profile::loadFromJsonString(jsonContent);
    renamed.setTitle(trimmedTitle);

    // Write back to the SAME filename (ProfileStorage → local fallback).
    bool success = false;
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        success = m_profileStorage->writeProfile(filename, renamed.toJsonString());
    }
    if (!success) {
        success = renamed.saveToFile(userProfilesPath() + "/" + filename + ".json");
    }
    if (!success) {
        qWarning() << "ProfileManager::renameProfile: failed to write" << filename;
        return false;
    }

    // Favorites store the display title alongside the filename — keep it in sync.
    // The profile JSON is already written at this point, so a sync miss would
    // leave the favorites list showing a stale title; surface it rather than
    // swallow it (the preceding isFavoriteProfile guard makes it near-unreachable).
    if (m_settings && m_settings->app()->isFavoriteProfile(filename)) {
        if (!m_settings->app()->updateFavoriteProfile(filename, filename, trimmedTitle)) {
            qWarning() << "ProfileManager::renameProfile: favorite title sync failed for" << filename;
        }
    }

    // If the renamed profile is the one currently loaded, update the live copy so
    // editor/idle headers reflect the new name immediately.
    if (m_baseProfileName == filename) {
        m_currentProfile.setTitle(trimmedTitle);
        emit currentProfileChanged();
    }

    refreshProfiles();
    return true;
}


// === Profile editing (recipe/frame) ===

void ProfileManager::uploadRecipeProfile(const QVariantMap& recipeParams) {
    RecipeParams recipe = RecipeParams::fromVariantMap(recipeParams);

    // Validate recipe parameters before generating frames
    QStringList issues = recipe.validate();
    if (!issues.isEmpty()) {
        qWarning() << "RecipeParams validation issues:" << issues.join("; ");
    }
    recipe.clamp();  // Ensure values are within hardware limits

    const QString& pt = m_currentProfile.profileType();
    bool isSimpleProfile = (pt == QLatin1String("settings_2a") || pt == QLatin1String("settings_2b"));

    if (isSimpleProfile) {
        // Simple profile path: write RecipeParams back to scalar fields and
        // regenerate frames using the de1app-compatible generators.
        applyRecipeToScalarFields(recipe);
        m_currentProfile.regenerateSimpleFrames();
        m_currentProfile.setTargetWeight(recipe.targetWeight);
        m_currentProfile.setTargetVolume(recipe.targetVolume);
    } else {
        // Recipe/D-Flow/A-Flow path — and ADVANCED, which shares this branch.
        //
        // What we compare against decides whether a save regenerates frames, and a
        // regeneration is NOT a no-op: D-Flow derives exit_pressure_over from the
        // soak pressure (soak < 2.8 ? soak : soak/2 + 0.6, floored 1.2), so an
        // open-and-close save on a profile whose stored value is off-formula would
        // silently rewrite it.
        //
        // For D-Flow/A-Flow the editor was populated by `prep` from the frames
        // (getOrConvertRecipeParams), so the frames are the only honest baseline —
        // m_currentProfile.recipeParams() is now a default-constructed struct, and
        // comparing against it would report "changed" on every save.
        //
        // ADVANCED must NOT use that baseline. extractRecipeParams falls past the
        // dflow/aflow dispatch into the heuristic frame-pattern detector, so its
        // output never equals the defaults the advanced editor was populated with;
        // needFrameRegen would be permanently true, regenerateFromRecipe() early-
        // returns for advanced, and the else-branch that applies targetWeight /
        // targetVolume would be skipped — silently dropping a target edit. Advanced
        // keeps comparing defaults against defaults, which is correctly equal.
        const QString editorTypeForSave = m_currentProfile.editorType();
        const bool derivesFromFrames = (editorTypeForSave == QLatin1String("dflow")
                                        || editorTypeForSave == QLatin1String("aflow"));
        RecipeParams oldRecipe = derivesFromFrames
            ? RecipeAnalyzer::extractRecipeParams(m_currentProfile)
            : m_currentProfile.recipeParams();
        bool needFrameRegen = m_currentProfile.steps().isEmpty()
                           || !oldRecipe.frameAffectingFieldsEqual(recipe);

        // Refuse to rebuild a profile whose frames `prep` could not read.
        //
        // Both plugins index frame roles positionally with no validation, so a
        // D-Flow profile with fewer than 3 frames (or A-Flow with fewer than 6)
        // has no parameters to derive — prep warns and hands back whatever the
        // params already were. Regenerating from those replaces the profile's
        // real frames with a fabricated layout built from numbers that never
        // came from it, which is REC-1 wearing a different hat.
        //
        // setRecipeParams() below flips hasRecipeParams unconditionally, so
        // regenerateFromRecipe()'s own guard cannot catch this — the check has
        // to happen here, against the frames as they stand.
        const bool fits = m_currentProfile.steps().isEmpty()
                       || RecipeAnalyzer::framesFitEditorLayout(m_currentProfile);
        if (needFrameRegen && !fits) {
            qWarning() << "uploadRecipeProfile:" << m_currentProfile.title() << "has"
                       << m_currentProfile.steps().size()
                       << "frames, which its editor cannot read — keeping them rather than "
                          "regenerating from parameters that were not derived from them";
            needFrameRegen = false;
        }

        m_currentProfile.setRecipeParams(recipe);

        if (needFrameRegen) {
            m_currentProfile.regenerateFromRecipe();
        } else {
            m_currentProfile.setTargetWeight(recipe.targetWeight);
            m_currentProfile.setTargetVolume(recipe.targetVolume);
        }
    }

    // Clear overrides so uploadCurrentProfile doesn't apply a stale delta
    // and the shot plan shows the edited profile's own values.
    if (m_settings) {
        m_settings->brew()->clearAllBrewOverrides();
    }

    // Sync stop targets to MachineState so SAW/volume checks use current values
    if (m_machineState) {
        // Through the ladder, not the raw profile value: the overrides were
        // just cleared so these resolve identically when idle, but mid-shot
        // targetWeight() honours the latch while the raw read would shove a
        // new target at the machine during the pour (reachable from MCP /
        // the web editor, which can save a profile at any time).
        m_machineState->setTargetWeight(targetWeight());
        m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        m_machineState->setProfileType(m_currentProfile.profileType());
    }

    // Mark as modified
    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();
    emit targetWeightChanged();

    // NOTE: BLE upload deferred to editor exit (QML calls uploadCurrentProfile() explicitly).
    // This avoids flooding the DE1 with BLE writes on every slider tick. See #557.

    qDebug() << "Recipe profile updated with" << m_currentProfile.steps().size() << "frames (BLE upload deferred)";
}

void ProfileManager::applyRecipeToScalarFields(const RecipeParams& recipe) {
    // Common preinfusion fields
    m_currentProfile.setPreinfusionTime(recipe.preinfusionTime);
    m_currentProfile.setPreinfusionFlowRate(recipe.preinfusionFlowRate);
    m_currentProfile.setPreinfusionStopPressure(recipe.preinfusionStopPressure);

    // Temperature presets from per-step temperatures
    m_currentProfile.setTemperaturePresets({
        recipe.tempStart, recipe.tempPreinfuse,
        recipe.tempHold, recipe.tempDecline
    });

    // Compute tempStepsEnabled: true if any step temp differs from another
    bool tempsDiffer = !qFuzzyCompare(recipe.tempStart, recipe.tempPreinfuse)
                    || !qFuzzyCompare(recipe.tempStart, recipe.tempHold)
                    || !qFuzzyCompare(recipe.tempStart, recipe.tempDecline);
    m_currentProfile.setTempStepsEnabled(tempsDiffer);

    // espressoHoldTime/espressoDeclineTime are used by both generators as the
    // holdTime/declineTime parameters -- always set them regardless of profile type
    m_currentProfile.setEspressoHoldTime(recipe.holdTime);
    m_currentProfile.setEspressoDeclineTime(recipe.simpleDeclineTime);

    const QString& pt = m_currentProfile.profileType();
    if (pt == QLatin1String("settings_2a")) {
        m_currentProfile.setEspressoPressure(recipe.espressoPressure);
        m_currentProfile.setPressureEnd(recipe.pressureEnd);
        m_currentProfile.setMaximumFlow(recipe.limiterValue);
        m_currentProfile.setMaximumFlowRangeDefault(recipe.limiterRange);
    } else {
        // settings_2b -- also set flow-specific hold/decline time fields
        m_currentProfile.setFlowProfileHoldTime(recipe.holdTime);
        m_currentProfile.setFlowProfileDeclineTime(recipe.simpleDeclineTime);
        m_currentProfile.setFlowProfileHold(recipe.holdFlow);
        m_currentProfile.setFlowProfileDecline(recipe.flowEnd);
        m_currentProfile.setMaximumPressure(recipe.limiterValue);
        m_currentProfile.setMaximumPressureRangeDefault(recipe.limiterRange);
    }

    // Set espressoTemperature from the first preset (will be synced from first frame
    // after regenerateSimpleFrames, but set it here for consistency)
    m_currentProfile.setEspressoTemperature(recipe.tempStart);
}

ProfileManager::WriteBack ProfileManager::writeProfileBackIfLossless(
    const QString& resolvedName, const QString& filePath, bool preferStorage,
    const Profile& profile, const QString& excludedKey, QStringList* parityOut)
{
    const bool toStorage = preferStorage && m_profileStorage
                           && m_profileStorage->isConfigured()
                           && m_profileStorage->profileExists(resolvedName);
    const bool toFile = !filePath.isEmpty() && !filePath.startsWith(QLatin1Char(':'));
    if (!toStorage && !toFile)
        return WriteBack::NotWritable;

    // Read from the destination, never from "wherever we happened to have a path".
    QString beforeJson;
    if (toStorage) {
        beforeJson = m_profileStorage->readProfile(resolvedName);
    } else {
        QFile before(filePath);
        if (before.open(QIODevice::ReadOnly))
            beforeJson = QString::fromUtf8(before.readAll());
    }

    // An unreadable destination is a refusal. Treating it as "nothing to compare"
    // would let a transient SAF read error switch the audit off and clobber a file
    // whose distinguishing feature is that it is old enough to carry foreign keys.
    if (beforeJson.isEmpty()) {
        if (parityOut)
            *parityOut = QStringList{QStringLiteral("could not read the stored copy to compare against")};
        return WriteBack::Refused;
    }

    QJsonObject wasOnDisk = QJsonDocument::fromJson(beforeJson.toUtf8()).object();
    QJsonObject willWrite = profile.toJsonObject();
    if (!excludedKey.isEmpty()) {
        wasOnDisk.remove(excludedKey);
        willWrite.remove(excludedKey);
    }
    const QStringList parity = Profile::jsonParityErrors(wasOnDisk, willWrite);
    if (!parity.isEmpty()) {
        if (parityOut) *parityOut = parity;
        return WriteBack::Refused;
    }

    const bool ok = toStorage
        ? m_profileStorage->writeProfile(resolvedName, profile.toJsonString())
        : profile.saveToFile(filePath);   // QSaveFile: atomic, checks write + commit
    return ok ? WriteBack::Written : WriteBack::Failed;
}

void ProfileManager::setCurrentProfileRecommendedDose(double doseG) {
    // 0 CLEARS the recommendation. Storing 0 with the flag on would be a
    // recommendation of zero grams, which flows into dialing_get_context and the AI
    // advisor and into ratio arithmetic — and it would contradict the .tcl importer,
    // which reads de1app's 0 as "not set" (Profile::loadFromTclString). One field,
    // one meaning.
    if (doseG <= 0.0) {
        m_currentProfile.setHasRecommendedDose(false);
    } else {
        m_currentProfile.setRecommendedDose(qMin(doseG, 100.0));
        m_currentProfile.setHasRecommendedDose(true);
    }
    m_profileModified = true;
    emit currentProfileChanged();
    emit profileModifiedChanged();
}

QVariantMap ProfileManager::getOrConvertRecipeParams() {
    const QString& et = m_currentProfile.editorType();

    // D-Flow/A-Flow: ALWAYS derive from the frames. A stored recipe block is a
    // cache, never an oracle — the frames are what the machine executes and what
    // both plugins reconstruct their editor from on every load.
    //
    // There used to be a branch above this one that returned a stored block
    // directly whenever the profile had one. That left finding REC-1 half-fixed:
    // gating the WRITE stopped new fabricated blocks appearing, but every
    // profile that already carried one — including the five shipped A-Flow
    // built-ins, whose identical blocks claim 88 °C / 20 s / 9 bar against
    // frames that say 93 / 60 / 10 — still took the short-circuit, so `prep`
    // never ran and the editor still showed the stale numbers. Deriving
    // unconditionally is also what this change's own spec requires: "WHEN a
    // profile carries a recipe block whose values contradict its frames, THEN
    // the parameters used are those derived from the frames."
    //
    // Nothing is lost by ignoring the block: prepDFlow/prepAFlow start FROM the
    // stored params and overwrite only what the frames determine, so fields no
    // frame carries (dose) still come through.
    if (isDFlowTitle(m_currentProfile.title()) || isAFlowTitle(m_currentProfile.title())
        || et == QLatin1String("dflow") || et == QLatin1String("aflow")) {
        bool derived = false;
        QVariantMap out =
            RecipeAnalyzer::extractRecipeParams(m_currentProfile, &derived).toVariantMap();
        // A qWarning in the log does not reach the editor or an MCP client. When
        // the frames could not be read, the values below are NOT this profile's —
        // say so in the payload so a caller can refuse to dial from them.
        if (!derived) {
            out[QStringLiteral("parametersDerivedFromFrames")] = false;
            out[QStringLiteral("warning")] =
                QStringLiteral("This profile's %1 frames do not fit the layout its editor "
                               "reads, so these values were not derived from it.")
                    .arg(m_currentProfile.steps().size());
        }
        return out;
    }

    // Simple profiles (settings_2a/2b): populate RecipeParams from scalar fields
    const QString& pt = m_currentProfile.profileType();
    if (pt == QLatin1String("settings_2a") || pt == QLatin1String("settings_2b")) {
        RecipeParams params;
        params.targetWeight = m_currentProfile.targetWeight();
        params.targetVolume = m_currentProfile.targetVolume();
        params.fillTemperature = m_currentProfile.espressoTemperature();
        params.pourTemperature = m_currentProfile.espressoTemperature();
        // Temperature presets: [0]=Start, [1]=Preinfuse, [2]=Hold, [3]=Decline
        const auto& presets = m_currentProfile.temperaturePresets();
        double baseTemp = m_currentProfile.espressoTemperature();
        params.tempStart = presets.value(0, baseTemp);
        params.tempPreinfuse = presets.value(1, baseTemp);
        params.tempHold = presets.value(2, baseTemp);
        params.tempDecline = presets.value(3, baseTemp);
        params.preinfusionTime = m_currentProfile.preinfusionTime();
        params.preinfusionFlowRate = m_currentProfile.preinfusionFlowRate();
        params.preinfusionStopPressure = m_currentProfile.preinfusionStopPressure();
        // Both settings_2a and settings_2b use espressoHoldTime/espressoDeclineTime
        // for frame generation. De1app's flow_to_advanced_list uses these same fields,
        // NOT flow_profile_hold_time/flow_profile_decline_time (which are GUI-only metadata).
        params.holdTime = m_currentProfile.espressoHoldTime();
        params.simpleDeclineTime = m_currentProfile.espressoDeclineTime();
        if (pt == QLatin1String("settings_2a")) {
            params.editorType = EditorType::Pressure;
            params.espressoPressure = m_currentProfile.espressoPressure();
            params.pressureEnd = m_currentProfile.pressureEnd();
            params.limiterValue = m_currentProfile.maximumFlow();
            params.limiterRange = m_currentProfile.maximumFlowRangeDefault();
        } else {
            params.editorType = EditorType::Flow;
            params.holdFlow = m_currentProfile.flowProfileHold();
            params.flowEnd = m_currentProfile.flowProfileDecline();
            params.limiterValue = m_currentProfile.maximumPressure();
            params.limiterRange = m_currentProfile.maximumPressureRangeDefault();
        }
        return params.toVariantMap();
    }

    // Advanced profile — no recipe params, return defaults
    return RecipeParams().toVariantMap();
}

void ProfileManager::createNewRecipe(const QString& title) {
    createNewProfileWithEditorType(EditorType::DFlow, title);
}

void ProfileManager::createNewAFlowRecipe(const QString& title) {
    createNewProfileWithEditorType(EditorType::AFlow, title);
}

void ProfileManager::createNewPressureProfile(const QString& title) {
    createNewProfileWithEditorType(EditorType::Pressure, title);
}

void ProfileManager::createNewFlowProfile(const QString& title) {
    createNewProfileWithEditorType(EditorType::Flow, title);
}

void ProfileManager::createNewProfileWithEditorType(EditorType type, const QString& title) {
    RecipeParams recipe;
    recipe.editorType = type;
    recipe.applyEditorDefaults();
    recipe.clamp();  // Ensure values are within hardware limits

    m_currentProfile = RecipeGenerator::createProfile(recipe, title);
    m_baseProfileName = "";
    m_profileModified = true;

    if (m_settings) {
        m_settings->app()->setSelectedFavoriteProfile(-1);
        m_settings->brew()->clearAllBrewOverrides();
    }
    if (m_machineState) {
        // Through the ladder, not the raw profile value: the overrides were
        // just cleared so these resolve identically when idle, but mid-shot
        // targetWeight() honours the latch while the raw read would shove a
        // new target at the machine during the pour (reachable from MCP /
        // the web editor, which can save a profile at any time).
        m_machineState->setTargetWeight(targetWeight());
        m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        m_machineState->setProfileType(m_currentProfile.profileType());
    }

    emit currentProfileChanged();
    emit profileModifiedChanged();
    emit targetWeightChanged();
    emit profilesChanged();
    emit allBuiltInProfileListChanged();

    uploadCurrentProfile();
    qDebug() << "Created new" << editorTypeToString(type) << "profile:" << title;
}

void ProfileManager::convertCurrentProfileToAdvanced() {
    // Convert to advanced mode: set profileType to settings_2c and strip
    // D-Flow/A-Flow title prefix so editorType() derives as "advanced".
    // The frames are already generated and are preserved as-is.
    m_currentProfile.setProfileType(QStringLiteral("settings_2c"));

    // Strip D-Flow/A-Flow prefix (case-insensitive, matching isDFlowTitle/isAFlowTitle).
    // setTitle() already strips leading '*', so title() never has one.
    QString title = m_currentProfile.title();
    auto stripPrefix = [&](const QString& prefix) {
        // title starts with prefix (case-insensitive) — strip it
        QString after = title.mid(prefix.length());
        if (after.startsWith(QLatin1String(" / ")))
            after = after.mid(3);
        else if (after.startsWith(QLatin1Char('/')))
            after = after.mid(1).trimmed();
        else
            after = after.trimmed();
        return after.isEmpty() ? QStringLiteral("Advanced Profile") : after;
    };

    if (isDFlowTitle(title)) {
        m_currentProfile.setTitle(stripPrefix(QStringLiteral("D-Flow")));
    } else if (isAFlowTitle(title)) {
        m_currentProfile.setTitle(stripPrefix(QStringLiteral("A-Flow")));
    }

    m_profileModified = true;

    emit currentProfileChanged();
    emit profileModifiedChanged();

    qDebug() << "Converted profile to Advanced mode:" << m_currentProfile.title();
}

void ProfileManager::createNewProfile(const QString& title) {
    // Create a new profile with a single default frame
    m_currentProfile = Profile();
    m_currentProfile.setTitle(title);
    m_currentProfile.setAuthor("");
    m_currentProfile.setProfileNotes("");
    m_currentProfile.setBeverageType("espresso");
    m_currentProfile.setProfileType("settings_2c");
    m_currentProfile.setTargetWeight(36.0);
    m_currentProfile.setTargetVolume(0.0);
    m_currentProfile.setEspressoTemperature(93.0);


    // Add a single default extraction frame
    ProfileFrame defaultFrame;
    defaultFrame.name = "Extraction";
    defaultFrame.temperature = 93.0;
    defaultFrame.sensor = "coffee";
    defaultFrame.pump = "pressure";
    defaultFrame.transition = "fast";
    defaultFrame.pressure = 9.0;
    defaultFrame.flow = 2.0;
    defaultFrame.seconds = 60.0;
    defaultFrame.volume = 0;
    defaultFrame.exitIf = false;
    if (!m_currentProfile.addStep(defaultFrame)) {
        qWarning() << "createNewBlankProfile: failed to add default frame";
    }

    m_baseProfileName = "";
    m_profileModified = true;

    if (m_settings) {
        m_settings->app()->setSelectedFavoriteProfile(-1);  // New profile, not in favorites
        m_settings->brew()->clearAllBrewOverrides();
    }
    if (m_machineState) {
        // Through the ladder, not the raw profile value: the overrides were
        // just cleared so these resolve identically when idle, but mid-shot
        // targetWeight() honours the latch while the raw read would shove a
        // new target at the machine during the pour (reachable from MCP /
        // the web editor, which can save a profile at any time).
        m_machineState->setTargetWeight(targetWeight());
        m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        m_machineState->setProfileType(m_currentProfile.profileType());
    }

    emit currentProfileChanged();
    emit profileModifiedChanged();
    emit targetWeightChanged();

    uploadCurrentProfile();
    qDebug() << "Created new blank profile:" << title;
}


// === Frame operations (advanced editor) ===

void ProfileManager::addFrame(int afterIndex) {
    if (m_currentProfile.steps().size() >= Profile::MAX_FRAMES) {
        qWarning() << "Cannot add frame: maximum" << Profile::MAX_FRAMES << "frames reached";
        return;
    }

    // Create a new default frame
    ProfileFrame newFrame;
    newFrame.name = QString("Step %1").arg(m_currentProfile.steps().size() + 1);
    newFrame.temperature = 93.0;
    newFrame.sensor = "coffee";
    newFrame.pump = "pressure";
    newFrame.transition = "fast";
    newFrame.pressure = 9.0;
    newFrame.flow = 2.0;
    newFrame.seconds = 30.0;
    newFrame.volume = 0;
    newFrame.exitIf = false;

    bool added = false;
    if (afterIndex < 0 || static_cast<qsizetype>(afterIndex) >= m_currentProfile.steps().size()) {
        // Add at end
        added = m_currentProfile.addStep(newFrame);
    } else {
        // Insert after specified index
        added = m_currentProfile.insertStep(afterIndex + 1, newFrame);
    }
    if (!added) {
        qWarning() << "Failed to add frame: maximum frame count reached (" << Profile::MAX_FRAMES << ")";
        return;
    }

    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Added frame at index" << (afterIndex + 1) << ", total frames:" << m_currentProfile.steps().size();
}

void ProfileManager::deleteFrame(int index) {
    if (index < 0 || static_cast<qsizetype>(index) >= m_currentProfile.steps().size()) {
        qWarning() << "Cannot delete frame: invalid index" << index;
        return;
    }

    // Don't allow deleting the last frame
    if (m_currentProfile.steps().size() <= 1) {
        qWarning() << "Cannot delete the last frame";
        return;
    }

    if (!m_currentProfile.removeStep(index)) {
        qWarning() << "deleteFrame: removeStep failed for index" << index;
        return;
    }


    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Deleted frame at index" << index << ", total frames:" << m_currentProfile.steps().size();
}

void ProfileManager::moveFrameUp(int index) {
    if (index <= 0 || static_cast<qsizetype>(index) >= m_currentProfile.steps().size()) {
        return;  // Can't move up if already at top or invalid
    }

    m_currentProfile.moveStep(index, index - 1);


    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Moved frame from" << index << "to" << (index - 1);
}

void ProfileManager::moveFrameDown(int index) {
    if (index < 0 || static_cast<qsizetype>(index) >= m_currentProfile.steps().size() - 1) {
        return;  // Can't move down if already at bottom or invalid
    }

    m_currentProfile.moveStep(index, index + 1);


    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Moved frame from" << index << "to" << (index + 1);
}

void ProfileManager::duplicateFrame(int index) {
    if (index < 0 || static_cast<qsizetype>(index) >= m_currentProfile.steps().size()) {
        qWarning() << "Cannot duplicate frame: invalid index" << index;
        return;
    }

    if (m_currentProfile.steps().size() >= Profile::MAX_FRAMES) {
        qWarning() << "Cannot duplicate frame: maximum" << Profile::MAX_FRAMES << "frames reached";
        return;
    }

    ProfileFrame copy = m_currentProfile.steps().at(index);
    copy.name = copy.name + " (copy)";
    if (!m_currentProfile.insertStep(index + 1, copy)) {
        qWarning() << "duplicateFrame: insertStep failed at index" << (index + 1);
        return;
    }


    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Duplicated frame at index" << index;
}

void ProfileManager::setFrameProperty(int index, const QString& property, const QVariant& value) {
    if (index < 0 || static_cast<qsizetype>(index) >= m_currentProfile.steps().size()) {
        qWarning() << "setFrameProperty: invalid index" << index;
        return;
    }

    ProfileFrame frame = m_currentProfile.steps().at(index);

    // Basic properties
    if (property == "name") frame.name = value.toString();
    else if (property == "temperature") frame.temperature = value.toDouble();
    else if (property == "sensor") frame.sensor = value.toString();
    else if (property == "pump") frame.pump = value.toString();
    else if (property == "transition") frame.transition = value.toString();
    else if (property == "pressure") frame.pressure = value.toDouble();
    else if (property == "flow") frame.flow = value.toDouble();
    else if (property == "seconds") frame.seconds = value.toDouble();
    else if (property == "volume") frame.volume = value.toDouble();
    // Exit conditions
    else if (property == "exitIf") frame.exitIf = value.toBool();
    else if (property == "exitType") frame.exitType = value.toString();
    else if (property == "exitPressureOver") frame.exitPressureOver = value.toDouble();
    else if (property == "exitPressureUnder") frame.exitPressureUnder = value.toDouble();
    else if (property == "exitFlowOver") frame.exitFlowOver = value.toDouble();
    else if (property == "exitFlowUnder") frame.exitFlowUnder = value.toDouble();
    else if (property == "exitWeight") frame.exitWeight = value.toDouble();
    // Limiter
    else if (property == "maxFlowOrPressure") frame.maxFlowOrPressure = value.toDouble();
    else if (property == "maxFlowOrPressureRange") frame.maxFlowOrPressureRange = value.toDouble();
    // Popup message
    else if (property == "popup") frame.popup = value.toString();
    else {
        qWarning() << "setFrameProperty: unknown property" << property;
        return;
    }

    m_currentProfile.setStepAt(index, frame);


    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
}

QVariantMap ProfileManager::getFrameAt(int index) const {
    if (index < 0 || static_cast<qsizetype>(index) >= m_currentProfile.steps().size()) {
        return QVariantMap();
    }

    const ProfileFrame& frame = m_currentProfile.steps().at(index);
    QVariantMap map;

    // Basic properties
    map["name"] = frame.name;
    map["temperature"] = frame.temperature;
    map["sensor"] = frame.sensor;
    map["pump"] = frame.pump;
    map["transition"] = frame.transition;
    map["pressure"] = frame.pressure;
    map["flow"] = frame.flow;
    map["seconds"] = frame.seconds;
    map["volume"] = frame.volume;

    // Exit conditions
    map["exitIf"] = frame.exitIf;
    map["exitType"] = frame.exitType;
    map["exitPressureOver"] = frame.exitPressureOver;
    map["exitPressureUnder"] = frame.exitPressureUnder;
    map["exitFlowOver"] = frame.exitFlowOver;
    map["exitFlowUnder"] = frame.exitFlowUnder;
    map["exitWeight"] = frame.exitWeight;

    // Limiter
    map["maxFlowOrPressure"] = frame.maxFlowOrPressure;
    map["maxFlowOrPressureRange"] = frame.maxFlowOrPressureRange;

    // Popup
    map["popup"] = frame.popup;

    return map;
}

int ProfileManager::frameCount() const {
    return static_cast<int>(m_currentProfile.steps().size());
}


// === Flow calibration ===

void ProfileManager::applyFlowCalibration() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    double multiplier = m_settings->calibration()->effectiveFlowCalibration(m_baseProfileName);
    m_device->setFlowCalibrationMultiplier(multiplier);
}


// === Private helpers ===

void ProfileManager::loadDefaultProfile() {
    m_currentProfile = Profile::loadFromFile(QStringLiteral(":/profiles/default.json"));
    if (m_settings) {
        m_settings->app()->setSelectedFavoriteProfile(-1);
    }
    resetBrewOverridesForLoadedProfile();
}

QString ProfileManager::profilesPath() const {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    path += "/profiles";

    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return path;
}

QString ProfileManager::userProfilesPath() const {
    QString path = profilesPath() + "/user";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

QString ProfileManager::downloadedProfilesPath() const {
    QString path = profilesPath() + "/downloaded";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

double ProfileManager::getGroupTemperature() const {
    if (m_settings && m_settings->brew()->hasTemperatureOverride()) {
        double temp = m_settings->brew()->temperatureOverride();
        qDebug() << "getGroupTemperature: using override" << temp << "C";
        return temp;
    }
    return m_currentProfile.espressoTemperature();
}

QList<ProfileFrame> ProfileManager::framesShiftedToTemperature(double targetTemp) const {
    const double delta = targetTemp - m_currentProfile.espressoTemperature();
    QList<ProfileFrame> steps = m_currentProfile.steps();
    for (int i = 0; i < steps.size(); ++i)
        steps[i].temperature += delta;
    return steps;
}

void ProfileManager::applyTemperatureToProfile(double newTemperature) {
    // Bake the brew temperature into the profile using the SAME anchor as the
    // live-brew override path (espressoTemperature), so saving and brewing agree.
    m_currentProfile.setSteps(framesShiftedToTemperature(newTemperature));
    m_currentProfile.setEspressoTemperature(newTemperature);
    emit currentProfileChanged();
    // The new temperature is now the profile default, so any active override is
    // redundant. Clear it BEFORE re-uploading — otherwise uploadCurrentProfile would
    // re-apply the (now stale) override as a second delta, making the uploaded shot
    // disagree with the saved profile.
    if (m_settings && m_settings->brew()->hasTemperatureOverride()) {
        m_settings->brew()->clearTemperatureOverride();
    }
    uploadCurrentProfile();
    if (!m_baseProfileName.isEmpty())
        saveProfile(m_baseProfileName);
}

QString ProfileManager::temperatureDisplay(double anchorTemp, bool hasOverride,
                                           double overrideTemp,
                                           double baselineShiftC) const {
    QVector<double> temps;
    temps.reserve(static_cast<qsizetype>(m_currentProfile.steps().size()));
    for (const ProfileFrame& f : m_currentProfile.steps())
        temps.append(f.temperature);
    const bool fahrenheit = m_settings && m_settings->app()
        && m_settings->app()->temperatureUnit() == QLatin1String("fahrenheit");
    return TemperatureDisplay::format(temps, anchorTemp, hasOverride, overrideTemp, fahrenheit, baselineShiftC);
}

QString ProfileManager::temperatureDisplayForSteps(const QVariantList& stepTempsC,
                                                   double anchorTemp, bool hasOverride,
                                                   double overrideTemp,
                                                   double baselineShiftC) const {
    QVector<double> temps;
    temps.reserve(stepTempsC.size());
    for (const QVariant& v : stepTempsC) {
        const double t = v.toDouble();
        if (t > 0)
            temps.append(t);
    }
    const bool fahrenheit = m_settings && m_settings->app()
        && m_settings->app()->temperatureUnit() == QLatin1String("fahrenheit");
    return TemperatureDisplay::format(temps, anchorTemp, hasOverride, overrideTemp, fahrenheit, baselineShiftC);
}

void ProfileManager::migrateProfileFolders() {
    QString basePath = profilesPath();
    QString userPath = basePath + "/user";
    QString downloadedPath = basePath + "/downloaded";

    // Check if migration already done (user folder exists and has content, or we've done it before)
    QDir userDir(userPath);
    QDir downloadedDir(downloadedPath);

    // If user folder already exists, migration was already done
    if (userDir.exists()) {
        // Just ensure downloaded folder exists too
        if (!downloadedDir.exists()) {
            downloadedDir.mkpath(".");
        }
        return;
    }

    qDebug() << "Migrating profile folders...";

    // Create both folders
    userDir.mkpath(".");
    downloadedDir.mkpath(".");

    // Move all existing .json files (except _current.json) from profiles/ to profiles/user/
    QDir baseDir(basePath);
    QStringList filters;
    filters << "*.json";
    QStringList files = baseDir.entryList(filters, QDir::Files);

    for (const QString& file : files) {
        if (file == "_current.json") {
            continue;  // Skip the temp file
        }

        QString srcPath = basePath + "/" + file;
        QString dstPath = userPath + "/" + file;

        if (QFile::rename(srcPath, dstPath)) {
            qDebug() << "Migrated profile:" << file;
        } else {
            qWarning() << "Failed to migrate profile:" << file;
        }
    }

    qDebug() << "Profile folder migration complete";
}

void ProfileManager::migrateProfileFormat() {
    // One-time migration: resave user/downloaded profiles in the unified de1app-compatible
    // JSON format so they can be shared directly with de1app users.
    if (m_settings && m_settings->value("profile_format_migrated", false).toBool()) {
        return;  // Already done
    }

    qDebug() << "Migrating profile JSON format to de1app-compatible v2...";
    int migrated = 0;
    int failed = 0;

    // Helper: check and resave a single file if it lacks the "version" field
    auto migrateFile = [&](const QString& filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "migrateProfileFormat: Cannot open profile:" << filePath
                       << "-" << file.errorString();
            failed++;
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isNull()) {
            qWarning() << "migrateProfileFormat: Invalid JSON in profile:" << filePath;
            failed++;
            return;
        }

        QJsonObject obj = doc.object();
        if (obj.contains("version")) return;  // Already in v2 format

        Profile profile = Profile::fromJson(doc);
        if (profile.title().isEmpty() || profile.steps().isEmpty()) {
            qWarning() << "migrateProfileFormat: Profile has empty title or steps:" << filePath;
            failed++;
            return;
        }

        // Same parity gate as upgradeStoredEncoding, and for the same reason: this
        // rewrites a user's file, and a title/steps sanity check does not prove the
        // rewrite is lossless. This pass runs from the constructor, BEFORE any
        // loadProfile(), so without the gate it would reach the legacy population
        // first and rewrite unaudited exactly the files the on-load upgrade exists
        // to protect. A profile that cannot be converted losslessly is left in its
        // stored format — it still loads.
        const QStringList parity = Profile::jsonParityErrors(obj, profile.toJsonObject());
        if (!parity.isEmpty()) {
            qWarning() << "migrateProfileFormat: leaving" << filePath
                       << "in its stored format — converting it would not be lossless:"
                       << parity.join(QStringLiteral("; "));
            failed++;
            return;
        }

        if (profile.saveToFile(filePath)) {
            migrated++;
        } else {
            qWarning() << "migrateProfileFormat: Failed to write migrated profile:" << filePath;
            failed++;
        }
    };

    // Migrate user profiles
    QDir userDir(userProfilesPath());
    for (const QString& file : userDir.entryList({"*.json"}, QDir::Files)) {
        if (file == "_current.json") continue;
        migrateFile(userDir.filePath(file));
    }

    // Migrate downloaded profiles
    QDir downloadedDir(downloadedProfilesPath());
    for (const QString& file : downloadedDir.entryList({"*.json"}, QDir::Files)) {
        migrateFile(downloadedDir.filePath(file));
    }

    // Migrate ProfileStorage profiles (Android SAF)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        for (const QString& name : m_profileStorage->listProfiles()) {
            QString jsonContent = m_profileStorage->readProfile(name);
            if (jsonContent.isEmpty()) continue;

            QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8());
            if (doc.isNull()) {
                qWarning() << "migrateProfileFormat: Invalid JSON in SAF profile:" << name;
                failed++;
                continue;
            }

            QJsonObject obj = doc.object();
            if (obj.contains("version")) continue;

            Profile profile = Profile::fromJson(doc);
            if (profile.title().isEmpty() || profile.steps().isEmpty()) {
                qWarning() << "migrateProfileFormat: SAF profile has empty title or steps:" << name;
                failed++;
                continue;
            }

            if (m_profileStorage->writeProfile(name, profile.toJsonString())) {
                migrated++;
            } else {
                qWarning() << "migrateProfileFormat: Failed to write SAF profile:" << name;
                failed++;
            }
        }
    }

    if (failed > 0) {
        qWarning() << "Profile format migration incomplete:" << migrated << "updated,"
                   << failed << "failed. Will retry on next launch.";
    } else {
        if (m_settings) {
            m_settings->setValue("profile_format_migrated", true);
        }
        qDebug() << "Profile format migration complete:" << migrated << "profiles updated";
    }
}

void ProfileManager::stripStoredRecipeBlocks() {
    // One-time upgrade: remove the `recipe` block from every already-saved profile,
    // promoting a genuinely-set dose to recommended_dose on the way.
    //
    // REPLACES migrateRecipeFrames(), which regenerated frames FROM the block. That
    // was written before #1646 established the frames as the source of truth, and
    // retiring it is a deliberate behaviour change: an install that never ran it
    // keeps its frames instead of having them rebuilt from a block we now know is
    // untrustworthy — five shipped A-Flow built-ins carried blocks contradicting
    // their own frames. Its settings flag is retired with it, so an install that
    // skipped the old pass is not skipped by this one.
    //
    // Runs BEFORE migrateProfileFormat() so a block-carrying profile is normalised
    // once, by the pass that knows what the block is, instead of being rewritten by
    // both in the same startup. Note this is NOT because the other pass would refuse
    // them — `recipe` is excused in deliberatelyDroppedKeys() for every caller of
    // jsonParityErrors, migrateProfileFormat included.
    //
    // Profiles that arrive AFTER this pass — imports, share codes, SAF syncs,
    // restored backups — are handled by the strip-on-load write-back in loadProfile().
    // TRANSITIONAL — deletable once the population has drained.
    //
    // Decenza was the only producer of the block: de1app has no such key in any of
    // its 88 profiles, Decaid models ten fields and drops the rest, and Visualizer
    // normalises it away in both its JSON and TCL renderings. So the set of files
    // carrying one is closed and shrinking, and after a release has shipped with this
    // pass these can all go, in order:
    //
    //   1. this pass and its `recipe_blocks_stripped` flag
    //   2. the strip-on-load write-back in loadProfile() and Profile's stripped flag
    //   3. the recipe.dose promotion in Profile::fromJson()
    //   4. the `recipe` entry in deliberatelyDroppedKeys()
    //
    // What must NOT go is the `recipe` entry in kKnownProfileKeys — see the comment
    // there. It is one string, and it is what makes a straggler (an old share code, a
    // restored backup, a device that skipped a release) simply drop its block on the
    // next save instead of having it preserved forever by the passthrough.
    if (m_settings && m_settings->value("recipe_blocks_stripped", false).toBool()) {
        return;
    }

    qDebug() << "Stripping stored recipe blocks...";
    int migrated = 0;
    int failed = 0;     // write errors only — these are worth retrying
    int refused = 0;    // not losslessly rewritable; retrying cannot help
    int promoted = 0;   // blocks whose dose became a recommended_dose

    // Returns the stripped profile, or nullopt when there is nothing to do or the
    // file cannot be used. Every rejection is reported and counted — three distinct
    // outcomes collapsing into one silent `false` is how a corrupt profile stays
    // invisible on every launch while the completion flag is set anyway.
    auto stripped = [&](const QString& label, const QByteArray& raw) -> std::optional<Profile> {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (doc.isNull()) {
            qWarning() << "stripStoredRecipeBlocks: cannot parse" << label << "-"
                       << parseError.errorString() << "at offset" << parseError.offset;
            failed++;
            return std::nullopt;
        }
        const QJsonObject obj = doc.object();
        if (!obj.contains(QStringLiteral("recipe"))) return std::nullopt;   // nothing to do

        Profile profile = Profile::fromJson(doc);
        if (profile.title().isEmpty()) {
            qWarning() << "stripStoredRecipeBlocks: abandoning" << label
                       << "- it carries a recipe block but no title, so it cannot be"
                       << "safely rewritten";
            failed++;
            return std::nullopt;
        }

        // fromJson has already dropped the block and promoted any set dose; all that
        // is left is to confirm the rewrite loses nothing else. The parity check
        // excuses `recipe` deliberately (see deliberatelyDroppedKeys in profile.cpp),
        // so anything it reports here is a genuine loss and a reason to stop.
        const QStringList parity = Profile::jsonParityErrors(obj, profile.toJsonObject());
        if (!parity.isEmpty()) {
            // Not a failure, and deliberately not a warning. A profile stored in a
            // non-canonical encoding cannot be rewritten losslessly by any pass, so
            // retrying forever would only repeat the message on every launch — the
            // same reason upgradeStoredEncoding leaves such files alone. Its block
            // stays, harmlessly: nothing reads one, and loadProfile will try again
            // per-profile through the same gate if the encoding is ever repaired.
            qInfo() << "stripStoredRecipeBlocks: leaving" << label
                    << "as it is — rewriting it would not be lossless:"
                    << parity.join(QStringLiteral(", "));
            refused++;
            return std::nullopt;
        }

        if (profile.hasRecommendedDose()
            && !profileJsonToBool(obj.value(QStringLiteral("has_recommended_dose")), false)) {
            promoted++;
        }
        return profile;
    };

    auto migrateFile = [&](const QString& filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "stripStoredRecipeBlocks: cannot open" << filePath;
            failed++;
            return;
        }
        const QByteArray raw = file.readAll();
        file.close();

        const std::optional<Profile> profile = stripped(filePath, raw);
        if (!profile) return;

        // saveToFile, never a raw QFile: it writes through QSaveFile (temp + rename)
        // and checks both the byte count and commit(), so an interrupted or short
        // write leaves the user's original intact instead of a truncated file. A
        // startup pass rewriting every profile a user owns, purely to tidy them, is
        // precisely the case its comment names.
        if (!profile->saveToFile(filePath)) {
            qWarning() << "stripStoredRecipeBlocks: failed to write" << filePath
                       << "- left as it was; will retry on next launch";
            failed++;
            return;
        }
        qDebug() << "stripStoredRecipeBlocks: stripped" << filePath;
        migrated++;
    };

    QDir userDir(userProfilesPath());
    for (const QString& file : userDir.entryList({"*.json"}, QDir::Files)) {
        if (file == "_current.json") continue;   // as the pass this replaces did
        migrateFile(userDir.filePath(file));
    }

    QDir downloadedDir(downloadedProfilesPath());
    for (const QString& file : downloadedDir.entryList({"*.json"}, QDir::Files)) {
        migrateFile(downloadedDir.filePath(file));
    }

    if (m_profileStorage && m_profileStorage->isConfigured()) {
        for (const QString& name : m_profileStorage->listProfiles()) {
            const QString jsonContent = m_profileStorage->readProfile(name);
            if (jsonContent.isEmpty()) {
                // Same condition as an unopenable file above, and the same bucket.
                // readProfile() returns "" for ANY failure without logging, so a
                // stale SAF grant would otherwise skip the entire external store in
                // silence and still let the completion flag be set.
                qWarning() << "stripStoredRecipeBlocks: cannot read SAF profile" << name
                           << "- will retry on next launch";
                failed++;
                continue;
            }

            const std::optional<Profile> profile = stripped(name, jsonContent.toUtf8());
            if (!profile) continue;

            if (m_profileStorage->writeProfile(name, profile->toJsonString())) {
                migrated++;
            } else {
                qWarning() << "stripStoredRecipeBlocks: failed to write SAF profile:" << name;
                failed++;
            }
        }
    }

    if (failed > 0) {
        qWarning() << "Recipe block strip incomplete:" << migrated << "stripped,"
                   << refused << "left as they are," << promoted << "dose(s) promoted,"
                   << failed << "failed. Will retry on next launch.";
    } else {
        if (m_settings) m_settings->setValue("recipe_blocks_stripped", true);
        qDebug() << "Recipe block strip complete:" << migrated << "profile(s) stripped,"
                 << promoted << "dose(s) promoted to recommended_dose,"
                 << refused << "left as they are (non-canonical encoding)";
    }
}


void ProfileManager::migrateReadOnlyProfiles() {
    // One-time migration: rename user profiles that shadow built-in profiles,
    // and detect broken D-Flow/A-Flow profiles with wrong frame counts.
    // v2: re-run for 1.6.1 — the v1 migration missed profiles whose format was
    // changed by migrateProfileFormat() before compareProfiles() ran.
    if (m_settings && m_settings->value("readonly_profiles_migrated_v2", false).toBool()) {
        return;
    }

    int renamed = 0;
    int broken = 0;
    int failed = 0;

    // Helper: migrate a single profile file
    auto migrateFile = [&](const QString& filePath, const QString& filename,
                           bool isStorage) {
        // Load profile
        Profile profile;
        if (isStorage) {
            QString jsonContent = m_profileStorage ? m_profileStorage->readProfile(filename) : QString();
            if (jsonContent.isEmpty()) return;
            profile = Profile::loadFromJsonString(jsonContent);
        } else {
            profile = Profile::loadFromFile(filePath);
        }
        if (profile.title().isEmpty()) return;

        bool needsSave = false;
        QString newFilename = filename;
        QString newTitle = profile.title();

        // 4b: Handle user copies that shadow built-in profiles
        if (isBuiltInFilename(filename)) {
            // Compare user copy against built-in using the unified comparison
            // (same logic as de1app import and device migration).
            Profile builtIn = Profile::loadFromFile(QStringLiteral(":/profiles/") + filename + QStringLiteral(".json"));
            bool isModified = !ProfileSaveHelper::compareProfiles(profile, builtIn);
            // compareProfiles doesn't check title — check separately
            if (!isModified && profile.title() != builtIn.title()) isModified = true;

            if (isModified) {
                // User actually changed something — rename to preserve their edits
                newTitle = profile.title() + " (user)";
                profile.setTitle(newTitle);
                profile.setReadOnly(0);  // User copy is editable
                newFilename = titleToFilename(newTitle);
                needsSave = true;

                qDebug() << "migrateReadOnlyProfiles: renamed modified user override:"
                         << filename << "->" << newFilename;
                renamed++;
            } else {
                // Unmodified copy of built-in — just delete it, built-in will take over
                if (isStorage && m_profileStorage && m_profileStorage->isConfigured()) {
                    m_profileStorage->deleteProfile(filename);
                } else {
                    QFile::remove(filePath);
                }
                qDebug() << "migrateReadOnlyProfiles: deleted unmodified shadow of built-in:"
                         << filename;
                return;  // No further processing needed
            }
        }

        // 4c: Fix simple profiles (settings_2a/2b) with stale stored steps.
        // De1app regenerates frames from scalar parameters at upload time — stored
        // steps are irrelevant. Clear them so Decenza regenerates from scalars.
        // Also fix settings_2b profiles where espressoHoldTime was incorrectly
        // populated from flowProfileHoldTime (the old getOrConvertRecipeParams bug).
        QString profileType = profile.profileType();
        bool isSimple = (profileType == QLatin1String("settings_2a") || profileType == QLatin1String("settings_2b"));

        if (isSimple && !profile.steps().isEmpty()) {
            // For settings_2b: if espressoHoldTime is 0 but flowProfileHoldTime is not,
            // the profile was corrupted by the old editor bug. Fix by swapping.
            if (profileType == QLatin1String("settings_2b")) {
                if (qFuzzyIsNull(profile.espressoHoldTime()) && profile.flowProfileHoldTime() > 0) {
                    profile.setEspressoHoldTime(profile.flowProfileHoldTime());
                    qDebug() << "migrateReadOnlyProfiles: fixed settings_2b hold time for" << filename
                             << "from flowProfileHoldTime:" << profile.flowProfileHoldTime();
                }
                if (qFuzzyIsNull(profile.espressoDeclineTime()) && profile.flowProfileDeclineTime() > 0) {
                    profile.setEspressoDeclineTime(profile.flowProfileDeclineTime());
                    qDebug() << "migrateReadOnlyProfiles: fixed settings_2b decline time for" << filename
                             << "from flowProfileDeclineTime:" << profile.flowProfileDeclineTime();
                }
            }

            // Clear stale stored steps — they'll be regenerated from scalars on load
            profile.setSteps({});
            profile.regenerateSimpleFrames();
            needsSave = true;
            qDebug() << "migrateReadOnlyProfiles: regenerated simple profile frames for" << filename;
        }

        // 4d: Detect broken D-Flow/A-Flow profiles (wrong frame count)
        qsizetype stepCount = profile.steps().size();
        bool isDFlow = isDFlowTitle(newTitle);
        bool isAFlow = isAFlowTitle(newTitle);

        if (isDFlow && stepCount != 3) {
            // Strip D-Flow prefix and mark as broken
            QString stripped = newTitle;
            if (stripped.startsWith(QLatin1String("D-Flow / ")))
                stripped = stripped.mid(9);
            else if (stripped.startsWith(QLatin1String("D-Flow /")))
                stripped = stripped.mid(8).trimmed();
            newTitle = stripped + " (broken)";
            profile.setTitle(newTitle);

            newFilename = titleToFilename(newTitle);
            needsSave = true;

            qWarning() << "migrateReadOnlyProfiles: broken D-Flow profile"
                       << filename << "has" << stepCount << "frames (expected 3),"
                       << "renamed to:" << newTitle;
            broken++;
        } else if (isAFlow && stepCount != 9) {
            // Strip A-Flow prefix and mark as broken
            QString stripped = newTitle;
            if (stripped.startsWith(QLatin1String("A-Flow / ")))
                stripped = stripped.mid(9);
            else if (stripped.startsWith(QLatin1String("A-Flow /")))
                stripped = stripped.mid(8).trimmed();
            newTitle = stripped + " (broken)";
            profile.setTitle(newTitle);

            newFilename = titleToFilename(newTitle);
            needsSave = true;

            qWarning() << "migrateReadOnlyProfiles: broken A-Flow profile"
                       << filename << "has" << stepCount << "frames (expected 9),"
                       << "renamed to:" << newTitle;
            broken++;
        }

        if (!needsSave) return;

        // Save with new filename
        bool saved = false;
        if (isStorage && m_profileStorage && m_profileStorage->isConfigured()) {
            saved = m_profileStorage->writeProfile(newFilename, profile.toJsonString());
            if (saved && newFilename != filename) {
                m_profileStorage->deleteProfile(filename);
            }
        } else {
            QString dir = QFileInfo(filePath).absolutePath();
            QString newPath = dir + "/" + newFilename + ".json";
            saved = profile.saveToFile(newPath);
            if (saved && newFilename != filename) {
                QFile::remove(filePath);
            }
        }

        if (!saved) {
            qWarning() << "migrateReadOnlyProfiles: failed to save" << newFilename;
            failed++;
            return;
        }

        // Update favorites and currentProfile AFTER all renames are finalized
        // (fixes issue where shadow-rename + broken-detection would leave
        // favorites pointing to intermediate filename)
        if (newFilename != filename) {
            if (m_settings) {
                if (m_settings->app()->isFavoriteProfile(filename)) {
                    m_settings->app()->updateFavoriteProfile(filename, newFilename, newTitle);
                }
                if (m_settings->app()->currentProfile() == filename) {
                    m_settings->app()->setCurrentProfile(newFilename);
                    qDebug() << "migrateReadOnlyProfiles: updated currentProfile:"
                             << filename << "->" << newFilename;
                }
            }
        }
    };

    // Migrate user profiles (legacy local folder)
    QDir userDir(userProfilesPath());
    QStringList filters;
    filters << "*.json";
    QStringList files = userDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        migrateFile(userDir.filePath(file), name, false);
    }

    // Migrate downloaded profiles (legacy local folder)
    QDir downloadedDir(downloadedProfilesPath());
    files = downloadedDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        migrateFile(downloadedDir.filePath(file), name, false);
    }

    // Migrate ProfileStorage (SAF on Android)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        QStringList storageProfiles = m_profileStorage->listProfiles();
        for (const QString& name : storageProfiles) {
            migrateFile(QString(), name, true);
        }
    }

    if (failed > 0) {
        qWarning() << "Read-only profile migration incomplete:" << renamed << "renamed,"
                   << broken << "broken," << failed << "failed. Will retry on next launch.";
    } else {
        if (m_settings) m_settings->setValue("readonly_profiles_migrated_v2", true);
        qDebug() << "Read-only profile migration complete:" << renamed << "renamed,"
                 << broken << "broken profiles detected";

        // Refresh profiles list after migration
        if (renamed > 0 || broken > 0) {
            refreshProfiles();
        }
    }
}


