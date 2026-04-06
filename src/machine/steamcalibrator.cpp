#include "steamcalibrator.h"
#include "../models/steamdatamodel.h"
#include "../core/settings.h"
#include "../ble/de1device.h"

#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QtMath>
#include <algorithm>

SteamCalibrator::SteamCalibrator(Settings* settings, DE1Device* device, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_device(device)
{
    loadCalibration();
}

int SteamCalibrator::currentFlowRate() const
{
    if (m_currentStep >= 0 && m_currentStep < m_flowSteps.size())
        return m_flowSteps[m_currentStep];
    return 0;
}

int SteamCalibrator::recommendedFlow() const { return m_calibrationResult.recommendedFlow; }
double SteamCalibrator::recommendedDilution() const { return m_calibrationResult.recommendedDilution; }
double SteamCalibrator::bestCV() const { return m_calibrationResult.bestCV; }

bool SteamCalibrator::hasCalibration() const
{
    return !m_calibrationResult.steps.isEmpty() && m_calibrationResult.recommendedFlow > 0;
}

QVariantList SteamCalibrator::results() const
{
    QVariantList list;
    for (const auto& step : m_calibrationResult.steps) {
        QVariantMap map;
        map[QStringLiteral("flowRate")] = step.flowRate;
        map[QStringLiteral("steamTemp")] = step.steamTemp;
        map[QStringLiteral("avgPressure")] = step.avgPressure;
        map[QStringLiteral("pressureCV")] = step.pressureCV;
        map[QStringLiteral("oscillationRate")] = step.oscillationRate;
        map[QStringLiteral("peakToPeakRange")] = step.peakToPeakRange;
        map[QStringLiteral("pressureSlope")] = step.pressureSlope;
        map[QStringLiteral("estimatedDryness")] = step.estimatedDryness;
        map[QStringLiteral("estimatedDilution")] = step.estimatedDilution;
        map[QStringLiteral("sampleCount")] = step.sampleCount;
        map[QStringLiteral("durationSeconds")] = step.durationSeconds;
        list.append(map);
    }
    return list;
}

void SteamCalibrator::setState(CalibrationState state)
{
    if (m_state == state) return;
    m_state = state;
    emit stateChanged();
}

void SteamCalibrator::setStatusMessage(const QString& msg)
{
    if (m_statusMessage == msg) return;
    m_statusMessage = msg;
    emit statusMessageChanged();
}

// --- Heater power lookup ---

double SteamCalibrator::heaterWattsForModel(int machineModel, int heaterVoltage)
{
    double watts;
    switch (machineModel) {
    case 1: case 2: case 3: case 4: case 5:
        watts = 1500.0;
        break;
    case 6: // XXL
        watts = 2200.0;
        break;
    case 7: // XXXL / Bengle
        watts = 3000.0;
        break;
    default:
        watts = 1500.0;
        break;
    }

    if (heaterVoltage > 0 && heaterVoltage <= 120)
        watts *= 0.80;

    return watts;
}

// --- Steam dryness estimation ---

double SteamCalibrator::estimateDryness(double heaterWatts, double flowMlPerSec, double steamTempC)
{
    if (flowMlPerSec <= 0.01 || heaterWatts <= 0) return 1.0;

    double energyToBoil = SPECIFIC_HEAT_WATER * 80.0;
    double superheat = qMax(0.0, steamTempC - 100.0) * 2.0;
    double totalEnergyPerGram = energyToBoil + LATENT_HEAT_VAPORIZATION + superheat;

    double powerNeeded = flowMlPerSec * totalEnergyPerGram;
    return qMin(1.0, heaterWatts / powerNeeded);
}

double SteamCalibrator::estimateDilution(double dryness, double milkMassG,
                                          double deltaTempC, double pitcherMassG)
{
    double energyMilk = milkMassG * SPECIFIC_HEAT_MILK * deltaTempC;
    double energyPitcher = pitcherMassG * SPECIFIC_HEAT_STEEL * deltaTempC;
    double totalEnergy = energyMilk + energyPitcher;

    double condensateCooling = SPECIFIC_HEAT_WATER * 37.0;
    double energyPerGramSteam = LATENT_HEAT_VAPORIZATION + condensateCooling;

    double effectiveEnergyPerGram = dryness * energyPerGramSteam
                                   + (1.0 - dryness) * condensateCooling;

    double waterAddedG = totalEnergy / effectiveEnergyPerGram;
    return (waterAddedG / milkMassG) * 100.0;
}

// --- Sweep generation ---

QVector<int> SteamCalibrator::generateFlowSweep(int machineModel, int heaterVoltage)
{
    int start, end, step;

    switch (machineModel) {
    case 1: case 2: case 3: case 4:
        start = 40; end = 140; step = 20;
        break;
    case 6: // XXL
        start = 50; end = 175; step = 25;
        break;
    case 7: // XXXL / Bengle
        start = 80; end = 230; step = 30;
        break;
    default:
        start = 40; end = 190; step = 25;
        break;
    }

    if (heaterVoltage > 0 && heaterVoltage <= 120)
        end = qMin(end, start + 4 * step);

    QVector<int> steps;
    for (int flow = start; flow <= end; flow += step)
        steps.append(flow);
    return steps;
}

// --- Recommendation algorithm ---

int SteamCalibrator::findRecommendedFlow(const QVector<CalibrationStepResult>& steps)
{
    if (steps.isEmpty()) return 0;

    // Find the minimum CV across all steps
    double minCV = std::numeric_limits<double>::max();
    for (const auto& s : steps) {
        if (s.sampleCount >= 30 && s.pressureCV < minCV)
            minCV = s.pressureCV;
    }

    if (minCV >= std::numeric_limits<double>::max()) return steps.first().flowRate;

    // Find the highest flow rate whose CV is within CV_MARGIN (20%) of the best
    double cvThreshold = minCV * (1.0 + CV_MARGIN);
    int bestFlow = 0;
    for (const auto& s : steps) {
        if (s.sampleCount >= 30 && s.pressureCV <= cvThreshold && s.flowRate > bestFlow)
            bestFlow = s.flowRate;
    }

    return bestFlow > 0 ? bestFlow : steps.first().flowRate;
}

// --- Calibration workflow ---

void SteamCalibrator::startCalibration()
{
    if (m_state != Idle && m_state != Results) return;

    int model = m_device ? m_device->machineModel() : 0;
    int voltage = m_device ? m_device->heaterVoltage() : 0;
    m_heaterWatts = heaterWattsForModel(model, voltage);

    m_originalFlow = m_settings->steamFlow();
    m_originalTemp = static_cast<int>(m_settings->steamTemperature());
    m_originalKeepHeaterOn = m_settings->keepSteamHeaterOn();

    m_settings->setKeepSteamHeaterOn(true);

    m_calibrationResult = CalibrationResult();
    m_calibrationResult.machineModel = model;
    m_calibrationResult.heaterVoltage = voltage;
    m_calibrationResult.steamTemp = m_originalTemp;

    m_flowSteps = generateFlowSweep(model, voltage);
    m_currentStep = 0;
    m_heaterReady = true;
    emit heaterReadyChanged();

    setState(Instructions);
    setStatusMessage(QStringLiteral("Steam into air at %1 flow rates. Each step auto-stops after ~20 seconds.")
                         .arg(m_flowSteps.size()));
    emit stepChanged();
}

void SteamCalibrator::cancelCalibration()
{
    if (m_state == Idle) return;

    m_settings->setSteamFlow(m_originalFlow);
    m_settings->setSteamTemperature(m_originalTemp);
    m_settings->setKeepSteamHeaterOn(m_originalKeepHeaterOn);

    setState(Idle);
    setStatusMessage(QString());
    emit stepChanged();
}

void SteamCalibrator::advanceToNextStep()
{
    if (m_currentStep >= m_flowSteps.size()) {
        finishCalibration();
        return;
    }

    int flow = m_flowSteps[m_currentStep];
    m_settings->setSteamFlow(flow);
    emit settingsApplied();

    m_heaterReady = false;
    emit heaterReadyChanged();

    setState(WaitingToStart);
    setStatusMessage(QStringLiteral("Step %1 of %2: Flow %3 mL/s\nWaiting for heater...")
                         .arg(m_currentStep + 1)
                         .arg(m_flowSteps.size())
                         .arg(flow / 100.0, 0, 'f', 2));
    emit stepChanged();
}

void SteamCalibrator::onSteamStarted()
{
    if (m_state != WaitingToStart) return;

    m_steamingElapsed = 0;
    m_hasEnoughData = false;
    m_autoStopRequested = false;

    setState(Steaming);
    setStatusMessage(QStringLiteral("Steaming at %1 mL/s...")
                         .arg(currentFlowRate() / 100.0, 0, 'f', 2));
    emit steamingElapsedChanged();
    emit hasEnoughDataChanged();
}

void SteamCalibrator::onSteamSample(double elapsed)
{
    if (m_state != Steaming) return;

    m_steamingElapsed = elapsed;
    emit steamingElapsedChanged();

    double usableTime = elapsed - TRIM_SECONDS;
    if (usableTime >= TARGET_DURATION && !m_hasEnoughData) {
        m_hasEnoughData = true;
        emit hasEnoughDataChanged();
    }

    // Auto-stop after enough data + small buffer
    if (usableTime >= TARGET_DURATION + 2.0 && !m_autoStopRequested) {
        m_autoStopRequested = true;
        if (m_device && m_device->isConnected()) {
            qDebug() << "SteamCalibrator: auto-stopping steam after" << elapsed << "s";
            m_device->requestState(DE1::State::Idle);
        }
    }
}

void SteamCalibrator::updateHeaterTemp(double steamTempC)
{
    if (m_currentHeaterTemp != steamTempC) {
        m_currentHeaterTemp = steamTempC;
        emit currentHeaterTempChanged();
    }

    if (m_state == WaitingToStart) {
        int targetTemp = m_originalTemp;
        bool ready = (steamTempC >= targetTemp - 5);
        if (ready != m_heaterReady) {
            m_heaterReady = ready;
            emit heaterReadyChanged();
            if (ready) {
                setStatusMessage(QStringLiteral("Step %1 of %2: Flow %3 mL/s — Ready (%4°C). Start steaming.")
                                     .arg(m_currentStep + 1)
                                     .arg(m_flowSteps.size())
                                     .arg(currentFlowRate() / 100.0, 0, 'f', 2)
                                     .arg(static_cast<int>(steamTempC)));
            }
        }
    }
}

void SteamCalibrator::onSteamEnded(const SteamDataModel* model)
{
    if (m_state != Steaming) return;

    setState(Analyzing);

    auto result = analyzeStability(model->pressureData(), currentFlowRate(),
                                   m_originalTemp, m_heaterWatts, TRIM_SECONDS);

    if (result.durationSeconds < MIN_DURATION || result.sampleCount < MIN_SAMPLES) {
        setStatusMessage(QStringLiteral("Too short (%1s). Steam for at least 15 seconds. Try again.")
                             .arg(result.durationSeconds, 0, 'f', 0));
        setState(WaitingToStart);
        return;
    }

    m_calibrationResult.steps.append(result);

    CalibrationStepRawData raw;
    raw.pressure = model->pressureData();
    raw.flow = model->flowData();
    raw.temperature = model->temperatureData();
    m_calibrationResult.rawData.append(raw);

    emit stepAnalyzed();

    m_currentStep++;

    if (m_currentStep >= m_flowSteps.size()) {
        finishCalibration();
    } else {
        setStatusMessage(QStringLiteral("Step complete — CV: %1. Wait for heater...")
                             .arg(result.pressureCV, 0, 'f', 3));
        advanceToNextStep();
    }
}

void SteamCalibrator::finishCalibration()
{
    int recFlow = findRecommendedFlow(m_calibrationResult.steps);
    m_calibrationResult.recommendedFlow = recFlow;

    // Find the step for the recommended flow to get its dilution/CV
    for (const auto& step : m_calibrationResult.steps) {
        if (step.flowRate == recFlow) {
            m_calibrationResult.recommendedDilution = step.estimatedDilution;
            m_calibrationResult.bestCV = step.pressureCV;
            break;
        }
    }

    m_calibrationResult.timestamp = QDateTime::currentDateTime();

    m_settings->setSteamFlow(m_originalFlow);
    m_settings->setSteamTemperature(m_originalTemp);
    m_settings->setKeepSteamHeaterOn(m_originalKeepHeaterOn);

    saveCalibration();
    saveDetailedLog();

    setState(Results);

    // Find the min CV for context
    double minCV = std::numeric_limits<double>::max();
    for (const auto& s : m_calibrationResult.steps) {
        if (s.pressureCV < minCV) minCV = s.pressureCV;
    }

    setStatusMessage(QStringLiteral("Recommended: %1 mL/s (CV %2, est. %3% dilution)\nBest CV was %4 at the most stable flow rate.")
                         .arg(recFlow / 100.0, 0, 'f', 2)
                         .arg(m_calibrationResult.bestCV, 0, 'f', 3)
                         .arg(m_calibrationResult.recommendedDilution, 0, 'f', 1)
                         .arg(minCV, 0, 'f', 3));

    emit calibrationComplete();
}

void SteamCalibrator::applyRecommendation()
{
    if (!hasCalibration()) return;

    m_settings->setSteamFlow(m_calibrationResult.recommendedFlow);
    m_originalFlow = m_calibrationResult.recommendedFlow;

    setState(Idle);
    setStatusMessage(QString());
}

// --- Stability analysis ---

CalibrationStepResult SteamCalibrator::analyzeStability(
    const QVector<QPointF>& pressureData,
    int flowRate,
    int steamTemp,
    double heaterWatts,
    double trimSeconds)
{
    CalibrationStepResult result;
    result.flowRate = flowRate;
    result.steamTemp = steamTemp;

    // Collect samples after trim period, excluding:
    // - Negative or discontinuous timestamps (timer wrap bug)
    // - Heater exhaustion tail (3+ consecutive samples below 0.3 bar)
    QVector<double> values;
    QVector<double> times;
    double startTime = -1;
    double endTime = 0;
    double prevTime = -1;
    int lowPressureRun = 0;
    constexpr double EXHAUST_THRESHOLD = 0.3;
    constexpr int EXHAUST_COUNT = 3;

    for (const auto& pt : pressureData) {
        double t = pt.x();
        if (t < trimSeconds) continue;
        if (t < 0) break;
        if (prevTime >= 0 && ((t - prevTime) > 5.0 || t < prevTime)) break;
        prevTime = t;

        if (pt.y() < EXHAUST_THRESHOLD) {
            lowPressureRun++;
            if (lowPressureRun >= EXHAUST_COUNT) {
                qsizetype trimCount = qMin(static_cast<qsizetype>(EXHAUST_COUNT - 1), values.size());
                values.resize(values.size() - trimCount);
                times.resize(times.size() - trimCount);
                break;
            }
        } else {
            lowPressureRun = 0;
        }

        if (startTime < 0) startTime = t;
        endTime = t;
        values.append(pt.y());
        times.append(t);
    }

    result.sampleCount = static_cast<int>(values.size());
    if (values.size() < 2) return result;

    result.durationSeconds = endTime - startTime;

    // Mean
    double sum = 0;
    for (double v : values) sum += v;
    double mean = sum / values.size();
    result.avgPressure = mean;

    // Variance, stddev, CV
    double sumSq = 0;
    double minVal = values[0], maxVal = values[0];
    for (double v : values) {
        double diff = v - mean;
        sumSq += diff * diff;
        minVal = qMin(minVal, v);
        maxVal = qMax(maxVal, v);
    }
    double variance = sumSq / values.size();
    double stddev = qSqrt(variance);

    result.pressureCV = (mean > 0.01) ? stddev / mean : 0.0;
    result.peakToPeakRange = maxVal - minVal;

    // Oscillation rate: zero-crossings of detrended signal
    int crossings = 0;
    for (qsizetype i = 1; i < values.size(); i++) {
        double prev = values[i - 1] - mean;
        double curr = values[i] - mean;
        if ((prev >= 0 && curr < 0) || (prev < 0 && curr >= 0))
            crossings++;
    }
    result.oscillationRate = (result.durationSeconds > 0)
                                 ? crossings / result.durationSeconds
                                 : 0.0;

    // Linear regression slope — uses filtered values/times
    double sumT = 0, sumP = 0, sumTP = 0, sumTT = 0;
    qsizetype n = values.size();
    for (qsizetype i = 0; i < n; i++) {
        double t = times[i] - startTime;
        double p = values[i];
        sumT += t;
        sumP += p;
        sumTP += t * p;
        sumTT += t * t;
    }
    double denom = n * sumTT - sumT * sumT;
    result.pressureSlope = (qAbs(denom) > 1e-10)
                               ? (n * sumTP - sumT * sumP) / denom
                               : 0.0;

    // Thermodynamic estimates
    double flowMlPerSec = flowRate / 100.0;
    result.estimatedDryness = estimateDryness(heaterWatts, flowMlPerSec, steamTemp);
    result.estimatedDilution = estimateDilution(result.estimatedDryness);

    return result;
}

// --- Persistence ---

void SteamCalibrator::saveCalibration() const
{
    QSettings settings(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));

    QJsonObject obj;
    obj[QStringLiteral("timestamp")] = m_calibrationResult.timestamp.toString(Qt::ISODate);
    obj[QStringLiteral("machineModel")] = m_calibrationResult.machineModel;
    obj[QStringLiteral("heaterVoltage")] = m_calibrationResult.heaterVoltage;
    obj[QStringLiteral("recommendedFlow")] = m_calibrationResult.recommendedFlow;
    obj[QStringLiteral("steamTemp")] = m_calibrationResult.steamTemp;
    obj[QStringLiteral("recommendedDilution")] = m_calibrationResult.recommendedDilution;
    obj[QStringLiteral("bestCV")] = m_calibrationResult.bestCV;

    QJsonArray stepsArr;
    for (const auto& step : m_calibrationResult.steps) {
        QJsonObject s;
        s[QStringLiteral("flowRate")] = step.flowRate;
        s[QStringLiteral("steamTemp")] = step.steamTemp;
        s[QStringLiteral("avgPressure")] = step.avgPressure;
        s[QStringLiteral("pressureCV")] = step.pressureCV;
        s[QStringLiteral("oscillationRate")] = step.oscillationRate;
        s[QStringLiteral("peakToPeakRange")] = step.peakToPeakRange;
        s[QStringLiteral("pressureSlope")] = step.pressureSlope;
        s[QStringLiteral("estimatedDryness")] = step.estimatedDryness;
        s[QStringLiteral("estimatedDilution")] = step.estimatedDilution;
        s[QStringLiteral("sampleCount")] = step.sampleCount;
        s[QStringLiteral("durationSeconds")] = step.durationSeconds;
        stepsArr.append(s);
    }
    obj[QStringLiteral("steps")] = stepsArr;

    settings.setValue(QStringLiteral("steam/calibration"),
                      QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void SteamCalibrator::loadCalibration()
{
    QSettings settings(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
    QString json = settings.value(QStringLiteral("steam/calibration")).toString();
    if (json.isEmpty()) return;

    QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
    if (obj.isEmpty()) return;

    m_calibrationResult.timestamp = QDateTime::fromString(
        obj[QStringLiteral("timestamp")].toString(), Qt::ISODate);
    m_calibrationResult.machineModel = obj[QStringLiteral("machineModel")].toInt();
    m_calibrationResult.heaterVoltage = obj[QStringLiteral("heaterVoltage")].toInt();
    m_calibrationResult.recommendedFlow = obj[QStringLiteral("recommendedFlow")].toInt();
    m_calibrationResult.steamTemp = obj[QStringLiteral("steamTemp")].toInt();
    m_calibrationResult.recommendedDilution = obj[QStringLiteral("recommendedDilution")].toDouble();
    m_calibrationResult.bestCV = obj[QStringLiteral("bestCV")].toDouble();

    QJsonArray stepsArr = obj[QStringLiteral("steps")].toArray();
    for (const auto& val : stepsArr) {
        QJsonObject s = val.toObject();
        CalibrationStepResult step;
        step.flowRate = s[QStringLiteral("flowRate")].toInt();
        step.steamTemp = s[QStringLiteral("steamTemp")].toInt();
        step.avgPressure = s[QStringLiteral("avgPressure")].toDouble();
        step.pressureCV = s[QStringLiteral("pressureCV")].toDouble();
        step.oscillationRate = s[QStringLiteral("oscillationRate")].toDouble();
        step.peakToPeakRange = s[QStringLiteral("peakToPeakRange")].toDouble();
        step.pressureSlope = s[QStringLiteral("pressureSlope")].toDouble();
        step.estimatedDryness = s[QStringLiteral("estimatedDryness")].toDouble();
        step.estimatedDilution = s[QStringLiteral("estimatedDilution")].toDouble();
        step.sampleCount = s[QStringLiteral("sampleCount")].toInt();
        step.durationSeconds = s[QStringLiteral("durationSeconds")].toDouble();
        m_calibrationResult.steps.append(step);
    }
}

// --- Detailed log ---

QString SteamCalibrator::logFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/steam_calibration_log.json");
}

QString SteamCalibrator::saveDetailedLog() const
{
    QString path = logFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    auto pointsToArray = [](const QVector<QPointF>& points) -> QJsonArray {
        QJsonArray arr;
        for (const auto& p : points) {
            QJsonArray pt;
            pt.append(p.x());
            pt.append(p.y());
            arr.append(pt);
        }
        return arr;
    };

    QJsonObject root;
    root[QStringLiteral("timestamp")] = m_calibrationResult.timestamp.toString(Qt::ISODate);
    root[QStringLiteral("machineModel")] = m_calibrationResult.machineModel;
    root[QStringLiteral("heaterVoltage")] = m_calibrationResult.heaterVoltage;
    root[QStringLiteral("recommendedFlowMlPerSec")] = m_calibrationResult.recommendedFlow / 100.0;
    root[QStringLiteral("steamTemperatureC")] = m_calibrationResult.steamTemp;
    root[QStringLiteral("recommendedDilutionPct")] = m_calibrationResult.recommendedDilution;
    root[QStringLiteral("bestCV")] = m_calibrationResult.bestCV;

    QJsonArray stepsArr;
    for (qsizetype i = 0; i < m_calibrationResult.steps.size(); i++) {
        const auto& step = m_calibrationResult.steps[i];
        QJsonObject s;
        s[QStringLiteral("flowMlPerSec")] = step.flowRate / 100.0;
        s[QStringLiteral("steamTemperatureC")] = step.steamTemp;
        s[QStringLiteral("avgPressureBar")] = step.avgPressure;
        s[QStringLiteral("pressureCV")] = step.pressureCV;
        s[QStringLiteral("oscillationRateHz")] = step.oscillationRate;
        s[QStringLiteral("peakToPeakRangeBar")] = step.peakToPeakRange;
        s[QStringLiteral("pressureSlopeBarPerSec")] = step.pressureSlope;
        s[QStringLiteral("estimatedDryness")] = step.estimatedDryness;
        s[QStringLiteral("estimatedDilutionPct")] = step.estimatedDilution;
        s[QStringLiteral("durationSec")] = step.durationSeconds;
        s[QStringLiteral("sampleCount")] = step.sampleCount;

        if (i < m_calibrationResult.rawData.size()) {
            const auto& raw = m_calibrationResult.rawData[i];
            s[QStringLiteral("pressureData")] = pointsToArray(raw.pressure);
            s[QStringLiteral("flowData")] = pointsToArray(raw.flow);
            s[QStringLiteral("temperatureData")] = pointsToArray(raw.temperature);
        }

        stepsArr.append(s);
    }
    root[QStringLiteral("steps")] = stepsArr;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "Steam calibration log saved to" << path;
    } else {
        qWarning() << "Failed to save steam calibration log:" << file.errorString();
    }

    return path;
}
