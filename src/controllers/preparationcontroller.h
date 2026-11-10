#pragma once

#include "core/preparationstate.h"
#include "core/stableweightcapture.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <optional>
#include <functional>
#include <deque>
#include <vector>

class MachineState;
class ScaleDeviceProxy;
class Settings;
class SettingsBrew;

// Native owner of the preparation mode and both stable-weight detectors.
// Timing is driven by one timer and StableWeightCapture's explicit deadlines.
class PreparationController final : public PreparationState
{
    Q_OBJECT

public:
    explicit PreparationController(Settings* settings, MachineState* machineState,
                                   QObject* parent = nullptr);

    void setScaleDeviceProxy(ScaleDeviceProxy* proxy);

    bool selectMode(const QString& mode) override;
    bool startMilkAttempt(const QString& owner) override;
    bool cancelMilkAttempt(const QString& owner) override;
    void releaseMilkClient(const QString& clientId) override;
    bool setMilkTimeLocked(bool locked) override;
    // The next start's duration, or the actual timeout of an active steam.
    int plannedSteamDurationSec() const;
    int plannedSteamDurationSec(const QVariantMap& pitcher, bool milkAutoCaptureEnabled,
                                int timeoutSec) const;

#ifdef DECENZA_TESTING
    void setClockForTesting(std::function<qint64()> clock);
    void evaluateForTesting();
#endif

signals:
    void applySteamSettingsRequested();
    void steamTimeoutImmediateRequested(int timeout);
    void steamSessionEndedRequested();
    void captureDingRequested();

private:
    void refresh(bool resetDetectors = false);
    void refreshPass(bool resetDetectors);
    void onPhaseChanged();
    void onScaleWeightChanged();
    void onScaleWeightSampleReceived(double weight);
    void onShotTimeChanged();
    void onTareCompleted();
    void onPitcherChanged();
    void onPitcherPresetsChanged();
    void finishSteamSession();
    // Writes the selected pitcher's own duration; returns that pitcher, or {} for
    // none or heater-off.
    QVariantMap restorePitcherSteamDuration();
    void beginSteamSession();
    int scaledSteamDurationAtStart(QString* source = nullptr) const;
    int scaledSteamDurationAtStart(const QVariantMap& pitcher, bool milkAutoCaptureEnabled,
                                   QString* source = nullptr) const;
    void scheduleWake(std::optional<qint64> beanWake,
                      std::optional<qint64> milkWake);
    void disconnectScaleConnections();
    void applyModeFromPhase();
    void closeMilkAttempt();
    bool canCaptureInPreparationPhase() const;
    bool scaleCanCapture() const { return !scaleCaptureBlocker(); }
    // Why the scale cannot feed the detectors, or nullptr when it can.
    const char* scaleCaptureBlocker() const;
    // Why the milk detector may not sample now, or nullptr: it needs an open attempt,
    // weight-timed steaming on, and a selected non-heater-off pitcher with a saved
    // empty weight. Scale and phase are checked separately.
    const char* milkSampleBlocker() const;
    // Why a milk weight would not scale the steam time, or nullptr when it would.
    const char* steamTimeBlocker() const;
    // reason is a string literal (or nullptr when armed), compared by address.
    void logMilkTransitions(const StableWeightCapture::Result& result,
                            StableWeightCapture::Arming arming, const char* reason,
                            double raw, double pitcher);
    double selectedPitcherWeight() const;
    void handleBeanResult(const StableWeightCapture::Result& result);
    void handleMilkResult(const StableWeightCapture::Result& result, double rawWeight);
    QString milkSampleDiagnostics(const StableWeightCapture::Result& result, double rawWeight) const;
    qint64 monotonicNowMs() const;
    WeightCaptureSnapshot beanSnapshot(const StableWeightCapture::Result& result,
                                       bool active, double raw, double cup) const;
    WeightCaptureSnapshot milkSnapshot(const StableWeightCapture::Result& result,
                                       bool active, double raw, double cup) const;

    QPointer<Settings> m_settings;
    QPointer<MachineState> m_machineState;
    QPointer<ScaleDeviceProxy> m_scaleDeviceProxy;
    QPointer<SettingsBrew> m_brew;
    StableWeightCapture m_beanCapture;
    StableWeightCapture m_milkCapture;
    QElapsedTimer m_clock;
#ifdef DECENZA_TESTING
    std::function<qint64()> m_testClock;
#endif
    QTimer m_wakeTimer;
    std::vector<QMetaObject::Connection> m_scaleConnections;
    PreparationSnapshot m_snapshot;
    int m_fallbackPitcherIndex = -1;
    double m_fallbackPitcherEmptyWeightG = 0.0;
    std::optional<double> m_rejectedMilkRaw;
    std::optional<double> m_rejectedMilkNet;
    QSet<QString> m_milkOwners;
    double m_steamElapsedTracker = 0.0;
    bool m_wasSteaming = false;
    bool m_scaleTargetAwaitingSample = true;
    bool m_realScaleSamplePending = false;
    bool m_refreshing = false;
    bool m_refreshPending = false;
    bool m_resetPending = false;
    // Last milk-capture state written to the log, so only transitions are logged.
    bool m_milkGateLogged = false;
    StableWeightCapture::Arming m_loggedMilkArming = StableWeightCapture::Arming::Off;
    const char* m_loggedMilkReason = nullptr;
    bool m_loggedMilkSeeded = false;
    bool m_loggedMilkLoad = false;
    double m_loggedMilkZero = 0.0;
    struct MilkSample { qint64 timeMs; double weight; };
    std::deque<MilkSample> m_milkSamples;
    std::optional<MilkSample> m_lastMilkSample;
    quint64 m_tareSequence = 0;
    quint64 m_milkBaselineTareSequence = 0;
    bool m_milkRemovalPending = false;
    bool m_milkAwaitingEmpty = false;
    std::optional<double> m_offerMilkG; // what the open offer has captured
    double m_offerRefRaw = 0.0;         // scale reading with that load on
    double m_offerGrossG = 0.0;         // pitcher plus milk
    bool m_offerBaselineLost = false;   // detector reset with the pitcher on
    bool m_dropMilkCapture = false;
};
