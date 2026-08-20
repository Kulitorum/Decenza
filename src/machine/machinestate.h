#pragma once

#include "samplestreak.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QtQml/qqmlregistration.h>
#include "../ble/protocol/de1characteristics.h"

class DE1Device;
class ScaleDevice;
class Profile;
class Settings;
class ShotTimingController;
class QQmlEngine;
class QJSEngine;

class MachineState : public QObject {
    Q_OBJECT

    // Compile-time QML registration, replacing BOTH the setContextProperty("MachineState", …)
    // and the qmlRegisterUncreatableType<MachineState>(…, "MachineStateType") that main.cpp used
    // to do. The two existed because a context property shadows a type of the same name, so the
    // enums had to be reached under a second, invented name. A QML_SINGLETON needs no such
    // split: `MachineState.Phase.Pouring` resolves through the singleton, and unlike either of
    // the runtime calls it is visible to qmllint, qmlcachegen and the language server. Full
    // rationale in src/controllers/maincontroller.h.
    //
    // ONE REAL BEHAVIOURAL CHANGE, and it is not obvious from the macros. Qt resolves enums on a
    // singleton INSIDE the instance guard — qqmltypewrapper.cpp:320,
    // `if (QObject *qobjectSingleton = enginePrivate->singletonInstance<QObject*>(type))`, with
    // the enum branch within it. The old uncreatable-type registration took the `else` at :361,
    // which needs no instance at all. So the 155 `MachineState.Phase.X` reads in qml/ (on 153
    // lines) used to be instance-independent constants and now depend on setQmlInstance().
    //
    // Miss that call and `MachineState.Phase` is `undefined`, so reading `.Pouring` off it
    // THROWS a TypeError with a file and line — these sites are the loud ones. The quiet damage
    // is elsewhere in the same failure: `Connections { target: MachineState }` (20+ sites)
    // resolves its target to null and simply never connects, and plain reads like
    // `MachineState.isReady` are `undefined`, hence falsy, so guarded actions refuse while
    // logging only their own "cannot start" line. If you are ever debugging that, look at the
    // Connections, not at the enums. tst_qmlregistration asserts the publish call for exactly
    // this reason.
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool isFlowing READ isFlowing NOTIFY phaseChanged)
    Q_PROPERTY(bool isHeating READ isHeating NOTIFY phaseChanged)
    Q_PROPERTY(bool isReady READ isReady NOTIFY phaseChanged)
    Q_PROPERTY(double shotTime READ shotTime NOTIFY shotTimeChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)
    Q_PROPERTY(double targetVolume READ targetVolume WRITE setTargetVolume NOTIFY targetVolumeChanged)
    // The type-id of the scale ACTUALLY serving, which is not always the saved primary:
    // the WiFi→BLE fallback preserves the WiFi primary on purpose, and a USB scale never
    // occupies main()'s physicalScale at all. Everything that keys persistent per-scale
    // state on "which scale is this?" — SAW learning, the Calibration tab's model/reset —
    // must read THIS, so the pool being written is the pool being shown and reset.
    // See SettingsCalibration and docs/CLAUDE_MD/SAW_LEARNING.md.
    Q_PROPERTY(QString activeScaleType READ activeScaleType NOTIFY activeScaleTypeChanged)
    // Display label matching activeScaleType, so a UI showing the two together cannot
    // name one scale while reporting the other's learned model.
    Q_PROPERTY(QString activeScaleName READ activeScaleName NOTIFY activeScaleTypeChanged)
    Q_PROPERTY(double scaleWeight READ scaleWeight NOTIFY scaleWeightChanged)
    Q_PROPERTY(double scaleFlowRate READ scaleFlowRate NOTIFY scaleFlowRateChanged)
    Q_PROPERTY(double smoothedScaleFlowRate READ smoothedScaleFlowRate NOTIFY scaleFlowRateChanged)
    Q_PROPERTY(double cumulativeVolume READ cumulativeVolume NOTIFY cumulativeVolumeChanged)
    Q_PROPERTY(double preinfusionVolume READ preinfusionVolume NOTIFY preinfusionVolumeChanged)
    Q_PROPERTY(double pourVolume READ pourVolume NOTIFY pourVolumeChanged)
    // True while the DE1's front standby switch is cutting AC power (substate
    // Error_NoAC), on firmware new enough to report it reliably. Always false while
    // disconnected — see updatePhase(). Firmware < 1337 reports this substate
    // spuriously, matching de1app's own gate.
    Q_PROPERTY(bool standbySwitchOpen READ standbySwitchOpen NOTIFY standbySwitchOpenChanged)
public:
    enum class Phase {
        Disconnected,
        Sleep,
        Idle,
        Heating,
        Ready,
        EspressoPreheating,  // Machine is in Espresso state but warming up
        Preinfusion,
        Pouring,
        Ending,
        Steaming,
        HotWater,
        Flushing,
        Refill,
        Descaling,           // Machine is running descale routine
        Cleaning,            // Machine is running clean routine
        Transport            // Machine is draining water (air purge) for transport
    };
    Q_ENUM(Phase)

    explicit MachineState(DE1Device* device, QObject* parent = nullptr);
    // Clears the SAW scale-type provider it installed on SettingsCalibration, which
    // captures `this` — see syncServingScale().
    ~MachineState() override;

    Phase phase() const { return m_phase; }
    QString phaseString() const;
    bool isFlowing() const;
    bool isHeating() const;
    bool isReady() const;
    double shotTime() const;
    double targetWeight() const { return m_targetWeight; }
    double targetVolume() const { return m_targetVolume; }
    double cumulativeVolume() const { return m_cumulativeVolume; }
    double preinfusionVolume() const { return m_preinfusionVolume; }
    double pourVolume() const { return m_pourVolume; }
    bool standbySwitchOpen() const { return m_standbySwitchOpen; }
    // QML_SINGLETON hooks. The engine does not create this object: main.cpp builds it on the
    // stack and publishes the pointer before QQmlEngine::load(). See maincontroller.h.
    static void setQmlInstance(MachineState *instance);
    static MachineState *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    ScaleDevice* scale() const;
    void setScale(ScaleDevice* scale);
    QString activeScaleType() const;
    QString activeScaleName() const;
    void setSettings(Settings* settings);
    void setTimingController(ShotTimingController* controller);
    void setTargetWeight(double weight);
    void setTargetVolume(double volume);
    void setProfileType(const QString& type) { m_profileType = type; }
    // Scale accessors (forward from current scale)
    double scaleWeight() const;
    double scaleFlowRate() const;
    double smoothedScaleFlowRate() const;

    // Called by WeightProcessor (via signal) to update cached flow rate
    void updateCachedFlowRates(double flowRate, double flowRateShort);

    // Called by MainController when shot samples arrive
    void onFlowSample(double flowRate, double deltaTime);

    // Tare the scale (call from MainController when first user frame starts)
    Q_INVOKABLE void tareScale(double triggerWeight = -1e18);

    // #1161: true iff stop-at-volume (SAV) ended the current/just-ended
    // shot. Set in checkStopAtVolume; reset by updatePhase at the START OF
    // EXTRACTION of the next shot (the EspressoPreheating→Preinfusion
    // transition), NOT at EspressoPreheating entry where the
    // espressoCycleStarted signal fires. So it remains valid both at
    // shot-end and when read from onEspressoCycleStarted.
    bool wasVolumeStopped() const { return m_stopAtVolumeTriggered; }

signals:
    void phaseChanged();
    void shotTimeChanged();
    void targetWeightChanged();
    void targetVolumeChanged();
    void cumulativeVolumeChanged();
    void preinfusionVolumeChanged();
    void pourVolumeChanged();
    void standbySwitchOpenChanged();
    void scaleWeightChanged();
    void activeScaleTypeChanged();
    void scaleFlowRateChanged();
    void espressoCycleStarted();  // When entering espresso preheating (clear graph here)
    // The PAIR of espressoCycleStarted: fires when the espresso cycle is left,
    // whether or not flow ever happened. shotEnded is NOT that pair — it fires
    // only when flow STOPS, so a cycle aborted during preheat (user stops, the
    // machine aborts, BLE drops) emits espressoCycleStarted with no shotEnded
    // ever following. Anything armed on cycle-start and released on shotEnded
    // therefore leaks on an aborted cycle — release on this instead.
    void espressoCycleEnded();
    void shotStarted();           // When extraction actually begins (flow starts)
    void shotEnded();
    void targetWeightReached();
    void targetVolumeReached();
    void tareCompleted();         // Emitted when scale reports ~0g after tare command
    void flowBeforeAutoTare();    // Emitted when auto-tare fires during preheat (tells WeightProcessor to reset)
    void sawBypassed();           // Emitted when SAW is skipped due to untared cup
    // Steam flow just ended (auto-stop at the timeout OR a manual stop).
    // Exactly-once per steam (m_steamFlowStopPending arms at steam flow start
    // and is consumed by whichever stop site fires first; a mid-steam BLE
    // disconnect disarms without emitting), and only for a steam whose flow
    // was actually observed. "Observed" means a flowing substate was seen:
    // isFlowing() whitelists Steaming AND Pouring, so a steam first seen at
    // Pouring still arms; only a steam first seen at Puffing/Ending (both
    // flowing notifications missed — those substates map straight into
    // Phase::Steaming without flow) never arms, so no stale-clock ghost
    // event. (FinalHeating maps to Phase::Heating — normal pre-flow warmup;
    // that steam arms later at the flowing transition.) Emitted synchronously from
    // updatePhase() so consumers (LiveSteamCoach) see it BEFORE the deferred
    // phaseChanged when the stop also leaves the Steaming phase.
    void steamFlowStopped();

private slots:
    void onDE1StateChanged();
    void onDE1SubStateChanged();
    void onScaleWeightChanged(double weight);
    void onScaleWeightSampleReceived(double weight);
    void onShotTimerTick();
    void onTimingControllerTareComplete();

private:
    void resetAutoTareGates();
    void issueAutoTare();
    static MachineState *s_qmlInstance;

    // Install the serving-scale provider on SettingsCalibration, which resolves the SAW
    // pool key for every consumer. Called from setSettings() ONLY, and once is enough:
    // the provider is a closure over m_scale, so it follows every later setScale() and
    // every connectedChanged without being reinstalled. (An earlier revision called it
    // from setScale() too, which was the right shape for a pushed VALUE and pointless
    // for a pull provider.)
    void syncServingScale();

    void updatePhase();
    void startShotTimer();
    void stopShotTimer();
    void checkStopAtWeightHotWater(double weight);
    void checkStopAtVolume();
    void checkStopAtVolumeHotWater();
    void checkStopAtTime();

    DE1Device* m_device = nullptr;
    QPointer<ScaleDevice> m_scale;  // Auto-nulls when scale is destroyed (prevents dangling pointer)
    Settings* m_settings = nullptr;
    ShotTimingController* m_timingController = nullptr;

    Phase m_phase = Phase::Disconnected;
    DE1::SubState m_previousSubState = DE1::SubState::Ready;
    double m_shotTime = 0.0;
    double m_targetWeight = 36.0;
    double m_targetVolume = 0.0;
    QString m_profileType = "settings_2c";
    double m_cumulativeVolume = 0.0;    // Total volume from flow meter (preinfusion + pour)
    int m_lastEmittedCumulativeVolumeMl = -1;  // Throttle: only emit when rounded ml changes
    double m_preinfusionVolume = 0.0;   // Volume during preinfusion substate (ml)
    int m_lastEmittedPreinfusionVolumeMl = -1;  // Throttle: only emit when rounded ml changes
    double m_pourVolume = 0.0;          // Volume during pouring substate (ml)
    int m_lastEmittedPourVolumeMl = -1;         // Throttle: only emit when rounded ml changes
    bool m_standbySwitchOpen = false;

    QTimer* m_shotTimer = nullptr;
    qint64 m_shotStartTime = 0;
    // steamFlowStopped arming: set when steam flow is observed starting,
    // consumed (exactly once) by the first flow-stop site that fires. See the
    // signal doc.
    bool m_steamFlowStopPending = false;
    bool m_stopAtWeightTriggered = false;
    bool m_stopAtVolumeTriggered = false;
    bool m_stopAtTimeTriggered = false;
    bool m_tareCompleted = false;
    bool m_waitingForTare = false;  // True after tare sent, waiting for scale to report ~0g
    QTimer* m_tareTimeoutTimer = nullptr;

    // Cached flow rates from WeightProcessor (updated via signal from worker thread)
    double m_cachedFlowRate = 0.0;
    double m_cachedFlowRateShort = 0.0;

    // Throttled debug logging for scale weight during active phases
    qint64 m_lastWeightLogMs = 0;

    // Auto-tare during "flow before" phase (cup placed during preheat).
    // There is deliberately no wall-clock holdoff here any more: m_awaitingTareEffect
    // below is the event-based interlock that replaced it.
    double m_autoTareLastSample = 0.0;

    // Auto-tare settle gate and post-tare zero verification. See
    // onScaleWeightSampleReceived() for what each one defends against; all of them
    // are consecutive-sample measures rather than timers, so they scale with
    // whatever rate the scale actually reports at.
    SampleStreak::Window m_autoTareWindow{3};   // spread of the last N samples
    SampleStreak::Counter m_zeroDriftStreak;    // consecutive samples showing a drifted zero
    SampleStreak::Counter m_unsettledCupStreak; // consecutive samples a cup sat unsettled
    // Reset whenever the pre-flow window is left, so it really is per-window.
    int m_retareAttempts = 0;
    // Set when a tare command goes out, cleared when the scale is OBSERVED to zero.
    // This is the event-based interlock that replaces the old wall-clock holdoff: a
    // scale that has not yet processed the last tare cannot be sent another one.
    bool m_awaitingTareEffect = false;
    // A lift/placement is a STEP; a bad zero is a gradual drift. Set on a large
    // negative step so the drift gate cannot mistake a lifted cup for a bad tare,
    // whatever the cup weighs.
    bool m_liftSuspected = false;
    bool m_retareIssuedThisWindow = false;  // so recovery can be reported once
    bool m_settleWaitLogged = false;        // latch for the once-per-streak notice

    // Hot water fire-and-forget tare: baseline weight at tare time.
    // SAW uses (scale_weight - baseline) so it works whether or not the BLE tare executes.
    double m_hotWaterTareBaseline = 0.0;
    qint64 m_hotWaterTareTimeMs = 0;  // For burst logging first 2s after tare
    double m_hotWaterMaxEffectiveWeight = 0.0;  // Peak effective weight seen (guards baseline clearing)
    double m_hotWaterFrozenWeight = -1.0;       // Effective weight at SAW trigger (-1 = not frozen). Reset on every new flow cycle start.
    double m_hotWaterSawTriggerWeight = -1.0;  // Raw scale weight at SAW trigger for learning overshoot (-1 = no trigger)

    // Throttle scaleWeightChanged / scaleFlowRateChanged to QML (10Hz cap).
    // Trailing-edge timers ensure the last update is never dropped.
    QElapsedTimer m_weightEmitTimer;                 // Throttle gate for scaleWeightChanged
    QTimer* m_weightTrailingTimer = nullptr;          // Trailing-edge for scaleWeightChanged
    QElapsedTimer m_flowRateEmitTimer;               // Throttle gate for scaleFlowRateChanged
    QTimer* m_flowRateTrailingTimer = nullptr;        // Trailing-edge for scaleFlowRateChanged

#ifdef DECENZA_TESTING
    friend class tst_SAV;
    friend class tst_MachineState;
    friend class tst_ProfileManager;
    friend class tst_MachineStatusSnapshot;
    friend class tst_LiveSteamCoach;
#endif
};
