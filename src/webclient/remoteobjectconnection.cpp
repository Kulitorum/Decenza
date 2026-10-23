#include "remoteobjectconnection.h"
#include "core/networklogging.h"
#include "remote/qtrowebsocketdevice.h"
#include <QRemoteObjectNode>
#include <QUrlQuery>
#include <QWebSocket>
#include <utility>

RemoteObjectConnection::RemoteObjectConnection(QObject* parent) : QObject(parent) {}
RemoteObjectConnection::~RemoteObjectConnection() { close(); }

void RemoteObjectConnection::setSession(RemoteSessionBackend* session)
{
    if (m_session == session) return;
    close();
    for (const auto& connection : std::as_const(m_connections)) disconnect(connection);
    m_connections.clear();
    m_attemptedGeneration = 0;
    m_session = session;
    if (session) {
        m_connections << connect(session, &RemoteSessionBackend::connectionRequested,
                                  this, [this, session] {
                          const quint64 generation = session->connectionGeneration();
                          QMetaObject::invokeMethod(this,
                              [this, guard = QPointer<RemoteSessionBackend>(session), generation] {
                                  if (guard && guard == m_session
                                      && guard->connectionGeneration() == generation)
                                      synchronize();
                              }, Qt::QueuedConnection);
                      })
                      << connect(session, &RemoteSessionBackend::connectedChanged,
                                  this, &RemoteObjectConnection::synchronizeServices)
                      << connect(session, &RemoteSessionBackend::staleChanged,
                                  this, &RemoteObjectConnection::synchronizeServices)
                      << connect(session, &QObject::destroyed, this, [this] {
                          m_session = nullptr; close(); m_attemptedGeneration = 0;
                      });
    }
    emit sessionChanged();
    synchronize();
}

QQmlListProperty<RemoteService> RemoteObjectConnection::services()
{
    return {this, this, appendService, serviceCount, serviceAt, clearServices};
}
void RemoteObjectConnection::appendService(QQmlListProperty<RemoteService>* list, RemoteService* service)
{
    auto* self = static_cast<RemoteObjectConnection*>(list->data);
    if (!service || self->m_services.contains(service)) return;
    self->m_services.append(service);
    service->setResourceOrigin(self->m_session ? QUrl(self->m_session->apiOrigin()) : QUrl());
    service->setNode(self->m_session && self->m_session->connected() && !self->m_session->stale()
                         ? self->m_node : nullptr);
    emit self->servicesChanged();
    self->synchronize();
}
qsizetype RemoteObjectConnection::serviceCount(QQmlListProperty<RemoteService>* list)
{ return static_cast<RemoteObjectConnection*>(list->data)->m_services.size(); }
RemoteService* RemoteObjectConnection::serviceAt(QQmlListProperty<RemoteService>* list, qsizetype index)
{ return static_cast<RemoteObjectConnection*>(list->data)->m_services.value(index); }
void RemoteObjectConnection::clearServices(QQmlListProperty<RemoteService>* list)
{
    auto* self = static_cast<RemoteObjectConnection*>(list->data);
    for (const auto& service : std::as_const(self->m_services)) if (service) service->setNode(nullptr);
    self->m_services.clear();
    emit self->servicesChanged();
    self->synchronize();
}

void RemoteObjectConnection::close()
{
    if (m_closing) return;
    m_closing = true;
    for (const auto& service : std::as_const(m_services)) if (service) service->setNode(nullptr);
    if (m_session && m_session->service()) m_session->service()->setNode(nullptr);
    auto* socket = std::exchange(m_socket, nullptr);
    auto* node = std::exchange(m_node, nullptr);
    if (socket) disconnect(socket, nullptr, this, nullptr);
    if (node) {
        node->setParent(nullptr);
        // Property notifications can close this transport inside QtRO's packet
        // dispatch. Retire its connection only after that dispatch has unwound.
        QMetaObject::invokeMethod(node, [node, guard = QPointer<QWebSocket>(socket)] {
            if (guard) guard->abort();
            node->deleteLater();
        }, Qt::QueuedConnection);
    }
    m_closing = false;
}

void RemoteObjectConnection::fail(const QString& message)
{
    if (m_error != message) { m_error = message; emit errorMessageChanged(); }
    if (m_session) m_session->transportFailed(message, !m_session->connected());
    close();
}

void RemoteObjectConnection::synchronizeServices()
{
    if (m_closing) return;
    const auto session = m_session;
    const quint64 generation = session ? session->connectionGeneration() : 0;
    NETWORK_LOG_STDERR("RemoteObjectConnection",
        QStringLiteral("Synchronizing %1 domain services; connected=%2 stale=%3 node=%4")
            .arg(m_services.size()).arg(session && session->connected())
            .arg(!session || session->stale()).arg(m_node != nullptr));
    for (const auto& service : std::as_const(m_services)) if (service) {
        const bool verified = session && session == m_session
            && generation == session->connectionGeneration()
            && session->connected() && !session->stale();
        service->setResourceOrigin(session ? QUrl(session->apiOrigin()) : QUrl());
        service->setNode(verified ? m_node : nullptr);
    }
}

void RemoteObjectConnection::synchronize()
{
    if (m_closing) return;
    if (!m_session || !m_session->connectionWanted()) {
        close();
        return;
    }
    if (m_attemptedGeneration == m_session->connectionGeneration()) return;
    close();
    if (!m_session || !m_session->connectionWanted()) return;
    m_attemptedGeneration = m_session->connectionGeneration();
    if (!m_error.isEmpty()) { m_error.clear(); emit errorMessageChanged(); }
    if (!m_session || !m_session->connectionWanted()
        || m_attemptedGeneration != m_session->connectionGeneration()) return;
    QUrl url(m_session->apiOrigin());
    url.setScheme(url.scheme() == QStringLiteral("https") ? QStringLiteral("wss") : QStringLiteral("ws"));
    url.setPath(QStringLiteral("/api/session/objects"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("packageId"), m_session->expectedPackageId());
    query.addQueryItem(QStringLiteral("hostId"), m_session->expectedHostId());
    url.setQuery(query);
    m_node = new QRemoteObjectNode(this);
    m_socket = new QWebSocket(m_session->apiOrigin(), QWebSocketProtocol::VersionLatest, m_node);
    auto* socket = m_socket;
    connect(socket, &QWebSocket::connected, this, [this, socket] {
        if (socket != m_socket || !m_session || !m_session->service() || !m_node) return;
        auto* device = new QtRoWebSocketDevice(socket, [] { return true; },
            2 * 1024 * 1024, 2 * 1024 * 1024, 128 * 1024, m_node);
        m_node->addClientSideConnection(device);
        const auto session = m_session;
        const auto node = QPointer<QRemoteObjectNode>(m_node);
        const quint64 generation = session->connectionGeneration();
        session->transportOpened();
        if (session && session == m_session && node && node == m_node
            && generation == session->connectionGeneration() && session->connectionWanted()
            && session->service())
            session->service()->setNode(node);
    });
    connect(socket, &QWebSocket::disconnected, this, [this, socket] {
        QMetaObject::invokeMethod(this, [this, guard = QPointer<QWebSocket>(socket)] {
            if (guard && guard == m_socket)
                fail(tr("The host session ended. Reconnect, or reload the interface if the host restarted."));
        }, Qt::QueuedConnection);
    });
    connect(socket, &QWebSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        const QString message = m_session && m_session->connected() ? socket->errorString()
            : tr("The host identity or authentication could not be verified. Reload the interface to pair with the current host.");
        QMetaObject::invokeMethod(this, [this, message, guard = QPointer<QWebSocket>(socket)] {
            if (guard && guard == m_socket) fail(message);
        }, Qt::QueuedConnection);
    });
    socket->open(url);
}
