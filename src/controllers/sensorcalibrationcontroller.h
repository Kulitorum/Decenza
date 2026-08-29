#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

#include "../ble/protocol/de1characteristics.h"

class DE1Device;
class TranslationManager;

// Sensor calibration, done the way Decent's own test profiles describe it: run
// the profile, compare the screen against your external gauge, enter the gauge
// reading, repeat until they agree.
//
// The machine's half of the correction is the loaded profile's DECLARED FINAL
// FRAME — not a live measurement, and not a per-profile scalar. Both wrong
// answers have been tried:
//
//   - A scalar goes stale. de1app sends ::settings(espresso_pressure)
//     (de1_skin_settings.tcl:2391); observed showing a Goal of 6.0 bar with the
//     calibration profile loaded and holding at 9.0, because the D-Flow editor
//     sets that global to 6.0 (profile_editors/D_Flow/code.tcl:154). Entering a
//     gauge reading against it writes a ~3 bar error.
//   - Measuring the hold from live samples needs a window search, and it picked
//     the profile's 20 s 7 bar lead-in over its 60 s 9 bar hold. Deleted; see
//     git history before rebuilding it.
//
// A correction is only computed while that sensor's own test profile is loaded,
// so the declared hold is the number the user is actually watching.

class SensorCalibrationController : public QObject {
    Q_OBJECT

public:
    // The two sensors Decenza calibrates. Flow is deliberately absent: flow
    // correction is the Flow Calibration card's multiplier (MMR 0x80383C) plus
    // auto-calibration, and a second firmware-side correction would fight it.
    enum class Sensor {
        Pressure = 0,
        Temperature = 1
    };
    Q_ENUM(Sensor)

    // Everything that differs between the two sensors, in ONE place. A new
    // sensor is a row here; nothing else in the feature branches on which one it
    // is. Both Calibration-card rows and every wizard step read this table.
    struct SensorSpec {
        Sensor sensor;
        DE1::Calibration::Target target;
        // The shipped test profile that drives this sensor, by filename. Also
        // the guard: a correction can only be computed while this profile is
        // active, because its declared hold is the value being compared.
        const char* profileFilename;
        // Translation keys. The label names the Calibration-card row; the
        // instrument key states what hardware the user must fit, and is shown
        // BOTH on that row and in the wizard, so someone who lacks the
        // instrument can tell without opening anything.
        const char* labelKey;
        const char* labelFallback;
        const char* instrumentKey;
        const char* instrumentFallback;
        // Unit shown beside every value for this sensor. Temperature is always
        // Celsius here regardless of the user's display preference — the machine
        // register is Celsius, and converting silently is how a Fahrenheit
        // number ends up written into it.
        const char* unitLabel;
        // Physical bounds for an instrument reading. Outside these the entry is
        // a mistake, not a correction.
        double minValue;
        double maxValue;
        // The largest correction accepted in one write. Its job is catching a
        // typo or a unit mix-up (PSI for bar, Fahrenheit for Celsius) — NOT
        // preventing an unrecoverable state, because with an instrument in hand
        // there isn't one: re-running walks the calibration back, which is what
        // the profile note means by "retest until the two agree".
        double maxCorrection;
        // Stepper increment for the instrument entry. The user nudges from the
        // declared hold rather than typing, so this is the resolution a gauge or
        // thermometer is actually read to.
        double entryStep;
    };

    static const QVector<SensorSpec>& sensorSpecs();
    // Null for an unknown sensor rather than a default row: a caller with a bad
    // id must fail visibly, not silently calibrate pressure.
    static const SensorSpec* specFor(Sensor sensor);

    // The two facts this class needs about the loaded profile. A PULL provider
    // rather than a ProfileManager pointer, for the reason SettingsCalibration's
    // scale-type provider gives: the dependency is two values, not a manager,
    // and taking the manager would drag its resource loading and disk paths into
    // every consumer — including the test binary, which then cannot construct
    // one without linking the profile resources.
    //
    // `filename` is the profile's FILENAME (ProfileManager::baseProfileName),
    // never the display name, which decorates itself once the profile is edited.
    struct ProfileContext {
        QString filename;
        double holdPressure = 0.0;
        double holdTemperature = 0.0;
    };

    explicit SensorCalibrationController(DE1Device* device,
                                         TranslationManager* translationManager,
                                         QObject* parent = nullptr);

    // Installed once by main(); the closure must not outlive what it captures.
    void setProfileContextProvider(std::function<ProfileContext()> provider);
    // Call when the active profile changes, so QML re-reads the accessors below.
    void noteProfileChanged();

    // ---- Table accessors, for QML ----
    // One table, read through here, so no QML file carries a second copy of any
    // per-sensor fact.
    Q_INVOKABLE int sensorCount() const;
    Q_INVOKABLE int calibrationTarget(int sensor) const;
    Q_INVOKABLE QString profileFilename(int sensor) const;
    Q_INVOKABLE QString label(int sensor) const;
    Q_INVOKABLE QString instrumentText(int sensor) const;
    Q_INVOKABLE QString unitLabel(int sensor) const;
    // Bounds for the entry stepper. It starts at the declared hold and is capped
    // at the largest accepted correction either side, so the guard is enforced
    // by the control's range rather than only by rejectionReason() rejecting
    // afterwards — the user cannot dial in a value that would be refused.
    Q_INVOKABLE double minValue(int sensor) const;
    Q_INVOKABLE double maxValue(int sensor) const;
    Q_INVOKABLE double maxCorrection(int sensor) const;
    Q_INVOKABLE double entryStep(int sensor) const;

    // True while this sensor's test profile is the active one. Everything below
    // depends on it: a declared hold only means something when the machine is
    // running the profile that declares it.
    Q_INVOKABLE bool isTestProfileActive(int sensor) const;

    // The value the machine holds to and displays during the test — the final
    // frame's pressure, or its temperature, depending on the sensor. NaN when
    // this sensor's test profile is not loaded or declares nothing usable;
    // never 0, which would read as a real reading.
    Q_INVOKABLE double declaredHoldValue(int sensor) const;

    // Validates an instrument reading against this sensor's two guards and
    // returns an empty string when it passes, or a translated reason naming the
    // check that failed. One implementation, so the wizard cannot enforce a
    // different rule from the one documented on the table.
    Q_INVOKABLE QString rejectionReason(int sensor, double instrumentReading) const;

    // The ONLY way a correction reaches the machine.
    //
    // The pair (what the machine holds to, what the instrument read) is
    // assembled here and nowhere else, so QML cannot name the machine's half.
    // Refuses — sending nothing — unless this sensor's test profile is active,
    // it declares a usable hold, and the reading passes its guards.
    Q_INVOKABLE bool applyCorrection(int sensor, double instrumentReading);

signals:
    void contextChanged();

private:
    // Falls back to the English literal when no TranslationManager was supplied
    // (tests, tools), matching DatabaseBackupManager::tr_.
    QString tr_(const char* key, const char* fallback) const;

    // Takes an already-resolved context so a caller asking both "is the test
    // profile loaded" and "what does it declare" pays one provider call, not one
    // per question. NaN unless ctx is this sensor's test profile and usable.
    static double declaredHoldFor(const SensorSpec& spec, const ProfileContext& ctx);

    // Resolves the active profile's identity and declared holds. Empty filename
    // when nothing is installed, which reads as "no test profile active".
    ProfileContext profileContext() const;

    DE1Device* m_device = nullptr;
    TranslationManager* m_translationManager = nullptr;
    std::function<ProfileContext()> m_profileContext;

#ifdef DECENZA_TESTING
    friend class tst_SensorCalibration;
#endif
};
