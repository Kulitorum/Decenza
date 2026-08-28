#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../controllers/maincontroller.h"
#include "../controllers/profilemanager.h"
#include "../core/settings.h"
#include "../core/settings_brew.h"
#include "../core/settings_dye.h"
#include "../core/settings_theme.h"
#include "../core/settings_calibration.h"
#include "../core/databasebackupmanager.h"
#include "../network/mqttclient.h"

#include <QJsonObject>
#include <QJsonArray>

namespace {

// The profile a flow-calibration tool acts on: the explicit `profileFilename`
// argument, else the active profile. Shared by the flow_calibration verbs so
// the three cannot disagree about what "the current profile" means — they must
// resolve identically or a caller can read one profile's value and write another's.
//
// The existence check is the point of the helper, not a nicety. The per-profile
// calibration map is keyed by whatever string it is handed, and nothing downstream
// validates: setProfileFlowCalibration() rejects only an empty name and an
// out-of-range number. So a filename that does not resolve — a title instead of a
// filename, or the ".json" form that baseProfileName() never returns — writes an
// entry no reader will ever look up, reports success, and then persists across
// restarts and through settings export/import while the machine keeps using the
// real key. Failing loudly here is the only place that catches it; every other
// profile-taking tool (profiles_delete, profiles_rename, profiles_set_active)
// already guards the same way.
//
// `requireExists` is false for action=clear alone: REMOVING a key that
// names a missing profile is the one operation that should still work, because
// that is how an orphan written before this check existed gets cleaned up. Reading
// or writing one is always a mistake.
//
// Returns empty on failure with `error` set to the caller-facing reason.
QString resolveFlowCalProfile(const QJsonObject& args, ProfileManager* profileManager,
                              QString& error, bool requireExists = true)
{
    QString filename = args["profileFilename"].toString();
    if (filename.isEmpty() && profileManager)
        filename = profileManager->baseProfileName();
    if (filename.isEmpty()) {
        error = QStringLiteral("No profile filename specified and no active profile");
        return QString();
    }
    // A null ProfileManager (test harnesses) leaves nothing to validate against.
    // Resolve without the check rather than refusing every call.
    if (requireExists && profileManager && !profileManager->profileExists(filename)) {
        error = QStringLiteral("Profile not found: ") + filename
              + QStringLiteral(" (expected a filename without the .json extension)");
        return QString();
    }
    return filename;
}

} // namespace

void registerControlTools(McpToolRegistry* registry, DE1Device* device, MachineState* machineState,
                          ProfileManager* profileManager, MainController* mainController,
                          Settings* settings)
{
    // machine_wake
    registry->registerTool(
        "machine_wake",
        "Wake the machine from sleep mode",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [device](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!device || !device->isConnected()) {
                result["error"] = "Machine not connected";
                return result;
            }
            device->wakeUp();
            result["success"] = true;
            result["message"] = "Wake command sent";
            return result;
        },
        "control", McpTierCore);

    // machine_sleep
    registry->registerTool(
        "machine_sleep",
        "Put the machine to sleep",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [device, machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!device || !device->isConnected()) {
                result["error"] = "Machine not connected";
                return result;
            }
            if (machineState && machineState->isFlowing()) {
                result["error"] = "Cannot sleep while operation is in progress";
                return result;
            }
            device->goToSleep();
            result["success"] = true;
            result["message"] = "Sleep command sent";
            return result;
        },
        "control", McpTierCore);

    // machine_start — one tool, four operations. The readiness guard the four used
    // to repeat verbatim lives once, here.
    auto startGuard = [device, machineState](QJsonObject& result) -> bool {
        if (!device || !device->isConnected()) {
            result["error"] = "Machine not connected";
            return false;
        }
        if (!machineState) {
            result["error"] = "Machine state not available";
            return false;
        }
        if (!machineState->isReady()) {
            result["error"] = "Machine not ready (current phase: " + machineState->phaseString() + ")";
            return false;
        }
        return true;
    };

    const QVector<McpToolAction> startActions{
        McpRegistryHelpers::syncAction("espresso", "control",
        [device, profileManager, settings, startGuard](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!startGuard(result)) return result;

            // Apply brew overrides if provided — same as QML BrewDialog.
            // Absent arguments default to the CURRENT effective values, never
            // to 0: the old code passed a missing dose as 0 straight into
            // activateBrewWithOverrides, wiping the live dose — merely a
            // mislabeled shot record before, but a 0 g stop target under a
            // ratio anchor (add-yield-ratio-anchor). Same for a missing
            // temperature, which armed a 0 °C override.
            bool hasOverrides = args.contains("dose") || args.contains("yield") ||
                                args.contains("temperature") || args.contains("grind") ||
                                args.contains("rpm");
            if (hasOverrides && profileManager && settings) {
                const double dose = args.contains("dose") ? args["dose"].toDouble()
                                                          : profileManager->brewByRatioDose();
                double yieldValue;
                QString yieldMode;
                if (args.contains("yield")) {
                    yieldValue = args["yield"].toDouble();
                    yieldMode = QStringLiteral("absolute");
                } else if (profileManager->brewByRatioActive()) {
                    // Preserve an armed ratio anchor rather than flattening
                    // it to the grams it happens to derive right now.
                    yieldValue = profileManager->brewByRatio();
                    yieldMode = QStringLiteral("ratio");
                } else {
                    yieldValue = profileManager->targetWeight();
                    yieldMode = QStringLiteral("absolute");
                }
                const double temperature = args.contains("temperature")
                    ? args["temperature"].toDouble()
                    : (settings->brew()->hasTemperatureOverride()
                           ? settings->brew()->temperatureOverride()
                           : profileManager->profileTargetTemperature());
                const QString grind = args.contains("grind")
                    ? args["grind"].toString()
                    : settings->dye()->dyeGrinderSetting();
                // RPM override: -1 leaves the live RPM untouched (the common case);
                // only a supplied rpm changes it. Independent of the grind override.
                const int rpm = args.contains("rpm") ? args["rpm"].toInt() : -1;
                profileManager->activateBrewWithOverrides(dose, yieldValue, yieldMode,
                                                          temperature, grind, rpm);
            }

            device->startEspresso();
            result["success"] = true;
            result["message"] = hasOverrides ? "Espresso started with brew overrides" : "Espresso started";
            return result;
        }, QStringLiteral("Start pulling an espresso shot")),
        McpRegistryHelpers::syncAction("steam", "control",
        [device, startGuard](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!startGuard(result)) return result;
            device->startSteam();
            result["success"] = true;
            result["message"] = "Steam started";
            return result;
        }, QStringLiteral("Start steaming milk")),
        McpRegistryHelpers::syncAction("hot_water", "control",
        [device, startGuard](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!startGuard(result)) return result;
            device->startHotWater();
            result["success"] = true;
            result["message"] = "Hot water started";
            return result;
        }, QStringLiteral("Dispense hot water")),
        McpRegistryHelpers::syncAction("flush", "control",
        [device, startGuard](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!startGuard(result)) return result;
            device->startFlush();
            result["success"] = true;
            result["message"] = "Flush started";
            return result;
        }, QStringLiteral("Flush the group head")),
    };

    registry->registerActionTool(
        "machine_start",
        "Start an operation on the machine: espresso, steam, hot_water or flush. The machine must "
        "be in Ready state. Only works on DE1 v1.0 headless machines — most machines have a GHC "
        "and require a physical button press. Do not offer this unless the user explicitly asks. "
        "action=espresso takes optional brew overrides that apply to this shot only and clear when "
        "it ends, matching the QML BrewDialog.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"dose", QJsonObject{{"type", "number"}, {"description", "espresso: override dose weight for this shot (grams)"}}},
            {"yield", QJsonObject{{"type", "number"}, {"description", "espresso: override target yield for this shot (grams)"}}},
            {"temperature", QJsonObject{{"type", "number"}, {"description", "espresso: override temperature for this shot (Celsius)"}}},
            {"grind", QJsonObject{{"type", "string"}, {"description", "espresso: override grind setting for this shot"}}},
            {"rpm", QJsonObject{{"type", "integer"}, {"description", "espresso: override grinder motor RPM for this shot (variable-RPM grinders), paired with grind"}}}
        }}},
        startActions,
        McpTierCore,
        QStringLiteral("Which operation to start"));

    // machine_stop
    registry->registerTool(
        "machine_stop",
        "Stop the current operation (espresso, steam, hot water, or flush)",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [device, machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!device || !device->isConnected()) {
                result["error"] = "Machine not connected";
                return result;
            }
            // "In progress" means the machine is RUNNING an operation, which
            // is not the same as liquid moving. isFlowing() excludes espresso
            // preheat, Ending, and every non-flowing steam substate — so a
            // stop during preheat used to be refused with "no operation in
            // progress" while the machine went right on to pour a shot the
            // caller had explicitly asked to abort. That is the one moment a
            // stop is most likely to be wanted and most likely to be
            // automated. requestIdle() is safe from any state (it just asks
            // for Idle), so gate on the operation phases instead.
            using Phase = MachineState::Phase;
            const Phase phase = machineState ? machineState->phase() : Phase::Disconnected;
            const bool operationRunning =
                phase == Phase::EspressoPreheating || phase == Phase::Preinfusion ||
                phase == Phase::Pouring || phase == Phase::Ending ||
                phase == Phase::Steaming || phase == Phase::HotWater ||
                phase == Phase::Flushing || phase == Phase::Descaling ||
                phase == Phase::Cleaning || phase == Phase::Transport;
            if (!operationRunning) {
                result["error"] = "No operation in progress";
                return result;
            }
            device->requestIdle();
            result["success"] = true;
            result["message"] = "Stop command sent";
            return result;
        },
        "control", McpTierCore);

    // machine_skip_frame
    registry->registerTool(
        "machine_skip_frame",
        "Skip to the next profile frame during espresso extraction",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [device, machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!device || !device->isConnected()) {
                result["error"] = "Machine not connected";
                return result;
            }
            if (!machineState || !machineState->isFlowing()) {
                result["error"] = "No extraction in progress";
                return result;
            }
            device->skipToNextFrame();
            result["success"] = true;
            result["message"] = "Skipped to next frame";
            return result;
        },
        "control", McpTierCore);

    // backup_now
    registry->registerTool(
        "backup_now",
        "Create an immediate backup of the database, settings, profiles, and media. "
        "Returns the backup file path on success.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!mainController || !mainController->backupManager()) {
                result["error"] = "Backup manager not available";
                return result;
            }
            bool ok = mainController->backupManager()->createBackup(true);
            if (ok) {
                result["success"] = true;
                result["message"] = "Backup created successfully";
            } else {
                result["error"] = "Backup creation failed";
            }
            return result;
        },
        "control", McpTierNiche);

    // reset_saw_learning — two verbs, and they stay verbs rather than "one tool
    // whose optional argument decides how much it erases". An omitted argument that
    // means "wipe everything" is the wrong default for an irreversible tool.
    const QVector<McpToolAction> sawActions{
        McpRegistryHelpers::syncAction("all", "settings",
        [settings](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }
            settings->calibration()->resetSawLearning();
            result["success"] = true;
            result["message"] = "SAW learning data reset";
            return result;
        }, QStringLiteral("Erase ALL stop-at-weight learning (global pool, every profile/scale "
                          "history, bootstrap) — irreversible")),
        McpRegistryHelpers::syncAction("profile", "settings",
        [settings, profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }
            QString filename = args["profileFilename"].toString();
            if (filename.isEmpty() && profileManager) {
                filename = profileManager->baseProfileName();
            }
            if (filename.isEmpty()) {
                result["error"] = "No profile filename specified and no active profile";
                return result;
            }
            QString scale = args["scaleType"].toString();
            // Default to the scale actually serving, matching what the shot engine learns
            // under and what the Calibration tab shows. Defaulting to the saved primary
            // would silently clear a pool the user is not using.
            //
            // Ask SettingsCalibration directly rather than rehydrating the rule here.
            // This used to be two hand-rolled fallbacks, and the second one —
            // settings->scaleType() — did NOT normalize, so a legacy display name
            // surviving migration would clear "<profile>::Decent Scale" while the learner
            // writes "<profile>::decent", reporting success having cleared nothing. It
            // was also echoed verbatim to the model in result["scaleType"] below.
            if (scale.isEmpty()) scale = settings->calibration()->currentScaleType();
            settings->calibration()->resetSawLearningForProfile(filename, scale);
            result["success"] = true;
            result["profileFilename"] = filename;
            result["scaleType"] = scale;
            result["message"] = QString("SAW learning reset for %1 on %2 (all baskets)")
                                    .arg(filename, scale);
            return result;
        }, QStringLiteral("Erase stop-at-weight learning for one profile/scale pair, across every "
                          "basket — irreversible")),
    };

    registry->registerActionTool(
        "reset_saw_learning",
        "Erase stop-at-weight learning. action=all clears the global pool, every "
        "per-(profile, scale) history and pending batch, and the bootstrap. action=profile clears "
        "one pair only, defaulting to the active profile and the scale currently serving shots. "
        "Both are irreversible. Useful when switching beans or grind, where the learned flow "
        "deceleration curve no longer applies.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"profileFilename", QJsonObject{{"type", "string"}, {"description", "action=profile: profile filename (defaults to the active profile)"}}},
            {"scaleType", QJsonObject{{"type", "string"}, {"description", "action=profile: scale type (defaults to the scale currently serving shots, which is not always the saved primary)"}}},
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        sawActions,
        McpTierStandard,
        QStringLiteral("How much to erase"));

    // flow_calibration — read, hand-set and clear one profile's multiplier. These
    // three were already documented as a set (each old description had to explain
    // the other two), so one tool with three verbs is what they always were.
    const QVector<McpToolAction> flowCalActions{
        McpRegistryHelpers::syncAction("get", "read",
        [settings, profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }
            SettingsCalibration* calibration = settings->calibration();

            if (args["allProfiles"].toBool()) {
                // Whole-map listing. Deliberately does NOT resolve or validate a
                // profile name: this is the branch a caller reaches for when it does
                // not know the names, and it is also the only place an ORPHAN entry
                // is visible — a key written before resolveFlowCalProfile() started
                // rejecting unknown names, which no reader will ever look up.
                const QJsonObject map = calibration->allProfileFlowCalibrations();
                const bool listAutoOn = calibration->autoFlowCalibration();
                QJsonArray profiles;
                int orphanCount = 0;
                for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
                    const bool exists = !profileManager || profileManager->profileExists(it.key());
                    if (!exists) ++orphanCount;
                    profiles.append(QJsonObject{
                        {"profileFilename", it.key()},
                        {"multiplier", it.value().toDouble()},
                        {"profileExists", exists},
                        {"pendingAutoCalShots",
                         static_cast<int>(calibration->flowCalPendingIdeals(it.key()).size())}
                    });
                }
                result["profiles"] = profiles;
                result["profileCount"] = profiles.size();
                result["orphanCount"] = orphanCount;
                result["globalMultiplier"] = calibration->flowCalibrationMultiplier();
                result["autoFlowCalibration"] = listAutoOn;
                result["autoCalBatchSize"] = static_cast<int>(SettingsCalibration::kFlowCalBatchSize);
                // Every entry is inert while auto calibration is off, so say it once
                // here rather than repeating an "inEffect: false" on every row.
                result["state"] = listAutoOn
                    ? QString("%1 profile(s) have a stored flow calibration; each is in effect "
                              "when that profile is loaded.").arg(profiles.size())
                    : QString("%1 profile(s) have a stored flow calibration, but auto calibration "
                              "is OFF, so none of them is in effect — the machine uses the global "
                              "multiplier %2 for every profile.")
                          .arg(profiles.size()).arg(calibration->flowCalibrationMultiplier());
                if (orphanCount > 0) {
                    result["orphanNote"] =
                        QString("%1 entry/entries name a profile that no longer exists. They are "
                                "never read; action=clear removes one by name.")
                            .arg(orphanCount);
                }
                return result;
            }

            QString resolveError;
            QString filename = resolveFlowCalProfile(args, profileManager, resolveError);
            if (filename.isEmpty()) {
                result["error"] = resolveError;
                return result;
            }
            // profileFlowCalibration() returns 0.0 for "not stored" — unlike
            // hasProfileFlowCalibration(), which reports false whenever auto
            // calibration is off, even with a value on disk. Report the stored
            // fact and the auto-calibration switch separately so a caller can
            // tell "never learned" from "learned but currently ignored".
            const double perProfile = calibration->profileFlowCalibration(filename);
            const bool hasPerProfile = perProfile > 0.0;
            const bool autoOn = calibration->autoFlowCalibration();
            result["profileFilename"] = filename;
            result["hasPerProfileMultiplier"] = hasPerProfile;
            if (hasPerProfile) result["perProfileMultiplier"] = perProfile;
            result["globalMultiplier"] = calibration->flowCalibrationMultiplier();
            result["effectiveMultiplier"] = calibration->effectiveFlowCalibration(filename);
            result["effectiveSource"] = (autoOn && hasPerProfile) ? "perProfile" : "global";
            result["autoFlowCalibration"] = autoOn;
            // Auto calibration updates the stored value only on a full batch, so a
            // partial batch explains a value that has not moved for several shots.
            const int pending = static_cast<int>(calibration->flowCalPendingIdeals(filename).size());
            result["pendingAutoCalShots"] = pending;
            result["autoCalBatchSize"] = static_cast<int>(SettingsCalibration::kFlowCalBatchSize);

            // Consecutive shots that produced NO ideal, and why the last one
            // didn't. Without this, "0 of 5 collected" is the answer for a
            // brand-new profile AND for one being rejected every single shot and
            // always will be — and it reads as "keep pulling shots", which is
            // exactly wrong for the second. That sentence sent the #1872
            // reporter looking in the wrong place.
            const int rejected = calibration->flowCalRejectedShots(filename);
            result["rejectedShotsSinceLastIdeal"] = rejected;
            if (rejected > 0)
                result["lastRejectionReason"] = calibration->flowCalLastRejectionReason(filename);

            // One plain-language reading of the four flags above. An LLM asked "what is
            // this profile's calibration doing?" otherwise has to re-derive the
            // per-profile/global/auto interaction from the parts, and the inert case
            // (a stored value with auto off) is the one it gets wrong.
            QString state;
            if (!autoOn) {
                state = hasPerProfile
                    ? QString("Auto calibration is OFF, so %1 uses the global multiplier %2. A "
                              "per-profile value of %3 is stored but ignored until auto "
                              "calibration is turned back on.")
                          .arg(filename).arg(calibration->flowCalibrationMultiplier()).arg(perProfile)
                    : QString("Auto calibration is OFF, so %1 uses the global multiplier %2. No "
                              "per-profile value is stored.")
                          .arg(filename).arg(calibration->flowCalibrationMultiplier());
            } else if (!hasPerProfile) {
                state = QString("Auto calibration is on but has not committed a value for %1 yet, "
                                "so it falls back to the global multiplier %2. %3 of %4 shots "
                                "collected toward the first update.")
                            .arg(filename).arg(calibration->flowCalibrationMultiplier())
                            .arg(pending).arg(SettingsCalibration::kFlowCalBatchSize);
                if (rejected > 0)
                    state += QString(" But the last %1 shot(s) produced no usable measurement at "
                                     "all: %2. Pulling more shots will not help until that "
                                     "changes.")
                                 .arg(rejected)
                                 .arg(calibration->flowCalLastRejectionReason(filename));
            } else {
                state = QString("%1 is calibrated at %2 (per-profile, in effect). Auto calibration "
                                "is on: %3 of %4 shots collected toward the next update.")
                            .arg(filename).arg(perProfile)
                            .arg(pending).arg(SettingsCalibration::kFlowCalBatchSize);
                if (rejected > 0)
                    state += QString(" The last %1 shot(s) produced no usable measurement: %2. "
                                     "Until that changes the value will stay where it is.")
                                 .arg(rejected)
                                 .arg(calibration->flowCalLastRejectionReason(filename));
            }
            result["state"] = state;
            return result;
        }),
        McpRegistryHelpers::syncAction("set", "settings",
        [settings, profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }
            if (!args.contains("multiplier") || !args["multiplier"].isDouble()) {
                result["error"] = "multiplier is required and must be a number";
                return result;
            }
            const double multiplier = args["multiplier"].toDouble();
            // Pre-checked here purely so the refusal can say WHY. setProfileFlowCalibration
            // enforces the same bounds and returns false; that bool alone reaches the caller
            // as an unexplained failure.
            if (multiplier < SettingsCalibration::kProfileFlowCalMin
                || multiplier > SettingsCalibration::kProfileFlowCalMax) {
                result["error"] = QString("multiplier %1 is outside the accepted range %2-%3")
                                      .arg(multiplier)
                                      .arg(SettingsCalibration::kProfileFlowCalMin)
                                      .arg(SettingsCalibration::kProfileFlowCalMax);
                return result;
            }
            QString resolveError;
            QString filename = resolveFlowCalProfile(args, profileManager, resolveError);
            if (filename.isEmpty()) {
                result["error"] = resolveError;
                return result;
            }
            SettingsCalibration* cal = settings->calibration();
            const double previous = cal->effectiveFlowCalibration(filename);
            if (!cal->setProfileFlowCalibration(filename, multiplier)) {
                result["error"] = "Failed to store flow calibration for " + filename;
                return result;
            }
            // No applyFlowCalibration() call here: MainController connects
            // perProfileFlowCalibrationChanged to it, so the write above already
            // pushed the new multiplier to a connected machine.
            result["success"] = true;
            result["profileFilename"] = filename;
            result["multiplier"] = multiplier;
            result["previousEffectiveMultiplier"] = previous;
            result["effectiveMultiplier"] = cal->effectiveFlowCalibration(filename);
            result["autoFlowCalibration"] = cal->autoFlowCalibration();
            if (cal->autoFlowCalibration()) {
                result["note"] = QString("Stored and in effect for %1. Auto calibration is on, "
                                         "so it will keep adjusting this value from future shots.")
                                     .arg(filename);
            } else {
                // The write is real but inert until auto calibration is switched back on:
                // effectiveFlowCalibration() ignores per-profile values while auto is off.
                result["warning"] = QString("Stored for %1 but NOT in effect: auto calibration is "
                                            "off, so the machine uses the global multiplier %2. "
                                            "Set flowCalibrationMultiplier with settings_set to "
                                            "change what the machine uses, or turn auto "
                                            "calibration on to use this per-profile value.")
                                        .arg(filename)
                                        .arg(cal->flowCalibrationMultiplier());
            }
            return result;
        }, QStringLiteral("Overwrite the profile's flow calibration with a hand-set multiplier")),
        McpRegistryHelpers::syncAction("clear", "settings",
        [settings, profileManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }
            QString resolveError;
            QString filename = resolveFlowCalProfile(args, profileManager, resolveError,
                                                     /*requireExists=*/false);
            if (filename.isEmpty()) {
                result["error"] = resolveError;
                return result;
            }
            // Reported rather than treated as an error: clearing an already-clear
            // profile is not a failure, but a caller that believed it was removing
            // something should be able to tell that nothing was there.
            const bool hadCalibration =
                settings->calibration()->profileFlowCalibration(filename) > 0.0;
            settings->calibration()->clearProfileFlowCalibration(filename);
            result["success"] = true;
            result["profileFilename"] = filename;
            result["hadCalibration"] = hadCalibration;
            result["message"] = hadCalibration
                ? "Flow calibration cleared for " + filename
                : "No flow calibration was stored for " + filename + "; nothing to clear";
            return result;
        }, QStringLiteral("Clear the profile's flow calibration (re-learned over future shots)")),
    };

    registry->registerActionTool(
        "flow_calibration",
        "Per-profile flow calibration multiplier: get, set or clear it. Above 1.0 means the machine "
        "under-reported flow. All three default to the current profile. get with allProfiles=true "
        "lists every calibrated profile — the only way to ask that, since an unknown name is "
        "rejected. set takes 0.5-2.7 and refuses anything outside it. Auto calibration and the "
        "global fallback: get_agent_file topic \"flow_calibration\".",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"profileFilename", QJsonObject{{"type", "string"}, {"description", "Profile filename without the .json extension (defaults to the current profile)"}}},
            {"allProfiles", QJsonObject{{"type", "boolean"}, {"description", "get only: list every profile with a stored calibration; ignores profileFilename"}}},
            {"multiplier", QJsonObject{{"type", "number"}, {"description", "set only: the multiplier to store, 0.5-2.7"}}},
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        flowCalActions);

    // apply_theme
    registry->registerTool(
        "apply_theme",
        "Apply a preset theme. Built-in themes: 'Default Dark', 'Default Light'. "
        "User-created themes are also available.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"name", QJsonObject{{"type", "string"}, {"description", "Theme name to apply (e.g. 'Default Dark', 'Default Light')"}}}
            }},
            {"required", QJsonArray{"name"}}
        },
        [settings](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }
            QString name = args["name"].toString();
            if (name.isEmpty()) {
                result["error"] = "Theme name is required";
                return result;
            }
            if (!settings->theme()->applyPresetTheme(name)) {
                result["error"] = "No theme named '" + name + "'. Built-in names are "
                                  "'Default', 'Default Dark' and 'Default Light'; any other "
                                  "name must match a saved user theme exactly.";
                return result;
            }
            result["success"] = true;
            result["message"] = "Applied theme: " + name;
            return result;
        },
        "settings", McpTierNiche);

    // mqtt — connect / disconnect / publish_discovery. Tier niche: a Home Assistant
    // bridge is not what a client should be shown first when the list is long.
    const QVector<McpToolAction> mqttActions{
        McpRegistryHelpers::syncAction("connect", "control",
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!mainController || !mainController->mqttClient()) {
                result["error"] = "MQTT client not available";
                return result;
            }
            MqttClient* mqtt = mainController->mqttClient();
            if (mqtt->isConnected()) {
                result["message"] = "Already connected to MQTT broker";
                return result;
            }
            mqtt->connectToBroker();
            result["success"] = true;
            result["message"] = "MQTT connection initiated";
            return result;
        }),
        McpRegistryHelpers::syncAction("disconnect", "control",
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!mainController || !mainController->mqttClient()) {
                result["error"] = "MQTT client not available";
                return result;
            }
            MqttClient* mqtt = mainController->mqttClient();
            if (!mqtt->isConnected()) {
                // Already in the requested state. Success, not an error — asking
                // to disconnect something already disconnected got what it asked
                // for. But it must be DISTINGUISHABLE from a disconnect that ran,
                // so the no-op is a field, not just a change of wording. This
                // used to return a bare `message` and neither `success` nor
                // `error`, which is a third state a model cannot classify.
                //
                // action=publish_discovery treats the same state as an error, and
                // that is deliberate: it cannot do its job without a connection,
                // whereas the job here is already done.
                result["success"] = true;
                result["alreadyDisconnected"] = true;
                result["message"] = "Already disconnected from MQTT broker";
                return result;
            }
            mqtt->disconnectFromBroker();
            result["success"] = true;
            result["alreadyDisconnected"] = false;
            result["message"] = "MQTT disconnected";
            return result;
        }),
        McpRegistryHelpers::syncAction("publish_discovery", "control",
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!mainController || !mainController->mqttClient()) {
                result["error"] = "MQTT client not available";
                return result;
            }
            MqttClient* mqtt = mainController->mqttClient();
            if (!mqtt->isConnected()) {
                result["error"] = "Not connected to MQTT broker. Call action=connect first.";
                return result;
            }
            mqtt->publishDiscovery();
            result["success"] = true;
            result["message"] = "Home Assistant discovery messages published";
            return result;
        }),
    };

    registry->registerActionTool(
        "mqtt",
        "MQTT broker for Home Assistant: connect, disconnect, or publish_discovery. Broker "
        "settings (host, port, credentials) are configured with settings_set first, and "
        "publish_discovery needs a live connection.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        mqttActions,
        McpTierNiche);
}
