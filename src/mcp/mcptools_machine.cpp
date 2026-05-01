#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../machine/steamhealthtracker.h"
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
        info.recommendation = QStringLiteral(
            "Significant steam-flow restriction detected — likely milk residue in the steam tip or scale buildup. "
            "Start with a steam-tip soak in a milk cleaner (Rinza/Cafiza); if pressure doesn't recover, descaling is likely needed.");
    } else if (maxProgress >= monitorThreshold) {
        info.status = QStringLiteral("monitor");
        info.recommendation = QStringLiteral(
            "Steam pressure or temperature is rising — likely milk residue in the steam tip or scale buildup. "
            "Try a steam-tip soak in a milk cleaner first; if the trend persists, consider descaling.");
    } else {
        info.status = QStringLiteral("healthy");
        info.recommendation = QStringLiteral("Steam system is clean — no flow restriction detected");
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
        "Get current machine state: phase, connection status, readiness, heating, water level, "
        "firmware version, and live shot scalars. `phase` is the canonical state field — one of "
        "Disconnected | Sleep | Idle | Heating | Ready | EspressoPreheating | Preinfusion | "
        "Pouring | Ending | Steaming | HotWater | Flushing | Refill | Descaling | Cleaning. "
        "Platform/OS info has moved to app_get_info to keep poll responses small.",
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
                result["waterLevelMl"] = device->waterLevelMl();
                result["waterLevelMm"] = device->waterLevelMm();
                result["firmwareVersion"] = device->firmwareVersion();
                result["isHeadless"] = device->isHeadless();
                result["pressureBar"] = device->pressure();
                result["temperatureC"] = device->temperature();
                result["steamTemperatureC"] = device->steamTemperature();
            }

            // Per MCP 2025-06-18: link the result to the canonical resource so
            // subscribing clients see updates flow through decenza://machine/state.
            QJsonObject link;
            link["uri"] = QStringLiteral("decenza://machine/state");
            link["title"] = QStringLiteral("Machine State");
            link["mimeType"] = "application/json";
            result["_resourceLinks"] = QJsonArray{ link };

            // Steam health — only included when status is "monitor" or "warning"
            if (mainController) {
                auto info = computeSteamHealth(mainController->steamHealthTracker());
                if (info.hasData && info.status != QStringLiteral("healthy")) {
                    QJsonObject sh;
                    sh["sessionCount"] = info.sessionCount;
                    sh["pressureRestrictionProgress0to1"] = info.pressureProgress;
                    sh["temperatureRestrictionProgress0to1"] = info.temperatureProgress;
                    sh["status"] = info.status;
                    sh["recommendation"] = info.recommendation;
                    result["steamHealth"] = sh;
                }
            }

            return result;
        },
        "read");

    // app_get_info — diagnostics block previously embedded in machine_get_state.
    // Split out so tight polling loops (monitoring a shot) don't re-ship ~10
    // unchanging fields per response (#988).
    registry->registerTool(
        "app_get_info",
        "Get app and device info: appVersion, qtVersion, OS, kernel, architecture, "
        "deviceModel, screen size/DPI, devicePixelRatio. Diagnostics-grade — call once "
        "per session, not every poll. machine_get_state used to embed this block.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [](const QJsonObject&) -> QJsonObject {
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
            if (auto* screen = QGuiApplication::primaryScreen()) {
                platform["screenSize"] = QString("%1x%2")
                    .arg(screen->size().width()).arg(screen->size().height());
                platform["screenPhysicalSize"] = QString("%1x%2")
                    .arg(screen->physicalSize().width(), 0, 'f', 1)
                    .arg(screen->physicalSize().height(), 0, 'f', 1);
                platform["screenDpi"] = screen->physicalDotsPerInch();
                platform["devicePixelRatio"] = screen->devicePixelRatio();
            }
            return platform;
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

            QJsonObject link;
            link["uri"] = QStringLiteral("decenza://machine/telemetry");
            link["title"] = QStringLiteral("Machine Telemetry");
            link["mimeType"] = "application/json";
            result["_resourceLinks"] = QJsonArray{ link };

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
        "flow-restriction progress toward warn thresholds, status, and recommendation. "
        "Rising pressure/temperature can indicate milk residue in the steam tip or scale buildup "
        "(the tool cannot distinguish the two — recommendations cover both). "
        "Use this when the user asks about steam health, steam wand cleaning, or descaling.",
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
                result["pressureRestrictionProgress0to1"] = info.pressureProgress;
                result["temperatureRestrictionProgress0to1"] = info.temperatureProgress;
                result["warnThresholdProgress0to1"] = tracker->trendProgressThreshold();
            }

            return result;
        },
        "read");
}
