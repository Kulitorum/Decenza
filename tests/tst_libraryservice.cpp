#include "remote/libraryservice.h"
#include "rep_libraryservice_source.h"
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QScopeGuard>

#include "core/settings.h"
#include "core/settings_network.h"
#include "core/settings_theme.h"
#include "core/widgetlibrary.h"
#include "remote/libraryservicesource.h"

#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
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

class TestLibraryService final : public QObject
{
    Q_OBJECT
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

    void libraryServiceValidatesArgumentsAndChecksLongLayoutBaseline()
    {
        Settings settings;
        WidgetLibrary library(&settings);
        NativeLibraryServiceSource source(&library, &settings);
        QSignalSpy completed(&source, &NativeLibraryServiceSource::operationCompleted);
        QVERIFY(completed.isValid());

        source.execute(QStringLiteral("unknown-key"), QStringLiteral("getEntry"),
                       {{QStringLiteral("entryId"), QStringLiteral("safe-id")},
                        {QStringLiteral("unexpected"), true}});
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.takeFirst().at(4).toString(), QStringLiteral("invalidRequest"));

        // The community library was removed; its operations are unknown, not stubs.
        source.execute(QStringLiteral("retired-operation"), QStringLiteral("browseCommunity"), {});
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

        settings.theme()->setGlassChrome(true);
        settings.theme()->setBackgroundPreset(QStringLiteral("espresso"));
        settings.theme()->setBackgroundPattern(QStringLiteral("dots"));
        const QString themeId = library.addCurrentTheme(QStringLiteral("Library theme test"));
        QVERIFY(!themeId.isEmpty());
        const auto cleanupTheme = qScopeGuard([&] {
            library.removeEntry(themeId);
            settings.theme()->deleteUserTheme(QStringLiteral("Library theme test"));
        });
        const auto savedTheme = QJsonDocument::fromJson(library.exportEntry(themeId)).object()
            .value("data").toObject().value("theme").toObject();
        QCOMPARE(savedTheme.value("appearance").toObject(), settings.theme()->savedAppearance());
        settings.theme()->setGlassChrome(false);
        settings.theme()->clearBackground();
        QVERIFY(library.applyThemeEntry(themeId));
        QVERIFY(settings.theme()->glassChrome());
        QCOMPARE(settings.theme()->backgroundPreset(), QStringLiteral("espresso"));
        QCOMPARE(settings.theme()->backgroundPattern(), QStringLiteral("dots"));
    }
};

QTEST_GUILESS_MAIN(TestLibraryService)
#include "tst_libraryservice.moc"
