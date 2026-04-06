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
    if (m_currentStep >= 0 && m_currentStep < m_sweepPlan.size())
        return m_sweepPlan[m_currentStep].flowRate;
    return 0;
}

int SteamCalibrator::currentSteamTemp() const
{
    if (m_currentStep >= 0 && m_currentStep < m_sweepPlan.size())
        return m_sweepPlan[m_currentStep].steamTemp;
    return 0;
}

int SteamCalibrator::recommendedFlow() const { return m_calibrationResult.recommendedFlow; }
int SteamCalibrator::recommendedTemp() const { return m_calibrationResult.recommendedTemp; }
double SteamCalibrator::recommendedDilution() const { return m_calibrationResult.recommendedDilution; }

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
        map[QStringLiteral("stabilityScore")] = step.stabilityScore;
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
    // Base wattage per model
    double watts;
    switch (machineModel) {
    case 1: // DE1
    case 2: // DE1+
    case 3: // DE1PRO
    case 4: // DE1XL
    case 5: // DE1CAFE
        watts = 1500.0;
        break;
    case 6: // DE1XXL
        watts = 2200.0;
        break;
    case 7: // DE1XXXL / Bengle
        watts = 3000.0;
        break;
    default:
        watts = 1500.0;  // Conservative default
        break;
    }

    // Voltage adjustment: power scales with V²/V_rated².
    // Most DE1s are rated for their local voltage, but 110V machines
    // on weak circuits may get less effective power.
    if (heaterVoltage > 0 && heaterVoltage <= 120) {
        // 110V machines get ~80% of rated power vs 220V
        watts *= 0.80;
    }

    return watts;
}

// --- Steam dryness estimation ---

double SteamCalibrator::estimateDryness(double heaterWatts, double flowMlPerSec, double steamTempC)
{
    if (flowMlPerSec <= 0.01 || heaterWatts <= 0) return 1.0;

    // Energy needed per gram of water to become steam:
    // 1. Heat from ~20°C (inlet) to 100°C: specific_heat_water * 80
    // 2. Vaporize at 100°C: latent_heat
    // 3. Superheat from 100°C to steamTempC: specific_heat_steam * (steamTempC - 100)
    //    (steam specific heat ~2.0 J/(g·°C))
    double energyToBoil = SPECIFIC_HEAT_WATER * 80.0;  // ~334 J/g
    double superheat = qMax(0.0, steamTempC - 100.0) * 2.0;  // ~120 J/g at 160°C
    double totalEnergyPerGram = energyToBoil + LATENT_HEAT_VAPORIZATION + superheat;
    // Total: ~334 + 2257 + 120 = ~2711 J/g at 160°C

    // Power needed for full vaporization at this flow rate
    // flowMlPerSec ≈ flowGramsPerSec for water
    double powerNeeded = flowMlPerSec * totalEnergyPerGram;

    // Dryness = fraction of water that is fully vaporized
    double dryness = qMin(1.0, heaterWatts / powerNeeded);
    return dryness;
}

double SteamCalibrator::estimateDilution(double dryness, double milkMassG,
                                          double deltaTempC, double pitcherMassG)
{
    // Energy needed to heat milk and pitcher
    double energyMilk = milkMassG * SPECIFIC_HEAT_MILK * deltaTempC;
    double energyPitcher = pitcherMassG * SPECIFIC_HEAT_STEEL * deltaTempC;
    double totalEnergy = energyMilk + energyPitcher;

    // Energy delivered per gram of steam condensate:
    // Condensation releases latent heat, then the condensate cools from 100°C to final temp (~63°C)
    double condensateCooling = SPECIFIC_HEAT_WATER * 37.0;  // 100°C → 63°C
    double energyPerGramSteam = LATENT_HEAT_VAPORIZATION + condensateCooling;

    // With dry steam, all injected water delivers full energy.
    // With wet steam (dryness < 1), some water arrives as liquid,
    // contributing only sensible heat (100°C → 63°C), not latent heat.
    double effectiveEnergyPerGram = dryness * energyPerGramSteam
                                   + (1.0 - dryness) * condensateCooling;

    double waterAddedG = totalEnergy / effectiveEnergyPerGram;
    double dilutionPct = (waterAddedG / milkMassG) * 100.0;

    return dilutionPct;
}

// --- Sweep generation ---

QVector<int> SteamCalibrator::generateFlowSweep(int machineModel, int heaterVoltage)
{
    int start, end, step;

    switch (machineModel) {
    case 1: case 2: case 3: case 4: // DE1, DE1+, PRO, XL
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

QVector<int> SteamCalibrator::generateTempSweep()
{
    // Test 3 temperatures: lower, mid, higher
    return {150, 160, 170};
}

// --- Calibration workflow ---

void SteamCalibrator::startCalibration()
{
    if (m_state != Idle && m_state != Results) return;

    int model = m_device ? m_device->machineModel() : 0;
    int voltage = m_device ? m_device->heaterVoltage() : 0;
    m_heaterWatts = heaterWattsForModel(model, voltage);

    // Save original settings to restore on cancel
    m_originalFlow = m_settings->steamFlow();
    m_originalTemp = static_cast<int>(m_settings->steamTemperature());

    m_calibrationResult = CalibrationResult();
    m_calibrationResult.machineModel = model;
    m_calibrationResult.heaterVoltage = voltage;

    // Build Phase 1 plan: sweep flow rates at current temperature
    m_phase = FlowSweep;
    m_sweepPlan.clear();
    int temp = m_originalTemp;
    for (int flow : generateFlowSweep(model, voltage))
        m_sweepPlan.append({flow, temp});

    m_currentStep = 0;

    setState(Instructions);
    setStatusMessage(QStringLiteral("Fill a pitcher with water. Phase 1 tests %1 flow rates at %2°C, "
                                    "then Phase 2 refines with different temperatures.")
                         .arg(m_sweepPlan.size()).arg(temp));
    emit stepChanged();
}

void SteamCalibrator::cancelCalibration()
{
    if (m_state == Idle) return;

    m_settings->setSteamFlow(m_originalFlow);
    m_settings->setSteamTemperature(m_originalTemp);

    setState(Idle);
    setStatusMessage(QString());
    emit stepChanged();
}

void SteamCalibrator::advanceToNextStep()
{
    if (m_currentStep >= m_sweepPlan.size()) {
        if (m_phase == FlowSweep) {
            buildPhase2Plan();
            if (m_sweepPlan.isEmpty()) {
                finishCalibration();
                return;
            }
            m_currentStep = 0;
        } else {
            finishCalibration();
            return;
        }
    }

    auto& step = m_sweepPlan[m_currentStep];
    m_settings->setSteamFlow(step.flowRate);
    m_settings->setSteamTemperature(step.steamTemp);

    QString phaseLabel = (m_phase == FlowSweep)
        ? QStringLiteral("Phase 1 — Flow")
        : QStringLiteral("Phase 2 — Temperature");

    setState(WaitingToStart);
    setStatusMessage(QStringLiteral("%1: Step %2 of %3\nFlow: %4 mL/s at %5°C\nStart steaming water now")
                         .arg(phaseLabel)
                         .arg(m_currentStep + 1)
                         .arg(m_sweepPlan.size())
                         .arg(step.flowRate / 100.0, 0, 'f', 2)
                         .arg(step.steamTemp));
    emit stepChanged();
}

void SteamCalibrator::buildPhase2Plan()
{
    m_phase = TempSweep;
    m_sweepPlan.clear();

    // Find the top 2 flow rates from Phase 1 by stability score
    QVector<CalibrationStepResult> sorted = m_calibrationResult.steps;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.stabilityScore > b.stabilityScore;
    });

    QVector<int> bestFlows;
    for (const auto& s : sorted) {
        if (bestFlows.size() >= 2) break;
        if (s.stabilityScore >= GOOD_SCORE_THRESHOLD * 0.7)  // At least 52.5 to be worth testing
            bestFlows.append(s.flowRate);
    }

    if (bestFlows.isEmpty() && !sorted.isEmpty())
        bestFlows.append(sorted.first().flowRate);

    // Test each best flow at temperatures not already tested in Phase 1
    int phase1Temp = m_calibrationResult.steps.isEmpty() ? m_originalTemp
                                                          : m_calibrationResult.steps.first().steamTemp;
    QVector<int> temps = generateTempSweep();
    temps.removeAll(phase1Temp);  // Don't re-test the Phase 1 temperature

    for (int flow : bestFlows) {
        for (int temp : temps)
            m_sweepPlan.append({flow, temp});
    }

    setStatusMessage(QStringLiteral("Phase 2: Testing %1 best flow rate(s) at %2 temperatures. "
                                    "Change water and continue.")
                         .arg(bestFlows.size()).arg(temps.size()));
}

void SteamCalibrator::onSteamStarted()
{
    if (m_state != WaitingToStart) return;

    setState(Steaming);
    setStatusMessage(QStringLiteral("Steaming at %1 mL/s, %2°C — steam for at least 15 seconds")
                         .arg(currentFlowRate() / 100.0, 0, 'f', 2)
                         .arg(currentSteamTemp()));
}

void SteamCalibrator::onSteamEnded(const SteamDataModel* model)
{
    if (m_state != Steaming) return;

    setState(Analyzing);

    auto result = analyzeStability(model->pressureData(), currentFlowRate(),
                                   currentSteamTemp(), m_heaterWatts, TRIM_SECONDS);

    if (result.durationSeconds < MIN_DURATION || result.sampleCount < MIN_SAMPLES) {
        setStatusMessage(QStringLiteral("Too short (%1s). Steam for at least 15 seconds. Try again.")
                             .arg(result.durationSeconds, 0, 'f', 0));
        setState(WaitingToStart);
        return;
    }

    m_calibrationResult.steps.append(result);

    // Capture raw time-series data for detailed logging
    CalibrationStepRawData raw;
    raw.pressure = model->pressureData();
    raw.flow = model->flowData();
    raw.temperature = model->temperatureData();
    m_calibrationResult.rawData.append(raw);

    emit stepAnalyzed();

    m_currentStep++;

    if (m_currentStep >= m_sweepPlan.size()) {
        if (m_phase == FlowSweep) {
            buildPhase2Plan();
            if (m_sweepPlan.isEmpty()) {
                finishCalibration();
                return;
            }
            m_currentStep = 0;
            advanceToNextStep();
        } else {
            finishCalibration();
        }
    } else {
        setStatusMessage(QStringLiteral("Step complete — Stability: %1, Est. dilution: %2%. Change water and continue.")
                             .arg(result.stabilityScore, 0, 'f', 0)
                             .arg(result.estimatedDilution, 0, 'f', 1));
        advanceToNextStep();
    }
}

void SteamCalibrator::finishCalibration()
{
    // Find best combination: lowest dilution among stable steps
    const CalibrationStepResult* best = nullptr;
    for (const auto& step : m_calibrationResult.steps) {
        if (step.stabilityScore < GOOD_SCORE_THRESHOLD * 0.7)
            continue;  // Skip clearly unstable
        if (!best || step.estimatedDilution < best->estimatedDilution)
            best = &step;
    }

    // Fallback: if no step is stable enough, pick the highest stability score
    if (!best) {
        for (const auto& step : m_calibrationResult.steps) {
            if (!best || step.stabilityScore > best->stabilityScore)
                best = &step;
        }
    }

    if (best) {
        m_calibrationResult.recommendedFlow = best->flowRate;
        m_calibrationResult.recommendedTemp = best->steamTemp;
        m_calibrationResult.recommendedDilution = best->estimatedDilution;
    }
    m_calibrationResult.timestamp = QDateTime::currentDateTime();

    // Restore original settings (user applies recommendation explicitly)
    m_settings->setSteamFlow(m_originalFlow);
    m_settings->setSteamTemperature(m_originalTemp);

    saveCalibration();
    saveDetailedLog();

    setState(Results);

    if (best && best->stabilityScore >= GOOD_SCORE_THRESHOLD) {
        setStatusMessage(QStringLiteral("Recommended: %1 mL/s at %2°C (est. %3% dilution)")
                             .arg(best->flowRate / 100.0, 0, 'f', 2)
                             .arg(best->steamTemp)
                             .arg(best->estimatedDilution, 0, 'f', 1));
    } else if (best) {
        setStatusMessage(QStringLiteral("Best found: %1 mL/s at %2°C (est. %3% dilution). "
                                        "Consider checking flow calibration for better results.")
                             .arg(best->flowRate / 100.0, 0, 'f', 2)
                             .arg(best->steamTemp)
                             .arg(best->estimatedDilution, 0, 'f', 1));
    } else {
        setStatusMessage(QStringLiteral("No valid results. Please try again with longer steam sessions."));
    }

    emit calibrationComplete();
}

void SteamCalibrator::applyRecommendation()
{
    if (!hasCalibration()) return;

    m_settings->setSteamFlow(m_calibrationResult.recommendedFlow);
    m_settings->setSteamTemperature(m_calibrationResult.recommendedTemp);
    m_originalFlow = m_calibrationResult.recommendedFlow;
    m_originalTemp = m_calibrationResult.recommendedTemp;

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

    // Collect samples after trim period
    QVector<double> values;
    double startTime = -1;
    double endTime = 0;

    for (const auto& pt : pressureData) {
        if (pt.x() < trimSeconds) continue;
        if (startTime < 0) startTime = pt.x();
        endTime = pt.x();
        values.append(pt.y());
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

    // Linear regression slope
    double sumT = 0, sumP = 0, sumTP = 0, sumTT = 0;
    qsizetype n = values.size();
    qsizetype valIdx = 0;
    for (const auto& pt : pressureData) {
        if (pt.x() < trimSeconds) continue;
        double t = pt.x() - startTime;
        double p = pt.y();
        sumT += t;
        sumP += p;
        sumTP += t * p;
        sumTT += t * t;
        valIdx++;
        if (valIdx >= n) break;
    }
    double denom = n * sumTT - sumT * sumT;
    result.pressureSlope = (qAbs(denom) > 1e-10)
                               ? (n * sumTP - sumT * sumP) / denom
                               : 0.0;

    result.stabilityScore = computeStabilityScore(result);

    // Thermodynamic estimates
    double flowMlPerSec = flowRate / 100.0;
    result.estimatedDryness = estimateDryness(heaterWatts, flowMlPerSec, steamTemp);
    result.estimatedDilution = estimateDilution(result.estimatedDryness);

    return result;
}

double SteamCalibrator::computeStabilityScore(const CalibrationStepResult& step)
{
    constexpr double W_CV = 4.0;
    constexpr double W_OSC = 0.15;
    constexpr double W_RANGE = 0.8;
    constexpr double W_SLOPE = 2.0;

    double penalty = W_CV * step.pressureCV
                   + W_OSC * step.oscillationRate
                   + W_RANGE * step.peakToPeakRange
                   + W_SLOPE * qAbs(step.pressureSlope);

    return qBound(0.0, 100.0 * qMax(0.0, 1.0 - penalty), 100.0);
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
    obj[QStringLiteral("recommendedTemp")] = m_calibrationResult.recommendedTemp;
    obj[QStringLiteral("recommendedDilution")] = m_calibrationResult.recommendedDilution;

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
        s[QStringLiteral("stabilityScore")] = step.stabilityScore;
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
    root[QStringLiteral("recommendedTemperatureC")] = m_calibrationResult.recommendedTemp;
    root[QStringLiteral("recommendedDilutionPct")] = m_calibrationResult.recommendedDilution;

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
        s[QStringLiteral("stabilityScore")] = step.stabilityScore;
        s[QStringLiteral("estimatedDryness")] = step.estimatedDryness;
        s[QStringLiteral("estimatedDilutionPct")] = step.estimatedDilution;
        s[QStringLiteral("durationSec")] = step.durationSeconds;
        s[QStringLiteral("sampleCount")] = step.sampleCount;

        // Raw time-series data
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
    m_calibrationResult.recommendedTemp = obj[QStringLiteral("recommendedTemp")].toInt();
    m_calibrationResult.recommendedDilution = obj[QStringLiteral("recommendedDilution")].toDouble();

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
        step.stabilityScore = s[QStringLiteral("stabilityScore")].toDouble();
        step.estimatedDryness = s[QStringLiteral("estimatedDryness")].toDouble();
        step.estimatedDilution = s[QStringLiteral("estimatedDilution")].toDouble();
        step.sampleCount = s[QStringLiteral("sampleCount")].toInt();
        step.durationSeconds = s[QStringLiteral("durationSeconds")].toDouble();
        m_calibrationResult.steps.append(step);
    }
}
