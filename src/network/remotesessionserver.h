#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>
#include <QtWebSockets/qwebsocketprotocol.h>

class QTcpSocket;
class QIODevice;
class QWebSocket;
class QWebSocketCorsAuthenticator;
class QWebSocketServer;
class QtRoWebSocketDevice;

// Owns the WebSocket half of the read-only browser session transport. The
// listener remains ShotServer's QTcpServer; accepted HTTP sockets are handed
// here only after ShotServer has validated the still-unconsumed upgrade.
class RemoteSessionServer final : public QObject
{
    Q_OBJECT

public:
    using SnapshotProvider = std::function<QJsonObject(quint64 sequence)>;
    using SessionValidator = std::function<bool(const QByteArray& request)>;
    using RemoteObjectHandler = std::function<void(QIODevice* device)>;

    enum class UpgradeDisposition { NotTarget, NeedMore, Rejected, Accepted };
    struct UpgradeInspection {
        UpgradeDisposition disposition = UpgradeDisposition::NotTarget;
        int statusCode = 400;
        QByteArray responseBody;
        QByteArray requestHeaders;
    };

    // Inspect only bytes still buffered by ShotServer. On Accepted, the full
    // header block is returned unchanged so callers can validate the session;
    // no bytes are consumed. Qt parses them in Qt 6.12
    // qtwebsockets/src/websockets/qwebsocketserver_p.cpp:454-486.
    static UpgradeInspection inspectUpgradeBuffer(
        const QByteArray& peekedBytes, qint64 bytesAvailable, qint64 maximumHeaderSize,
        bool secureOrigin, bool requireSession, const SessionValidator& validateSession,
        const QString& expectedPackageId = {}, const QString& expectedHostId = {});

    explicit RemoteSessionServer(SnapshotProvider snapshotProvider,
                                 SessionValidator sessionValidator,
                                 bool secureOrigin,
                                 QObject* parent = nullptr);
    ~RemoteSessionServer() override;

    void adoptConnection(QTcpSocket* socket);
    void setRemoteObjectHandler(const QString& packageId, const QString& hostId,
                                RemoteObjectHandler handler);
    QString remoteObjectPackageId() const;
    QString remoteObjectHostId() const;
    bool ownsSocket(const QTcpSocket* socket) const;
    void forgetSocket(QTcpSocket* socket);
    void closeAll();

signals:
    // Qt 6.12 qtwebsockets/src/websockets/qwebsocket_p.cpp:693-704 removes
    // raw-socket callbacks during releaseConnections(). Use the WebSocket's
    // lifetime to maintain ShotServer's connection-limit accounting.
    void connectionClosed(QTcpSocket* socketIdentity);

private slots:
    void takePendingConnections();
    void publishSnapshots();

private:
    struct Client {
        QPointer<QWebSocket> socket;
        QTcpSocket* socketIdentity = nullptr; // Identity only; may already be destroyed.
        QByteArray authenticatedRequest;
        quint64 sequence = 0;
    };

    struct RemoteObjectClient {
        QPointer<QWebSocket> socket;
        QTcpSocket* socketIdentity = nullptr;
        QByteArray authenticatedRequest;
        QPointer<QtRoWebSocketDevice> device;
    };

    bool sendSnapshot(QWebSocket* socket, Client& client);
    void removeClient(QWebSocket* socket);
    bool acceptRemoteObjectClient(QWebSocket* socket, QTcpSocket* socketIdentity);
    void removeRemoteObjectClient(QWebSocket* socket);
    bool validateRemoteObjectClient(RemoteObjectClient& client);
    void closeClient(QWebSocket* socket, QWebSocketProtocol::CloseCode closeCode,
                     const QString& reason);

    QWebSocketServer* m_server = nullptr;
    QTimer m_publishTimer;
    SnapshotProvider m_snapshotProvider;
    SessionValidator m_sessionValidator;
    QString m_remoteObjectPackageId;
    QString m_remoteObjectHostId;
    RemoteObjectHandler m_remoteObjectHandler;
    QHash<QTcpSocket*, QPointer<QTcpSocket>> m_handedOffSockets;
    QHash<QWebSocket*, Client> m_clients;
    QHash<QWebSocket*, RemoteObjectClient> m_remoteObjectClients;
};
