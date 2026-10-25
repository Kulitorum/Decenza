// Remote Web Access: connections RemoteAccess admitted through the Tailscale
// Funnel and handed over. They run through the normal request loop, with every
// request re-checked against the admission and the remote allowlist.

#include "shotserver.h"
#include "remotewebroute.h"
#include "core/diagnosticlogging.h"

#include <QMetaObject>
#include <QPointer>

QString ShotServer::createRemoteWebAdmission(const QString& userAgent)
{
    if (!isRunning())
        return QString();
    return createSession(m_remoteWebSessions, kRemoteWebSessionsKey, userAgent,
                         RemoteWebRoute::kAdmissionLifetimeDays);
}

bool ShotServer::remoteWebAdmissionValid(const QString& admissionId) const
{
    return sessionValid(m_remoteWebSessions, admissionId);
}

void ShotServer::adoptRemoteWebConnection(QTcpSocket* socket)
{
    if (!socket)
        return;
    if (!isRunning() || m_remoteWebClients.size() >= MAX_REMOTE_WEB_CONNECTIONS) {
        if (isRunning())
            DIAG_WARN(NETWORK, "ShotServer") << "Remote Web Access connection limit reached ("
                                             << MAX_REMOTE_WEB_CONNECTIONS << ") — refused one";
        socket->close();
        socket->deleteLater();
        return;
    }
    socket->setParent(this);
    m_clients.insert(socket);
    m_remoteWebClients.insert(socket);
    connect(socket, &QTcpSocket::readyRead, this, &ShotServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &ShotServer::onDisconnected);
    m_pendingRequests[socket].lastActivity.start();
    // The request that got it admitted is already waiting; readyRead will not
    // fire again for bytes that arrived before the connect above.
    if (socket->bytesAvailable() > 0)
        QMetaObject::invokeMethod(socket, [socket] { emit socket->readyRead(); }, Qt::QueuedConnection);
}

void ShotServer::endRemoteWebSessions()
{
    if (m_remoteWebSessions.isEmpty() && m_remoteWebClients.isEmpty())
        return;
    m_remoteWebSessions.clear();
    saveSessions(m_remoteWebSessions, kRemoteWebSessionsKey);
    // close() reaches onDisconnected, which retires each one (or hands it back
    // to the session server's own cleanup for an upgraded WebSocket).
    const auto clients = m_remoteWebClients;
    for (QTcpSocket* socket : clients)
        socket->close();
    DIAG_INFO(NETWORK, "ShotServer") << "Remote Web Access sessions ended:" << clients.size()
                                     << "connection(s) closed";
}

ShotServer::RemoteWebVerdict ShotServer::remoteWebVerdict(const QByteArray& headerBlock) const
{
    if (!remoteWebAdmissionValid(RemoteWebRoute::admissionId(headerBlock)))
        return RemoteWebVerdict::Drop;
    const qsizetype lineEnd = headerBlock.indexOf("\r\n");
    const QList<QByteArray> parts = headerBlock.left(lineEnd < 0 ? headerBlock.size() : lineEnd).split(' ');
    const QString path = parts.size() > 1 ? QString::fromLatin1(parts[1]) : QString();
    return RemoteWebRoute::isAllowedPath(path) ? RemoteWebVerdict::Allow : RemoteWebVerdict::NotFound;
}

bool ShotServer::holdRemoteWebRequest(QTcpSocket* socket)
{
    const auto pendingIt = m_pendingRequests.constFind(socket);
    if (pendingIt != m_pendingRequests.cend()
        && (!pendingIt->headerData.isEmpty() || pendingIt->headerEnd >= 0))
        return false;  // mid-request: checked when this request began
    const QByteArray available = socket->peek(MAX_HEADER_SIZE + 1);
    const qsizetype headerEnd = available.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (available.size() <= MAX_HEADER_SIZE)
            return true;  // wait for the rest of the headers, unread
        refuseRemoteWebRequest(socket, RemoteWebVerdict::Drop, available);
        return true;
    }
    const RemoteWebVerdict verdict = remoteWebVerdict(available.left(headerEnd));
    if (verdict == RemoteWebVerdict::Allow)
        return false;
    refuseRemoteWebRequest(socket, verdict, available.left(headerEnd));
    return true;
}

void ShotServer::refuseRemoteWebRequest(QTcpSocket* socket, RemoteWebVerdict verdict,
                                        const QByteArray& headerBlock)
{
    if (verdict == RemoteWebVerdict::NotFound) {
        // Admitted, but not something the remote route serves. Named in the log:
        // a web-interface call missing from the allowlist shows up here first.
        const qsizetype lineEnd = headerBlock.indexOf("\r\n");
        const QList<QByteArray> parts = headerBlock.left(lineEnd).split(' ');
        DIAG_WARN(NETWORK, "ShotServer") << "Remote Web Access does not serve"
                                         << RemoteWebRoute::pathOnly(QString::fromLatin1(parts.value(1)));
        socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        socket->flush();
    } else {
        // No valid admission (rotated, switched off, or never held): say nothing,
        // as RemoteAccess does to a stranger.
        DIAG_DEBUG(NETWORK, "ShotServer") << "Remote Web Access request without a valid admission";
    }
    socket->close();
}
