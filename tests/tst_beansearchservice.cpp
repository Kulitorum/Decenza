#include <QtTest>
#include <QQmlEngine>
#include <QHostAddress>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QTcpServer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <memory>
#include <QUuid>
#include <utility>

#include "core/settings.h"
#include "history/coffeebagstorage.h"
#include "history/equipmentstorage.h"
#include "history/unifiedbeansearchmodel.h"
#include "remote/beansearchservice_qml.h"
#include "remote/beansearchservicesource.h"
#include "rep_beansearchservice_replica.h"
#include "shotrowfixtures.h"

using ShotRowFixtures::withRawDb;

namespace {
void createSearchTables(QSqlDatabase& db)
{
    QSqlQuery query(db);
    QVERIFY(query.exec(R"(
        CREATE TABLE shots (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            bean_brand TEXT, bean_type TEXT, beanbase_id TEXT, beanbase_json TEXT,
            roast_level TEXT, grinder_setting TEXT, dose_weight REAL, yield_override REAL,
            timestamp INTEGER, yield_mode TEXT, yield_anchor_value REAL, rpm INTEGER,
            bag_id INTEGER, equipment_id INTEGER
        )
    )"));
    QVERIFY(EquipmentStorage::ensureTablesStatic(db));
    QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
}

qint64 insertTeaBag(QSqlDatabase& db, const QString& roaster, const QString& name)
{
    CoffeeBag bag;
    bag.kind = QStringLiteral("tea");
    bag.roasterName = roaster;
    bag.coffeeName = name;
    return CoffeeBagStorage::insertBagStatic(db, bag);
}

class DelayedBeanSearchSource final : public BeanSearchStateSimpleSource
{
public:
    struct Call { QString requestId; QString cancelRequestId; QString query; QString kind; };
    QList<Call> calls;
    QStringList cancelled;

    void search(QString requestId, QString cancelRequestId,
                QString query, QString bagKind) override
    {
        if (!cancelRequestId.isEmpty())
            cancelled.append(cancelRequestId);
        calls.append({std::move(requestId), std::move(cancelRequestId),
                      std::move(query), std::move(bagKind)});
    }
    void cancelSearch(QString requestId) override { cancelled.append(std::move(requestId)); }
    void reply(const QString& requestId, const QVariantList& rows)
    {
        emit resultsReady(requestId, rows, false, true, {});
    }
};
}

class tst_BeanSearchService final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void cleanup() { BeanSearchService::setPublishedNode(nullptr); }

    void tcpSearchesAreCorrelatedAndSupersededQueryIsCancelled()
    {
        DelayedBeanSearchSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<BeanSearchStateSourceAPI>(&source));

        QRemoteObjectNode firstNode;
        QRemoteObjectNode secondNode;
        QVERIFY(firstNode.connectToNode(host.hostUrl()));
        QVERIFY(secondNode.connectToNode(host.hostUrl()));
        QQmlEngine firstEngine;
        QQmlEngine secondEngine;
        auto* first = BeanSearchServiceForeign::create(&firstEngine, &firstEngine);
        auto* second = BeanSearchServiceForeign::create(&secondEngine, &secondEngine);
        first->setNode(&firstNode);
        second->setNode(&secondNode);

        // Wait for the actual generated TCP replicas before issuing requests.
        auto* firstReady = firstNode.acquire<BeanSearchStateReplica>();
        auto* secondReady = secondNode.acquire<BeanSearchStateReplica>();
        QTRY_VERIFY_WITH_TIMEOUT(firstReady->isReplicaValid() && secondReady->isReplicaValid(), 5000);

        // Tea mode suppresses Visualizer canonical search. These controlled
        // replies prove request correlation without making external requests.
        first->setBagKind(QStringLiteral("tea"));
        QCOMPARE(source.calls.size(), 0); // Filter initialization alone is not a search.
        first->refresh();
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 1, 5000);
        const QString supersededId = source.calls.at(0).requestId;
        first->setQuery(QStringLiteral("Alpha"));
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 2, 5000);
        QCOMPARE(source.calls.at(1).cancelRequestId, supersededId);
        QTRY_VERIFY_WITH_TIMEOUT(source.cancelled.contains(supersededId), 5000);
        const QString firstId = source.calls.at(1).requestId;
        second->setBagKind(QStringLiteral("tea"));
        second->refresh();
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 3, 5000);
        const QString secondId = source.calls.at(2).requestId;

        const QVariantList staleRows{QVariantMap{{QStringLiteral("roasterName"), QStringLiteral("stale")}}};
        source.reply(supersededId, staleRows);
        QCoreApplication::processEvents();
        QVERIFY(first->results().isEmpty());
        source.reply(firstId, {QVariantMap{{QStringLiteral("roasterName"), QStringLiteral("Alpha Tea")}}});
        source.reply(secondId, {QVariantMap{{QStringLiteral("roasterName"), QStringLiteral("Beta Tea")}}});
        QTRY_COMPARE_WITH_TIMEOUT(first->results().size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(second->results().size(), 1, 5000);
        QCOMPARE(first->get(0).value(QStringLiteral("roasterName")).toString(), QStringLiteral("Alpha Tea"));
        QCOMPARE(second->get(0).value(QStringLiteral("roasterName")).toString(), QStringLiteral("Beta Tea"));
        QVERIFY(!first->searching());
        QVERIFY(!second->searching());
    }

    void requestedSearchWaitsForReplicaAndResumesAfterReconnect()
    {
        QTcpServer portProbe;
        QVERIFY(portProbe.listen(QHostAddress::LocalHost, 0));
        const quint16 port = portProbe.serverPort();
        portProbe.close();
        const QUrl address(QStringLiteral("tcp://127.0.0.1:%1").arg(port));

        DelayedBeanSearchSource firstSource;
        auto firstHost = std::make_unique<QRemoteObjectHost>();
        QVERIFY(firstHost->setHostUrl(address));
        QVERIFY(firstHost->enableRemoting<BeanSearchStateSourceAPI>(&firstSource));

        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(address));
        QQmlEngine engine;
        auto* service = BeanSearchServiceForeign::create(&engine, &engine);

        // A filter set before the search is only configuration. A user query
        // entered before the replica is ready is remembered and sent once the
        // remote source becomes available.
        service->setBagKind(QStringLiteral("tea"));
        QCOMPARE(firstSource.calls.size(), 0);
        service->setQuery(QStringLiteral("Jasmine"));
        QVERIFY(!service->ready());
        QCOMPARE(firstSource.calls.size(), 0);
        service->setNode(&node);
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(firstSource.calls.size(), 1, 5000);
        const QString firstId = firstSource.calls.first().requestId;
        QCOMPARE(firstSource.calls.first().query, QStringLiteral("Jasmine"));
        QCOMPARE(firstSource.calls.first().kind, QStringLiteral("tea"));

        // Losing the transport ends the visible request. Rejoining starts one
        // fresh read-only search for the same user-requested query; a delayed
        // result from the old connection cannot replace its rows.
        firstHost.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!service->ready(), 5000);
        QVERIFY(!service->searching());
        DelayedBeanSearchSource secondSource;
        QRemoteObjectHost secondHost;
        QVERIFY(secondHost.setHostUrl(address));
        QVERIFY(secondHost.enableRemoting<BeanSearchStateSourceAPI>(&secondSource));
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(secondSource.calls.size(), 1, 5000);
        const QString secondId = secondSource.calls.first().requestId;
        QVERIFY(firstId != secondId);
        QCOMPARE(secondSource.calls.first().query, QStringLiteral("Jasmine"));
        QCOMPARE(secondSource.calls.first().kind, QStringLiteral("tea"));

        firstSource.reply(firstId, {QVariantMap{{QStringLiteral("roasterName"), QStringLiteral("stale")}}});
        QCoreApplication::processEvents();
        QVERIFY(service->results().isEmpty());
        secondSource.reply(secondId, {QVariantMap{{QStringLiteral("roasterName"), QStringLiteral("Jasmine Tea")}}});
        QTRY_COMPARE_WITH_TIMEOUT(service->results().size(), 1, 5000);
        QCOMPARE(service->get(0).value(QStringLiteral("roasterName")).toString(),
                 QStringLiteral("Jasmine Tea"));
    }

    void nativeInProcessSearchUsesSharedMergeRules()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("bean-search-inprocess.db"));
        qint64 teaId = 0;
        withRawDb(path, "bean_search_service_inprocess", [&](QSqlDatabase& db) {
            createSearchTables(db);
            teaId = insertTeaBag(db, QStringLiteral("Garden"), QStringLiteral("Jasmine"));
        });
        QVERIFY(teaId > 0);

        CoffeeBagStorage storage;
        storage.initialize(path);
        Settings settings;
        NativeBeanSearchServiceSource source(&storage, &settings, path);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("bean-search-test:///native")),
                               QRemoteObjectHost::AllowExternalRegistration));
        QVERIFY(host.enableRemoting<BeanSearchStateSourceAPI>(&source));
        BeanSearchService::setPublishedNode(&host);

        QQmlEngine engine;
        auto* service = BeanSearchServiceForeign::create(&engine, &engine);
        auto* initialized = host.acquire<BeanSearchStateReplica>();
        QTRY_VERIFY_WITH_TIMEOUT(initialized->isInitialized(), 5000);
        service->setBagKind(QStringLiteral("tea"));
        service->refresh();
        QTRY_COMPARE_WITH_TIMEOUT(service->results().size(), 1, 10000);
        const QVariantMap actual = service->get(0);
        QCOMPARE(actual.value(QStringLiteral("id")).toLongLong(), teaId);
        QCOMPARE(actual.value(QStringLiteral("tier")).toInt(),
                 static_cast<int>(UnifiedBeanSearchModel::Tier::Inventory));
        QCOMPARE(actual.value(QStringLiteral("sources")).toString(), QStringLiteral("inventory"));
        QCOMPARE(actual.value(QStringLiteral("roasterName")).toString(), QStringLiteral("Garden"));
    }

    void cancelledHistoryWorkersRemainInsideConcurrentBound()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("bean-search-worker-cap.db"));
        withRawDb(path, "bean_search_worker_cap", [&](QSqlDatabase& db) {
            createSearchTables(db);
        });

        CoffeeBagStorage storage;
        storage.initialize(path);
        Settings settings;
        NativeBeanSearchServiceSource source(&storage, &settings, path);
        QSignalSpy results(&source, &BeanSearchStateSimpleSource::resultsReady);
        QStringList cancelledIds;
        for (int index = 0; index < 12; ++index) {
            const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            cancelledIds.append(id);
            source.search(id, {}, QStringLiteral("tea %1").arg(index), QStringLiteral("tea"));
        }
        for (const QString& id : std::as_const(cancelledIds))
            source.cancelSearch(id);

        // Cancellation drops each visible query immediately, while its SQLite
        // worker remains active until it completes. A replacement must receive
        // an explicit busy result instead of starting an uncounted 13th worker.
        const QString rejectedId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.search(rejectedId, {}, QStringLiteral("tea replacement"), QStringLiteral("tea"));
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().at(0).toString(), rejectedId);
        QVERIFY(results.first().at(1).toList().isEmpty());
        QVERIFY(results.first().at(3).toBool());
        QCOMPARE(results.first().at(4).toString(), QStringLiteral("historyBusy"));

        // Each accepted query also queued an inventory read on the storage's
        // single worker. Let those canceled requests drain before storage is
        // destroyed so the test doesn't intentionally discard worker jobs.
        QTRY_VERIFY_WITH_TIMEOUT(storage.isDbWorkIdle(), 5000);
    }

    void oversizedMergedResultsFailWithoutTruncation()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("bean-search-payload-cap.db"));
        withRawDb(path, "bean_search_payload_cap", [&](QSqlDatabase& db) {
            createSearchTables(db);
            CoffeeBag bag;
            bag.kind = QStringLiteral("tea");
            bag.roasterName = QStringLiteral("Bulk roaster");
            bag.coffeeName = QStringLiteral("Bulk tea");
            bag.notes = QString(1600 * 1024, QLatin1Char('x'));
            QVERIFY(CoffeeBagStorage::insertBagStatic(db, bag) > 0);
        });

        CoffeeBagStorage storage;
        storage.initialize(path);
        Settings settings;
        NativeBeanSearchServiceSource source(&storage, &settings, path);
        QSignalSpy results(&source, &BeanSearchStateSimpleSource::resultsReady);
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.search(requestId, {}, QStringLiteral("bulk"), QStringLiteral("tea"));
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 10000);
        QCOMPARE(results.first().at(0).toString(), requestId);
        QVERIFY(results.first().at(1).toList().isEmpty());
        QVERIFY(!results.first().at(2).toBool());
        QVERIFY(results.first().at(3).toBool());
        QCOMPARE(results.first().at(4).toString(), QStringLiteral("resultTooLarge"));
    }
};

QTEST_MAIN(tst_BeanSearchService)
#include "tst_beansearchservice.moc"
