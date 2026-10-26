#include "preparationcontroller.h"

#include "core/diagnosticlogging.h"
#include "core/settings.h"
#include "core/settings_brew.h"
#include "core/settings_dye.h"
#include "core/steamcalculations.h"
#include "machine/machinestate.h"
#include "ble/scaledeviceproxy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr qint64 MaxSafeSequence = 9007199254740991LL;

QString formatGrams(double value)
{
    return QString::number(value, 'f', 1);
}

bool isPreparationPhase(MachineState::Phase phase)
{
    return phase == MachineState::Phase::Idle
        || phase == MachineState::Phase::Heating
        || phase == MachineState::Phase::Ready;
}

QString modeForOperationPhase(MachineState::Phase phase)
{
    switch (phase) {
    case MachineState::Phase::EspressoPreheating:
    case MachineState::Phase::Preinfusion:
    case MachineState::Phase::Pouring:
    case MachineState::Phase::Ending:
        return QStringLiteral("espresso");
    case MachineState::Phase::Steaming:
        return QStringLiteral("steam");
    case MachineState::Phase::HotWater:
        return QStringLiteral("hotwater");
    case MachineState::Phase::Flushing:
        return QStringLiteral("flush");
    default:
        return {};
    }
}

WeightCaptureSnapshot makeWeightSnapshot(const StableWeightCapture::Result& result,
                                         bool active, double raw, double cup,
                                         double minNet, double maxNet)
{
    WeightCaptureSnapshot state;
    state.active = active;
    state.seeded = result.seeded;
    state.loadPresent = result.loadPresent;
    state.isCaptured = result.captured;
    state.rawWeight = raw;
    state.cupWeight = cup;
    state.minNet = minNet;
    state.maxNet = maxNet;
    state.virtualZero = result.virtualZero;
    state.netWeight = result.netWeight;
    return state;
}

} // namespace

PreparationController::PreparationController(Settings* settings, MachineState* machineState,
                                             QObject* parent)
    : PreparationState(parent), m_settings(settings), m_machineState(machineState),
      m_brew(settings ? settings->brew() : nullptr)
{
    StableWeightCapture::Config milkConfig;
    milkConfig.minNet = 50.0;
    milkConfig.maxNet = 1500.0;
    milkConfig.tolerance = 1.5;
    const bool configured = m_milkCapture.setConfig(milkConfig);
    Q_ASSERT(configured);
    Q_UNUSED(configured);

    m_clock.start();
    m_wakeTimer.setSingleShot(true);
    connect(&m_wakeTimer, &QTimer::timeout, this, [this] { refresh(); });

    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged,
                this, &PreparationController::onPhaseChanged);
        connect(m_machineState, &MachineState::shotTimeChanged,
                this, &PreparationController::onShotTimeChanged);
        connect(m_machineState, &MachineState::tareCompleted,
                this, &PreparationController::onTareCompleted);
        connect(m_machineState, &MachineState::scaleWeightChanged,
                this, &PreparationController::onScaleWeightChanged);
        connect(m_machineState, &QObject::destroyed, this, [this] {
            m_machineState = nullptr;
            refresh();
        });
        if (m_machineState->phase() == MachineState::Phase::Steaming) {
            m_wasSteaming = true;
            m_steamElapsedTracker = m_machineState->shotTime();
        }
    }

    if (m_brew) {
        m_fallbackPitcherIndex = m_brew->selectedSteamPitcher();
        m_fallbackPitcherEmptyWeightG = selectedPitcherWeight();
        connect(m_brew, &SettingsBrew::doseCupTareWeightChanged,
                this, [this] { refresh(); });
        connect(m_brew, &SettingsBrew::milkAutoCaptureEnabledChanged,
                this, [this] { refresh(); });
        connect(m_brew, &SettingsBrew::selectedSteamPitcherChanged,
                this, &PreparationController::onPitcherChanged);
        connect(m_brew, &SettingsBrew::steamPitcherPresetsChanged,
                this, &PreparationController::onPitcherPresetsChanged);
        connect(m_brew, &QObject::destroyed, this, [this] {
            m_brew = nullptr;
            refresh();
        });
    }
    if (m_settings) {
        connect(m_settings, &QObject::destroyed, this, [this] {
            m_settings = nullptr;
            refresh();
        });
    }

    applyModeFromPhase();
    refresh();
}

void PreparationController::setScaleDeviceProxy(ScaleDeviceProxy* proxy)
{
    if (m_scaleDeviceProxy == proxy)
        return;
    disconnectScaleConnections();
    m_scaleDeviceProxy = proxy;
    m_scaleTargetAwaitingSample = true;
    m_realScaleSamplePending = false;
    m_snapshot.lastOnScaleMilkG = 0.0;
    if (proxy) {
        m_scaleConnections.emplace_back(connect(proxy, &ScaleDeviceProxy::connectedChanged,
                                                this, [this] {
            const bool connected = m_scaleDeviceProxy && m_scaleDeviceProxy->connected();
            if (!connected) {
                m_scaleTargetAwaitingSample = true;
                m_snapshot.lastOnScaleMilkG = 0.0;
            }
            DIAG_DEBUG(STEAM, "PreparationController")
                << QStringLiteral("Scale %1 — capture reset")
                       .arg(connected ? QStringLiteral("connected") : QStringLiteral("disconnected"));
            refresh(true);
        }));
        m_scaleConnections.emplace_back(connect(proxy, &ScaleDeviceProxy::targetChanged,
                                                this, [this] {
            // MachineState may have switched its scale immediately before the proxy
            // is re-pointed. Do not reuse its current reading as this target's live
            // milk value until the target emits an actual sample.
            m_scaleTargetAwaitingSample = true;
            m_realScaleSamplePending = false;
            m_snapshot.lastOnScaleMilkG = 0.0;
            DIAG_DEBUG(STEAM, "PreparationController")
                << "Scale target changed — capture reset, waiting for its first sample";
            refresh(true);
        }));
        m_scaleConnections.emplace_back(connect(proxy, &ScaleDeviceProxy::weightSampleReceived,
                                                this, &PreparationController::onScaleWeightSampleReceived));
        m_scaleConnections.emplace_back(connect(proxy, &ScaleDeviceProxy::weightChanged,
                                                this, [this] { refresh(); }));
        m_scaleConnections.emplace_back(connect(proxy, &QObject::destroyed, this, [this] {
            m_scaleDeviceProxy = nullptr;
            m_scaleTargetAwaitingSample = true;
            m_snapshot.lastOnScaleMilkG = 0.0;
            refresh();
        }));
    }
    refresh(true);
}

bool PreparationController::selectMode(const QString& mode)
{
    if (!PreparationSnapshot::isMode(mode) || !available()
        || (m_machineState && m_machineState->isOperating())) {
        return false;
    }
    if (m_snapshot.mode == mode) {
        if (mode == QLatin1String("steam"))
            m_milkCaptureSuppressed = false;
        refresh();
        return true;
    }

    if (m_snapshot.mode == QLatin1String("steam") && mode != QLatin1String("steam")) {
        m_snapshot.milkAttemptOpen = false;
        m_milkBeforeAttempt = 0.0;
        m_snapshot.lastOnScaleMilkG = 0.0;
    }

    m_snapshot.mode = mode;
    if (mode == QLatin1String("steam")) {
        m_milkCaptureSuppressed = false;
        m_milkBeforeAttempt = m_snapshot.sessionMeasuredMilkG;
        m_snapshot.milkAttemptOpen = true;
        m_snapshot.sessionMeasuredMilkG = 0.0;
        m_snapshot.lastOnScaleMilkG = 0.0;
        DIAG_DEBUG(STEAM, "PreparationController") << "Milk attempt opened (Steam selected)";
    }
    refresh();
    return true;
}

bool PreparationController::startMilkAttempt()
{
    if (!available() || (m_machineState && m_machineState->isOperating()))
        return false;
    if (m_snapshot.milkAttemptOpen)
        return true;
    m_snapshot.mode = QStringLiteral("steam");
    m_milkCaptureSuppressed = false;
    m_milkBeforeAttempt = m_snapshot.sessionMeasuredMilkG;
    m_snapshot.milkAttemptOpen = true;
    m_snapshot.sessionMeasuredMilkG = 0.0;
    m_snapshot.lastOnScaleMilkG = 0.0;
    DIAG_DEBUG(STEAM, "PreparationController") << "Milk attempt opened (milk button)";
    refresh();
    return true;
}

bool PreparationController::cancelMilkAttempt()
{
    if (!m_snapshot.milkAttemptOpen)
        return false;
    DIAG_DEBUG(STEAM, "PreparationController") << "Milk attempt cancelled";
    m_snapshot.milkAttemptOpen = false;
    m_milkCaptureSuppressed = true;
    m_snapshot.sessionMeasuredMilkG = m_milkBeforeAttempt;
    m_milkBeforeAttempt = 0.0;
    refresh();
    return true;
}

bool PreparationController::setMilkTimeLocked(bool locked)
{
    if (!available())
        return false;
    if (m_snapshot.milkTimeLocked == locked)
        return true;
    m_snapshot.milkTimeLocked = locked;
    refresh();
    return true;
}

#ifdef DECENZA_TESTING
void PreparationController::setClockForTesting(std::function<qint64()> clock)
{
    m_testClock = std::move(clock);
    m_wakeTimer.stop();
    refresh(true);
}

void PreparationController::evaluateForTesting()
{
    refresh();
}
#endif

qint64 PreparationController::monotonicNowMs() const
{
#ifdef DECENZA_TESTING
    if (m_testClock)
        return m_testClock();
#endif
    return m_clock.elapsed();
}

void PreparationController::refresh(bool resetDetectors)
{
    if (m_refreshing) {
        m_refreshPending = true;
        m_resetPending = m_resetPending || resetDetectors;
        return;
    }
    m_refreshing = true;
    bool resetPass = resetDetectors || m_resetPending;
    m_resetPending = false;
    do {
        m_refreshPending = false;
        refreshPass(resetPass);
        resetPass = m_resetPending;
        m_resetPending = false;
    } while (m_refreshPending);
    m_refreshing = false;
}

void PreparationController::refreshPass(bool resetDetectors)
{
    const qint64 nowMs = monotonicNowMs();
    const bool stateAvailable = m_settings && m_brew && m_machineState;
    const bool scaleAvailable = scaleCanCapture();
    const double scaleWeight = scaleAvailable && m_machineState
        ? m_machineState->scaleWeight() : 0.0;
    const bool validWeight = std::isfinite(scaleWeight);
    const double rawWeight = validWeight ? scaleWeight : 0.0;
    const bool preparationPhase = canCaptureInPreparationPhase();
    const bool scaleUsable = stateAvailable && scaleAvailable && validWeight;
    const bool beanActive = scaleUsable
        && preparationPhase && m_snapshot.mode == QLatin1String("espresso");
    const bool milkActive = scaleUsable
        && preparationPhase && m_snapshot.mode == QLatin1String("steam")
        && m_brew->milkAutoCaptureEnabled() && !m_milkCaptureSuppressed;
    using Arming = StableWeightCapture::Arming;
    const auto arming = [scaleUsable](bool active) {
        return active ? Arming::Armed : scaleUsable ? Arming::Tracking : Arming::Off;
    };
    const Arming beanArming = arming(beanActive);
    const Arming milkArming = arming(milkActive);
    const char* milkGateReason = nullptr;
    if (milkArming == Arming::Off) {
        milkGateReason = !stateAvailable ? "settings or machine state missing"
            : !scaleAvailable ? scaleCaptureBlocker()
            : "non-finite scale reading";
    } else if (milkArming == Arming::Tracking) {
        milkGateReason = m_snapshot.mode != QLatin1String("steam") ? "mode is not steam"
            : !preparationPhase ? "machine not idle"
            : !m_brew->milkAutoCaptureEnabled() ? "milk auto-capture off"
            : "attempt cancelled";
    }
    const double configuredBeanCupWeight = m_brew ? m_brew->doseCupTareWeight() : 0.0;
    const double beanCupWeight = std::isfinite(configuredBeanCupWeight)
        ? std::max(0.0, configuredBeanCupWeight) : 0.0;
    const double milkCupWeight = selectedPitcherWeight();

    StableWeightCapture::Result beanResult;
    StableWeightCapture::Result milkResult;
    if (resetDetectors) {
        // Disarm first so a device swap cannot emit a capture from the old
        // baseline before the new input starts its baseline dwell.
        m_beanCapture.update(rawWeight, beanCupWeight, Arming::Off, nowMs);
        m_milkCapture.update(rawWeight, milkCupWeight, Arming::Off, nowMs);
    }
    beanResult = m_beanCapture.update(rawWeight, beanCupWeight, beanArming, nowMs);
    milkResult = m_milkCapture.update(rawWeight, milkCupWeight, milkArming, nowMs);

    if (!beanResult.valid || !milkResult.valid) {
        // A non-finite scale sample is never allowed to carry a previous load
        // into the published snapshot or the next capture.
        m_beanCapture.update(0.0, beanCupWeight, Arming::Off, nowMs);
        m_milkCapture.update(0.0, milkCupWeight, Arming::Off, nowMs);
        beanResult = m_beanCapture.update(0.0, beanCupWeight, Arming::Off, nowMs);
        milkResult = m_milkCapture.update(0.0, milkCupWeight, Arming::Off, nowMs);
    }

    logMilkTransitions(milkResult, milkArming, milkGateReason, rawWeight, milkCupWeight);
    const bool wasMilkLoadPresent = m_snapshot.milk.loadPresent;
    handleBeanResult(beanResult);
    handleMilkResult(milkResult, rawWeight);

    if (!milkActive) {
        m_rejectedMilkRaw.reset();
        m_rejectedMilkNet.reset();
    }
    const bool realScaleSample = m_realScaleSamplePending;
    m_realScaleSamplePending = false;
    if (!resetDetectors && realScaleSample && milkActive && validWeight
        && !m_snapshot.milkTimeLocked && wasMilkLoadPresent && !milkResult.loadPresent
        && m_snapshot.mode == QLatin1String("steam") && preparationPhase) {
        m_snapshot.milkTimeLocked = true;
    }

    m_snapshot.bean = beanSnapshot(beanResult, beanActive, rawWeight, beanCupWeight);
    m_snapshot.milk = milkSnapshot(milkResult, milkActive, rawWeight, milkCupWeight);
    applySnapshot(m_snapshot, stateAvailable, true);
    scheduleWake(beanResult.nextWakeMs, milkResult.nextWakeMs);
}

void PreparationController::onPhaseChanged()
{
    if (!m_machineState)
        return;
    const MachineState::Phase phase = m_machineState->phase();
    if (phase == MachineState::Phase::Steaming) {
        if (!m_wasSteaming) {
            m_steamElapsedTracker = 0.0;
            beginSteamSession();
        }
        m_wasSteaming = true;
    } else {
        if (m_wasSteaming)
            finishSteamSession();
        m_wasSteaming = false;
    }
    applyModeFromPhase();
    refresh();
}

void PreparationController::onScaleWeightChanged()
{
    if (!m_scaleTargetAwaitingSample && scaleCanCapture() && m_machineState)
        cacheLastLiveMilk(m_machineState->scaleWeight());
    refresh();
}

void PreparationController::onScaleWeightSampleReceived(double weight)
{
    if (!m_scaleDeviceProxy || !m_scaleDeviceProxy->connected()
        || !m_machineState || m_scaleDeviceProxy->target() != m_machineState->scale()
        || !std::isfinite(weight)) {
        return;
    }
    m_scaleTargetAwaitingSample = false;
    m_realScaleSamplePending = true;
    cacheLastLiveMilk(weight);
    refresh();
}

void PreparationController::cacheLastLiveMilk(double scaleWeight)
{
    if (!std::isfinite(scaleWeight) || !m_machineState || !m_brew
        || m_machineState->phase() == MachineState::Phase::Steaming
        || !canCaptureInPreparationPhase() || m_snapshot.mode != QLatin1String("steam")
        || !scaleCanCapture()
        // The published empty reading has not moved with an unconfirmed tare yet.
        || m_milkCapture.tarePending()) {
        return;
    }
    const double milk = m_brew->netMilkForPitcher(
        m_brew->selectedSteamPitcher(), loadOnScaleG(scaleWeight));
    if (std::isfinite(milk) && milk > 0.0)
        m_snapshot.lastOnScaleMilkG = milk;
}

void PreparationController::onShotTimeChanged()
{
    if (m_machineState && m_machineState->phase() == MachineState::Phase::Steaming) {
        const double shotTime = m_machineState->shotTime();
        if (std::isfinite(shotTime) && shotTime >= 0.0)
            m_steamElapsedTracker = shotTime;
    }
    refresh();
}

void PreparationController::onTareCompleted()
{
    // Raised when the tare is requested, before the scale reports its zero
    // (ShotTimingController::tare()), which tare() relies on. MachineState's
    // wait-for-zero path raises it after, but runs only without a timing
    // controller, and main.cpp always sets one.
    DIAG_DEBUG(STEAM, "PreparationController") << "Tare reported — empty reading moves with the zero";
    m_rejectedMilkRaw.reset();
    m_rejectedMilkNet.reset();
    m_beanCapture.tare();
    m_milkCapture.tare();
    refresh();
}

void PreparationController::onPitcherChanged()
{
    // A pitcher switch closes an in-progress attempt just like Cancel. Preserve
    // the previously captured amount, which remains valid independently of the
    // live scale and the newly selected empty pitcher.
    if (m_snapshot.milkAttemptOpen)
        m_snapshot.sessionMeasuredMilkG = m_milkBeforeAttempt;
    m_snapshot.lastOnScaleMilkG = 0.0;
    m_snapshot.steamTimeoutScaled = false;
    m_snapshot.milkAttemptOpen = false;
    m_snapshot.milkTimeLocked = true;
    m_milkBeforeAttempt = 0.0;
    m_fallbackPitcherIndex = m_brew ? m_brew->selectedSteamPitcher() : -1;
    m_fallbackPitcherEmptyWeightG = selectedPitcherWeight();
    m_rejectedMilkRaw.reset();
    m_rejectedMilkNet.reset();
    // A changed cup tare alters the captured net. Let the detector observe that
    // input change while retaining its seeded scale baseline.
    refresh();
}

void PreparationController::onPitcherPresetsChanged()
{
    const int index = m_brew ? m_brew->selectedSteamPitcher() : -1;
    const double emptyWeightG = selectedPitcherWeight();
    if (index != m_fallbackPitcherIndex
        || emptyWeightG != m_fallbackPitcherEmptyWeightG) {
        m_snapshot.lastOnScaleMilkG = 0.0;
        m_rejectedMilkRaw.reset();
        m_rejectedMilkNet.reset();
    }
    m_fallbackPitcherIndex = index;
    m_fallbackPitcherEmptyWeightG = emptyWeightG;
    refresh();
}

int PreparationController::scaledSteamDurationAtStart(QString* source) const
{
    if (!m_brew || !m_snapshot.milkTimeLocked) {
        if (source)
            *source = QStringLiteral("not scaled: %1").arg(QLatin1String(steamTimeBlocker()));
        return 0;
    }

    double milk = 0.0;
    double liveLoad = std::numeric_limits<double>::quiet_NaN();
    const char* from = "live load";
    if (scaleCanCapture() && m_machineState) {
        liveLoad = loadOnScaleG(m_machineState->scaleWeight());
        milk = m_brew->netMilkForPitcher(m_brew->selectedSteamPitcher(), liveLoad);
    }
    if (!(milk > 0.0)) {
        milk = m_snapshot.sessionMeasuredMilkG;
        from = "captured milk";
    }
    if (!(milk > 0.0)) {
        milk = m_snapshot.lastOnScaleMilkG;
        from = "last on-scale milk";
    }

    const int pitcher = m_brew->selectedSteamPitcher();
    const int seconds = m_brew->scaledSteamTime(pitcher, milk);
    if (source) {
        const char* blocker = steamTimeBlocker();
        if (seconds > 0) {
            *source = QStringLiteral("%1 g from %2").arg(formatGrams(milk), QLatin1String(from));
        } else if (blocker) {
            *source = QStringLiteral("not scaled: %1").arg(QLatin1String(blocker));
        } else {
            *source = QStringLiteral("no usable milk weight (live load=%1, pitcher=%2, "
                                     "captured=%3, last on-scale=%4)")
                .arg(std::isfinite(liveLoad) ? formatGrams(liveLoad)
                                             : QLatin1String(scaleCaptureBlocker()),
                     formatGrams(selectedPitcherWeight()),
                     formatGrams(m_snapshot.sessionMeasuredMilkG),
                     formatGrams(m_snapshot.lastOnScaleMilkG));
        }
    }
    return seconds;
}

int PreparationController::plannedSteamDurationSec() const
{
    if (!m_brew)
        return 0;
    if (m_machineState && m_machineState->phase() == MachineState::Phase::Steaming)
        return m_brew->steamTimeout();
    const int scaled = scaledSteamDurationAtStart();
    return scaled > 0 ? scaled : m_brew->steamTimeout();
}

void PreparationController::beginSteamSession()
{
    QString source;
    const int seconds = scaledSteamDurationAtStart(&source);
    DIAG_INFO(STEAM, "PreparationController")
        << (seconds > 0 ? QStringLiteral("Steam start: %1 s scaled, %2").arg(seconds).arg(source)
                        : QStringLiteral("Steam start: preset duration %1 s, %2")
                              .arg(m_brew ? m_brew->steamTimeout() : 0).arg(source));
    if (seconds <= 0)
        return;

    m_brew->setSteamTimeout(seconds);
    m_snapshot.steamTimeoutScaled = true;
    emit steamTimeoutImmediateRequested(seconds);
}

void PreparationController::finishSteamSession()
{
    if (m_steamElapsedTracker >= 1.0 && m_snapshot.sessionMeasuredMilkG > 0.0 && m_brew) {
        m_brew->setLastSteamMilkG(m_snapshot.sessionMeasuredMilkG);
        m_brew->setLastSteamTimeS(m_steamElapsedTracker);
    }
    m_steamElapsedTracker = 0.0;
    m_snapshot.sessionMeasuredMilkG = 0.0;
    m_snapshot.lastOnScaleMilkG = 0.0;
    m_snapshot.steamTimeoutScaled = false;
    m_snapshot.milkAttemptOpen = false;
    m_milkBeforeAttempt = 0.0;
    m_snapshot.milkTimeLocked = true;
    m_milkCaptureSuppressed = false;

    if (m_brew) {
        const int selectedPitcher = m_brew->selectedSteamPitcher();
        const QVariantMap pitcher = m_brew->getSteamPitcherPreset(selectedPitcher);
        if (!pitcher.isEmpty() && !pitcher.value(QStringLiteral("disabled")).toBool()) {
            m_brew->setSteamTimeout(m_brew->effectiveSteamDurationSec(selectedPitcher, 0.0));
            m_brew->setSteamFlow(pitcher.value(QStringLiteral("flow"), 150).toInt());
        }
    }
    emit steamSessionEndedRequested();
}

void PreparationController::scheduleWake(std::optional<qint64> beanWake,
                                         std::optional<qint64> milkWake)
{
    std::optional<qint64> wake = beanWake;
    if (milkWake && (!wake || *milkWake < *wake))
        wake = milkWake;
    if (!wake) {
        m_wakeTimer.stop();
        return;
    }
#ifdef DECENZA_TESTING
    if (m_testClock) {
        m_wakeTimer.stop();
        return;
    }
#endif
    const qint64 remaining = std::max<qint64>(1, *wake - monotonicNowMs());
    const int interval = static_cast<int>(std::min<qint64>(remaining,
                                                           std::numeric_limits<int>::max()));
    m_wakeTimer.start(interval);
}

void PreparationController::disconnectScaleConnections()
{
    for (const QMetaObject::Connection& connection : m_scaleConnections)
        disconnect(connection);
    m_scaleConnections.clear();
}

void PreparationController::applyModeFromPhase()
{
    if (!m_machineState)
        return;
    const QString operationMode = modeForOperationPhase(m_machineState->phase());
    if (!operationMode.isEmpty()) {
        if (m_snapshot.mode == QLatin1String("steam")
            && operationMode != QLatin1String("steam")) {
            m_snapshot.milkAttemptOpen = false;
            m_milkBeforeAttempt = 0.0;
            m_snapshot.lastOnScaleMilkG = 0.0;
        }
        m_snapshot.mode = operationMode;
    }
}

bool PreparationController::canCaptureInPreparationPhase() const
{
    if (!m_machineState || m_machineState->isOperating())
        return false;
    return isPreparationPhase(m_machineState->phase());
}

const char* PreparationController::steamTimeBlocker() const
{
    if (!m_brew)
        return "settings missing";
    if (!m_snapshot.milkTimeLocked)
        return "time unlocked";
    return SteamCalculations::steamScalingBlocker(
        m_brew->getSteamPitcherPreset(m_brew->selectedSteamPitcher()),
        m_brew->milkAutoCaptureEnabled(), m_brew->steamSecondsPerGram());
}

const char* PreparationController::scaleCaptureBlocker() const
{
    if (!m_scaleDeviceProxy)
        return "no scale proxy";
    if (!m_scaleDeviceProxy->connected())
        return "scale not connected";
    if (m_scaleDeviceProxy->isFlowScale())
        return "flow scale (no absolute weight)";
    if (m_scaleTargetAwaitingSample)
        return "waiting for the scale's first sample";
    if (!m_machineState || m_scaleDeviceProxy->target() != m_machineState->scale())
        return "scale proxy and machine state point at different scales";
    return nullptr;
}

void PreparationController::logMilkTransitions(const StableWeightCapture::Result& result,
                                               StableWeightCapture::Arming arming,
                                               const char* reason, double raw, double pitcher)
{
    // Runs on every scale sample; text is built only for a line that is written.
    const auto values = [&] {
        return QStringLiteral("raw=%1 zero=%2 pitcher=%3")
            .arg(formatGrams(raw),
                 result.seeded ? formatGrams(result.virtualZero) : QStringLiteral("none"),
                 formatGrams(pitcher));
    };
    if (!m_milkGateLogged || arming != m_loggedMilkArming || reason != m_loggedMilkReason) {
        m_milkGateLogged = true;
        m_loggedMilkArming = arming;
        m_loggedMilkReason = reason;
        using Arming = StableWeightCapture::Arming;
        const char* state = arming == Arming::Off ? "off" : arming == Arming::Tracking ? "tracking" : "armed";
        DIAG_DEBUG(STEAM, "PreparationController")
            << QStringLiteral("Milk capture %1%2 mode=%3 %4")
                   .arg(QLatin1String(state),
                        reason ? QStringLiteral(": ") + QLatin1String(reason) : QString(),
                        m_snapshot.mode, values());
    }
    if (result.seeded != m_loggedMilkSeeded) {
        m_loggedMilkSeeded = result.seeded;
        m_loggedMilkZero = result.virtualZero;
        DIAG_DEBUG(STEAM, "PreparationController")
            << (result.seeded ? QStringLiteral("Milk empty reading learned ")
                              : QStringLiteral("Milk empty reading cleared "))
            + values();
    } else if (result.seeded && std::abs(result.virtualZero - m_loggedMilkZero) >= 1.0) {
        // Noise re-adopts the empty reading on every settle; a gram is a real move.
        DIAG_DEBUG(STEAM, "PreparationController")
            << QStringLiteral("Milk empty reading moved from %1 %2")
                   .arg(formatGrams(m_loggedMilkZero), values());
        m_loggedMilkZero = result.virtualZero;
    }
    if (result.loadPresent != m_loggedMilkLoad) {
        m_loggedMilkLoad = result.loadPresent;
        DIAG_DEBUG(STEAM, "PreparationController")
            << (result.loadPresent
                    ? QStringLiteral("Milk load placed load=%1 net=%2 ")
                          .arg(formatGrams(raw - result.virtualZero), formatGrams(result.netWeight))
                    : QStringLiteral("Milk load removed "))
            + values();
    }
}

double PreparationController::selectedPitcherWeight() const
{
    if (!m_brew)
        return 0.0;
    const QVariantMap pitcher = m_brew->getSteamPitcherPreset(m_brew->selectedSteamPitcher());
    if (pitcher.isEmpty() || pitcher.value(QStringLiteral("disabled")).toBool())
        return 0.0;
    const double weight = pitcher.value(QStringLiteral("pitcherWeightG")).toDouble();
    return std::isfinite(weight) && weight > 0.0 ? weight : 0.0;
}

void PreparationController::handleBeanResult(const StableWeightCapture::Result& result)
{
    if (!result.capturedGrams || !m_settings || !m_brew || !m_settings->dye())
        return;
    const double grams = *result.capturedGrams;
    m_settings->dye()->setDyeBeanWeight(grams);
    m_snapshot.lastBeanGrams = grams;
    if (m_snapshot.beanCaptureSequence < MaxSafeSequence)
        ++m_snapshot.beanCaptureSequence;
    if (m_brew->doseCaptureSoundEnabled())
        emit captureDingRequested();
}

void PreparationController::handleMilkResult(const StableWeightCapture::Result& result,
                                             double rawWeight)
{
    if (result.rejectedGrams) {
        // The event is raised for the current raw sample; retain that exact value
        // to match MilkCapture's rejected-load hold tolerance.
        m_rejectedMilkRaw = rawWeight;
        m_rejectedMilkNet = *result.rejectedGrams;
        const auto config = m_milkCapture.config();
        DIAG_DEBUG(STEAM, "PreparationController")
            << QStringLiteral("Milk load rejected net=%1 outside %2-%3 g raw=%4")
                   .arg(formatGrams(*result.rejectedGrams), formatGrams(config.minNet),
                        formatGrams(config.maxNet), formatGrams(rawWeight));
    }
    if (!result.capturedGrams || !m_brew)
        return;

    const double grams = *result.capturedGrams;
    int seconds = 0;
    m_snapshot.sessionMeasuredMilkG = grams;
    m_snapshot.milkAttemptOpen = false;
    m_milkBeforeAttempt = 0.0;
    if (m_snapshot.milkTimeLocked) {
        seconds = m_brew->scaledSteamTime(m_brew->selectedSteamPitcher(), grams);
        if (seconds > 0) {
            m_brew->setSteamTimeout(seconds);
            m_snapshot.steamTimeoutScaled = true;
            emit applySteamSettingsRequested();
        }
    }
    const char* blocker = steamTimeBlocker();
    DIAG_DEBUG(STEAM, "PreparationController")
        << QStringLiteral("Milk captured %1 g -> %2")
               .arg(formatGrams(grams),
                    seconds > 0 ? QStringLiteral("%1 s steam").arg(seconds)
                                : QStringLiteral("time not scaled: %1")
                                      .arg(QLatin1String(blocker ? blocker : "rate gave 0 s")));
    m_snapshot.lastMilkGrams = grams;
    m_snapshot.lastMilkSeconds = seconds;
    if (m_snapshot.milkCaptureSequence < MaxSafeSequence)
        ++m_snapshot.milkCaptureSequence;
    if (seconds > 0 && m_brew->doseCaptureSoundEnabled())
        emit captureDingRequested();
}

WeightCaptureSnapshot PreparationController::beanSnapshot(
    const StableWeightCapture::Result& result, bool active, double raw, double cup) const
{
    const auto config = m_beanCapture.config();
    return makeWeightSnapshot(result, active, raw, cup, config.minNet, config.maxNet);
}

WeightCaptureSnapshot PreparationController::milkSnapshot(
    const StableWeightCapture::Result& result, bool active, double raw, double cup) const
{
    const auto config = m_milkCapture.config();
    WeightCaptureSnapshot state = makeWeightSnapshot(result, active, raw, cup, config.minNet, config.maxNet);
    const bool rejectedHolds = m_rejectedMilkRaw && m_rejectedMilkNet && result.loadPresent
        && std::abs(raw - *m_rejectedMilkRaw) <= config.tolerance;
    state.lighterThanPitcher = rejectedHolds && *m_rejectedMilkNet < -5.0;
    state.belowMinimum = rejectedHolds && *m_rejectedMilkNet >= -5.0
        && *m_rejectedMilkNet < state.minNet;
    return state;
}
