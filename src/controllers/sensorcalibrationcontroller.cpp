#include "sensorcalibrationcontroller.h"

#include "calibrationlogging.h"
#include "../ble/de1device.h"
#include "../core/translationmanager.h"

#include <cmath>
#include <limits>

// The one per-sensor table. Every fact that differs between pressure and
// temperature is a column here; nothing else in the feature branches on which
// sensor it is.
//
// Bounds rationale:
//   pressure     0-14 bar covers everything a DE1 can read. A correction beyond
//                2 bar is a unit mix-up (PSI) or a slipped decimal — the test
//                profile's own lead-in and hold are 7 and 9 bar, so 2 bar is
//                also exactly the distance between them.
//   temperature  0-110 C spans the machine's range; 5 C is a generous real
//                offset and a Fahrenheit number lands far outside it.
const QVector<SensorCalibrationController::SensorSpec>&
SensorCalibrationController::sensorSpecs() {
    static const QVector<SensorSpec> specs = {
        SensorSpec{
            Sensor::Pressure,
            DE1::Calibration::Target::Pressure,
            "test_pressure_calibration",
            "settings.sensorCalibration.pressure.label",
            "Pressure Calibration",
            "settings.sensorCalibration.pressure.instrument",
            "Needs a portafilter fitted with a pressure gauge that leaks or flows slowly",
            "bar",
            /*minValue=*/0.0,
            /*maxValue=*/14.0,
            /*maxCorrection=*/2.0
        },
        SensorSpec{
            Sensor::Temperature,
            DE1::Calibration::Target::Temperature,
            "test_temperature_calibration",
            "settings.sensorCalibration.temperature.label",
            "Temperature Calibration",
            "settings.sensorCalibration.temperature.instrument",
            "Needs a basket fitted with a temperature probe",
            "\xc2\xb0""C",
            /*minValue=*/0.0,
            /*maxValue=*/110.0,
            /*maxCorrection=*/5.0
        }
    };
    return specs;
}

const SensorCalibrationController::SensorSpec*
SensorCalibrationController::specFor(Sensor sensor) {
    for (const auto& spec : sensorSpecs()) {
        if (spec.sensor == sensor)
            return &spec;
    }
    return nullptr;
}

namespace {

// Resolves an int from QML to a row, or nullptr. Kept in one place so every
// accessor below fails the same way on a bad id.
//
// Looks up by ENUM VALUE, not by table position. Those agree only while the rows
// happen to be declared in enum order — reorder sensorSpecs() and indexing by
// position silently addresses the wrong sensor while specFor() keeps working.
const SensorCalibrationController::SensorSpec* rowFor(int sensor) {
    using Sensor = SensorCalibrationController::Sensor;
    if (sensor < 0 || sensor > static_cast<int>(Sensor::Temperature))
        return nullptr;
    return SensorCalibrationController::specFor(static_cast<Sensor>(sensor));
}

constexpr double kNoValue = std::numeric_limits<double>::quiet_NaN();

}  // namespace

SensorCalibrationController::SensorCalibrationController(DE1Device* device,
                                                         TranslationManager* translationManager,
                                                         QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_translationManager(translationManager) {
}

void SensorCalibrationController::setProfileContextProvider(
        std::function<ProfileContext()> provider) {
    m_profileContext = std::move(provider);
    noteProfileChanged();
}

void SensorCalibrationController::noteProfileChanged() {
    // Every accessor below depends on which profile is loaded, so QML has to
    // re-read them when it changes.
    ++m_contextVersion;
    emit contextChanged();
}

SensorCalibrationController::ProfileContext
SensorCalibrationController::profileContext() const {
    return m_profileContext ? m_profileContext() : ProfileContext{};
}

QString SensorCalibrationController::tr_(const char* key, const char* fallback) const {
    if (m_translationManager)
        return m_translationManager->translateString(QString::fromUtf8(key),
                                                     QString::fromUtf8(fallback));
    return QString::fromUtf8(fallback);
}

int SensorCalibrationController::sensorCount() const {
    return static_cast<int>(sensorSpecs().size());
}

int SensorCalibrationController::calibrationTarget(int sensor) const {
    const auto* spec = rowFor(sensor);
    // -1 rather than a valid target: a caller with a bad id must not silently
    // address the pressure sensor.
    return spec ? static_cast<int>(spec->target) : -1;
}

QString SensorCalibrationController::profileFilename(int sensor) const {
    const auto* spec = rowFor(sensor);
    return spec ? QString::fromLatin1(spec->profileFilename) : QString();
}

QString SensorCalibrationController::label(int sensor) const {
    const auto* spec = rowFor(sensor);
    return spec ? tr_(spec->labelKey, spec->labelFallback) : QString();
}

QString SensorCalibrationController::instrumentText(int sensor) const {
    const auto* spec = rowFor(sensor);
    return spec ? tr_(spec->instrumentKey, spec->instrumentFallback) : QString();
}

QString SensorCalibrationController::unitLabel(int sensor) const {
    const auto* spec = rowFor(sensor);
    return spec ? QString::fromUtf8(spec->unitLabel) : QString();
}




bool SensorCalibrationController::isTestProfileActive(int sensor) const {
    const auto* spec = rowFor(sensor);
    if (!spec) return false;
    return profileContext().filename == QString::fromLatin1(spec->profileFilename);
}

double SensorCalibrationController::declaredHoldValue(int sensor) const {
    const auto* spec = rowFor(sensor);
    if (!spec) return kNoValue;
    // Only meaningful for the profile that declares it.
    if (!isTestProfileActive(sensor)) return kNoValue;

    const ProfileContext ctx = profileContext();
    const double value = (spec->sensor == Sensor::Pressure) ? ctx.holdPressure
                                                            : ctx.holdTemperature;

    // Reject a value the sensor could never read rather than passing it on as a
    // correction baseline.
    if (!std::isfinite(value) || value < spec->minValue || value > spec->maxValue)
        return kNoValue;
    return value;
}

QString SensorCalibrationController::rejectionReason(int sensor, double instrumentReading) const {
    const auto* spec = rowFor(sensor);
    if (!spec)
        return tr_("settings.sensorCalibration.reject.unknownSensor", "Unknown sensor.");

    if (!isTestProfileActive(sensor)) {
        return tr_("settings.sensorCalibration.reject.wrongProfile",
                   "Load the %1 test profile first.").arg(label(sensor));
    }

    const double declared = declaredHoldValue(sensor);
    if (!std::isfinite(declared)) {
        return tr_("settings.sensorCalibration.reject.noDeclaredHold",
                   "That profile does not declare a value to hold.");
    }

    if (!std::isfinite(instrumentReading))
        return tr_("settings.sensorCalibration.reject.notANumber", "Enter a number.");

    if (instrumentReading < spec->minValue || instrumentReading > spec->maxValue) {
        return tr_("settings.sensorCalibration.reject.outOfRange",
                   "That reading is outside %1 \xe2\x80\x93 %2 %3.")
            .arg(spec->minValue, 0, 'f', 1)
            .arg(spec->maxValue, 0, 'f', 1)
            .arg(QString::fromUtf8(spec->unitLabel));
    }

    const double correction = std::abs(instrumentReading - declared);
    // >=, not >. The pressure profile's lead-in and hold are 7 and 9 bar —
    // exactly maxCorrection apart — so a reading taken against the wrong one
    // lands precisely on the boundary and a > check would wave it through.
    if (correction >= spec->maxCorrection) {
        return tr_("settings.sensorCalibration.reject.tooFar",
                   "That is %1 %2 from what your machine is holding (%3 %2). "
                   "Corrections over %4 %2 are usually a typo or the wrong unit.")
            .arg(correction, 0, 'f', 1)
            .arg(QString::fromUtf8(spec->unitLabel))
            .arg(declared, 0, 'f', 1)
            .arg(spec->maxCorrection, 0, 'f', 1);
    }

    // The two agree — the check succeeded and there is nothing to correct.
    //
    // Blocked rather than sent, because a pair that agrees is a WRITE whose
    // effect on the machine is unverified: with no firmware source here, a
    // reported == measured pair either accumulates a zero delta (harmless) or
    // sets the offset to zero, which would discard a calibration the user
    // already had. Neither is what "I checked and they match" should do.
    //
    // Exact equality rather than an invented tolerance: a user who reads 9.0 on
    // the gauge and types 9.0 is saying they agree. Someone who types 8.9 means
    // a 0.1 correction, and that is their call to make.
    if (qFuzzyIsNull(correction)) {
        return tr_("settings.sensorCalibration.reject.alreadyAgree",
                   "Your gauge and your machine already agree at %1 %2 \xe2\x80\x94 "
                   "nothing to correct.")
            .arg(declared, 0, 'f', 1)
            .arg(QString::fromUtf8(spec->unitLabel));
    }

    return QString();
}

bool SensorCalibrationController::applyCorrection(int sensor, double instrumentReading) {
    const auto* spec = rowFor(sensor);
    if (!spec) {
        CAL_WARN("Wizard") << "correction refused, sensor out of range:" << sensor;
        return false;
    }

    const QString reason = rejectionReason(sensor, instrumentReading);
    if (!reason.isEmpty()) {
        CAL_WARN("Wizard") << "correction refused:" << reason;
        return false;
    }

    if (!m_device) {
        CAL_WARN("Wizard") << "correction refused \xe2\x80\x94 no machine";
        return false;
    }

    // The pair, assembled here and nowhere else. The machine's half is the
    // profile's declared hold — the number on screen the user compared against
    // — and the caller supplies only the instrument's reading.
    const double declared = declaredHoldValue(sensor);
    CAL_INFO("Wizard") << "applying" << QString::fromLatin1(spec->labelFallback)
                       << "correction: machine holds" << declared
                       << "instrument read" << instrumentReading;
    return m_device->sendCalibration(spec->target, DE1::Calibration::Command::Write,
                                     declared, instrumentReading);
}
