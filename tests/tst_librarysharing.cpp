#include "remote/libraryservice.h"
#include "rep_libraryservice_source.h"
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QScopedPointer>
#include <QSignalSpy>

#include "network/librarysharing.h"
#include "core/settings.h"
#include "core/settings_network.h"
#include "core/widgetlibrary.h"
#include "network/communitythumbnail.h"
#include "remote/libraryservicesource.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QStandardPaths>
#include <QtTest>

class PendingLibraryServiceSource final : public LibraryStateSimpleSource
{
public:
    QStringList requests;
    bool replyAutomatically = true;
    PendingLibraryServiceSource()
    {
        setReady(true);
    }
    void refresh() override {}
    void execute(QString id, QString operation, QVariantMap) override
    {
        requests.append(id);
        if (replyAutomatically) emit operationCompleted(id, operation, true, {}, {});
    }
};

class LibraryHttpFixture final : public QObject
{
    Q_OBJECT
public:
    explicit LibraryHttpFixture(QObject* parent = nullptr) : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray request = socket->property("requestBytes").toByteArray();
                    request += socket->readAll();
                    socket->setProperty("requestBytes", request);
                    if (!request.contains("\r\n\r\n")) return;
                    socket->disconnect(this);
                    const QByteArray line = request.left(request.indexOf("\r\n"));
                    if (line.startsWith("GET ")) {
                        if (line.startsWith("GET /library/thumbnails/")) {
                            ++m_thumbnailRequestCount;
                            const QList<QByteArray> pieces = line.split(' ');
                            if (pieces.size() > 1) m_lastThumbnailPath = pieces[1];
                            writeRawResponse(socket, m_thumbnailPayload, m_thumbnailStatus,
                                             m_thumbnailContentType);
                        } else if (++m_getCount == 1) {
                            writeResponse(socket, QJsonObject{
                                {"total", 1},
                                {"entries", QJsonArray{QJsonObject{{"id", "cached"}, {"createdAt", "2026-01-01"}}}}
                            });
                        } else {
                            m_heldBrowse = socket;
                        }
                    } else if (line.startsWith("DELETE ")) {
                        const QList<QByteArray> pieces = line.split(' ');
                        const QString id = pieces.size() > 1
                            ? QString::fromUtf8(pieces[1].mid(pieces[1].lastIndexOf('/') + 1))
                            : QString();
                        m_pendingDeletes.append(qMakePair(QPointer<QTcpSocket>(socket), id));
                    } else {
                        writeResponse(socket, QJsonObject{{"error", "unexpected request"}}, 404);
                    }
                });
            }
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QUrl apiBase() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/v1/library")
                        .arg(m_server.serverPort()));
    }
    int getCount() const { return m_getCount; }
    int thumbnailRequestCount() const { return m_thumbnailRequestCount; }
    QByteArray lastThumbnailPath() const { return m_lastThumbnailPath; }
    void setThumbnailResponse(const QByteArray& payload, int status = 200,
                              const QByteArray& contentType = QByteArrayLiteral("image/png"))
    {
        m_thumbnailPayload = payload;
        m_thumbnailStatus = status;
        m_thumbnailContentType = contentType;
    }
    qsizetype pendingDeleteCount() const { return m_pendingDeletes.size(); }
    bool finishDelete(const QString& id)
    {
        for (qsizetype i = 0; i < m_pendingDeletes.size(); ++i) {
            if (m_pendingDeletes.at(i).second != id)
                continue;
            const QPointer<QTcpSocket> socket = m_pendingDeletes.at(i).first;
            m_pendingDeletes.removeAt(i);
            writeResponse(socket, QJsonObject{{"success", true}});
            return true;
        }
        return false;
    }
    void finishBrowse()
    {
        finishBrowse(QJsonObject{
            {"total", 2},
            {"entries", QJsonArray{
                QJsonObject{{"id", "cached"}, {"createdAt", "2026-01-01"}},
                QJsonObject{{"id", "fresh"}, {"createdAt", "2026-02-01"}}
            }}
        });
    }
    void finishBrowse(const QJsonObject& response)
    {
        if (!m_heldBrowse) return;
        QPointer<QTcpSocket> socket = m_heldBrowse;
        m_heldBrowse.clear();
        writeResponse(socket, response);
    }
    void finishBrowseRaw(const QByteArray& payload)
    {
        if (!m_heldBrowse) return;
        QPointer<QTcpSocket> socket = m_heldBrowse;
        m_heldBrowse.clear();
        writeRawResponse(socket, payload);
    }

private:
    static void writeResponse(QTcpSocket* socket, const QJsonObject& body, int status = 200)
    {
        if (!socket) return;
        const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
        writeRawResponse(socket, payload, status);
    }
    static void writeRawResponse(QTcpSocket* socket, const QByteArray& payload, int status = 200,
                                 const QByteArray& contentType = QByteArrayLiteral("application/json"))
    {
        if (!socket) return;
        const QByteArray reason = status == 200 ? QByteArrayLiteral("OK") : QByteArrayLiteral("Not Found");
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n"
                      "Content-Type: " + contentType + "\r\n"
                      "Connection: close\r\n"
                      "Content-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n"
                      + payload);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    int m_getCount = 0;
    int m_thumbnailRequestCount = 0;
    QByteArray m_thumbnailPayload;
    QByteArray m_thumbnailContentType = QByteArrayLiteral("image/png");
    int m_thumbnailStatus = 200;
    QByteArray m_lastThumbnailPath;
    QPointer<QTcpSocket> m_heldBrowse;
    QList<QPair<QPointer<QTcpSocket>, QString>> m_pendingDeletes;
};

class TestLibrarySharing final : public QObject
{
    Q_OBJECT
private:
    static void clearCache()
    {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/library/community_cache.json");
        QFile::remove(path);
    }

private slots:
    void queuedReplyFromRetiredReplicaCannotCompleteAReusedId()
    {
        PendingLibraryServiceSource firstSource, secondSource;
        firstSource.replyAutomatically = secondSource.replyAutomatically = false;
        QRemoteObjectHost firstHost, secondHost;
        QVERIFY(firstHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(secondHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(firstHost.enableRemoting<LibraryStateSourceAPI>(&firstSource));
        QVERIFY(secondHost.enableRemoting<LibraryStateSourceAPI>(&secondSource));
        QRemoteObjectNode firstClient, secondClient;
        QVERIFY(firstClient.connectToNode(firstHost.hostUrl()));
        QVERIFY(secondClient.connectToNode(secondHost.hostUrl()));
        QScopedPointer<LibraryStateReplica> warmed(secondClient.acquire<LibraryStateReplica>());
        QTRY_VERIFY(warmed->isReplicaValid());
        QQmlEngine engine;
        LibraryService* service = LibraryService::create(&engine);
        service->setNode(&firstClient);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &LibraryService::operationCompleted);
        const QString id = QStringLiteral("reused-request");
        QCOMPARE(service->execute(QStringLiteral("deleteLocal"), {}, id), id);
        QTRY_COMPARE(firstSource.requests, QStringList{id});
        LibraryStateReplica* origin = nullptr;
        for (auto* replica : service->findChildren<LibraryStateReplica*>())
            if (replica->isReplicaValid()) origin = replica;
        QVERIFY(origin);
        // Queue the notification on the shipping handler before replacing its
        // replica. The current admission must survive that already queued reply.
        emit origin->operationCompleted(id, QStringLiteral("deleteLocal"), true, {}, {});
        service->setNode(&secondClient);
        QVERIFY(service->ready());
        QCOMPARE(service->execute(QStringLiteral("deleteLocal"), {}, id), id);
        QTRY_COMPARE(secondSource.requests, QStringList{id});
        QCOMPARE(completed.size(), 1);
        QVERIFY(!completed.first().at(2).toBool());
        QVERIFY(service->busy());
        emit secondSource.operationCompleted(id, QStringLiteral("deleteLocal"), true, {}, {});
        QTRY_COMPARE(completed.size(), 2);
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(!service->busy());
    }

    void failedAdmissionCannotReplayAReusedIdOnTheReplacementNode()
    {
        PendingLibraryServiceSource firstSource, secondSource;
        QRemoteObjectHost firstHost, secondHost;
        QVERIFY(firstHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(secondHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(firstHost.enableRemoting<LibraryStateSourceAPI>(&firstSource));
        QVERIFY(secondHost.enableRemoting<LibraryStateSourceAPI>(&secondSource));
        QRemoteObjectNode firstClient, secondClient;
        QVERIFY(firstClient.connectToNode(firstHost.hostUrl()));
        QVERIFY(secondClient.connectToNode(secondHost.hostUrl()));
        QScopedPointer<LibraryStateReplica> warmed(secondClient.acquire<LibraryStateReplica>());
        QTRY_VERIFY(warmed->isReplicaValid());
        QQmlEngine engine;
        LibraryService* service = LibraryService::create(&engine);
        service->setNode(&firstClient);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &LibraryService::operationCompleted);
        const QString id = QStringLiteral("reused-library-request");
        bool detached = false, reboundReady = false;
        connect(service, &LibraryService::busyChanged, this, [&] {
            if (detached || !service->busy()) return;
            detached = true;
            service->setNode(nullptr);
        });
        connect(service, &LibraryService::operationCompleted, this,
                [&](const QString& requestId, const QString&, bool success,
                    const QVariantMap&, const QString&) {
            if (requestId != id || success) return;
            service->setNode(&secondClient);
            reboundReady = service->ready();
            if (reboundReady) service->execute(QStringLiteral("deleteLocal"), {}, id);
        });
        QCOMPARE(service->execute(QStringLiteral("deleteLocal"), {}, id), id);
        QVERIFY(reboundReady);
        QTRY_COMPARE(completed.size(), 2);
        QVERIFY(!completed.first().at(2).toBool());
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(firstSource.requests.isEmpty());
        QCOMPARE(secondSource.requests, QStringList{id});
        QVERIFY(!service->busy());
    }

    void requestAdmissionSurvivesSynchronousObservers_data()
    {
        QTest::addColumn<QString>("transition");
        for (const char* transition : {"detach", "rebind", "same-node"})
            QTest::newRow(transition) << QString::fromLatin1(transition);
    }

    void requestAdmissionSurvivesSynchronousObservers()
    {
        QFETCH(QString, transition);
        PendingLibraryServiceSource firstSource, secondSource;
        QRemoteObjectHost firstHost, secondHost;
        QVERIFY(firstHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(secondHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(firstHost.enableRemoting<LibraryStateSourceAPI>(&firstSource));
        QVERIFY(secondHost.enableRemoting<LibraryStateSourceAPI>(&secondSource));
        QRemoteObjectNode firstClient, secondClient;
        QVERIFY(firstClient.connectToNode(firstHost.hostUrl()));
        QVERIFY(secondClient.connectToNode(secondHost.hostUrl()));
        // Prepare a ready replacement before request admission.
        QScopedPointer<LibraryStateReplica> warmed(secondClient.acquire<LibraryStateReplica>());
        QTRY_VERIFY(warmed->isReplicaValid());
        QQmlEngine engine;
        QPointer<LibraryService> service = LibraryService::create(&engine);
        service->setNode(&firstClient);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &LibraryService::operationCompleted);
        bool transitioned = false;
        connect(service, &LibraryService::busyChanged, this, [&] {
            if (!service) return;
            if (transitioned || !service->busy()) return;
            transitioned = true;
            if (transition == QLatin1String("same-node")) {
                service->setNode(&firstClient);
            } else {
                service->setNode(transition == QLatin1String("rebind") ? &secondClient : nullptr);
            }
        });
        const QString retiredId = service->execute(QStringLiteral("deleteLocal"), {});
        QVERIFY(transitioned);
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), retiredId);
        if (transition == QLatin1String("same-node")) {
            QVERIFY(completed.first().at(2).toBool());
            QCOMPARE(firstSource.requests, QStringList{retiredId});
            QVERIFY(secondSource.requests.isEmpty());
            return;
        }
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("disconnected"));
        service->setNode(&secondClient);
        QTRY_VERIFY(service->ready());
        const QString freshId = service->execute(QStringLiteral("deleteLocal"), {});
        QTRY_COMPARE(completed.size(), 2);
        QCOMPARE(completed.last().at(0).toString(), freshId);
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(firstSource.requests.isEmpty());
        QCOMPARE(secondSource.requests, QStringList{freshId});
        QVERIFY(!service->busy());
    }

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QTest::failOnWarning();
    }

    void cacheNotificationDoesNotCompleteBrowseEarly()
    {
        clearCache();
        Settings settings;
        LibraryHttpFixture fixture;
        QVERIFY(fixture.listen());
        LibrarySharing sharing(&settings, nullptr, nullptr, fixture.apiBase());
        QSignalSpy finished(&sharing, &LibrarySharing::browseRequestFinished);
        QVERIFY(finished.isValid());

        QVERIFY(sharing.browseCommunityForRequest(QStringLiteral("first")));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("first"));
        QCOMPARE(finished.at(0).at(1).toBool(), true);

        QSignalSpy cacheChanged(&sharing, &LibrarySharing::communityEntriesChanged);
        QVERIFY(sharing.browseCommunityForRequest(QStringLiteral("second")));
        QTRY_COMPARE(cacheChanged.size(), 1); // cached page is visible immediately
        QTRY_COMPARE(fixture.getCount(), 2);
        QCOMPARE(finished.size(), 1);         // but it is not a terminal result
        QVERIFY(sharing.isBrowsing());

        fixture.finishBrowse();
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished.at(1).at(0).toString(), QStringLiteral("second"));
        QCOMPARE(finished.at(1).at(1).toBool(), true);
        QCOMPARE(finished.at(1).at(3).toInt(), 2);
        QCOMPARE(finished.at(1).at(2).toList().size(), 2);
    }

    void concurrentDeleteRepliesKeepTheirServerIds()
    {
        clearCache();
        Settings settings;
        LibraryHttpFixture fixture;
        QVERIFY(fixture.listen());
        LibrarySharing sharing(&settings, nullptr, nullptr, fixture.apiBase());
        QSignalSpy finished(&sharing, &LibrarySharing::deleteRequestFinished);
        QVERIFY(finished.isValid());

        QVERIFY(sharing.deleteFromServerForRequest(QStringLiteral("request-a"), QStringLiteral("entry-a")));
        QVERIFY(sharing.deleteFromServerForRequest(QStringLiteral("request-b"), QStringLiteral("entry-b")));
        QTRY_COMPARE(fixture.pendingDeleteCount(), 2);
        QVERIFY(fixture.finishDelete(QStringLiteral("entry-b")));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("request-b"));
        QCOMPARE(finished.at(0).at(1).toString(), QStringLiteral("entry-b"));
        QCOMPARE(finished.at(0).at(2).toBool(), true);

        QVERIFY(fixture.finishDelete(QStringLiteral("entry-a")));
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished.at(1).at(0).toString(), QStringLiteral("request-a"));
        QCOMPARE(finished.at(1).at(1).toString(), QStringLiteral("entry-a"));
        QCOMPARE(finished.at(1).at(2).toBool(), true);
    }

    void requestSpecificUploadFailureKeepsItsRequestId()
    {
        Settings settings;
        LibrarySharing sharing(&settings, nullptr);
        QSignalSpy finished(&sharing, &LibrarySharing::uploadRequestFinished);
        QVERIFY(finished.isValid());

        QVERIFY(!sharing.uploadEntryForRequest(QStringLiteral("upload-1"),
                                                QStringLiteral("missing-entry")));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("upload-1"));
        QCOMPARE(finished.at(0).at(1).toString(), QStringLiteral("missing-entry"));
        QCOMPARE(finished.at(0).at(2).toBool(), false);
        QCOMPARE(finished.at(0).at(5).toString(), QStringLiteral("Entry not found"));
    }

    void requestSpecificDownloadRejectsMissingServerId()
    {
        Settings settings;
        LibrarySharing sharing(&settings, nullptr);
        QSignalSpy finished(&sharing, &LibrarySharing::downloadRequestFinished);
        QVERIFY(finished.isValid());

        QVERIFY(!sharing.downloadEntryForRequest(QStringLiteral("download-1"), QString()));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("download-1"));
        QCOMPARE(finished.at(0).at(1).toString(), QString());
        QCOMPARE(finished.at(0).at(2).toBool(), false);
        QCOMPARE(finished.at(0).at(5).toString(), QStringLiteral("invalidRequest"));
    }

    void communityThumbnailRoutesAcceptOnlyCanonicalEntryIds()
    {
        const QString entryId = QStringLiteral("c4a1dc4a-930b-4f8d-9491-6f3aa9c2bca2");
        QCOMPARE(CommunityThumbnail::sameOriginUrl(entryId),
                 QStringLiteral("/api/community/thumbnail?id=c4a1dc4a-930b-4f8d-9491-6f3aa9c2bca2"));
        QCOMPARE(CommunityThumbnail::sameOriginUrl(entryId, true),
                 QStringLiteral("/api/community/thumbnail?id=c4a1dc4a-930b-4f8d-9491-6f3aa9c2bca2&compact=1"));
        QVERIFY(CommunityThumbnail::sameOriginUrl(QStringLiteral("../../etc/passwd")).isEmpty());
        QVERIFY(CommunityThumbnail::sameOriginUrl(
                    QStringLiteral("https://evil.example/image.png")).isEmpty());
        QCOMPARE(CommunityThumbnail::resolvedUrl(QUrl(QStringLiteral("http://127.0.0.1:8888/wasm/")), entryId),
                 QStringLiteral("http://127.0.0.1:8888/api/community/thumbnail?id=") + entryId);
        QVERIFY(CommunityThumbnail::resolvedUrl(QUrl(), entryId).isEmpty());
        QVERIFY(CommunityThumbnail::resolvedUrl(QUrl(QStringLiteral("qrc:/")), entryId).isEmpty());
        QVERIFY(CommunityThumbnail::resolvedUrl(QUrl(QStringLiteral("https://host.example")),
                                                QStringLiteral("../bad")).isEmpty());
        QVERIFY(CommunityThumbnail::hasCanonicalThumbnail(
            entryId, QStringLiteral("https://decenza.coffee/library/thumbnails/")
                        + entryId + QStringLiteral("_full.png"), false));
        QVERIFY(!CommunityThumbnail::hasCanonicalThumbnail(
            entryId, QStringLiteral("https://evil.example/library/thumbnails/")
                        + entryId + QStringLiteral("_full.png"), false));

        Settings settings;
        LibrarySharing sharing(&settings, nullptr);
        QSignalSpy finished(&sharing, &LibrarySharing::communityThumbnailRequestFinished);
        QVERIFY(finished.isValid());
        QVERIFY(!sharing.fetchCommunityThumbnailForRequest(QStringLiteral("bad-id"),
                                                            QStringLiteral("../../etc/passwd")));
        QCOMPARE(finished.count(), 0);
    }

    void communityThumbnailFetchingValidatesPngAndBoundsResponseBytes()
    {
        Settings settings;
        LibraryHttpFixture fixture;
        QVERIFY(fixture.listen());
        LibrarySharing sharing(&settings, nullptr, nullptr, fixture.apiBase());
        QSignalSpy finished(&sharing, &LibrarySharing::communityThumbnailRequestFinished);
        QVERIFY(finished.isValid());
        const QString entryId = QStringLiteral("c4a1dc4a-930b-4f8d-9491-6f3aa9c2bca2");
        const QByteArray png = QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/HioAAAAASUVORK5CYII=");

        fixture.setThumbnailResponse(png);
        QVERIFY(sharing.fetchCommunityThumbnailForRequest(QStringLiteral("thumb-good"), entryId, true));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("thumb-good"));
        QVERIFY(finished.at(0).at(1).toBool());
        QCOMPARE(finished.at(0).at(2).toByteArray(), png);
        QVERIFY(fixture.lastThumbnailPath().endsWith("_compact.png"));

        fixture.setThumbnailResponse(QByteArray(1024 * 1024 + 1, 'x'));
        QVERIFY(sharing.fetchCommunityThumbnailForRequest(QStringLiteral("thumb-large"), entryId));
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(!finished.at(1).at(1).toBool());
        QCOMPARE(finished.at(1).at(3).toString(), QStringLiteral("thumbnailTooLarge"));

        fixture.setThumbnailResponse(png, 200, QByteArrayLiteral("text/html"));
        QVERIFY(sharing.fetchCommunityThumbnailForRequest(QStringLiteral("thumb-wrong-type"), entryId));
        QTRY_COMPARE(finished.size(), 3);
        QVERIFY(!finished.at(2).at(1).toBool());
        QCOMPARE(finished.at(2).at(3).toString(), QStringLiteral("thumbnailUnavailable"));
        QCOMPARE(fixture.thumbnailRequestCount(), 3);
    }

    void libraryServiceValidatesArgumentsAndChecksLongLayoutBaseline()
    {
        Settings settings;
        WidgetLibrary library(&settings);
        NativeLibraryServiceSource source(&library, nullptr, &settings);
        QSignalSpy completed(&source, &NativeLibraryServiceSource::operationCompleted);
        QVERIFY(completed.isValid());

        source.execute(QStringLiteral("unknown-key"), QStringLiteral("getEntry"),
                       {{QStringLiteral("entryId"), QStringLiteral("safe-id")},
                        {QStringLiteral("unexpected"), true}});
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("invalidRequest"));

        source.execute(QStringLiteral("oversized-image"), QStringLiteral("upload"),
                       {{QStringLiteral("entryId"), QStringLiteral("safe-id")},
                        {QStringLiteral("thumbnailFullPng"),
                         QImage(1025, 1, QImage::Format_ARGB32)}});
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("invalidRequest"));

        QString baseline = QStringLiteral("{\"zones\":{\"centerStatus\":[]},\"padding\":\"");
        baseline += QString(400, QLatin1Char('x')) + QStringLiteral("\"}");
        const QString previousLayout = settings.network()->layoutConfiguration();
        settings.network()->setLayoutConfiguration(baseline);
        baseline = settings.network()->layoutConfiguration();
        const QString entryJson = QStringLiteral(
            "{\"id\":\"service-layout-test\",\"type\":\"layout\","
            "\"createdAt\":\"2026-10-22T00:00:00Z\",\"data\":{"
            "\"layout\":{\"zones\":{\"centerStatus\":[]}}}}");
        QCOMPARE(library.importEntry(entryJson.toUtf8()), QStringLiteral("service-layout-test"));

        source.execute(QStringLiteral("export-layout"), QStringLiteral("exportEntry"),
                       {{QStringLiteral("entryId"), QStringLiteral("service-layout-test")}});
        QCOMPARE(completed.size(), 1);
        const QList<QVariant> exported = completed.takeFirst();
        QVERIFY(exported.at(2).toBool());
        const QString exportedJson = exported.at(3).toMap().value(QStringLiteral("json")).toString();
        QCOMPARE(QJsonDocument::fromJson(exportedJson.toUtf8()).object().value("data"),
                 QJsonDocument::fromJson(entryJson.toUtf8()).object().value("data"));
        source.execute(QStringLiteral("import-layout"), QStringLiteral("importEntry"),
                       {{QStringLiteral("json"), exportedJson}});
        QCOMPARE(completed.size(), 1);
        const QList<QVariant> imported = completed.takeFirst();
        QVERIFY(imported.at(2).toBool());
        QCOMPARE(imported.at(3).toMap().value(QStringLiteral("entryId")).toString(),
                 QStringLiteral("service-layout-test"));
        QCOMPARE(settings.network()->layoutConfiguration(), baseline);

        QImage thumbnail(12, 12, QImage::Format_ARGB32);
        thumbnail.fill(Qt::green);
        library.saveThumbnail(QStringLiteral("service-layout-test"), thumbnail);
        library.saveThumbnailCompact(QStringLiteral("service-layout-test"), thumbnail);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<LibraryStateSourceAPI>(&source));
        QRemoteObjectNode client;
        QVERIFY(client.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        LibraryService::setPublishedLocalLibrary(&library);
        LibraryService* service = LibraryService::create(&engine);
        service->setNode(&client);
        QTRY_VERIFY(service->ready());
        QVariantMap projected;
        for (const QVariant& value : service->localEntries()) {
            const QVariantMap entry = value.toMap();
            if (entry.value(QStringLiteral("id")).toString() == QStringLiteral("service-layout-test"))
                projected = entry;
        }
        QCOMPARE(projected.value(QStringLiteral("thumbnailFullUrl")).toString(),
                 QUrl::fromLocalFile(library.thumbnailPath(QStringLiteral("service-layout-test"))).toString());
        QCOMPARE(projected.value(QStringLiteral("thumbnailCompactUrl")).toString(),
                 QUrl::fromLocalFile(library.thumbnailCompactPath(QStringLiteral("service-layout-test"))).toString());

        source.execute(QStringLiteral("stale-layout"), QStringLiteral("applyLayout"),
                       {{QStringLiteral("entryId"), QStringLiteral("service-layout-test")},
                        {QStringLiteral("expectedLayout"), baseline + QStringLiteral(" ")}});
        QCOMPARE(completed.size(), 1);
        const QList<QVariant> stale = completed.takeFirst();
        QCOMPARE(stale.at(2).toBool(), false);
        QCOMPARE(stale.at(4).toString(), QStringLiteral("conflict"));

        source.execute(QStringLiteral("matching-long-layout"), QStringLiteral("applyLayout"),
                       {{QStringLiteral("entryId"), QStringLiteral("service-layout-test")},
                        {QStringLiteral("expectedLayout"), baseline}});
        QCOMPARE(completed.size(), 1);
        const QList<QVariant> applied = completed.takeFirst();
        QCOMPARE(applied.at(2).toBool(), true);
        QVERIFY(settings.network()->layoutConfiguration() != baseline);

        library.removeEntry(QStringLiteral("service-layout-test"));
        settings.network()->setLayoutConfiguration(previousLayout);
    }

    void reentrantBrowseUsesItsOwnTerminalSnapshot()
    {
        clearCache();
        Settings settings;
        LibraryHttpFixture fixture;
        QVERIFY(fixture.listen());
        LibrarySharing sharing(&settings, nullptr, nullptr, fixture.apiBase());
        QSignalSpy finished(&sharing, &LibrarySharing::browseRequestFinished);
        QVERIFY(finished.isValid());

        QVERIFY(sharing.browseCommunityForRequest(QStringLiteral("seed")));
        QTRY_COMPARE(finished.size(), 1);

        bool launchedThird = false;
        connect(&sharing, &LibrarySharing::browsingChanged, &sharing, [&] {
            if (!sharing.isBrowsing() && !launchedThird) {
                launchedThird = sharing.browseCommunityForRequest(QStringLiteral("third"));
            }
        });
        QVERIFY(sharing.browseCommunityForRequest(QStringLiteral("second"), QStringLiteral("item")));
        QTRY_COMPARE(fixture.getCount(), 2);
        fixture.finishBrowse(QJsonObject{
            {"total", 25},
            {"entries", QJsonArray{QJsonObject{{"id", "filtered"}, {"createdAt", "2026-03-01"}}}}
        });

        QTRY_VERIFY(launchedThird);
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished.at(1).at(0).toString(), QStringLiteral("second"));
        QCOMPARE(finished.at(1).at(1).toBool(), true);
        QCOMPARE(finished.at(1).at(3).toInt(), 25);
        QCOMPARE(finished.at(1).at(2).toList().size(), 1);
        QCOMPARE(finished.at(1).at(2).toList().first().toMap().value("id").toString(),
                 QStringLiteral("filtered"));
        QVERIFY(sharing.isBrowsing());

        QTRY_COMPARE(fixture.getCount(), 3);
        fixture.finishBrowse();
        QTRY_COMPARE(finished.size(), 3);
        QCOMPARE(finished.at(2).at(0).toString(), QStringLiteral("third"));
    }

    void malformedBrowseResponsePreservesPriorSnapshot()
    {
        clearCache();
        Settings settings;
        LibraryHttpFixture fixture;
        QVERIFY(fixture.listen());
        LibrarySharing sharing(&settings, nullptr, nullptr, fixture.apiBase());
        QSignalSpy finished(&sharing, &LibrarySharing::browseRequestFinished);
        QVERIFY(finished.isValid());

        QVERIFY(sharing.browseCommunityForRequest(QStringLiteral("seed")));
        QTRY_COMPARE(finished.size(), 1);
        const QVariantList previousEntries = sharing.communityEntries();
        const int previousTotal = sharing.totalCommunityResults();

        QVERIFY(sharing.browseCommunityForRequest(QStringLiteral("malformed"), QStringLiteral("item")));
        QTRY_COMPARE(fixture.getCount(), 2);
        fixture.finishBrowseRaw(QByteArrayLiteral("not-json"));
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished.at(1).at(0).toString(), QStringLiteral("malformed"));
        QCOMPARE(finished.at(1).at(1).toBool(), false);
        QVERIFY(!finished.at(1).at(4).toString().isEmpty());
        QCOMPARE(sharing.communityEntries(), previousEntries);
        QCOMPARE(sharing.totalCommunityResults(), previousTotal);

        QVERIFY(sharing.browseCommunityForRequest(QStringLiteral("bad-schema"), QStringLiteral("item")));
        QTRY_COMPARE(fixture.getCount(), 3);
        fixture.finishBrowseRaw(QByteArrayLiteral("{\"total\":1,\"entries\":{}}"));
        QTRY_COMPARE(finished.size(), 3);
        QCOMPARE(finished.at(2).at(0).toString(), QStringLiteral("bad-schema"));
        QCOMPARE(finished.at(2).at(1).toBool(), false);
        QVERIFY(finished.at(2).at(4).toString().contains(QStringLiteral("entries")));
        QCOMPARE(sharing.communityEntries(), previousEntries);
        QCOMPARE(sharing.totalCommunityResults(), previousTotal);
    }
};

QTEST_GUILESS_MAIN(TestLibrarySharing)
#include "tst_librarysharing.moc"
