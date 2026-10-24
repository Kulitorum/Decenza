#include <QRemoteObjectHost>
#include "remote/hostsessionservicesource.h"
#include "shotserver.h"
#include "ble/belkaportaldevice.h"

#include "core/packageidentity.h"

#include "remotesessionserver.h"
#include "remotelastshotsnapshot.h"
#include "layoutsnapshotlimits.h"
#include "remoteshotgraphsnapshot.h"
#include "remotesteamgraphsnapshot.h"
#include "machineuisnapshot.h"
#include "sessionappearance.h"
#include "sessionbackgroundimage.h"
#include "sessionactionrequest.h"
#include "steampitchersnapshot.h"
#include "steamuistate.h"
#include "steamhealthsnapshot.h"
#include "../ai/livesteamcoach.h"
#include "../machine/steamhealthtracker.h"
#include "../core/translationmanager.h"
#include "../core/accessibilitymanager.h"

#include "../ble/de1device.h"
#include "../ble/scaledeviceproxy.h"
#include "../ble/blemanager.h"
#ifndef Q_OS_IOS
#include "../usb/usbscalemanager.h"
#endif
#include "../core/batterymanager.h"
#include "../core/memorymonitor.h"
#include "../core/settings.h"
#include "../core/settings_network.h"
#include "../core/settings_dye.h"
#include "../history/equipmentstorage.h"
#include "beanbaseclient.h"
#include "../core/beanimagekey.h"
#include "../history/shothistorystorage.h"
#include "../core/dbutils.h"
#include "../core/settings_app.h"
#include "../core/settings_hardware.h"
#include "../core/settings_graph.h"
#include "../controllers/maincontroller.h"
#include "../controllers/steampageeditor.h"
#include "../machine/machinestate.h"
#include "../machine/frameexitreason.h"
#include "../models/shotdatamodel.h"
#include "../models/steamdatamodel.h"
#include "../screensaver/screensavervideomanager.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QThread>
#include <QRegularExpression>
#include <QQueue>
#include <QFileInfo>
#include <QTcpSocket>
#include <QVariantMap>
#include <QSqlQuery>
#include <algorithm>
#include <memory>
#include <utility>

namespace {
constexpr qsizetype kMaximumQueuedGraphRequests = 8;
constexpr qsizetype kMaximumGraphEntriesPerCurve = RemoteShotGraphSnapshot::MaximumEntriesPerCurve;
constexpr qsizetype kMaximumGraphPhaseMarkers = RemoteShotGraphSnapshot::MaximumPhaseMarkers;
constexpr int kMaximumConcurrentLastShotJobs = 2;
constexpr qsizetype kMaximumRememberedSessionActionIds = 256;

void rejectWebSocketRequest(QTcpSocket* socket, int status, const QByteArray& body)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;
    QString reason;
    switch (status) {
    case 400: reason = QStringLiteral("Bad Request"); break;
    case 401: reason = QStringLiteral("Unauthorized"); break;
    case 403: reason = QStringLiteral("Forbidden"); break;
    case 405: reason = QStringLiteral("Method Not Allowed"); break;
    case 413: reason = QStringLiteral("Payload Too Large"); break;
    case 503: reason = QStringLiteral("Service Unavailable"); break;
    default: reason = QStringLiteral("Bad Request"); break;
    }
    QByteArray response = QStringLiteral("HTTP/1.1 %1 %2\r\n")
                              .arg(status).arg(reason).toUtf8();
    response += QByteArrayLiteral("Content-Type: application/json\r\nConnection: close\r\nContent-Length: ");
    response += QByteArray::number(body.size());
    response += QByteArrayLiteral("\r\n\r\n");
    response += body;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

} // namespace

void ShotServer::setScaleDeviceProxy(ScaleDeviceProxy* proxy)
{
    m_scaleDeviceProxy = proxy;
}

void ShotServer::setBLEManager(BLEManager* manager)
{
    m_bleManager = manager;
}

void ShotServer::setUsbScaleManager(UsbScaleManager* manager)
{
    m_usbScaleManager = manager;
}

QJsonObject ShotServer::sessionStateSnapshot() const
{
    QJsonObject result;
    if (m_device) {
        result[QStringLiteral("connected")] = m_device->isConnected();
        result[QStringLiteral("state")] = m_device->stateString();
        result[QStringLiteral("substate")] = m_device->subStateString();
    }
    if (m_machineState) {
        result[QStringLiteral("phase")] = m_machineState->phaseString();
        result[QStringLiteral("isFlowing")] = m_machineState->isFlowing();
        result[QStringLiteral("isHeating")] = m_machineState->isHeating();
        result[QStringLiteral("isReady")] = m_machineState->isReady();
    }
    return result;
}

QJsonObject ShotServer::sessionTelemetrySnapshot() const
{
    QJsonObject result;
    if (m_device) {
        result[QStringLiteral("connected")] = m_device->isConnected();
        result[QStringLiteral("pressure")] = m_device->pressure();
        result[QStringLiteral("flow")] = m_device->flow();
        result[QStringLiteral("temperature")] = m_device->temperature();
        result[QStringLiteral("mixTemperature")] = m_device->mixTemperature();
        result[QStringLiteral("steamTemperature")] = m_device->steamTemperature();
        result[QStringLiteral("waterLevel")] = m_device->waterLevel();
        result[QStringLiteral("waterLevelMm")] = m_device->waterLevelMm();
        result[QStringLiteral("waterLevelMl")] = m_device->waterLevelMl();
        result[QStringLiteral("firmwareVersion")] = m_device->firmwareVersion();
        result[QStringLiteral("state")] = m_device->stateString();
        result[QStringLiteral("substate")] = m_device->subStateString();
    }
    if (m_machineState) {
        result[QStringLiteral("phase")] = m_machineState->phaseString();
        result[QStringLiteral("shotTime")] = m_machineState->shotTime();
        result[QStringLiteral("scaleWeight")] = m_machineState->scaleWeight();
        result[QStringLiteral("scaleFlowRate")] = m_machineState->scaleFlowRate();
        result[QStringLiteral("targetWeight")] = m_machineState->targetWeight();
    }
    if (m_batteryManager)
        result[QStringLiteral("batteryPercent")] = m_batteryManager->batteryPercent();
    if (m_settings)
        result[QStringLiteral("waterLevelDisplayUnit")] = m_settings->app()->waterLevelDisplayUnit();
    result[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    return result;
}

QJsonObject ShotServer::sessionSnapshot(quint64 sequence) const
{
    QJsonObject capabilities;
    capabilities[QStringLiteral("subscriptionReadOnly")] = true;
    capabilities[QStringLiteral("graphSnapshot")] = !m_shotDataModel.isNull();
    auto* steamModel = m_mainController ? m_mainController->steamDataModel() : nullptr;
    capabilities[QStringLiteral("steamGraphSnapshot")] = steamModel != nullptr;
    capabilities[QStringLiteral("lastShotSnapshot")] = m_storage != nullptr;
    capabilities[QStringLiteral("translationCatalog")] = !m_translationManager.isNull();
    capabilities[QStringLiteral("layoutSnapshot")] = m_settings && m_settings->network();
    capabilities[QStringLiteral("sessionActions")] = QJsonObject{
        {QStringLiteral("tare"), m_machineState && m_scaleDeviceProxy},
        {QStringLiteral("scan"), !m_bleManager.isNull()},
        {QStringLiteral("portalReconnect"), !m_belkaPortal.isNull()},
        {QStringLiteral("selectPitcher"), m_mainController && m_sessionSteamBrew},
        {QStringLiteral("steamPageControls"), m_mainController && m_sessionSteamBrew && m_machineState}
    };

    QJsonObject result;
    result[QStringLiteral("type")] = QStringLiteral("snapshot");
    result[QStringLiteral("protocolVersion")] = 1;
    result[QStringLiteral("packageId")] = QString::fromLatin1(decenzaPackageId());
    result[QStringLiteral("hostId")] = m_sessionHostId;
    result[QStringLiteral("sequence")] = static_cast<double>(sequence);
    result[QStringLiteral("accessibilityEnabled")] =
        m_accessibilityManager && m_accessibilityManager->enabled();
    result[QStringLiteral("appearance")] = SessionAppearance::toJson(m_sessionAppearance);
    result[QStringLiteral("steamPitchers")] = m_sessionSteamBrew
        ? QJsonValue(m_sessionSteamPitchers)
        : QJsonValue(QJsonValue::Null);
    result[QStringLiteral("equipmentRevision")] = static_cast<double>(m_equipmentRevision);
    if (m_storage)
        result[QStringLiteral("historyRevision")] = static_cast<double>(m_historyRevision);
    if (m_settings && m_settings->network())
        result[QStringLiteral("layoutRevision")] = static_cast<double>(m_layoutRevision);
    result[QStringLiteral("state")] = sessionStateSnapshot();
    result[QStringLiteral("telemetry")] = sessionTelemetrySnapshot();
    result[QStringLiteral("preparation")] = m_mainController && m_mainController->preparation()
        ? QJsonValue(m_mainController->preparation()->snapshot().toJson())
        : QJsonValue(QJsonValue::Null);
    SteamUiState steamUi;
    if (m_settings && m_settings->app()) {
        steamUi.viewMode = m_settings->app()->steamViewMode();
        steamUi.coachVisualEnabled = m_settings->app()->steamCoachVisualEnabled();
        steamUi.coachAudioEnabled = m_settings->app()->steamCoachAudioEnabled();
    }
    if (m_mainController && m_mainController->liveSteamCoach()) {
        auto* coach = m_mainController->liveSteamCoach();
        steamUi.coachCueText = coach->cueText();
        steamUi.coachCueSeverity = coach->cueSeverity();
        steamUi.coachCueActive = coach->cueActive();
        steamUi.durationMilkDerived = coach->durationMilkDerived();
    }
    if (m_accessibilityManager) {
        steamUi.extractionAnnouncementsEnabled = m_accessibilityManager->extractionAnnouncementsEnabled();
        steamUi.extractionAnnouncementInterval = m_accessibilityManager->extractionAnnouncementInterval();
        steamUi.extractionAnnouncementMode = m_accessibilityManager->extractionAnnouncementMode();
    }
    result[QStringLiteral("steamUi")] = steamUi.toJson();
    result[QStringLiteral("steamHealth")] = m_mainController && m_mainController->steamHealthTracker()
        ? m_mainController->steamHealthTracker()->warningSnapshot().toJson()
        : SteamHealthSnapshot::Snapshot{}.toJson();

    const MachineUiSnapshot machineUi = m_hostSessionSource
        ? m_hostSessionSource->machineUiSnapshot()
        : NativeHostSessionServiceSource::captureMachineUi(m_device, m_machineState,
            m_mainController, m_scaleDeviceProxy, m_bleManager, m_usbScaleManager,
            m_settings, m_belkaPortal);
    result[QStringLiteral("machineUi")] = machineUi.toJson();
    result[QStringLiteral("capabilities")] = capabilities;
    if (m_shotDataModel)
        result[QStringLiteral("graphRevision")] = static_cast<double>(m_graphRevision);
    if (steamModel)
        result[QStringLiteral("steamGraphRevision")] = static_cast<double>(steamModel->revision());
    if (m_translationManager)
        result[QStringLiteral("translationRevision")] =
            static_cast<double>(m_translationManager->catalogRevision());
    return result;
}

void ShotServer::handleSessionAction(QTcpSocket* socket, const QString& method,
                                     const QString& target,
                                     const QByteArray& request)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;

    const qsizetype bodyStart = request.indexOf("\r\n\r\n");
    const QByteArray body = bodyStart >= 0 ? request.mid(bodyStart + 4) : QByteArray();
    SessionActionRequest::Result parsed = SessionActionRequest::validate(
        method, target, body, QString::fromLatin1(decenzaPackageId()), m_sessionHostId);

    QJsonObject actionOutcome;
    const auto respond = [this, socket, &parsed, &actionOutcome](int status, bool accepted,
                                                 const QString& error) {
        QJsonObject result{
            {QStringLiteral("type"), QStringLiteral("actionResult")},
            {QStringLiteral("packageId"), QString::fromLatin1(decenzaPackageId())},
            {QStringLiteral("hostId"), m_sessionHostId},
            {QStringLiteral("requestId"), parsed.requestId},
            {QStringLiteral("action"), parsed.actionName},
            {QStringLiteral("accepted"), accepted}
        };
        if (!error.isEmpty())
            result.insert(QStringLiteral("error"), error);
        if (parsed.action == SessionActionRequest::Action::SteamEditor)
            result.insert(QStringLiteral("operation"), parsed.steamEditor.operationName);
        if (accepted) {
            for (auto it = actionOutcome.constBegin(); it != actionOutcome.constEnd(); ++it)
                result.insert(it.key(), it.value());
        }

        QByteArray headers = QByteArrayLiteral("Cache-Control: no-store\r\n");
        if (status == 405)
            headers += QByteArrayLiteral("Allow: POST\r\n");
        sendResponse(socket, status, QStringLiteral("application/json"),
                     QJsonDocument(result).toJson(QJsonDocument::Compact), headers);
    };

    if (!parsed.valid) {
        respond(parsed.httpStatus, false, parsed.error);
        return;
    }

    if (m_recentSessionActionRequestIds.contains(parsed.requestIdKey)) {
        respond(409, false, QStringLiteral("This action request ID was already used"));
        return;
    }

    auto* preparation = m_mainController ? m_mainController->preparation() : nullptr;
    const bool steamPageAction = SessionActionRequest::isSteamPageAction(parsed.action);
    if (SessionActionRequest::isPreparationAction(parsed.action)) {
        if (!preparation || !preparation->available()) {
            respond(503, false, QStringLiteral("Preparation service is unavailable"));
            return;
        }
    } else if (parsed.action == SessionActionRequest::Action::UiPreference) {
        const bool graphPreference = parsed.uiPreference == QStringLiteral("advancedMode")
            || parsed.uiPreference == QStringLiteral("flowMultiplier");
        if (!m_settings || (graphPreference ? !m_settings->graph() : !m_settings->app())) {
            respond(503, false, QStringLiteral("UI preferences are unavailable"));
            return;
        }
        QJsonValue current;
        if (graphPreference) {
            auto* graph = m_settings->graph();
            current = parsed.uiPreference == QStringLiteral("advancedMode")
                ? QJsonValue(graph->advancedMode()) : QJsonValue(graph->flowMultiplier());
        } else {
            auto* app = m_settings->app();
            current = parsed.uiPreference == QStringLiteral("steamViewMode")
                ? QJsonValue(app->steamViewMode())
                : parsed.uiPreference == QStringLiteral("steamCoachVisualEnabled")
                    ? QJsonValue(app->steamCoachVisualEnabled())
                    : QJsonValue(app->steamCoachAudioEnabled());
        }
        if (current != parsed.uiPreferenceExpectedValue) {
            respond(409, false, QStringLiteral("Preference changed; review the latest value"));
            return;
        }
    } else if (parsed.action == SessionActionRequest::Action::SteamGraphTrace) {
        if (!m_settings || !m_settings->graph()) {
            respond(503, false, QStringLiteral("Steam graph preferences are unavailable"));
            return;
        }
        auto* graph = m_settings->graph();
        const bool current = parsed.graphTrace == QStringLiteral("pressure") ? graph->showSteamPressure()
            : parsed.graphTrace == QStringLiteral("flow") ? graph->showSteamFlow()
            : graph->showSteamTemperature();
        if (current != parsed.graphTraceExpectedEnabled) {
            respond(409, false, QStringLiteral("Graph visibility changed; review the latest preference"));
            return;
        }
    } else if (parsed.action == SessionActionRequest::Action::SteamEditor) {
        if (!m_mainController || !m_mainController->steamPageEditor() || !m_machineState) {
            respond(503, false, QStringLiteral("Steam editor is unavailable"));
            return;
        }
        if (parsed.controlContextId != m_machineState->controlContextId()) {
            respond(409, false, QStringLiteral("Machine state changed; refresh before editing steam"));
            return;
        }
    } else if (parsed.action == SessionActionRequest::Action::SelectPitcher || steamPageAction) {
        if (!m_mainController || !m_sessionSteamBrew || !preparation
            || !preparation->available() || (steamPageAction && !m_machineState)) {
            respond(503, false, QStringLiteral("Steam preparation service is unavailable"));
            return;
        }
        if (steamPageAction && parsed.controlContextId != m_machineState->controlContextId()) {
            respond(409, false, QStringLiteral("Machine state changed; refresh before adjusting steam"));
            return;
        }
        if (parsed.action == SessionActionRequest::Action::SelectPitcher
            || parsed.action == SessionActionRequest::Action::SteamPrepare
            || parsed.action == SessionActionRequest::Action::SteamSelectPitcher) {
            const SteamPitcherSnapshot current = SteamPitcherSnapshot::project(
                m_sessionSteamBrew->steamPitcherPresets(),
                m_sessionSteamBrew->selectedSteamPitcher());
            if (parsed.steamPitchersRevision != current.revision()) {
                respond(409, false, QStringLiteral("Steam pitchers changed; refresh before selecting"));
                return;
            }
            if (current.presetAt(parsed.index).isEmpty()) {
                respond(400, false, QStringLiteral("Steam pitcher does not exist"));
                return;
            }
            if (parsed.action == SessionActionRequest::Action::SteamPrepare
                && parsed.index != current.selectedIndex) {
                respond(409, false, QStringLiteral("Selected pitcher changed; refresh before preparing steam"));
                return;
            }
        }
    } else if (parsed.action == SessionActionRequest::Action::Tare) {
        if (!m_machineState || !m_scaleDeviceProxy) {
            respond(503, false, QStringLiteral("Scale tare service is unavailable"));
            return;
        }
        if (!m_scaleDeviceProxy->connected()) {
            respond(409, false, QStringLiteral("No scale is connected"));
            return;
        }
    } else if (parsed.action == SessionActionRequest::Action::PortalReconnect) {
        if (!m_belkaPortal) {
            respond(503, false, QStringLiteral("PORTAL service is unavailable"));
            return;
        }
        if (!m_belkaPortal->owned() || m_belkaPortal->active()
            || m_belkaPortal->machineBusy()
            || m_belkaPortal->state() == QStringLiteral("disconnecting")) {
            respond(409, false, QStringLiteral("PORTAL cannot reconnect in its current state"));
            return;
        }
    } else if (!m_bleManager) {
        respond(503, false, QStringLiteral("Scale scan service is unavailable"));
        return;
    }

    // This bounded window prevents immediate retransmission from dispatching twice.
    // It is deliberately not permanent idempotency: once 256 newer action IDs
    // arrive, the oldest is evicted and a client with an uncertain result must
    // reconcile the session before deciding whether to retry.
    if (m_sessionActionRequestOrder.size() >= kMaximumRememberedSessionActionIds) {
        const QString oldest = m_sessionActionRequestOrder.dequeue();
        m_recentSessionActionRequestIds.remove(oldest);
    }
    m_sessionActionRequestOrder.enqueue(parsed.requestIdKey);
    m_recentSessionActionRequestIds.insert(parsed.requestIdKey);

    if (parsed.action == SessionActionRequest::Action::UiPreference) {
        if (parsed.uiPreference == QStringLiteral("advancedMode")) {
            m_settings->graph()->setAdvancedMode(parsed.uiPreferenceValue.toBool());
        } else if (parsed.uiPreference == QStringLiteral("flowMultiplier")) {
            m_settings->graph()->setFlowMultiplier(parsed.uiPreferenceValue.toInt());
        } else if (parsed.uiPreference == QStringLiteral("steamViewMode")) {
            auto* app = m_settings->app();
            app->setSteamViewMode(parsed.uiPreferenceValue.toString());
        } else if (parsed.uiPreference == QStringLiteral("steamCoachVisualEnabled")) {
            auto* app = m_settings->app();
            app->setSteamCoachVisualEnabled(parsed.uiPreferenceValue.toBool());
        } else {
            auto* app = m_settings->app();
            app->setSteamCoachAudioEnabled(parsed.uiPreferenceValue.toBool());
        }
        respond(200, true, {});
        return;
    }

    if (parsed.action == SessionActionRequest::Action::SteamGraphTrace) {
        auto* graph = m_settings->graph();
        if (parsed.graphTrace == QStringLiteral("pressure"))
            graph->setShowSteamPressure(parsed.graphTraceEnabled);
        else if (parsed.graphTrace == QStringLiteral("flow"))
            graph->setShowSteamFlow(parsed.graphTraceEnabled);
        else
            graph->setShowSteamTemperature(parsed.graphTraceEnabled);
        respond(200, true, {});
        return;
    }

    if (parsed.action == SessionActionRequest::Action::SteamEditor) {
        auto* editor = m_mainController->steamPageEditor();
        bool completed = false;
        bool succeeded = false;
        QString message;
        const auto connection = connect(editor, &SteamPageEditor::operationCompleted, this,
            [&](const QString& requestId, const QString& operation, bool success,
                const QString& detail, const QVariantMap& result, const QString& revision) {
                if (requestId != parsed.requestId || operation != parsed.steamEditor.operationName)
                    return;
                completed = true;
                succeeded = success;
                message = detail;
                actionOutcome.insert(QStringLiteral("editorRevision"), revision);
                actionOutcome.insert(QStringLiteral("result"), QJsonObject::fromVariantMap(result));
            });
        const bool accepted = editor->dispatch(parsed.steamEditor.command,
                                               parsed.expectedRevision, parsed.requestId);
        disconnect(connection);
        if (!completed || accepted != succeeded) {
            respond(500, false, QStringLiteral("Steam edit outcome could not be confirmed"));
            return;
        }
        respond(accepted ? 200 : 409, accepted, message);
        return;
    }

    if (parsed.action == SessionActionRequest::Action::SelectPitcher) {
        // Selection, validation and application run in the host event loop.
        // Milk fallback comes from the single host capture workflow.
        m_mainController->selectSteamPitcher(parsed.index, preparation->sessionMeasuredMilkG());
        respond(200, true, {});
        return;
    }
    if (parsed.action == SessionActionRequest::Action::Tare) {
        m_machineState->tareScale();
        respond(200, true, {});
        return;
    }
    if (parsed.action == SessionActionRequest::Action::Scan) {
        m_bleManager->scanForDevices();
        respond(200, true, {});
        return;
    }
    if (parsed.action == SessionActionRequest::Action::PortalReconnect) {
        const bool accepted = m_belkaPortal->reconnect();
        respond(accepted ? 200 : 409, accepted,
                accepted ? QString() : QStringLiteral("PORTAL reconnect was not dispatched"));
        return;
    }

    const bool navigationAction = parsed.action == SessionActionRequest::Action::SteamStop
        || parsed.action == SessionActionRequest::Action::SteamBack;
    QMetaObject::Connection outcomeConnection;
    if (navigationAction) {
        outcomeConnection = connect(m_mainController, &MainController::steamPageActionSucceeded,
                                    this, [&parsed, &actionOutcome](const QString& action, bool dismissPage) {
            if (action == parsed.actionName)
                actionOutcome.insert(QStringLiteral("dismissPage"), dismissPage);
        });
    }
    bool accepted = false;
    switch (parsed.action) {
    case SessionActionRequest::Action::Espresso:
        accepted = preparation->selectMode(QStringLiteral("espresso"));
        break;
    case SessionActionRequest::Action::Steam:
        accepted = preparation->selectMode(QStringLiteral("steam"));
        break;
    case SessionActionRequest::Action::Hotwater:
        accepted = preparation->selectMode(QStringLiteral("hotwater"));
        break;
    case SessionActionRequest::Action::Flush:
        accepted = preparation->selectMode(QStringLiteral("flush"));
        break;
    case SessionActionRequest::Action::MilkStart:
        accepted = preparation->startMilkAttempt();
        break;
    case SessionActionRequest::Action::MilkCancel:
        accepted = preparation->cancelMilkAttempt();
        break;
    case SessionActionRequest::Action::MilkLock:
        accepted = preparation->setMilkTimeLocked(true);
        break;
    case SessionActionRequest::Action::MilkUnlock:
        accepted = preparation->setMilkTimeLocked(false);
        break;
    case SessionActionRequest::Action::SteamPrepare:
        accepted = m_mainController->prepareSteamPage();
        break;
    case SessionActionRequest::Action::SteamSelectPitcher:
        accepted = m_mainController->selectSteamPagePitcher(parsed.index);
        break;
    case SessionActionRequest::Action::SteamAdjustTime:
        accepted = m_mainController->adjustSteamPageTimeout(parsed.delta);
        break;
    case SessionActionRequest::Action::SteamStop:
        accepted = m_mainController->stopSteamPage(parsed.expectedSoftStopped);
        break;
    case SessionActionRequest::Action::SteamBack:
        accepted = m_mainController->leaveSteamPage();
        break;
    case SessionActionRequest::Action::PortalReconnect:
    case SessionActionRequest::Action::SteamEditor:
    case SessionActionRequest::Action::SteamGraphTrace:
    case SessionActionRequest::Action::UiPreference:
    case SessionActionRequest::Action::SelectPitcher:
    case SessionActionRequest::Action::Tare:
    case SessionActionRequest::Action::Scan:
        break;
    }
    disconnect(outcomeConnection);

    if (accepted && navigationAction && !actionOutcome.contains(QStringLiteral("dismissPage"))) {
        respond(500, false, QStringLiteral("Steam action outcome could not be confirmed"));
        return;
    }

    if (!accepted) {
        respond(409, false, steamPageAction
            ? QStringLiteral("Steam action was not accepted in the current machine state")
            : QStringLiteral("Preparation action was not accepted"));
        return;
    }

    respond(200, true, {});
}

void ShotServer::handleSessionLayout(QTcpSocket* socket, const QString& method)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;

    const auto sendError = [this, socket](int status, const QString& detail) {
        const QByteArray body = QJsonDocument(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("layoutSnapshotError")},
            {QStringLiteral("protocolVersion"), 1},
            {QStringLiteral("packageId"), QString::fromLatin1(decenzaPackageId())},
            {QStringLiteral("hostId"), m_sessionHostId},
            {QStringLiteral("revision"), static_cast<double>(m_layoutRevision)},
            {QStringLiteral("error"), detail}
        }).toJson(QJsonDocument::Compact);
        const QByteArray headers = status == 405
            ? QByteArrayLiteral("Allow: GET\r\nCache-Control: no-store\r\n")
            : QByteArrayLiteral("Cache-Control: no-store\r\n");
        sendResponse(socket, status, QStringLiteral("application/json"), body, headers);
    };

    if (method != QStringLiteral("GET")) {
        sendError(405, QStringLiteral("Use GET"));
        return;
    }
    if (!m_settings || !m_settings->network()) {
        sendError(503, QStringLiteral("Layout settings are unavailable"));
        return;
    }

    const QByteArray configurationBytes =
        m_settings->network()->layoutConfiguration().toUtf8();
    if (configurationBytes.size() > LayoutSnapshotLimits::MaximumResponseBytes) {
        sendError(413, QStringLiteral("Layout snapshot exceeds 256 KiB"));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument configurationDocument =
        QJsonDocument::fromJson(configurationBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !configurationDocument.isObject()) {
        sendError(500, QStringLiteral("Layout configuration is invalid"));
        return;
    }

    const QJsonObject responseObject{
        {QStringLiteral("type"), QStringLiteral("layoutSnapshot")},
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("packageId"), QString::fromLatin1(decenzaPackageId())},
        {QStringLiteral("hostId"), m_sessionHostId},
        {QStringLiteral("revision"), static_cast<double>(m_layoutRevision)},
        {QStringLiteral("configuration"), configurationDocument.object()}
    };
    const QByteArray body = QJsonDocument(responseObject).toJson(QJsonDocument::Compact);
    if (body.size() > LayoutSnapshotLimits::MaximumResponseBytes) {
        sendError(413, QStringLiteral("Layout snapshot exceeds 256 KiB"));
        return;
    }
    sendResponse(socket, 200, QStringLiteral("application/json"), body,
                 QByteArrayLiteral("Cache-Control: no-store\r\n"));
}

void ShotServer::notifySessionHistoryChanged()
{
    constexpr quint64 kMaximumExactJsonInteger = 9007199254740991ULL;
    if (m_historyRevision < kMaximumExactJsonInteger)
        ++m_historyRevision;
}

void ShotServer::handleSessionLastShot(QTcpSocket* socket)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;

    const QString packageId = QString::fromLatin1(decenzaPackageId());
    if (!m_storage || !m_storage->isReady()) {
        const auto result = RemoteLastShotSnapshot::error(
            503, packageId, m_sessionHostId, m_historyRevision,
            QStringLiteral("unavailable"), QStringLiteral("Shot history is unavailable"));
        sendResponse(socket, result.httpStatus, QStringLiteral("application/json"), result.body,
                     QByteArrayLiteral("Cache-Control: no-store\r\n"));
        return;
    }
    if (m_activeLastShotJobs >= kMaximumConcurrentLastShotJobs) {
        const auto result = RemoteLastShotSnapshot::error(
            503, packageId, m_sessionHostId, m_historyRevision,
            QStringLiteral("unavailable"), QStringLiteral("Last-shot snapshot queue is full"));
        sendResponse(socket, result.httpStatus, QStringLiteral("application/json"), result.body,
                     QByteArrayLiteral("Cache-Control: no-store\r\n"));
        return;
    }

    const QString dbPath = m_storage->databasePath();
    const QString capturedHostId = m_sessionHostId;
    const quint64 capturedRevision = m_historyRevision;
    QPointer<ShotServer> safeThis(this);
    QPointer<QTcpSocket> safeSocket(socket);
    ++m_activeLastShotJobs;

    QThread* thread = QThread::create(
        [safeThis, safeSocket, dbPath, capturedHostId, capturedRevision, packageId]() {
            bool querySucceeded = false;
            bool foundShot = false;
            ShotProjection projection;
            const bool dbOpened = withTempDb(dbPath, "shs_session_last", [&](QSqlDatabase& db) {
                QSqlQuery query(db);
                querySucceeded = query.exec(QStringLiteral(
                    "SELECT id FROM shots ORDER BY timestamp DESC LIMIT 1"));
                if (!querySucceeded || !query.next())
                    return;

                foundShot = true;
                const qint64 shotId = query.value(0).toLongLong();
                const ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(
                    db, shotId, nullptr, Q_FUNC_INFO);
                projection = ShotHistoryStorage::convertShotRecord(record);
            });

            RemoteLastShotSnapshot::Result result;
            if (!dbOpened || !querySucceeded) {
                result = RemoteLastShotSnapshot::error(
                    503, packageId, capturedHostId, capturedRevision,
                    QStringLiteral("unavailable"), QStringLiteral("Shot history could not be read"));
            } else if (foundShot && !projection.isValid()) {
                result = RemoteLastShotSnapshot::error(
                    503, packageId, capturedHostId, capturedRevision,
                    QStringLiteral("unavailable"), QStringLiteral("Latest shot could not be loaded"));
            } else {
                result = RemoteLastShotSnapshot::serialize(
                    packageId, capturedHostId, capturedRevision,
                    foundShot ? std::optional<ShotProjection>(std::move(projection)) : std::nullopt);
            }

            if (!safeThis)
                return;
            QMetaObject::invokeMethod(safeThis,
                [safeThis, safeSocket, capturedHostId, capturedRevision, packageId,
                 response = std::move(result)]() mutable {
                    if (!safeThis)
                        return;
                    if (safeThis->m_activeLastShotJobs > 0)
                        --safeThis->m_activeLastShotJobs;
                    if (!safeSocket || safeSocket->state() != QAbstractSocket::ConnectedState)
                        return;

                    if (!safeThis->isRunning())
                        return;
                    if (capturedHostId != safeThis->m_sessionHostId
                        || capturedRevision != safeThis->m_historyRevision) {
                        response = RemoteLastShotSnapshot::error(
                            409, packageId, safeThis->m_sessionHostId, safeThis->m_historyRevision,
                            QStringLiteral("staleRevision"),
                            QStringLiteral("History changed while the snapshot was being read"));
                    }

                    safeThis->sendResponse(safeSocket, response.httpStatus,
                                           QStringLiteral("application/json"), response.body,
                                           QByteArrayLiteral("Cache-Control: no-store\r\n"));
                }, Qt::QueuedConnection);
        });
    thread->setParent(this);
    m_backgroundReadThreads.append(thread);
    connect(thread, &QThread::finished, this, [this, thread] {
        m_backgroundReadThreads.removeAll(thread);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ShotServer::setSessionAppearance(const QVariantMap& appearance)
{
    QVariantMap validated;
    if (!SessionAppearance::validate(appearance, &validated))
        validated.clear();
    if (m_sessionAppearance == validated)
        return;
    m_sessionAppearance = validated;
    if (m_hostSessionSource)
        m_hostSessionSource->setAppearance(validated);
    emit sessionAppearanceChanged();
}

void ShotServer::setAccessibilityManager(AccessibilityManager* manager)
{
    m_accessibilityManager = manager;
}

void ShotServer::setTranslationManager(TranslationManager* manager)
{
    m_translationManager = manager;
}

void ShotServer::handleSessionTranslations(QTcpSocket* socket)
{
    if (!m_translationManager) {
        sendResponse(socket, 503, "application/json",
                     R"({"error":"Translation catalog unavailable"})",
                     "Cache-Control: no-store\r\n");
        return;
    }

    QString error;
    QJsonObject result = m_translationManager->exportTranslationCatalog(&error);
    if (!error.isEmpty()) {
        QJsonObject failure{{QStringLiteral("error"), error}};
        sendResponse(socket, 413, "application/json",
                     QJsonDocument(failure).toJson(QJsonDocument::Compact),
                     "Cache-Control: no-store\r\n");
        return;
    }
    result.insert(QStringLiteral("packageId"), QString::fromLatin1(decenzaPackageId()));
    result.insert(QStringLiteral("hostId"), m_sessionHostId);
    sendResponse(socket, 200, "application/json",
                 QJsonDocument(result).toJson(QJsonDocument::Compact),
                 "Cache-Control: no-store\r\n");
}

void ShotServer::setShotDataModel(ShotDataModel* model)
{
    for (const QMetaObject::Connection& connection : std::as_const(m_graphModelConnections))
        disconnect(connection);
    m_graphModelConnections.clear();
    m_shotDataModel = model;
    m_graphRevision = model ? 1 : 0;
    if (!model)
        return;

    const auto bump = [this]() { notifySessionGraphChanged(); };
    m_graphModelConnections.append(connect(model, &ShotDataModel::flushed, this, bump));
    m_graphModelConnections.append(connect(model, &ShotDataModel::cleared, this, bump));
    m_graphModelConnections.append(connect(model, &ShotDataModel::phaseMarkersChanged, this, bump));
    m_graphModelConnections.append(connect(model, &ShotDataModel::goalCurvesChanged, this, bump));
    m_graphModelConnections.append(connect(model, &ShotDataModel::weightAtStopChanged, this, bump));
    m_graphModelConnections.append(connect(model, &ShotDataModel::finalWeightChanged, this, bump));
    m_graphModelConnections.append(connect(model, &ShotDataModel::weightDataCleared, this, bump));
    m_graphModelConnections.append(connect(model, &ShotDataModel::graphDataCorrected, this, bump));
}

void ShotServer::notifySessionGraphChanged()
{
    // JSON numbers are exact integers only through 2^53-1. Saturating keeps
    // the revision contract monotonic without eventually losing precision.
    constexpr quint64 kMaximumExactJsonInteger = 9007199254740991ULL;
    if (m_graphRevision < kMaximumExactJsonInteger)
        ++m_graphRevision;
}

void ShotServer::handleSessionGraph(QTcpSocket* socket, bool steam)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;
    auto& queue = steam ? m_steamGraphQueue : m_shotGraphQueue;
    for (auto it = queue.sockets.begin(); it != queue.sockets.end();) {
        if (!*it || (*it)->state() != QAbstractSocket::ConnectedState)
            it = queue.sockets.erase(it);
        else
            ++it;
    }
    if (queue.sockets.size() >= kMaximumQueuedGraphRequests) {
        sendResponse(socket, 503, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Graph snapshot queue is full\"}"));
        return;
    }
    queue.sockets.append(socket);
    startNextSessionGraphSnapshot(steam);
}

void ShotServer::startNextSessionGraphSnapshot(bool steam)
{
    auto& queue = steam ? m_steamGraphQueue : m_shotGraphQueue;
    for (auto it = queue.sockets.begin(); it != queue.sockets.end();) {
        if (!*it || (*it)->state() != QAbstractSocket::ConnectedState)
            it = queue.sockets.erase(it);
        else
            ++it;
    }
    if (queue.activeJobId != 0 || queue.sockets.isEmpty() || !isRunning())
        return;

    GraphSerializer serialize = steam ? captureSteamGraphSnapshot() : captureShotGraphSnapshot();
    const auto sockets = std::exchange(queue.sockets, QList<QPointer<QTcpSocket>>());
    if (!serialize) {
        for (const auto& socket : sockets) {
            if (socket && socket->state() == QAbstractSocket::ConnectedState)
                sendResponse(socket, 503, QStringLiteral("application/json"),
                             QByteArrayLiteral("{\"error\":\"Graph data is unavailable\"}"));
        }
        return;
    }

    // Share bounded worker/queue handling between graph types. Only value
    // captures cross to the worker; QObject state and sockets stay here.
    const QString hostId = m_sessionHostId;
    quint64 jobId = ++queue.nextJobId;
    if (jobId == 0)
        jobId = ++queue.nextJobId;
    queue.activeJobId = jobId;
    const auto result = std::make_shared<std::pair<int, QByteArray>>();
    QThread* worker = QThread::create([serializeGraph = std::move(serialize), result]() {
        *result = serializeGraph();
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this,
            [this, steam, hostId, jobId, sockets, result]() {
        auto& completedQueue = steam ? m_steamGraphQueue : m_shotGraphQueue;
        if (completedQueue.activeJobId == jobId)
            completedQueue.activeJobId = 0;
        if (isRunning() && hostId == m_sessionHostId) {
            for (const auto& socket : sockets) {
                if (socket && socket->state() == QAbstractSocket::ConnectedState)
                    sendResponse(socket, result->first, QStringLiteral("application/json"),
                                 result->second, QByteArrayLiteral("Cache-Control: no-store\r\n"));
            }
        }
        startNextSessionGraphSnapshot(steam);
    }, Qt::QueuedConnection);
    worker->start();
}

ShotServer::GraphSerializer ShotServer::captureSteamGraphSnapshot() const
{
    auto* model = m_mainController ? m_mainController->steamDataModel() : nullptr;
    if (!model)
        return {};
    RemoteSteamGraphSnapshot::Snapshot snapshot;
    snapshot.packageId = QString::fromLatin1(decenzaPackageId());
    snapshot.hostId = m_sessionHostId;
    snapshot.revision = model->revision();
    snapshot.rawTime = model->rawTime();
    snapshot.pressure = model->pressureData();
    snapshot.flow = model->flowData();
    snapshot.temperature = model->temperatureData();
    const QVariantList goals = model->flowGoalPoints();
    for (const auto& goal : goals)
        snapshot.flowGoalPoints.append(goal.toPointF());
    return [capturedSnapshot = std::move(snapshot)]() {
        const auto result = RemoteSteamGraphSnapshot::serialize(capturedSnapshot);
        return std::make_pair(result.httpStatus, result.body);
    };
}

ShotServer::GraphSerializer ShotServer::captureShotGraphSnapshot() const
{
    if (!m_shotDataModel)
        return {};
    // Capture all QObject-owned state on the main thread. Sensor vectors are
    // implicitly shared, so assigning them does not copy their point storage;
    // the worker never reads ShotDataModel, MainController, or a socket.
    // Qt 6.12 qtbase/src/corelib/tools/qarraydatapointer.h:35-39.
    const auto points = [](const QVariantList& values) {
        QVector<QPointF> result;
        result.reserve(values.size());
        for (const QVariant& value : values)
            result.append(value.toPointF());
        return result;
    };
    const auto markers = [](const QList<PhaseMarker>& values) {
        QList<RemoteShotGraphSnapshot::PhaseMarker> result;
        result.reserve(values.size());
        for (const PhaseMarker& marker : values) {
            result.append({marker.time, marker.label, marker.frameNumber,
                           marker.isFlowMode, marker.transitionReason,
                           FrameExit::markerSuffix(marker.transitionReason)});
        }
        return result;
    };

    RemoteShotGraphSnapshot::Input input;
    input.hostId = m_sessionHostId;
    input.revision = m_graphRevision;
    input.rawTime = m_shotDataModel->rawTime();
    input.weightAtStop = m_shotDataModel->weightAtStop();
    input.finalWeight = m_shotDataModel->finalWeight();
    const auto& modelMarkers = m_shotDataModel->phaseMarkersList();
    const bool tooManyMarkers = modelMarkers.size() > kMaximumGraphPhaseMarkers;
    if (tooManyMarkers)
        input.phaseMarkers.resize(kMaximumGraphPhaseMarkers + 1);
    else
        input.phaseMarkers = markers(modelMarkers);
    input.pressure = m_shotDataModel->pressureData();
    input.flow = m_shotDataModel->flowData();
    input.temperature = m_shotDataModel->temperatureData();
    input.temperatureMix = m_shotDataModel->temperatureMixData();
    input.weight = m_shotDataModel->weightData();
    input.weightFlowRate = m_shotDataModel->weightFlowRateData();
    input.resistance = m_shotDataModel->resistanceData();
    input.conductance = m_shotDataModel->conductanceData();
    input.darcyResistance = m_shotDataModel->darcyResistanceData();
    const bool oversizedSensorCurve = input.pressure.size() > kMaximumGraphEntriesPerCurve
        || input.flow.size() > kMaximumGraphEntriesPerCurve
        || input.temperature.size() > kMaximumGraphEntriesPerCurve
        || input.temperatureMix.size() > kMaximumGraphEntriesPerCurve
        || input.weight.size() > kMaximumGraphEntriesPerCurve
        || input.weightFlowRate.size() > kMaximumGraphEntriesPerCurve
        || input.resistance.size() > kMaximumGraphEntriesPerCurve
        || input.conductance.size() > kMaximumGraphEntriesPerCurve
        || input.darcyResistance.size() > kMaximumGraphEntriesPerCurve;
    // Avoid materializing QVariant point lists when a cheaper bound has
    // already determined the structured 413 response.
    if (!tooManyMarkers && !oversizedSensorCurve) {
        input.pressureGoal = points(m_shotDataModel->pressureGoalPointsVariant());
        input.flowGoal = points(m_shotDataModel->flowGoalPointsVariant());
        input.temperatureGoal = points(m_shotDataModel->temperatureGoalPointsVariant());
        input.temperatureMixGoal = points(m_shotDataModel->temperatureMixGoalPointsVariant());
    }

    if (m_mainController && m_mainController->profileManager()) {
        input.hasProfileTargetWeight = true;
        input.profileTargetWeight = m_mainController->profileManager()->targetWeight();
    }
    if (m_machineState) {
        input.hasMachineTargetWeight = true;
        input.machineTargetWeight = m_machineState->targetWeight();
    }

    return [capturedInput = std::move(input)]() {
        const auto result = RemoteShotGraphSnapshot::serialize(capturedInput);
        return std::make_pair(result.httpStatus, result.body);
    };
}

bool ShotServer::tryUpgradeSessionWebSocket(QTcpSocket* socket)
{
    if (!socket || !m_remoteSessionServer || socket->bytesAvailable() <= 0)
        return false;

    // Only inspect a brand-new request. During normal HTTP parsing the socket
    // may contain upload/body bytes that happen to begin with the upgrade
    // prefix; those belong to the already accepted request and must stay with
    // its parser.
    const auto pendingIt = m_pendingRequests.constFind(socket);
    if (pendingIt == m_pendingRequests.cend()
        || !pendingIt->headerData.isEmpty() || pendingIt->headerEnd >= 0) {
        return false;
    }

    const qint64 bytesAvailable = socket->bytesAvailable();
    const qint64 peekLimit = std::min<qint64>(bytesAvailable, MAX_HEADER_SIZE + 1);
    const QByteArray available = socket->peek(peekLimit);

    auto mutablePendingIt = m_pendingRequests.find(socket);
    if (mutablePendingIt != m_pendingRequests.end())
        mutablePendingIt->lastActivity.start();

    const auto checkSessionRequest = [this](const QByteArray& request) {
        return checkSession(request);
    };
    const RemoteSessionServer::UpgradeInspection inspection =
        RemoteSessionServer::inspectUpgradeBuffer(
            available, bytesAvailable, MAX_HEADER_SIZE, isSecurityEnabled(),
            isSecurityEnabled(), checkSessionRequest,
            m_remoteSessionServer->remoteObjectPackageId(),
            m_remoteSessionServer->remoteObjectHostId());
    using Disposition = RemoteSessionServer::UpgradeDisposition;
    if (inspection.disposition == Disposition::NotTarget)
        return false;
    if (inspection.disposition == Disposition::NeedMore)
        return true;
    if (inspection.disposition == Disposition::Rejected) {
        rejectWebSocketRequest(socket, inspection.statusCode, inspection.responseBody);
        return true;
    }

    // The QWebSocketServer only sees bytes still present in the socket, so do
    // not pass headerBlock to it or consume anything here. It owns handshake
    // parsing, frame parsing, and WebSocket close semantics after this point.
    // Qt 6.12 qtwebsockets/src/websockets/qwebsocketserver_p.cpp:544-561.
    disconnect(socket, &QTcpSocket::readyRead, this, &ShotServer::onReadyRead);
    if (QTimer* timer = m_keepAliveTimers.take(socket))
        timer->stop();
    cleanupPendingRequest(socket);
    m_pendingRequests.remove(socket);
    m_remoteSessionServer->adoptConnection(socket);
    return true;
}


void ShotServer::handleSessionBeanImage(QTcpSocket* socket, const QString& method, const QString& path)
{
    if (method != QLatin1String("GET")) {
        sendResponse(socket, 405, "application/json", R"({"error":"Use GET"})", "Allow: GET\r\n");
        return;
    }
    QString imageKey;
    if (!BeanImageKey::decodeSessionImagePath(path, &imageKey)) {
        sendResponse(socket, 400, "application/json", R"({"error":"Invalid image key"})");
        return;
    }
    auto* beans = m_mainController ? m_mainController->beanbase() : nullptr;
    const QString filePath = beans ? beans->bagImagePath(imageKey) : QString();
    if (filePath.isEmpty()) {
        sendResponse(socket, 404, "application/json", R"({"error":"No cached image"})");
        return;
    }
    sendFile(socket, filePath, filePath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)
        ? QStringLiteral("image/png") : QStringLiteral("image/jpeg"));
}

void ShotServer::handleSessionBackgroundImage(QTcpSocket* socket, const QString& method,
                                               const QString& path)
{
    static const QRegularExpression route(
        QStringLiteral("^/api/session/(background-image|background-preview)/([0-9a-f]{32}|[0-9a-f]{64})$"));
    const QRegularExpressionMatch match = route.match(path);
    if (method != QStringLiteral("GET")) {
        sendResponse(socket, 405, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Use GET\"}"),
                     QByteArrayLiteral("Allow: GET\r\nCache-Control: no-store\r\n"));
        return;
    }
    if (!match.hasMatch()) {
        sendResponse(socket, 404, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Background image not found\"}"),
                     QByteArrayLiteral("Cache-Control: no-store\r\n"));
        return;
    }
    const bool preview = match.captured(1) == QLatin1String("background-preview");
    if ((!preview && match.captured(2).size() != 32)
        || (preview && match.captured(2).size() != 64)) {
        sendResponse(socket, 404, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Background image not found\"}"),
                     QByteArrayLiteral("Cache-Control: no-store\r\n"));
        return;
    }
    if (!preview && match.captured(2) != m_sessionBackgroundImageRevision) {
        sendResponse(socket, 409, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Background image revision is stale\"}"),
                     QByteArrayLiteral("Cache-Control: no-store\r\n"));
        return;
    }
    if (!m_settings || !m_screensaverManager
        || (!preview && m_settings->theme()->backgroundSource() != QStringLiteral("image"))) {
        sendResponse(socket, 404, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Background image not found\"}"),
                     QByteArrayLiteral("Cache-Control: no-store\r\n"));
        return;
    }
    if (m_activeBackgroundImageJobs >= 2) {
        sendResponse(socket, 503, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Background image service is busy\"}"),
                     QByteArrayLiteral("Cache-Control: no-store\r\n"));
        return;
    }

    // Capture QObject-owned settings on the GUI thread; the worker only reads
    // the selected path and media-root strings and never touches Settings/QML.
    const QString selectedPath = preview
        ? m_screensaverManager->resolveBackgroundImageToken(match.captured(2))
        : m_settings->theme()->backgroundImagePath();
    if (selectedPath.isEmpty()) {
        sendResponse(socket, 404, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Background image not found\"}"),
                     QByteArrayLiteral("Cache-Control: no-store\r\n"));
        return;
    }
    const QString allowedRoot = QFileInfo(m_screensaverManager->personalMediaDirectory()).absolutePath();
    const QString requestedRevision = preview ? QString() : match.captured(2);
    QPointer<ShotServer> safeThis(this);
    QPointer<QTcpSocket> safeSocket(socket);
    ++m_activeBackgroundImageJobs;
    QThread* thread = QThread::create(
        [safeThis, safeSocket, selectedPath, allowedRoot, requestedRevision, preview]() {
            SessionBackgroundImage::Result result;
            QString error;
            const bool ok = SessionBackgroundImage::load(selectedPath, allowedRoot, &result, &error);
            QMetaObject::invokeMethod(safeThis,
                [safeThis, safeSocket, requestedRevision, preview, ok,
                 imageResult = std::move(result), error]() mutable {
                    if (!safeThis)
                        return;
                    if (safeThis->m_activeBackgroundImageJobs > 0)
                        --safeThis->m_activeBackgroundImageJobs;
                    if (!safeSocket || safeSocket->state() != QAbstractSocket::ConnectedState)
                        return;
                    if (!preview && requestedRevision != safeThis->m_sessionBackgroundImageRevision) {
                        safeThis->sendResponse(safeSocket, 409, QStringLiteral("application/json"),
                            QByteArrayLiteral("{\"error\":\"Background image revision is stale\"}"),
                            QByteArrayLiteral("Cache-Control: no-store\r\n"));
                    } else if (!ok) {
                        safeThis->sendResponse(safeSocket, 404, QStringLiteral("application/json"),
                            QJsonDocument(QJsonObject{{QStringLiteral("error"), error}})
                                .toJson(QJsonDocument::Compact),
                            QByteArrayLiteral("Cache-Control: no-store\r\n"));
                    } else {
                        safeThis->sendResponse(safeSocket, 200,
                            imageResult.contentType, imageResult.bytes,
                            QByteArrayLiteral("Cache-Control: no-store\r\n"));
                    }
                }, Qt::QueuedConnection);
        });
    thread->setParent(this);
    m_backgroundReadThreads.append(thread);
    connect(thread, &QThread::finished, this, [this, thread] {
        m_backgroundReadThreads.removeAll(thread);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ShotServer::setRemoteObjectHost(QRemoteObjectHost* host)
{
    m_remoteObjectHost = host;
    if (m_remoteSessionServer) {
        m_remoteSessionServer->setRemoteObjectHandler(
            QString::fromLatin1(decenzaPackageId()), m_sessionHostId,
            [guard = QPointer<QRemoteObjectHost>(host)](QIODevice* device) {
                if (guard) guard->addHostSideConnection(device);
            });
    }
}

void ShotServer::setHostSessionSource(NativeHostSessionServiceSource* source)
{
    m_hostSessionSource = source;
    if (source) {
        if (!m_sessionHostId.isEmpty())
            source->setIdentity(QString::fromLatin1(decenzaPackageId()), m_sessionHostId);
        source->setAppearance(m_sessionAppearance);
        connect(this, &ShotServer::runningChanged, source, [this, source] {
            if (!m_sessionHostId.isEmpty())
                source->setIdentity(QString::fromLatin1(decenzaPackageId()), m_sessionHostId);
        });
    }
}
