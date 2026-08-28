#include "sensorcalibrationcontroller.h"

#include "calibrationlogging.h"
#include "../ble/de1device.h"
#include "../core/translationmanager.h"

#include <algorithm>
#include <cmath>
#include <limits>

// The one per-sensor table. Every fact that differs between pressure and
// temperature is a column here; nothing else in the feature branches on which
// sensor it is.
//
// Bounds rationale:
//   pressure     0-14 bar covers everything a DE1 can read; corrections beyond
//                2 bar are a unit mix-up (PSI) or a slipped decimal, not a
//                calibration. The 0.5 bar/s rate ceiling is the auto-flow
//                finder's measured value (maincontroller.cpp:3140) rather than
//                a tighter guess — see the field comment in the header for what
//                happened to the guess.
//   temperature  0-110 C spans the machine's range; 5 C is a generous real
//                offset and a Fahrenheit number lands far outside it. Its rate
//                ceiling has no equivalent reference, so it is set at the same
//                0.5 per second, which is loose for a quantity with the group's
//                thermal mass and therefore errs toward accepting a hold rather
//                than rejecting one. Tighten it from a real run, not from here.
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
            /*maxCorrection=*/2.0,
            /*maxRateOfChange=*/0.5,
            /*holdFloor=*/1.5
        },
        SensorSpec{
            Sensor::Temperature,
            DE1::Calibration::Target::Temperature,
            "test_temperature_calibration",
            "settings.sensorCalibration.temperature.label",
            "Temperature Calibration",
            "settings.sensorCalibration.temperature.instrument",
            "Needs a basket fitted with a temperature probe",
            "°C",
            /*minValue=*/0.0,
            /*maxValue=*/110.0,
            /*maxCorrection=*/5.0,
            /*maxRateOfChange=*/0.5,
            /*holdFloor=*/50.0
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
const SensorCalibrationController::SensorSpec* rowFor(int sensor) {
    if (sensor < 0 || sensor >= SensorCalibrationController::sensorSpecs().size())
        return nullptr;
    return &SensorCalibrationController::sensorSpecs().at(sensor);
}

double median(QVector<double> values) {
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const qsizetype mid = values.size() / 2;
    if (values.size() % 2 == 1)
        return values.at(mid);
    return (values.at(mid - 1) + values.at(mid)) / 2.0;
}

}  // namespace

SensorCalibrationController::SensorCalibrationController(DE1Device* device,
                                                         MachineState* machineState,
                                                         TranslationManager* translationManager,
                                                         QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_machineState(machineState)
    , m_translationManager(translationManager) {
    if (m_device)
        connect(m_device, &DE1Device::shotSampleReceived,
                this, &SensorCalibrationController::onShotSample);
    if (m_machineState)
        connect(m_machineState, &MachineState::phaseChanged,
                this, &SensorCalibrationController::onPhaseChanged);
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
    if (!spec) return QString();
    return tr_(spec->labelKey, spec->labelFallback);
}

QString SensorCalibrationController::instrumentText(int sensor) const {
    const auto* spec = rowFor(sensor);
    if (!spec) return QString();
    return tr_(spec->instrumentKey, spec->instrumentFallback);
}

QString SensorCalibrationController::unitLabel(int sensor) const {
    const auto* spec = rowFor(sensor);
    return spec ? QString::fromUtf8(spec->unitLabel) : QString();
}

double SensorCalibrationController::minValue(int sensor) const {
    const auto* spec = rowFor(sensor);
    return spec ? spec->minValue : 0.0;
}

double SensorCalibrationController::maxValue(int sensor) const {
    const auto* spec = rowFor(sensor);
    return spec ? spec->maxValue : 0.0;
}

double SensorCalibrationController::maxCorrection(int sensor) const {
    const auto* spec = rowFor(sensor);
    return spec ? spec->maxCorrection : 0.0;
}

QString SensorCalibrationController::rejectionReason(int sensor, double instrumentReading) const {
    const auto* spec = rowFor(sensor);
    if (!spec) {
        return tr_("settings.sensorCalibration.reject.unknownSensor", "Unknown sensor.");
    }

    if (!std::isfinite(instrumentReading)) {
        return tr_("settings.sensorCalibration.reject.notANumber", "Enter a number.");
    }

    if (instrumentReading < spec->minValue || instrumentReading > spec->maxValue) {
        return tr_("settings.sensorCalibration.reject.outOfRange",
                   "That reading is outside %1 – %2 %3.")
            .arg(spec->minValue, 0, 'f', 1)
            .arg(spec->maxValue, 0, 'f', 1)
            .arg(QString::fromUtf8(spec->unitLabel));
    }

    // Only checkable once a run has been measured; before that there is nothing
    // to compare against and the range check above is the whole guard.
    if (!hasMeasurement())
        return QString();

    const double correction = std::abs(instrumentReading - measuredValue());
    if (correction > spec->maxCorrection) {
        return tr_("settings.sensorCalibration.reject.tooFar",
                   "That is %1 %2 from what the machine read (%3 %2). "
                   "Corrections over %4 %2 are usually a typo or the wrong unit.")
            .arg(correction, 0, 'f', 1)
            .arg(QString::fromUtf8(spec->unitLabel))
            .arg(measuredValue(), 0, 'f', 1)
            .arg(spec->maxCorrection, 0, 'f', 1);
    }

    return QString();
}

void SensorCalibrationController::arm(int sensor) {
    const auto* spec = rowFor(sensor);
    if (!spec) {
        CAL_WARN("Wizard") << "arm refused, sensor out of range:" << sensor;
        return;
    }

    m_sensor = spec->sensor;
    m_samples.clear();
    m_measured.reset();
    m_sawPour = false;
    m_observeStartS = 0.0;
    m_haveObserveStart = false;
    emit sensorChanged();
    CAL_INFO("Wizard") << "armed for" << QString::fromLatin1(spec->labelFallback);
    setState(State::Armed);
}

void SensorCalibrationController::reset() {
    m_samples.clear();
    m_measured.reset();
    m_sawPour = false;
    m_observeStartS = 0.0;
    m_haveObserveStart = false;
    setState(State::Idle);
}

double SensorCalibrationController::measuredValue() const {
    return m_measured.value_or(0.0);
}

double SensorCalibrationController::currentReading() const {
    if (!m_device) return 0.0;
    // The one place the sensor choice reaches the device. Both readings come off
    // the same shot sample, so no second stream is involved.
    switch (m_sensor) {
        case Sensor::Pressure:    return m_device->pressure();
        case Sensor::Temperature: return m_device->temperature();
    }
    return 0.0;
}

void SensorCalibrationController::onShotSample(const ShotSample& sample) {
    if (m_state != State::Observing) return;

    // Samples are only meaningful while water is actually moving.
    //
    // Observing starts at EspressoPreheating so the page can say the run is
    // under way, but that phase covers Heating/FinalHeating/Stabilising — the
    // group sitting still. For pressure the hold floor excludes those anyway
    // (~0 bar); for TEMPERATURE it does not, because a stabilising group sits at
    // roughly the same temperature it holds at, the rate test cannot separate
    // them, and the two merge into one window whose median is diluted with
    // readings taken while the probe was in still air.
    //
    // isFlowing() is the project's single definition of "water is moving" and
    // excludes Ending, so it also keeps the terminal drip out of the window.
    if (!m_machineState || !m_machineState->isFlowing()) return;

    if (!m_haveObserveStart) {
        m_observeStartS = sample.timer;
        m_haveObserveStart = true;
    }

    // sample.timer is a 16-bit field that wraps every 65536/100 = 655.36 s
    // (maincontroller.h:739-742 states this must not be subtracted across
    // persistent state without an explicit unwrap). A calibration run is far
    // shorter than that, but it does not start at zero — the machine's clock is
    // wherever it happens to be — so a run CAN straddle a wrap.
    //
    // Unwrapping matters more here than the odds suggest, because the failure is
    // silent: without it one elapsed value jumps ~-655 s, that step is scored as
    // rate 0 (holding) via the non-positive-dt branch, and the enclosing window's
    // span goes negative so it is discarded. The user is told the run never held
    // steady when it held perfectly.
    constexpr double kSampleTimerModSec = 65536.0 / 100.0;
    double elapsed = sample.timer - m_observeStartS;
    if (elapsed < 0.0) elapsed += kSampleTimerModSec;

    m_samples.append(Sample{ elapsed, currentReading() });
}

void SensorCalibrationController::onPhaseChanged() {
    if (!m_machineState) return;
    if (m_state == State::Idle) return;

    const MachineState::Phase phase = m_machineState->phase();

    // A dropped link or a sleeping machine is NEVER a completion. This is the
    // trap TransportPage.qml:47-70 documents for the drain: updatePhase() forces
    // Disconnected on a BLE drop, and treating that as "the run ended" would
    // hand back a value measured from a truncated run.
    if (phase == MachineState::Phase::Disconnected || phase == MachineState::Phase::Sleep) {
        if (m_state == State::Observing || m_state == State::Armed) {
            CAL_WARN("Wizard") << "run aborted, machine went"
                               << (phase == MachineState::Phase::Sleep ? "to sleep" : "away");
            m_samples.clear();
            m_measured.reset();
            setState(State::Aborted);
        }
        return;
    }

    const bool pouring = (phase == MachineState::Phase::Pouring ||
                          phase == MachineState::Phase::Preinfusion ||
                          phase == MachineState::Phase::EspressoPreheating);
    if (pouring) {
        if (m_state != State::Observing) {
            m_samples.clear();
            m_observeStartS = 0.0;
            m_haveObserveStart = false;
            // Not inherited from a previous run in the same session.
            m_sawPour = false;
            setState(State::Observing);
        }
        // Latched so a run that never started cannot be judged "no hold", which
        // is a different and misleading answer from "you never ran it".
        if (phase == MachineState::Phase::Pouring)
            m_sawPour = true;
        return;
    }

    if (m_state != State::Observing)
        return;

    // Only a settled landing means the run actually ran to a stop. Whitelisted
    // rather than "anything that is not pouring", for the same reason the drain
    // whitelists: an unlisted phase is not evidence of completion.
    const bool settled = (phase == MachineState::Phase::Idle ||
                          phase == MachineState::Phase::Ready ||
                          phase == MachineState::Phase::Heating ||
                          phase == MachineState::Phase::Ending);
    if (!settled)
        return;

    if (!m_sawPour) {
        CAL_INFO("Wizard") << "run ended without ever pouring";
        setState(State::NoHold);
        return;
    }

    m_measured = findSteadyHold();
    if (m_measured) {
        CAL_INFO("Wizard") << "machine held at" << *m_measured
                           << "over" << m_samples.size() << "samples";
        setState(State::Measured);
    } else {
        CAL_INFO("Wizard") << "no steady hold in" << m_samples.size() << "samples";
        setState(State::NoHold);
    }
}

std::optional<double> SensorCalibrationController::findSteadyHold() const {
    const auto* spec = specFor(m_sensor);
    if (!spec) return std::nullopt;

    // A hold shorter than this is a transient, not a plateau to measure.
    constexpr double kMinWindowSeconds = 2.0;
    constexpr int kMinWindowSamples = 8;

    if (m_samples.size() < kMinWindowSamples)
        return std::nullopt;

    // 3-sample centred moving average, for the reason the auto-flow finder gives
    // at maincontroller.cpp:3173: the DE1's PID makes small rapid corrections
    // that exceed any honest rate ceiling and would chop one real hold into
    // several short ones.
    QVector<double> smoothed(m_samples.size());
    for (qsizetype i = 0; i < m_samples.size(); ++i) {
        if (i == 0 || i == m_samples.size() - 1) {
            smoothed[i] = m_samples[i].value;
        } else {
            smoothed[i] = (m_samples[i - 1].value + m_samples[i].value + m_samples[i + 1].value) / 3.0;
        }
    }

    qsizetype bestStart = -1, bestEnd = -1;
    qsizetype winStart = -1;

    const auto closeWindow = [&](qsizetype endIndex) {
        if (winStart < 0) return;
        const double span = m_samples[endIndex].elapsedS - m_samples[winStart].elapsedS;
        const qsizetype count = endIndex - winStart + 1;
        const bool longEnough = span >= kMinWindowSeconds && count >= kMinWindowSamples;
        const bool longest = bestStart < 0 ||
                             span > (m_samples[bestEnd].elapsedS - m_samples[bestStart].elapsedS);
        if (longEnough && longest) {
            bestStart = winStart;
            bestEnd = endIndex;
        }
        winStart = -1;
    };

    for (qsizetype i = 1; i < m_samples.size(); ++i) {
        const double dt = m_samples[i].elapsedS - m_samples[i - 1].elapsedS;
        // A non-positive interval is not evidence of holding — it is a duplicate
        // or out-of-order timestamp, and scoring it as rate 0 would extend a
        // window across a discontinuity. Break the window instead.
        const bool usableInterval = dt > 1e-6;
        const double rate = usableInterval
                                ? std::abs(smoothed[i] - smoothed[i - 1]) / dt
                                : std::numeric_limits<double>::infinity();
        const bool holding = usableInterval
                             && rate <= spec->maxRateOfChange
                             && smoothed[i] >= spec->holdFloor;

        if (holding) {
            if (winStart < 0)
                winStart = i - 1;
        } else {
            closeWindow(i - 1);
        }
    }
    closeWindow(m_samples.size() - 1);

    if (bestStart < 0)
        return std::nullopt;

    QVector<double> window;
    window.reserve(bestEnd - bestStart + 1);
    for (qsizetype i = bestStart; i <= bestEnd; ++i)
        window.append(m_samples[i].value);
    // Median, not mean: one PID spike inside an otherwise flat hold should not
    // move the number the correction is computed from.
    return median(window);
}

void SensorCalibrationController::setState(State state) {
    if (m_state == state) return;
    m_state = state;
    emit stateChanged();
}
