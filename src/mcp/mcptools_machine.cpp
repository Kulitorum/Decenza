#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../machine/steamhealthtracker.h"
#include "../machine/steamcalibrator.h"
#include "../models/shotdatamodel.h"
#include "../controllers/maincontroller.h"
#include "../controllers/profilemanager.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QSysInfo>
#include <QScreen>
#include <QGuiApplication>
#include "version.h"
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

// Compute steam health progress and status from tracker state
struct SteamHealthInfo {
    bool hasData = false;
    int sessionCount = 0;
    double pressureProgress = 0.0;
    double temperatureProgress = 0.0;
    QString status;
    QString recommendation;
};

static SteamHealthInfo computeSteamHealth(SteamHealthTracker* tracker)
{
    SteamHealthInfo info;
    if (!tracker) return info;

    info.sessionCount = tracker->sessionCount();
    info.hasData = tracker->hasData();

    if (!info.hasData) {
        // Differentiate fresh-install, first-baseline-in-progress, and
        // post-reset states so an AI agent asking "what's my steam health"
        // can explain *why* there's no trend yet. Mirrors the QML
        // settings screen's establishing-baseline messaging.
        const int total = tracker->minSessionsForTrend();
        switch (tracker->baselineState()) {
        case SteamHealthTracker::EstablishingAfterReset:
            info.status = QStringLiteral("establishing_after_reset");
            info.recommendation = QStringLiteral(
                "A significant pressure drop was detected (likely descale or steam-wand clean). "
                "Collecting sessions to calibrate against the freshly-clean baseline. "
                "%1 of %2 sessions logged.")
                .arg(info.sessionCount).arg(total);
            break;
        case SteamHealthTracker::EstablishingInitial:
            info.status = QStringLiteral("establishing_baseline");
            info.recommendation = QStringLiteral(
                "Establishing initial baseline. %1 of %2 sessions logged — "
                "trend detection begins once the window is full.")
                .arg(info.sessionCount).arg(total);
            break;
        case SteamHealthTracker::Empty:
        default:
            info.status = QStringLiteral("insufficient_data");
            info.recommendation = QStringLiteral(
                "No steam sessions recorded yet. At least %1 sessions are needed to establish a baseline.")
                .arg(total);
            break;
        }
        return info;
    }

    double pressureRange = tracker->pressureThreshold() - tracker->baselinePressure();
    info.pressureProgress = pressureRange > 0
        ? qBound(0.0, (tracker->currentPressure() - tracker->baselinePressure()) / pressureRange, 1.0)
        : 0.0;
    double tempRange = tracker->temperatureThreshold() - tracker->baselineTemperature();
    info.temperatureProgress = tempRange > 0
        ? qBound(0.0, (tracker->currentTemperature() - tracker->baselineTemperature()) / tempRange, 1.0)
        : 0.0;

    double warnThreshold = tracker->trendProgressThreshold();
    double monitorThreshold = warnThreshold / 2.0;
    double maxProgress = qMax(info.pressureProgress, info.temperatureProgress);
    if (maxProgress >= warnThreshold) {
        info.status = QStringLiteral("warning");
        info.recommendation = QStringLiteral("Significant scale buildup detected — descaling recommended soon");
    } else if (maxProgress >= monitorThreshold) {
        info.status = QStringLiteral("monitor");
        info.recommendation = QStringLiteral("Scale buildup is progressing — consider descaling in the coming weeks");
    } else {
        info.status = QStringLiteral("healthy");
        info.recommendation = QStringLiteral("Steam system is clean — no scale buildup detected");
    }
    return info;
}

void registerMachineTools(McpToolRegistry* registry, DE1Device* device,
                          MachineState* machineState, MainController* mainController,
                          ProfileManager* profileManager)
{
    // machine_get_state
    registry->registerTool(
        "machine_get_state",
        "Get current machine state: phase, connection status, readiness, heating, water level, firmware version, and platform/OS info (Android SDK version, device model, screen size)",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [device, machineState, profileManager, mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            auto now = QDateTime::currentDateTime();
            result["currentDateTime"] = now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate);
            if (profileManager) {
                result["activeProfile"] = profileManager->currentProfileName();
                // Use profileManager as authoritative source — it checks brew-by-ratio override first
                result["targetWeightG"] = profileManager->targetWeight();
            }
            if (machineState) {
                result["phase"] = machineState->phaseString();
                result["isHeating"] = machineState->isHeating();
                result["isReady"] = machineState->isReady();
                result["isFlowing"] = machineState->isFlowing();
                result["shotTimeSec"] = machineState->shotTime();
                result["targetVolumeMl"] = machineState->targetVolume();
                result["scaleWeightG"] = machineState->scaleWeight();
            }
            if (device) {
                result["connected"] = device->isConnected();
                result["stateString"] = device->stateString();
                result["waterLevelMl"] = device->waterLevelMl();
                result["waterLevelMm"] = device->waterLevelMm();
                result["firmwareVersion"] = device->firmwareVersion();
                result["isHeadless"] = device->isHeadless();
                result["pressureBar"] = device->pressure();
                result["temperatureC"] = device->temperature();
                result["steamTemperatureC"] = device->steamTemperature();
            }

            // Platform / OS info
            QJsonObject platform;
            platform["appVersion"] = QString(VERSION_STRING);
            platform["qtVersion"] = QString(qVersion());
            platform["os"] = QSysInfo::prettyProductName().simplified();
            platform["osType"] = QSysInfo::productType();
            platform["osVersion"] = QSysInfo::productVersion();
            platform["kernelType"] = QSysInfo::kernelType();
            platform["kernelVersion"] = QSysInfo::kernelVersion();
            platform["architecture"] = QSysInfo::currentCpuArchitecture();
            platform["deviceModel"] = QSysInfo::machineHostName();
#ifdef Q_OS_ANDROID
            // Android SDK version (API level)
            platform["androidSdkVersion"] = QJniObject::getStaticField<jint>(
                "android/os/Build$VERSION", "SDK_INT");
            QJniObject model = QJniObject::getStaticObjectField<jstring>(
                "android/os/Build", "MODEL");
            if (model.isValid())
                platform["deviceModel"] = model.toString();
            QJniObject manufacturer = QJniObject::getStaticObjectField<jstring>(
                "android/os/Build", "MANUFACTURER");
            if (manufacturer.isValid())
                platform["manufacturer"] = manufacturer.toString();
#endif
#ifdef Q_OS_IOS
            platform["osType"] = "ios";
#endif
            // Screen info
            if (auto* screen = QGuiApplication::primaryScreen()) {
                platform["screenSize"] = QString("%1x%2")
                    .arg(screen->size().width()).arg(screen->size().height());
                platform["screenPhysicalSize"] = QString("%1x%2")
                    .arg(screen->physicalSize().width(), 0, 'f', 1)
                    .arg(screen->physicalSize().height(), 0, 'f', 1);
                platform["screenDpi"] = screen->physicalDotsPerInch();
                platform["devicePixelRatio"] = screen->devicePixelRatio();
            }
            result["platform"] = platform;

            // Steam health — only included when status is "monitor" or "warning"
            if (mainController) {
                auto info = computeSteamHealth(mainController->steamHealthTracker());
                if (info.hasData && info.status != QStringLiteral("healthy")) {
                    QJsonObject sh;
                    sh["sessionCount"] = info.sessionCount;
                    sh["pressureScaleBuildupProgress0to1"] = info.pressureProgress;
                    sh["temperatureScaleBuildupProgress0to1"] = info.temperatureProgress;
                    sh["status"] = info.status;
                    sh["recommendation"] = info.recommendation;
                    result["steamHealth"] = sh;
                }
            }

            return result;
        },
        "read");

    // machine_get_telemetry
    registry->registerTool(
        "machine_get_telemetry",
        "Get live telemetry: pressure, flow, temperature, weight, goal values. During a shot, also returns time-series data so far.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [device, machineState, mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (device) {
                result["pressureBar"] = device->pressure();
                result["flowMlPerSec"] = device->flow();
                result["temperatureC"] = device->temperature();
                result["mixTemperatureC"] = device->mixTemperature();
                result["steamTemperatureC"] = device->steamTemperature();
                result["goalPressureBar"] = device->goalPressure();
                result["goalFlowMlPerSec"] = device->goalFlow();
                result["goalTemperatureC"] = device->goalTemperature();
            }
            if (machineState) {
                result["scaleWeightG"] = machineState->scaleWeight();
                result["scaleFlowRateMlPerSec"] = machineState->scaleFlowRate();
                result["shotTimeSec"] = machineState->shotTime();
            }

            // Include time-series data during active shot
            if (mainController && machineState && machineState->isFlowing()) {
                auto* model = mainController->shotDataModel();
                if (model) {
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
                    result["pressureData"] = pointsToArray(model->pressureData());
                    result["flowData"] = pointsToArray(model->flowData());
                    result["temperatureData"] = pointsToArray(model->temperatureData());
                    result["weightData"] = pointsToArray(model->weightData());
                }
            }
            return result;
        },
        "read");

    // steam_get_health
    registry->registerTool(
        "steam_get_health",
        "Get detailed steam system health: baseline and current pressure/temperature, "
        "scale buildup progress toward warn thresholds, status, and recommendation. "
        "Use this when the user asks about steam health, descaling, or scale buildup.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            auto* tracker = mainController ? mainController->steamHealthTracker() : nullptr;
            auto info = computeSteamHealth(tracker);

            result["hasData"] = info.hasData;
            result["sessionCount"] = info.sessionCount;
            result["status"] = info.status;
            result["recommendation"] = info.recommendation;

            if (tracker) {
                result["baselinePressureBar"] = tracker->baselinePressure();
                result["currentPressureBar"] = tracker->currentPressure();
                result["pressureThresholdBar"] = tracker->pressureThreshold();
                result["baselineTemperatureC"] = tracker->baselineTemperature();
                result["currentTemperatureC"] = tracker->currentTemperature();
                result["temperatureThresholdC"] = tracker->temperatureThreshold();
            }

            if (info.hasData) {
                result["pressureScaleBuildupProgress0to1"] = info.pressureProgress;
                result["temperatureScaleBuildupProgress0to1"] = info.temperatureProgress;
                result["warnThresholdProgress0to1"] = tracker->trendProgressThreshold();
            }

            return result;
        },
        "read");

    // steam_calibration_status
    registry->registerTool(
        "steam_calibration_status",
        "Get steam calibration results: recommended flow rate, estimated dilution percentage, "
        "and detailed per-step data including pressure CV (coefficient of variation), "
        "oscillation rates, and dryness estimates. "
        "Use this after running a steam calibration to review results, or to check "
        "if a calibration has been performed.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [mainController](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            auto* calibrator = mainController ? mainController->steamCalibrator() : nullptr;
            if (!calibrator) {
                result["hasCalibration"] = false;
                return result;
            }

            const auto& cal = calibrator->calibrationResult();
            result["hasCalibration"] = calibrator->hasCalibration();
            result["state"] = calibrator->state();
            result["logFilePath"] = SteamCalibrator::logFilePath();

            if (!calibrator->hasCalibration()) return result;

            result["timestamp"] = cal.timestamp.toOffsetFromUtc(cal.timestamp.offsetFromUtc())
                                      .toString(Qt::ISODate);
            result["machineModel"] = cal.machineModel;
            result["heaterVoltage"] = cal.heaterVoltage;
            result["recommendedFlowMlPerSec"] = cal.recommendedFlow / 100.0;
            result["steamTemperatureC"] = cal.steamTemp;
            result["recommendedDilutionPct"] = cal.recommendedDilution;
            result["bestCV"] = cal.bestCV;

            QJsonArray steps;
            for (const auto& step : cal.steps) {
                QJsonObject s;
                s["flowMlPerSec"] = step.flowRate / 100.0;
                s["steamTemperatureC"] = step.steamTemp;
                s["avgPressureBar"] = step.avgPressure;
                s["pressureCV"] = step.pressureCV;
                s["oscillationRateHz"] = step.oscillationRate;
                s["peakToPeakRangeBar"] = step.peakToPeakRange;
                s["pressureSlopeBarPerSec"] = step.pressureSlope;
                s["estimatedDryness0to1"] = step.estimatedDryness;
                s["estimatedDilutionPct"] = step.estimatedDilution;
                s["sampleCount"] = step.sampleCount;
                s["durationSec"] = step.durationSeconds;
                steps.append(s);
            }
            result["steps"] = steps;

            return result;
        },
        "read");

    // steam_calibration_log
    registry->registerTool(
        "steam_calibration_log",
        "Get the detailed steam calibration log with full time-series data for every "
        "calibration step. Each step includes raw pressure, flow, and temperature arrays "
        "sampled at ~5Hz. Use this to analyze pressure curves, compare steaming into air "
        "vs. water, or plot stability patterns. Returns the last calibration run's data.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            QString path = SteamCalibrator::logFilePath();

            QFile file(path);
            if (!file.exists()) {
                result["error"] = QStringLiteral("No calibration log found. Run a calibration first.");
                return result;
            }
            if (!file.open(QIODevice::ReadOnly)) {
                result["error"] = QStringLiteral("Failed to read log: ") + file.errorString();
                return result;
            }

            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
            file.close();

            if (err.error != QJsonParseError::NoError) {
                result["error"] = QStringLiteral("Log file parse error: ") + err.errorString();
                return result;
            }

            return doc.object();
        },
        "read");
}
