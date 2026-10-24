#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QPointer>
#include <QHostAddress>
#include <utility>
#include <QRemoteObjectHost>
#include <QSignalSpy>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketServer>

#include "hostsessiontestfixture.h"
#include "remote/qtrowebsocketdevice.h"
#include "remotesessionprobe.h"
#include "webclient/remoteobjectconnection.h"

namespace {
class SocketFixture final : public QObject
{
public:
    SocketFixture()
        : sockets(QStringLiteral("Host session fixture"), QWebSocketServer::NonSecureMode)
    {
        source.setSnapshot(HostSessionTest::snapshot(HostSessionTest::appearance({}, false, 0), 0));
        source.setReady(true);
        if (!host.setHostUrl(QUrl(QStringLiteral("decenza-session-test:///host")),
                              QRemoteObjectHost::AllowExternalRegistration)
            || !host.enableRemoting<HostSessionStateSourceAPI>(&source)
            || !sockets.listen(QHostAddress::LocalHost, 0))
            qFatal("Could not start the host session WebSocket fixture");
        connect(&sockets, &QWebSocketServer::newConnection, this, [this] {
            while (sockets.hasPendingConnections()) {
                auto* socket = sockets.nextPendingConnection();
                clients.append(socket);
                requests.append(socket->request().url());
                auto* device = new QtRoWebSocketDevice(socket, [] { return true; },
                    128 * 1024, 512 * 1024, 2 * 1024 * 1024, &host);
                host.addHostSideConnection(device);
                connect(socket, &QWebSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    ~SocketFixture() override
    {
        disconnect(&sockets, nullptr, this, nullptr);
        for (const auto& client : std::as_const(clients)) if (client) {
            disconnect(client, nullptr, nullptr, nullptr);
            client->abort();
            delete client;
        }
    }
    QString origin() const { return QStringLiteral("http://127.0.0.1:%1").arg(sockets.serverPort()); }
    HostSessionService* configure(RemoteSessionBackend* backend, QQmlEngine* engine)
    {
        auto* service = HostSessionService::create(engine, engine);
        backend->setService(service);
        backend->setApiOrigin(origin());
        backend->setBootstrapIdentity(QString::fromLatin1(DECENZA_PACKAGE_ID), HostSessionTest::hostId());
        return service;
    }
    HostSessionTest::Source source;
    QRemoteObjectHost host;
    QWebSocketServer sockets;
    QList<QPointer<QWebSocket>> clients;
    QList<QUrl> requests;
};

void addService(RemoteObjectConnection* connection, RemoteService* service)
{
    auto list = connection->services();
    list.append(&list, service);
}
}

class tst_RemoteSessionBackend final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void bootstrapConnectsDirectlyToQtRoAndAuthorizesServicesAfterIdentity()
    {
        SocketFixture server;
        QQmlEngine engine;
        RemoteSessionBackend backend;
        server.configure(&backend, &engine);
        RemoteObjectConnection connection;
        ProbeService probe;
        connection.setSession(&backend);
        addService(&connection, &probe);
        QVERIFY(!probe.node);
        backend.connectToHost();
        QTRY_VERIFY(backend.connected());
        QVERIFY(!backend.stale());
        QTRY_VERIFY(probe.node);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests.first().path(), QStringLiteral("/api/session/objects"));
        const QUrlQuery identity(server.requests.first());
        QCOMPARE(identity.queryItemValue(QStringLiteral("packageId")), QString::fromLatin1(DECENZA_PACKAGE_ID));
        QCOMPARE(identity.queryItemValue(QStringLiteral("hostId")), HostSessionTest::hostId());
        QCOMPARE(backend.hostId(), HostSessionTest::hostId());
        QCOMPARE(backend.pressureBar(), 8.2);
        QCOMPARE(backend.flowMlPerSecond(), 2.1);

        auto next = server.source.snapshot();
        next.insert(QStringLiteral("sequence"), 1);
        auto machine = next.value(QStringLiteral("machineUi")).toMap();
        machine.insert(QStringLiteral("scaleWeight"), 42.0);
        next.insert(QStringLiteral("machineUi"), machine);
        server.source.setSnapshot(next);
        QTRY_COMPARE(backend.sequence(), qint64(1));
        QCOMPARE(backend.machineUi().scaleWeight, 42.0);

        backend.disconnectFromHost();
        QVERIFY(!backend.connected());
        QVERIFY(backend.stale());
        QVERIFY(!probe.node);
        QCOMPARE(backend.pressureBar(), 0.0);
        QCOMPARE(backend.machineUi().scaleWeight, 0.0);
    }

    void qmlServicesListAttachesEveryServiceToVerifiedSession()
    {
        SocketFixture server;
        QQmlEngine engine;
        RemoteSessionBackend backend;
        server.configure(&backend, &engine);

        QQmlComponent component(&engine);
        component.loadUrl(QUrl(QStringLiteral(
            "qrc:/qt/qml/DecenzaConnectionTests/RemoteObjectConnectionServicesFixture.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> fixture(component.create());
        QVERIFY2(fixture, qPrintable(component.errorString()));

        auto* connection = qvariant_cast<RemoteObjectConnection*>(fixture->property("connection"));
        QVERIFY(connection);
        auto services = connection->services();
        QCOMPARE(services.count(&services), qsizetype(2));
        auto* first = qobject_cast<ProbeService*>(services.at(&services, 0));
        auto* second = qobject_cast<ProbeService*>(services.at(&services, 1));
        QVERIFY(first);
        QVERIFY(second);

        connection->setSession(&backend);
        QVERIFY(!first->node);
        QVERIFY(!second->node);
        backend.connectToHost();
        QTRY_VERIFY(backend.connected());
        QVERIFY(!backend.stale());
        QTRY_VERIFY(first->node && second->node);
        QCOMPARE(first->node, second->node);
        QCOMPARE(first->attachments, 1);
        QCOMPARE(second->attachments, 1);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests.first().path(), QStringLiteral("/api/session/objects"));

        backend.disconnectFromHost();
        QTRY_VERIFY(!first->node && !second->node);
        QCOMPARE(first->detachments, 1);
        QCOMPARE(second->detachments, 1);
    }

    void missingOrMismatchedBootstrapCannotOpenTheTransport()
    {
        SocketFixture server;
        QQmlEngine engine;
        RemoteSessionBackend backend;
        auto* service = HostSessionService::create(&engine, &engine);
        backend.setService(service);
        backend.setApiOrigin(server.origin());
        RemoteObjectConnection connection;
        connection.setSession(&backend);
        for (const auto& package : {QString(), QString(64, QLatin1Char('f'))}) {
            backend.setBootstrapIdentity(package, HostSessionTest::hostId());
            backend.connectToHost();
            QVERIFY(backend.reloadRequired());
            QVERIFY(!backend.connectionWanted());
            QVERIFY(server.requests.isEmpty());
        }
    }

    void sourceEpochMismatchRequiresReloadWithoutExposingOtherServices()
    {
        SocketFixture server;
        auto values = server.source.snapshot();
        values.insert(QStringLiteral("hostId"), QStringLiteral("9a54d42f-93de-48a7-ae78-38925bd0c3e1"));
        server.source.setSnapshot(values);
        QQmlEngine engine;
        RemoteSessionBackend backend;
        server.configure(&backend, &engine);
        RemoteObjectConnection connection;
        ProbeService probe;
        connection.setSession(&backend);
        addService(&connection, &probe);
        backend.connectToHost();
        QTRY_VERIFY(backend.reloadRequired());
        QVERIFY(!backend.connected());
        QVERIFY(!backend.connectionWanted());
        QCOMPARE(probe.attachments, 0);
        QVERIFY(!probe.node);
    }

    void malformedOrMissingFactsNeverAuthorizeTheSession()
    {
        const auto valid = HostSessionTest::snapshot(HostSessionTest::appearance({}, false, 0), 0);
        QList<QVariantMap> malformed;
        auto values = valid; values.remove(QStringLiteral("machineUi")); malformed.append(values);
        values = valid; values.insert(QStringLiteral("pressureBar"), QStringLiteral("8")); malformed.append(values);
        values = valid; values.insert(QStringLiteral("machineUi"), QVariantMap{}); malformed.append(values);
        values = valid; values.insert(QStringLiteral("appearance"), QVariantMap{}); malformed.append(values);
        values = valid; values.insert(QStringLiteral("extractionAnnouncementInterval"), 1); malformed.append(values);
        for (const auto& fields : malformed) {
            HostSessionTest::Fixture server;
            server.source.setSnapshot(fields);
            QQmlEngine engine;
            RemoteSessionBackend backend;
            server.attach(&backend, &engine);
            backend.connectToHost();
            QTRY_VERIFY(!backend.errorMessage().isEmpty());
            QVERIFY(!backend.connected());
            QVERIFY(backend.stale());
            QVERIFY(backend.appearance().isEmpty());
        }
    }

    void lostOrSkippedSessionUpdatesRequireExplicitReconnect()
    {
        SocketFixture server;
        QQmlEngine engine;
        RemoteSessionBackend backend;
        server.configure(&backend, &engine);
        RemoteObjectConnection connection;
        ProbeService probe;
        connection.setSession(&backend);
        addService(&connection, &probe);
        backend.connectToHost();
        QTRY_VERIFY(backend.connected());
        auto skipped = server.source.snapshot();
        skipped.insert(QStringLiteral("sequence"), 2);
        server.source.setSnapshot(skipped);
        QTRY_VERIFY(backend.stale());
        QVERIFY(!backend.connectionWanted());
        QVERIFY(!probe.node);
        QCOMPARE(probe.attachments, 1);
        QCOMPARE(server.requests.size(), 1);

        backend.connectToHost();
        QTRY_VERIFY(backend.connected());
        QCOMPARE(backend.sequence(), qint64(2));
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(probe.attachments, 2);
        server.source.setReady(false);
        QTRY_VERIFY(backend.stale());
        QVERIFY(!backend.connected());
        QVERIFY(!probe.node);
    }

    void reentrantDisconnectCannotRestoreFreshState_data()
    {
        QTest::addColumn<int>("publication");
        QTest::newRow("connected") << 0;
        QTest::newRow("fresh") << 1;
        QTest::newRow("appearance") << 2;
        QTest::newRow("status") << 3;
    }
    void reentrantDisconnectCannotRestoreFreshState()
    {
        QFETCH(int, publication);
        HostSessionTest::Fixture server;
        QQmlEngine engine;
        RemoteSessionBackend backend;
        server.attach(&backend, &engine);
        bool disconnected = false;
        const auto leave = [&] {
            if (disconnected) return;
            disconnected = true;
            backend.disconnectFromHost();
        };
        if (publication == 0)
            connect(&backend, &RemoteSessionBackend::connectedChanged, &backend,
                    [&] { if (backend.connected()) leave(); });
        else if (publication == 1)
            connect(&backend, &RemoteSessionBackend::staleChanged, &backend,
                    [&] { if (!backend.stale()) leave(); });
        else if (publication == 2)
            connect(&backend, &RemoteSessionBackend::appearanceChanged, &backend,
                    [&] { if (!backend.appearance().isEmpty()) leave(); });
        else
            connect(&backend, &RemoteSessionBackend::statusChanged, &backend,
                    [&] { if (backend.status() == QStringLiteral("connected")) leave(); });
        backend.connectToHost();
        QTRY_VERIFY(disconnected);
        QVERIFY(!backend.connected());
        QVERIFY(backend.stale());
        QVERIFY(backend.appearance().isEmpty());
        QCOMPARE(backend.status(), QStringLiteral("disconnected"));
        QVERIFY(!backend.connectionWanted());
    }

    void invalidOriginsCannotStartAConnection()
    {
        HostSessionTest::Fixture server;
        QQmlEngine engine;
        RemoteSessionBackend backend;
        server.attach(&backend, &engine);
        for (const auto& origin : {QStringLiteral("ftp://127.0.0.1"), QStringLiteral("http://user@localhost"),
                                  QStringLiteral("https://localhost/path"), QStringLiteral("https://localhost?x=1")}) {
            backend.setApiOrigin(origin);
            backend.connectToHost();
            QCOMPARE(backend.status(), QStringLiteral("error"));
            QVERIFY(!backend.connectionWanted());
        }
    }
};

QTEST_GUILESS_MAIN(tst_RemoteSessionBackend)
#include "tst_remotesessionbackend.moc"
