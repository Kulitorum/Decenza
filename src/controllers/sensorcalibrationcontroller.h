#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "../ble/protocol/de1characteristics.h"
#include "../machine/machinestate.h"

class DE1Device;
class TranslationManager;
struct ShotSample;

// Measures what the DE1's own sensor reported during a calibration run, so the
// user never has to type that half of the correction.
//
// WHY THIS EXISTS AT ALL
// ----------------------
// Every failure mode in sensor calibration reduces to the (reported, measured)
// pair being mismatched: a profile target mistaken for a reading, a value
// remembered from the wrong shot, a stale number left in a field. de1app is
// exposed to all three because both halves are text fields on a settings page,
// and it sends `::settings(espresso_pressure)` — a PROFILE parameter — as the
// machine-reported value. That is only correct for a simple pressure profile;
// an advanced profile carries a stale scalar (profiles/A-Flow____default-medium.tcl
// declares espresso_pressure 6.0 beside frames that hold 10.0 bar).
//
// This class removes the class of error rather than guarding it: the reported
// value can ONLY come from a run this object watched, live. There is no setter,
// no default, and no path that reads a historical shot.
//
// WHY NOT REUSE THE AUTO-FLOW WINDOW FINDER
// -----------------------------------------
// MainController's steady-window search (maincontroller.cpp:3140+) gates on
// weight flow >= 0.5 g/s, cross-checks machine flow against scale flow, and
// skips windows starting before 10 s. All three are right for flow calibration
// and wrong here: a calibration run uses a blind or deliberately leaking
// portafilter, often with no scale at all, and its hold can start early.
//
// NO TIMERS. Transitions come from MachineState::phaseChanged and from the
// sample stream, per the project rule.
class SensorCalibrationController : public QObject {
    Q_OBJECT

    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged FINAL)
    Q_PROPERTY(int sensor READ sensorInt NOTIFY sensorChanged FINAL)
    Q_PROPERTY(bool hasMeasurement READ hasMeasurement NOTIFY stateChanged FINAL)
    Q_PROPERTY(double measuredValue READ measuredValue NOTIFY stateChanged FINAL)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY stateChanged FINAL)

public:
    // The two sensors Decenza calibrates. Flow is deliberately absent: flow
    // correction is the Flow Calibration card's multiplier (MMR 0x80383C) plus
    // auto-calibration, and a second firmware-side correction would fight it.
    enum class Sensor {
        Pressure = 0,
        Temperature = 1
    };
    Q_ENUM(Sensor)

    enum class State {
        Idle,       // nothing armed
        Armed,      // waiting for the user to start the run
        Observing,  // the machine is pouring; samples are accumulating
        Measured,   // a steady hold was found; measuredValue() is usable
        NoHold,     // the run finished but never held steadily — no value
        Aborted     // the link dropped, the machine slept, or the run was cut short
    };
    Q_ENUM(State)

    // Everything that differs between the two sensors, in ONE place. A new
    // sensor is a row here; nothing else in the feature branches on which one it
    // is. Both Maintenance rows and every wizard step read this table.
    struct SensorSpec {
        Sensor sensor;
        DE1::Calibration::Target target;
        // Which shipped test profile drives this sensor's run.
        const char* profileFilename;
        // Translation keys. The label names the Maintenance row; the instrument
        // key states what hardware the user must fit, and is shown BOTH on the
        // Maintenance row and in the wizard's prepare step, so someone who lacks
        // the instrument can tell without opening anything.
        const char* labelKey;
        const char* labelFallback;
        const char* instrumentKey;
        const char* instrumentFallback;
        // Unit shown beside every value for this sensor. Temperature is always
        // Celsius here regardless of the user's display preference — the machine
        // register is Celsius, and converting silently is how a Fahrenheit number
        // ends up written into it.
        const char* unitLabel;
        // Physical bounds for an instrument reading. Outside these the entry is
        // a mistake, not a correction.
        double minValue;
        double maxValue;
        // The largest correction accepted in one write. Its job is catching a
        // typo or a unit mix-up (PSI for bar, Fahrenheit for Celsius) — NOT
        // preventing an unrecoverable state, because with an instrument in hand
        // there isn't one: re-running walks the calibration back. What it buys is
        // convergence speed and one less baffling cycle.
        double maxCorrection;
        // Rate-of-change ceiling that still counts as "holding", in units per
        // second.
        //
        // This is the auto-flow finder's measured 0.5 (maincontroller.cpp:3140),
        // NOT a tighter number invented for calibration. An earlier draft used
        // 0.15 on the reasoning that "a calibration hold is longer and flatter
        // than a pour" — plausible, unmeasured, and wrong: the DE1's PID moves
        // pressure ~0.1-0.2 bar between samples, which at a 5 Hz sample rate is
        // already ~0.5-1.0 bar/s, so 0.15 rejected every real hold. The rule the
        // rest of this change follows applies here too — use the referenced
        // number, and tighten only with a measurement to point at.
        double maxRateOfChange;
        // Readings below this are the machine ramping or idle, not holding.
        double holdFloor;
    };

    static const QVector<SensorSpec>& sensorSpecs();
    // Null for an out-of-range value rather than a default row: a caller with a
    // bad sensor id must fail visibly, not silently calibrate pressure.
    static const SensorSpec* specFor(Sensor sensor);

    explicit SensorCalibrationController(DE1Device* device,
                                         MachineState* machineState,
                                         TranslationManager* translationManager,
                                         QObject* parent = nullptr);

    // ---- Table accessors, for QML ----
    // One table, read through here, so no QML file carries a second copy of any
    // per-sensor fact.
    Q_INVOKABLE int sensorCount() const;
    Q_INVOKABLE int calibrationTarget(int sensor) const;
    Q_INVOKABLE QString profileFilename(int sensor) const;
    Q_INVOKABLE QString label(int sensor) const;
    Q_INVOKABLE QString instrumentText(int sensor) const;
    Q_INVOKABLE QString unitLabel(int sensor) const;
    Q_INVOKABLE double minValue(int sensor) const;
    Q_INVOKABLE double maxValue(int sensor) const;
    Q_INVOKABLE double maxCorrection(int sensor) const;

    // Validates an instrument reading against this sensor's two guards and
    // returns an empty string when it passes, or a translated reason naming the
    // check that failed. One implementation, so the wizard cannot enforce a
    // different rule from the one documented on the table.
    Q_INVOKABLE QString rejectionReason(int sensor, double instrumentReading) const;

    // ---- Session ----
    // Arms for one sensor and one run. Any previous measurement is dropped: a
    // value must never outlive the run that produced it.
    Q_INVOKABLE void arm(int sensor);
    Q_INVOKABLE void reset();

    int stateInt() const { return static_cast<int>(m_state); }
    int sensorInt() const { return static_cast<int>(m_sensor); }
    bool hasMeasurement() const { return m_state == State::Measured; }
    // Meaningless unless hasMeasurement() — guarded rather than returning a
    // plausible zero, which would read as "the machine reported nothing wrong".
    double measuredValue() const;
    int sampleCount() const { return static_cast<int>(m_samples.size()); }

signals:
    void stateChanged();
    void sensorChanged();

private:
    // Plain members, not slots. A slot taking `const ShotSample&` would put an
    // incomplete type in moc's generated metacall, which would force this header
    // to include de1device.h — a heavy include for every TU that names this
    // class. Qt's pointer-to-member connect needs no slot marking.
    void onShotSample(const ShotSample& sample);
    void onPhaseChanged();

    struct Sample {
        double elapsedS;
        double value;
    };

    // Falls back to the English literal when no TranslationManager was
    // supplied (tests, tools), matching DatabaseBackupManager::tr_.
    QString tr_(const char* key, const char* fallback) const;
    void setState(State state);
    // Finds the longest window whose smoothed rate of change stays under the
    // sensor's ceiling while the reading stays above its floor, and returns the
    // median reading over it. Empty when no window qualifies.
    std::optional<double> findSteadyHold() const;
    double currentReading() const;

    DE1Device* m_device = nullptr;
    MachineState* m_machineState = nullptr;
    TranslationManager* m_translationManager = nullptr;

    Sensor m_sensor = Sensor::Pressure;
    State m_state = State::Idle;
    QVector<Sample> m_samples;
    std::optional<double> m_measured;
    // Origin for sample timestamps, in the MACHINE's own elapsed-time clock
    // (ShotSample::timer), latched at the first sample of the run.
    //
    // Not wall clock. The rate test divides by the interval between samples, so
    // using the app's clock would make the answer depend on event-loop
    // scheduling — a delayed delivery would read as a fast-moving sensor. The
    // machine timestamps its own samples; that is the clock the reading belongs
    // to.
    double m_observeStartS = 0.0;
    bool m_haveObserveStart = false;
    // Latches that water actually moved. A run that reached preinfusion and was
    // stopped before pouring lands in a settled phase with samples that never
    // meant anything; without this it would be reported as "no steady hold",
    // which sends the user off to run it more carefully when the real answer is
    // that the run did not get far enough.
    //
    // (It is NOT what stops a never-started run being judged NoHold — that is
    // handled earlier, by onPhaseChanged returning while the state is still
    // Armed rather than Observing.)
    //
    // Cleared whenever Observing (re)starts, not only in arm()/reset(), so a
    // second run cannot inherit the first's latch.
    bool m_sawPour = false;

#ifdef DECENZA_TESTING
    friend class tst_SensorCalibration;
#endif
};
