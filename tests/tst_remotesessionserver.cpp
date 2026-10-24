#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QIODevice>
#include <QUrl>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocket>
#include <QSignalSpy>

#include <utility>

#include "network/remotesessionserver.h"
#include "remote/qtrowebsocketdevice.h"

namespace {
using Disposition = RemoteSessionServer::UpgradeDisposition;
using Inspection = RemoteSessionServer::UpgradeInspection;

constexpr qint64 kMaximumHeaderSize = 64 * 1024;

QByteArray validRequest(const QByteArray& extraHeaders = {},
                        const QByteArray& target = QByteArrayLiteral("/api/session/ws"))
{
    QByteArray request = QByteArrayLiteral("GET ") + target
        + QByteArrayLiteral(" HTTP/1.1\r\nHost: espresso.local:8080\r\n"
                            "Origin: http://espresso.local:8080\r\n");
    request += extraHeaders;
    request += QByteArrayLiteral("\r\n");
    return request;
}

Inspection inspect(const QByteArray& bytes, qint64 available = -1,
                   bool secure = false, bool requireSession = false,
                   const RemoteSessionServer::SessionValidator& validator = {},
                   const QString& expectedPackageId = {}, const QString& expectedHostId = {})
{
    if (available < 0)
        available = bytes.size();
    return RemoteSessionServer::inspectUpgradeBuffer(
        bytes, available, kMaximumHeaderSize, secure, requireSession, validator,
        expectedPackageId, expectedHostId);
}

QString testPackageId() { return QString(64, QLatin1Char('a')); }
QString testHostId() { return QStringLiteral("01234567-89ab-cdef-0123-456789abcdef"); }

QByteArray remoteObjectsTarget(const QString& packageId = testPackageId(),
                               const QString& hostId = testHostId(),
                               const QString& clientId = QStringLiteral("12345678-1234-1234-1234-123456789abc"))
{
    return QByteArrayLiteral("/api/session/objects?packageId=") + packageId.toLatin1()
        + QByteArrayLiteral("&hostId=") + hostId.toLatin1()
        + QByteArrayLiteral("&clientId=") + clientId.toLatin1();
}

class SessionHarness
{
public:
    static RemoteSessionServer::SnapshotProvider defaultProvider()
    {
        return [](quint64 sequence) {
            return QJsonObject{{QStringLiteral("type"), QStringLiteral("snapshot")},
                               {QStringLiteral("sequence"), static_cast<double>(sequence)}};
        };
    }

    explicit SessionHarness(RemoteSessionServer::SessionValidator validator = {},
                            RemoteSessionServer::SnapshotProvider provider = {})
        : helper(provider ? std::move(provider) : defaultProvider(), std::move(validator), false)
    {
        QObject::connect(&listener, &QTcpServer::newConnection, &listener, [this] {
            while (listener.hasPendingConnections())
                helper.adoptConnection(listener.nextPendingConnection());
        });
    }

    bool start() { return listener.listen(QHostAddress::LocalHost, 0); }

    QTcpServer listener;
    RemoteSessionServer helper;
};
} // namespace

class TestRemoteSessionServer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<QTcpSocket*>(); }
    void init() { QTest::failOnWarning(); }

    void fragmentedRequestLineWaitsWithoutConsuming()
    {
        const QByteArray prefix = QByteArrayLiteral("GET /api/session/");
        const Inspection result = inspect(prefix, prefix.size());
        QCOMPARE(result.disposition, Disposition::NeedMore);
    }

    void fragmentedHeaderWaitsWithoutConsuming()
    {
        const QByteArray complete = validRequest();
        const QByteArray partial = complete.left(complete.size() - 2);
        const Inspection result = inspect(partial, partial.size());
        QCOMPARE(result.disposition, Disposition::NeedMore);
    }

    void validUpgradePreservesEntireHeader()
    {
        const QByteArray request = validRequest(
            QByteArrayLiteral("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"));
        const Inspection result = inspect(request);
        QCOMPARE(result.disposition, Disposition::Accepted);
        QCOMPARE(result.requestHeaders, request);
    }

    void authIsConditionalAndUsesValidatedRequest()
    {
        const QByteArray request = validRequest(QByteArrayLiteral("Cookie: session=abc\r\n"));
        QByteArray seenByValidator;
        const auto validator = [&seenByValidator](const QByteArray& raw) {
            seenByValidator = raw;
            return raw.contains("Cookie: session=abc\r\n");
        };
        QCOMPARE(inspect(request, -1, false, true, validator).disposition, Disposition::Accepted);
        QCOMPARE(seenByValidator, request);

        const auto reject = [](const QByteArray&) { return false; };
        const Inspection denied = inspect(request, -1, false, true, reject);
        QCOMPARE(denied.disposition, Disposition::Rejected);
        QCOMPARE(denied.statusCode, 401);

        // The app's existing security-off mode allows the same-origin route
        // without requiring a session cookie.
        QCOMPARE(inspect(validRequest(), -1, false, false, {}).disposition,
                 Disposition::Accepted);
    }

    void remoteObjectsRequireConfiguredMatchingIdentity()
    {
        const QByteArray request = validRequest(
            QByteArrayLiteral("Cookie: session=abc\r\n"), remoteObjectsTarget());
        const auto validator = [](const QByteArray& raw) {
            return raw.contains("Cookie: session=abc\r\n");
        };
        QCOMPARE(inspect(request, -1, false, true, validator).disposition,
                 Disposition::Rejected);
        QCOMPARE(inspect(request, -1, false, true, validator).statusCode, 503);

        Inspection result = inspect(request, -1, false, true, validator,
                                    testPackageId(), testHostId());
        QCOMPARE(result.disposition, Disposition::Accepted);
        QCOMPARE(result.requestHeaders, request);

        result = inspect(request, -1, false, true, validator,
                         testPackageId(), QStringLiteral("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"));
        QCOMPARE(result.disposition, Disposition::Rejected);
        QCOMPARE(result.statusCode, 409);
    }

    void remoteObjectsKeepOriginAndCookieChecks()
    {
        QByteArray request = validRequest({}, remoteObjectsTarget());
        request.replace("Origin: http://espresso.local:8080",
                        "Origin: http://attacker.example");
        Inspection result = inspect(request, -1, false, false, {},
                                     testPackageId(), testHostId());
        QCOMPARE(result.disposition, Disposition::Rejected);
        QCOMPARE(result.statusCode, 403);

        result = inspect(validRequest({}, remoteObjectsTarget()), -1, false, true, {},
                         testPackageId(), testHostId());
        QCOMPARE(result.disposition, Disposition::Rejected);
        QCOMPARE(result.statusCode, 401);
    }

    void remoteObjectsRejectMalformedIdentityQuery()
    {
        const QList<QByteArray> targets{
            QByteArrayLiteral("/api/session/objects"),
            QByteArrayLiteral("/api/session/objects?packageId=") + testPackageId().toLatin1(),
            remoteObjectsTarget() + QByteArrayLiteral("&hostId=") + testHostId().toLatin1(),
            remoteObjectsTarget() + QByteArrayLiteral("&clientId=12345678-1234-1234-1234-123456789abc"),
            remoteObjectsTarget(testPackageId(), testHostId(), QStringLiteral("00000000-0000-0000-0000-000000000000")),
            QByteArrayLiteral("/api/session/objects?packageId=") + testPackageId().toLatin1()
                + QByteArrayLiteral("&hostId=") + testHostId().toLatin1()
                + QByteArrayLiteral("&extra=1")
        };
        for (const QByteArray& target : targets) {
            const Inspection result = inspect(validRequest({}, target), -1, false, false, {},
                                              testPackageId(), testHostId());
            QCOMPARE(result.disposition, Disposition::Rejected);
            QCOMPARE(result.statusCode, 400);
        }
    }

    void missingOriginIsRejected()
    {
        QByteArray request = QByteArrayLiteral(
            "GET /api/session/ws HTTP/1.1\r\nHost: espresso.local:8080\r\n\r\n");
        const Inspection result = inspect(request);
        QCOMPARE(result.disposition, Disposition::Rejected);
        QCOMPARE(result.statusCode, 403);
    }

    void crossOriginAndSchemeMismatchAreRejected()
    {
        QByteArray crossOrigin = validRequest();
        crossOrigin.replace("http://espresso.local:8080", "http://attacker.example");
        Inspection result = inspect(crossOrigin);
        QCOMPARE(result.disposition, Disposition::Rejected);
        QCOMPARE(result.statusCode, 403);

        QByteArray changedScheme = validRequest();
        changedScheme.replace("http://espresso.local:8080", "https://espresso.local:8080");
        result = inspect(changedScheme);
        QCOMPARE(result.disposition, Disposition::Rejected);
        QCOMPARE(result.statusCode, 403);
    }

    void queryAndWrongMethodAreRejected()
    {
        Inspection result = inspect(validRequest({}, QByteArrayLiteral("/api/session/ws?x=1")));
        QCOMPARE(result.disposition, Disposition::Rejected);
        QCOMPARE(result.statusCode, 400);

        QByteArray wrongMethod = validRequest();
        wrongMethod.replace(0, 3, "PUT");
        result = inspect(wrongMethod);
        QCOMPARE(result.disposition, Disposition::Rejected);
        QCOMPARE(result.statusCode, 405);
    }

    void duplicatesAndHttpBodiesAreRejected()
    {
        Inspection result = inspect(validRequest(QByteArrayLiteral(
            "Origin: http://espresso.local:8080\r\n")));
        QCOMPARE(result.disposition, Disposition::Rejected);
        QCOMPARE(result.statusCode, 400);

        result = inspect(validRequest(QByteArrayLiteral("Content-Length: 0\r\n")));
        QCOMPARE(result.disposition, Disposition::Rejected);
        QCOMPARE(result.statusCode, 400);

        result = inspect(validRequest(QByteArrayLiteral("Transfer-Encoding: chunked\r\n")));
        QCOMPARE(result.disposition, Disposition::Rejected);
        QCOMPARE(result.statusCode, 400);

        QByteArray pipelined = validRequest();
        pipelined += QByteArrayLiteral("GET /api/state HTTP/1.1\r\n\r\n");
        result = inspect(pipelined);
        QCOMPARE(result.disposition, Disposition::Rejected);
        QCOMPARE(result.statusCode, 400);
    }

    void malformedHeadersAndOversizedUnterminatedLineAreRejected()
    {
        const Inspection malformed = inspect(QByteArrayLiteral(
            "GET /api/session/ws HTTP/1.1\r\nHost espresso.local:8080\r\n\r\n"));
        QCOMPARE(malformed.disposition, Disposition::Rejected);
        QCOMPARE(malformed.statusCode, 400);

        const QByteArray huge = QByteArrayLiteral("GET /api/session/ws")
            + QByteArray(kMaximumHeaderSize, 'X');
        const Inspection oversized = inspect(huge, huge.size());
        QCOMPARE(oversized.disposition, Disposition::Rejected);
        QCOMPARE(oversized.statusCode, 413);
    }

    void unrelatedRequestIsLeftToHttpParser()
    {
        const Inspection result = inspect(QByteArrayLiteral(
            "GET /api/state HTTP/1.1\r\nHost: espresso.local:8080\r\n\r\n"));
        QCOMPARE(result.disposition, Disposition::NotTarget);
    }

    void transportSendsIncreasingSnapshotSequence()
    {
        SessionHarness harness;
        QVERIFY(harness.start());

        QWebSocket client;
        QSignalSpy messages(&client, &QWebSocket::textMessageReceived);
        QVERIFY(messages.isValid());
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/api/session/ws")
                             .arg(harness.listener.serverPort())));

        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 5000);
        const auto first = QJsonDocument::fromJson(messages.takeFirst().at(0).toString().toUtf8());
        QVERIFY(first.isObject());
        QCOMPARE(first.object().value(QStringLiteral("sequence")).toDouble(), 1.0);

        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 5000);
        const auto second = QJsonDocument::fromJson(messages.takeFirst().at(0).toString().toUtf8());
        QVERIFY(second.isObject());
        QCOMPARE(second.object().value(QStringLiteral("sequence")).toDouble(), 2.0);
        client.close();
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 5000);
    }

    void transportClosesWhenSessionIsRevoked()
    {
        bool validSession = true;
        SessionHarness harness([&validSession](const QByteArray&) { return validSession; });
        QVERIFY(harness.start());

        QWebSocket client;
        QSignalSpy messages(&client, &QWebSocket::textMessageReceived);
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/api/session/ws")
                             .arg(harness.listener.serverPort())));
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 5000);
        validSession = false;
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 5000);
    }

    void remoteObjectClientLifetimesEndExactlyOnce()
    {
        SessionHarness harness;
        harness.helper.setRemoteObjectHandler(testPackageId(), testHostId(), [](QIODevice*) {});
        QSignalSpy opened(&harness.helper, &RemoteSessionServer::remoteObjectClientOpened);
        QSignalSpy closed(&harness.helper, &RemoteSessionServer::remoteObjectClientClosed);
        QVERIFY(harness.start());
        const auto url = [&harness](const QString& clientId) {
            return QUrl(QStringLiteral("ws://127.0.0.1:%1%2")
                .arg(harness.listener.serverPort())
                .arg(QString::fromLatin1(remoteObjectsTarget(testPackageId(), testHostId(), clientId))));
        };
        const QString a = QStringLiteral("12345678-1234-1234-1234-123456789abc");
        const QString b = QStringLiteral("12345678-1234-1234-1234-123456789abd");
        QWebSocket first;
        first.open(url(a));
        QTRY_COMPARE(opened.count(), 1);
        QCOMPARE(opened.first().first().toString(), a);
        QWebSocket duplicate;
        duplicate.open(url(a));
        QSignalSpy rejected(&duplicate, &QWebSocket::disconnected);
        QTRY_COMPARE(rejected.count(), 1);
        QCOMPARE(opened.count(), 1);
        QCOMPARE(closed.count(), 0);
        QCOMPARE(first.state(), QAbstractSocket::ConnectedState);
        QWebSocket second;
        second.open(url(b));
        QTRY_COMPARE(opened.count(), 2);
        first.close();
        QTRY_COMPARE(closed.count(), 1);
        QCOMPARE(closed.first().first().toString(), a);
        harness.helper.closeAll();
        QTRY_COMPARE(closed.count(), 2);
        QCOMPARE(closed.last().first().toString(), b);
        QTRY_COMPARE(second.state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(closed.count(), 2);
    }

    void remoteObjectsTransportHandsOffBoundedBinaryData()
    {
        SessionHarness harness;
        QPointer<QIODevice> remoteDevice;
        harness.helper.setRemoteObjectHandler(
            testPackageId(), testHostId(), [&remoteDevice](QIODevice* device) {
                remoteDevice = device;
            });
        QVERIFY(harness.start());

        QWebSocket client;
        QSignalSpy textMessages(&client, &QWebSocket::textMessageReceived);
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1%2")
                             .arg(harness.listener.serverPort())
                             .arg(QString::fromLatin1(remoteObjectsTarget()))));
        QTRY_VERIFY_WITH_TIMEOUT(remoteDevice, 5000);
        QCOMPARE(textMessages.size(), 0);

        QSignalSpy readyRead(remoteDevice, &QIODevice::readyRead);
        QVERIFY(readyRead.isValid());
        QSignalSpy disconnected(remoteDevice, SIGNAL(disconnected()));
        QVERIFY(disconnected.isValid());
        QSignalSpy binaryMessages(&client, &QWebSocket::binaryMessageReceived);
        QVERIFY(binaryMessages.isValid());
        const QByteArray payload = QByteArrayLiteral("qtro-binary-frame");
        QCOMPARE(client.sendBinaryMessage(payload), qint64(payload.size()));
        QTRY_COMPARE_WITH_TIMEOUT(readyRead.size(), 1, 5000);
        QCOMPARE(remoteDevice->readAll(), payload);

        const QByteArray response = QByteArrayLiteral("qtro-reply-frame");
        QCOMPARE(remoteDevice->write(response), qint64(response.size()));
        QTRY_COMPARE_WITH_TIMEOUT(binaryMessages.size(), 1, 5000);
        QCOMPARE(binaryMessages.takeFirst().at(0).toByteArray(), response);
        client.close();
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(disconnected.size(), 1, 5000);
    }

    void remoteObjectsTransportStreamsLargePacketsInBoundedChunks()
    {
        SessionHarness harness;
        QPointer<QIODevice> hostDevice;
        harness.helper.setRemoteObjectHandler(testPackageId(), testHostId(),
            [&hostDevice](QIODevice* device) { hostDevice = device; });
        QVERIFY(harness.start());
        QWebSocket socket;
        QtRoWebSocketDevice clientDevice(&socket, [] { return true; });
        QSignalSpy chunks(&socket, &QWebSocket::binaryMessageReceived);
        socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1%2")
            .arg(harness.listener.serverPort())
            .arg(QString::fromLatin1(remoteObjectsTarget()))));
        QTRY_VERIFY_WITH_TIMEOUT(hostDevice, 5000);

        // Exceeds both the old client write cap and old host accumulation cap,
        // and leaves room for the envelope of a maximum-size service request.
        QByteArray packet(2 * 1024 * 1024 + 4096, 'p');
        packet[0] = 'a';
        packet[128 * 1024] = 'b';
        packet[packet.size() - 1] = 'z';
        QCOMPARE(clientDevice.write(packet), qint64(packet.size()));
        QTRY_COMPARE_WITH_TIMEOUT(hostDevice->bytesAvailable(), qint64(packet.size()), 5000);
        QCOMPARE(hostDevice->readAll(), packet);
        QCOMPARE(hostDevice->write(packet), qint64(packet.size()));
        QTRY_COMPARE_WITH_TIMEOUT(clientDevice.bytesAvailable(), qint64(packet.size()), 5000);
        QCOMPARE(clientDevice.readAll(), packet);
        QVERIFY(chunks.size() > 1);
        for (const auto& chunk : chunks)
            QVERIFY(chunk.at(0).toByteArray().size() <= QtRoWebSocketDevice::MaximumIncomingMessageBytes);
        socket.close();
        QTRY_COMPARE_WITH_TIMEOUT(socket.state(), QAbstractSocket::UnconnectedState, 5000);
    }

    void remoteObjectsOversizedWriteClosesInsteadOfSilentlyDropping()
    {
        SessionHarness harness;
        QPointer<QIODevice> hostDevice;
        harness.helper.setRemoteObjectHandler(testPackageId(), testHostId(),
            [&hostDevice](QIODevice* device) { hostDevice = device; });
        QVERIFY(harness.start());
        QWebSocket socket;
        QtRoWebSocketDevice clientDevice(&socket, [] { return true; });
        QSignalSpy closed(&clientDevice, &QtRoWebSocketDevice::transportClosed);
        socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1%2")
            .arg(harness.listener.serverPort())
            .arg(QString::fromLatin1(remoteObjectsTarget()))));
        QTRY_VERIFY_WITH_TIMEOUT(hostDevice, 5000);
        QCOMPARE(clientDevice.write(QByteArray(QtRoWebSocketDevice::MaximumPacketBytes + 1, 'x')),
                 qint64(-1));
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
        QCOMPARE(socket.state(), QAbstractSocket::UnconnectedState);
    }

    void remoteObjectsTransportRevalidatesEveryIncomingFrame()
    {
        bool validSession = true;
        int validationCount = 0;
        SessionHarness harness([&](const QByteArray&) {
            ++validationCount;
            return validSession;
        });
        QPointer<QIODevice> remoteDevice;
        harness.helper.setRemoteObjectHandler(
            testPackageId(), testHostId(), [&remoteDevice](QIODevice* device) {
                remoteDevice = device;
            });
        QVERIFY(harness.start());

        QWebSocket client;
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1%2")
                             .arg(harness.listener.serverPort())
                             .arg(QString::fromLatin1(remoteObjectsTarget()))));
        QTRY_VERIFY_WITH_TIMEOUT(remoteDevice, 5000);
        QSignalSpy readyRead(remoteDevice, &QIODevice::readyRead);
        QVERIFY(readyRead.isValid());
        const int validationsAfterAccept = validationCount;
        validSession = false;
        client.sendBinaryMessage(QByteArrayLiteral("must-not-reach-qtro"));
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 5000);
        QVERIFY(validationCount > validationsAfterAccept);
        QCOMPARE(readyRead.size(), 0);
    }

    void remoteObjectsTransportPeriodicallyRevalidatesIdleSession()
    {
        bool validSession = true;
        SessionHarness harness([&](const QByteArray&) { return validSession; });
        harness.helper.setRemoteObjectHandler(testPackageId(), testHostId(), [](QIODevice*) {});
        QVERIFY(harness.start());

        QWebSocket client;
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1%2")
                             .arg(harness.listener.serverPort())
                             .arg(QString::fromLatin1(remoteObjectsTarget()))));
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::ConnectedState, 5000);
        validSession = false;
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 5000);
    }

    void remoteObjectsTransportRejectsTextAndUnconfiguredRoute()
    {
        SessionHarness harness;
        QVERIFY(harness.start());

        QWebSocket client;
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1%2")
                             .arg(harness.listener.serverPort())
                             .arg(QString::fromLatin1(remoteObjectsTarget()))));
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 5000);

        QPointer<QIODevice> remoteDevice;
        harness.helper.setRemoteObjectHandler(
            testPackageId(), testHostId(), [&remoteDevice](QIODevice* device) {
                remoteDevice = device;
            });
        QWebSocket textClient;
        textClient.open(QUrl(QStringLiteral("ws://127.0.0.1:%1%2")
                                 .arg(harness.listener.serverPort())
                                 .arg(QString::fromLatin1(remoteObjectsTarget()))));
        QTRY_VERIFY_WITH_TIMEOUT(remoteDevice, 5000);
        textClient.sendTextMessage(QStringLiteral("not a QtRO frame"));
        QTRY_VERIFY_WITH_TIMEOUT(textClient.state() == QAbstractSocket::UnconnectedState, 5000);
    }

    void transportRejectsClientMessages()
    {
        SessionHarness harness;
        QVERIFY(harness.start());

        QWebSocket client;
        QSignalSpy messages(&client, &QWebSocket::textMessageReceived);
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/api/session/ws")
                             .arg(harness.listener.serverPort())));
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 5000);
        client.sendTextMessage(QStringLiteral("write command"));
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 5000);
    }

    void transportShutdownRetiresActiveClient()
    {
        SessionHarness harness;
        QVERIFY(harness.start());

        QWebSocket client;
        QSignalSpy messages(&client, &QWebSocket::textMessageReceived);
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/api/session/ws")
                             .arg(harness.listener.serverPort())));
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 5000);
        harness.helper.closeAll();
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 5000);
    }

    void closingClientRemainsAccountedUntilDisconnected()
    {
        SessionHarness harness;
        QVERIFY(harness.start());

        QWebSocket client;
        QSignalSpy messages(&client, &QWebSocket::textMessageReceived);
        QSignalSpy closed(&harness.helper, &RemoteSessionServer::connectionClosed);
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/api/session/ws")
                             .arg(harness.listener.serverPort())));
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 5000);

        client.close();
        QVERIFY(QMetaObject::invokeMethod(&harness.helper, "publishSnapshots",
                                          Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 5000);
        QCOMPARE(closed.size(), 1);
    }

    void transportSendsLocalizedSnapshotLargerThanFourKiB()
    {
        SessionHarness harness({}, [](quint64 sequence) {
            return QJsonObject{
                {QStringLiteral("sequence"), static_cast<double>(sequence)},
                {QStringLiteral("localizedEquipmentText"), QString(4 * 1024, QChar(0x00e9))}
            };
        });
        QVERIFY(harness.start());

        QWebSocket client;
        QSignalSpy messages(&client, &QWebSocket::textMessageReceived);
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/api/session/ws")
                             .arg(harness.listener.serverPort())));
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 5000);
        const QJsonDocument payload = QJsonDocument::fromJson(
            messages.takeFirst().at(0).toString().toUtf8());
        QVERIFY(payload.isObject());
        QCOMPARE(payload.object().value(QStringLiteral("localizedEquipmentText")).toString().size(),
                 4 * 1024);
        QVERIFY(payload.toJson(QJsonDocument::Compact).size() > 4 * 1024);
        QVERIFY(client.state() == QAbstractSocket::ConnectedState);
        client.close();
    }

    void transportClosesWhenSnapshotExceedsLimit()
    {
        SessionHarness harness({}, [](quint64) {
            return QJsonObject{{QStringLiteral("data"), QString(32 * 1024 + 1, QLatin1Char('x'))}};
        });
        QVERIFY(harness.start());

        QWebSocket client;
        QSignalSpy messages(&client, &QWebSocket::textMessageReceived);
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/api/session/ws")
                             .arg(harness.listener.serverPort())));
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 5000);
        QCOMPARE(messages.size(), 0);
    }

    void transportRejectsOversizedIncomingFrame()
    {
        SessionHarness harness;
        QVERIFY(harness.start());

        QWebSocket client;
        QSignalSpy messages(&client, &QWebSocket::textMessageReceived);
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/api/session/ws")
                             .arg(harness.listener.serverPort())));
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 5000);
        QCOMPARE(client.sendBinaryMessage(QByteArray(4 * 1024 + 1, 'x')) > 0, true);
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 5000);
    }
};

QTEST_GUILESS_MAIN(TestRemoteSessionServer)
#include "tst_remotesessionserver.moc"
