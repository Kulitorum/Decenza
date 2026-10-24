#pragma once

#include "core/preparationstate.h"
#include "core/stableweightcapture.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <optional>
#include <functional>
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
    bool startMilkAttempt() override;
    bool cancelMilkAttempt() override;
    bool setMilkTimeLocked(bool locked) override;
    // The next start's duration, or the actual timeout of an active steam.
    int plannedSteamDurationSec() const;

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
    void cacheLastLiveMilk(double scaleWeight);
    void finishSteamSession();
    void beginSteamSession();
    int scaledSteamDurationAtStart() const;
    void scheduleWake(std::optional<qint64> beanWake,
                      std::optional<qint64> milkWake);
    void disconnectScaleConnections();
    void applyModeFromPhase();
    bool canCaptureInPreparationPhase() const;
    bool scaleCanCapture() const;
    double selectedPitcherWeight() const;
    void handleBeanResult(const StableWeightCapture::Result& result);
    void handleMilkResult(const StableWeightCapture::Result& result, double rawWeight);
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
    double m_milkBeforeAttempt = 0.0;
    double m_steamElapsedTracker = 0.0;
    bool m_wasSteaming = false;
    bool m_milkCaptureSuppressed = false;
    bool m_scaleTargetAwaitingSample = true;
    bool m_realScaleSamplePending = false;
    bool m_refreshing = false;
    bool m_refreshPending = false;
    bool m_resetPending = false;
};
