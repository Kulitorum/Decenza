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
    Q_PROPERTY(bool hasMeasurement READ hasMeasurement NOTIFY stateChanged FINAL)
    Q_PROPERTY(double measuredValue READ measuredValue NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool neverPoured READ neverPoured NOTIFY stateChanged FINAL)

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
        // For the PRESSURE row this is the auto-flow finder's measured 0.5 bar/s
        // (maincontroller.cpp:3140), NOT a tighter number invented for
        // calibration. An earlier draft used 0.15 on the reasoning that "a
        // calibration hold is longer and flatter than a pour" — plausible,
        // unmeasured, and wrong: the DE1's PID moves pressure ~0.1-0.2 bar
        // between samples, which at 5 Hz is already ~0.5-1.0 bar/s, so 0.15
        // rejected every real hold.
        //
        // The TEMPERATURE row's 0.5 is °C/s, a different quantity with no
        // reference behind it — see the table in the .cpp, which says so and
        // says to tighten it from a real run rather than from here.
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

    // The ONLY way a correction reaches the machine.
    //
    // The pair (what the machine read, what the instrument read) must never be
    // separable, because the entire feature is the claim that the first half was
    // measured rather than typed. Exposing a write that takes both halves put
    // that claim in the hands of one QML binding, and there was a reachable path
    // through it: with the confirm dialog open, a machine going to sleep resets
    // the measurement while the dialog — parented to the overlay — stays up, so
    // the write went out with reported = 0.0. A full-scale bogus correction,
    // from the state whose entire purpose is to say nothing was measured.
    //
    // Now there is no expressible way to write one without a measurement.
    // Returns false, having sent nothing, if this object did not measure the run
    // or the reading fails its guards.
    Q_INVOKABLE bool applyCorrection(int sensor, double instrumentReading);

    // ---- Session ----
    // Arms for one sensor and one run. Any previous measurement is dropped: a
    // value must never outlive the run that produced it.
    Q_INVOKABLE void arm(int sensor);
    Q_INVOKABLE void reset();

    int stateInt() const { return static_cast<int>(m_state); }
    int sensorInt() const { return static_cast<int>(m_sensor); }
    bool hasMeasurement() const { return m_state == State::Measured; }
    // True when the run ended without water ever moving. Distinguishes "it never
    // held" from "it never got going", which need opposite advice.
    bool neverPoured() const { return m_state == State::NoHold && !m_sawPour; }
    // NaN unless hasMeasurement(). Deliberately not 0.0: zero is a plausible
    // reading, so a caller that forgot to check would get a number that looks
    // like an answer. NaN propagates and shows as "NaN" rather than lying.
    //
    // (This used to return value_or(0.0) under a comment claiming it was
    // guarded. It was not, and that gap is what made the sleep-mid-dialog write
    // possible — the next reader trusts the stated guarantee and skips their own
    // check, which is exactly what happened.)
    double measuredValue() const;
    // Not a Q_PROPERTY: it changes on every sample while the state stays
    // Observing, so a stateChanged NOTIFY would be stale for the whole period it
    // is interesting. Tests use it directly.
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
    // Whether the device this correction would go to is still the one that was
    // measured. Set false by any abort.
    DE1Device* device() const { return m_device; }

    // Latches that water actually moved. A run that reached preinfusion and was
    // stopped before pouring lands in a settled phase with samples that never
    // meant anything — a preinfusion plateau sits above the temperature hold
    // floor and is flat, so without this the wizard would report it as a valid
    // measurement and the user would calibrate against water that barely moved.
    //
    // The two outcomes reach the page as DIFFERENT messages (see neverPoured):
    // "it never held steady, run it again more carefully" is the wrong advice
    // for a run that did not get far enough to hold at all.
    //
    // (It is NOT what stops a never-STARTED run being judged NoHold — that is
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
