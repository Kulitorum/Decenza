#include <QRemoteObjectHost>
#include "remote/hostsessionservicesource.h"
#include "shotserver.h"

#include "core/packageidentity.h"

#include "remotesessionserver.h"
#include "shotservertls.h"
#include "sessionappearance.h"
#include "sessionbackgroundimage.h"
#include "../core/translationmanager.h"

#include "../ble/de1device.h"
#include "../core/batterymanager.h"
#include "../core/settings.h"
#include "beanbaseclient.h"
#include "../core/beanimagekey.h"
#include "../core/settings_app.h"
#include "../controllers/maincontroller.h"
#include "../machine/machinestate.h"
#include "../screensaver/screensavervideomanager.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QThread>
#include <QRegularExpression>
#include <QFileInfo>
#include <QTcpSocket>
#include <QVariantMap>
#include <algorithm>
#include <utility>

namespace {

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

void ShotServer::setTranslationManager(TranslationManager* manager)
{
    m_translationManager = manager;
}

bool ShotServer::tryUpgradeSessionWebSocket(QTcpSocket* socket)
{
    if (!socket || !m_remoteSessionServer || socket->bytesAvailable() <= 0)
        return false;
    // A plaintext /mcp connection never upgrades; handleRequest() refuses the request.
    if (socket->property(ShotServerTls::kPlaintextMcpProperty).toBool())
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
            available, bytesAvailable, MAX_HEADER_SIZE, true,
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
