#include <QtTest>
#include <QQmlEngine>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QTcpServer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>
#include <memory>
#include <QUuid>
#include <utility>

#include "core/settings.h"
#include "history/coffeebagstorage.h"
#include "history/equipmentstorage.h"
#include "history/unifiedbeansearchmodel.h"
#include "network/beanbaseclient.h"
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

    void liveDetachCancelsOldSearchBeforeReentrantRefresh()
    {
        DelayedBeanSearchSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<BeanSearchStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        BeanSearchService::setPublishedNode(&node);
        auto* service = BeanSearchServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        service->setBagKind(QStringLiteral("tea"));
        service->refresh();
        QTRY_COMPARE(source.calls.size(), 1);
        const QString oldId = source.calls.first().requestId;
        bool attempted = false;
        bool readyDuringFailure = true;
        connect(service, &BeanSearchService::resultsChanged, service, [&] {
            if (!attempted && !service->searching()) {
                attempted = true;
                readyDuringFailure = service->ready();
                service->refresh();
            }
        });
        service->setNode(nullptr);
        QVERIFY(attempted);
        QVERIFY(!readyDuringFailure);
        QVERIFY(!service->searching());
        QTRY_VERIFY(source.cancelled.contains(oldId));
        QCOMPARE(source.calls.size(), 1);

        service->setNode(&node);
        QTRY_VERIFY(service->ready());
        QTRY_COMPARE(source.calls.size(), 2); // Intentional read-only resume is preserved.
        const QString resumedId = source.calls.last().requestId;
        QVERIFY(resumedId != oldId);
        source.reply(oldId, {QVariantMap{{QStringLiteral("roasterName"), QStringLiteral("stale")}}});
        source.reply(resumedId, {QVariantMap{{QStringLiteral("roasterName"), QStringLiteral("fresh")}}});
        QTRY_COMPARE(service->results().size(), 1);
        QCOMPARE(service->get(0).value(QStringLiteral("roasterName")).toString(), QStringLiteral("fresh"));
        QVERIFY(!service->searching());
    }

    void searchAdmissionCannotDispatchAfterDetach()
    {
        DelayedBeanSearchSource source;
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<BeanSearchStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        BeanSearchService::setPublishedNode(&node);
        QPointer<BeanSearchService> service = BeanSearchServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        service->setBagKind(QStringLiteral("tea"));
        bool changed = false;
        connect(service.data(), &BeanSearchService::searchingChanged, service.data(), [&] {
            if (changed || !service || !service->searching()) return;
            changed = true;
            service->setNode(nullptr);
        });
        service->refresh();
        QVERIFY(changed);
        QVERIFY(!service->ready());
        QVERIFY(!service->searching());
        QCoreApplication::processEvents();
        QCOMPARE(source.calls.size(), 0);
    }

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
            CoffeeBag coffee;
            coffee.roasterName = QStringLiteral("Coffee roaster");
            coffee.coffeeName = QStringLiteral("Coffee");
            QVERIFY(CoffeeBagStorage::insertBagStatic(db, coffee) > 0);
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

        // The tea scope excludes coffee inventory; the default scope retains
        // both bags, matching the canonical model's existing policy.
        service->setBagKind({});
        service->refresh();
        QTRY_COMPARE_WITH_TIMEOUT(service->results().size(), 2, 10000);
    }

    void cachedUnknownLinkCompletesOnceWithoutRetiringOtherProbes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("bean-search-unknown.db"));
        const QString unknownUrl = QStringLiteral("https://unknown.invalid/product");
        const QString heldUrl = QStringLiteral("https://held.invalid/product");
        const QString archivedUrl = QStringLiteral("https://archived.invalid/product");
        withRawDb(path, "bean_search_unknown", [&](QSqlDatabase& db) {
            createSearchTables(db);
            for (const QString& url : {unknownUrl, heldUrl, archivedUrl}) {
                CoffeeBag bag;
                bag.roasterName = url;
                bag.coffeeName = QStringLiteral("Coffee");
                bag.beanBaseData = QString::fromUtf8(QJsonDocument(
                    QJsonObject{{QStringLiteral("link"), url}}).toJson(QJsonDocument::Compact));
                QVERIFY(CoffeeBagStorage::insertBagStatic(db, bag) > 0);
            }
        });

        CoffeeBagStorage storage;
        storage.initialize(path);
        Settings settings;
        NativeBeanSearchServiceSource source(&storage, &settings, path);
        QSignalSpy results(&source, &BeanSearchStateSimpleSource::resultsReady);
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        // Empty coffee search is the shipping Manual entry path: inventory
        // URLs are probed, but no canonical search is needed.
        source.search(requestId, {}, {}, {});
        auto* client = source.findChild<BeanBaseClient*>();
        QVERIFY(client);
        // Hold both probes while the actual inventory/history lanes arrive.
        // This prevents network requests and isolates delivery of the queued
        // unknown answer below from the fixture's asynchronous setup.
        client->m_linkStateInFlight.insert(unknownUrl);
        client->m_linkStateInFlight.insert(heldUrl);
        client->m_linkStateByUrl.insert(archivedUrl, QStringLiteral("archived"));
        QSignalSpy linkReplies(client, &BeanBaseClient::linkStateResolved);
        QTRY_VERIFY_WITH_TIMEOUT(!results.isEmpty(), 5000);
        QCOMPARE(results.size(), 1);
        client->m_linkStateInFlight.remove(unknownUrl);
        client->m_linkStateUnresolvable.insert(unknownUrl);
        client->probeLinkState(unknownUrl);
        // Each explicit turn stops at its original posted-event boundary
        // (Qt 6.12 qcoreapplication.cpp:1844,1882-1885), even with the old loop.
        QCoreApplication::sendPostedEvents(client, QEvent::MetaCall);
        QCOMPARE(results.size(), 2); // Initial rows, then one unknown answer.
        QCoreApplication::sendPostedEvents(client, QEvent::MetaCall);
        QCOMPARE(results.size(), 2); // No self-sustaining unknown callback.
        QCOMPARE(linkReplies.size(), 1);
        QCOMPARE(linkReplies.first().at(0).toString(), unknownUrl);
        QCOMPARE(linkReplies.first().at(1).toString(), QStringLiteral("unknown"));
        QVERIFY(client->m_linkStateInFlight.contains(heldUrl));
        for (const auto& result : std::as_const(results)) {
            QCOMPARE(result.at(0).toString(), requestId);
            QVERIFY(result.at(2).toBool());
            QVERIFY(!result.at(3).toBool());
            QVERIFY(result.at(4).toString().isEmpty());
        }
        const QVariantList initialRows = results.first().at(1).toList();
        QCOMPARE(initialRows.size(), 3);
        QCOMPARE(UnifiedBeanSearchModel::rowLink(initialRows.last().toMap()), archivedUrl);
        QVERIFY(source.findChildren<QNetworkReply*>().isEmpty());

        client->finishLinkStateProbe(heldUrl, QStringLiteral("live"));
        QCOMPARE(results.size(), 3);
        QCOMPARE(linkReplies.size(), 2);
        QVERIFY(!results.last().at(2).toBool());
        QVERIFY(results.last().at(3).toBool());
        // Unknown still ranks with live, retaining the original stable order.
        QCOMPARE(results.last().at(1).toList(), initialRows);
        QVERIFY(source.m_queries.isEmpty());
        QCoreApplication::sendPostedEvents(client, QEvent::MetaCall);
        QCOMPARE(results.size(), 3);
        QCOMPARE(linkReplies.size(), 2);
        QVERIFY(source.findChildren<QNetworkReply*>().isEmpty());
    }

    void partialResultObserverCanCancelBeforeLinkProbes_data()
    {
        QTest::addColumn<bool>("deleteSource");
        QTest::newRow("cancel-query") << false;
        QTest::newRow("delete-source") << true;
    }

    void partialResultObserverCanCancelBeforeLinkProbes()
    {
        QFETCH(bool, deleteSource);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("bean-search-cancel-probe.db"));
        const QString url = QStringLiteral("https://unknown.invalid/cancelled");
        withRawDb(path, "bean_search_cancel_probe", [&](QSqlDatabase& db) {
            createSearchTables(db);
            CoffeeBag bag;
            bag.roasterName = QStringLiteral("Cancelled roaster");
            bag.coffeeName = QStringLiteral("Coffee");
            bag.beanBaseData = QString::fromUtf8(QJsonDocument(
                QJsonObject{{QStringLiteral("link"), url}}).toJson(QJsonDocument::Compact));
            QVERIFY(CoffeeBagStorage::insertBagStatic(db, bag) > 0);
        });
        CoffeeBagStorage storage;
        storage.initialize(path);
        Settings settings;
        auto source = std::make_unique<NativeBeanSearchServiceSource>(&storage, &settings, path);
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        int published = 0;
        QPointer<BeanBaseClient> client;
        connect(source.get(), &BeanSearchStateSimpleSource::resultsReady, this,
                [&](const QString& id, const QVariantList&, bool pending, bool complete,
                    const QString&) {
            QCOMPARE(id, requestId);
            QVERIFY(pending);
            QVERIFY(!complete);
            ++published;
            if (deleteSource) {
                source.reset();
            } else {
                source->cancelSearch(id);
                // Keep the canceled job alive until dispatch is inspected:
                // deleting it first could hide an incorrectly queued probe.
                QCoreApplication::removePostedEvents(client->parent(), QEvent::DeferredDelete);
            }
        });
        source->search(requestId, {}, {}, {});
        client = source->findChild<BeanBaseClient*>();
        QVERIFY(client);
        client->m_linkStateUnresolvable.insert(url);
        int linkReplies = 0;
        connect(client, &BeanBaseClient::linkStateResolved, this, [&] { ++linkReplies; });
        QTRY_COMPARE_WITH_TIMEOUT(published, 1, 5000);
        if (deleteSource) {
            QVERIFY(!source);
            QVERIFY(!client);
        } else {
            QVERIFY(source->m_queries.isEmpty());
            if (client)
                QCoreApplication::sendPostedEvents(client, QEvent::MetaCall);
            QVERIFY(source->findChildren<QNetworkReply*>().isEmpty());
        }
        QCOMPARE(linkReplies, 0); // Cancellation precedes any probe dispatch.
        QCOMPARE(published, 1);
        QTRY_VERIFY_WITH_TIMEOUT(storage.isDbWorkIdle(), 5000);
        if (source)
            QTRY_COMPARE_WITH_TIMEOUT(source->m_activeHistoryReads, 0, 5000);
    }

    void linkReplyObserverCanDeleteSource_data()
    {
        QTest::addColumn<bool>("terminalReply");
        QTest::newRow("later-partial-result") << false;
        QTest::newRow("terminal-result") << true;
    }

    void linkReplyObserverCanDeleteSource()
    {
        QFETCH(bool, terminalReply);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("bean-search-delete-on-link.db"));
        const QString firstUrl = QStringLiteral("https://first.invalid/product");
        const QString secondUrl = QStringLiteral("https://second.invalid/product");
        withRawDb(path, "bean_search_delete_on_link", [&](QSqlDatabase& db) {
            createSearchTables(db);
            for (const QString& url : {firstUrl, secondUrl}) {
                CoffeeBag bag;
                bag.roasterName = url;
                bag.coffeeName = QStringLiteral("Coffee");
                bag.beanBaseData = QString::fromUtf8(QJsonDocument(
                    QJsonObject{{QStringLiteral("link"), url}}).toJson(QJsonDocument::Compact));
                QVERIFY(CoffeeBagStorage::insertBagStatic(db, bag) > 0);
            }
        });
        CoffeeBagStorage storage;
        storage.initialize(path);
        Settings settings;
        auto source = std::make_unique<NativeBeanSearchServiceSource>(&storage, &settings, path);
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        int published = 0;
        connect(source.get(), &BeanSearchStateSimpleSource::resultsReady, this,
                [&](const QString& id, const QVariantList& rows, bool pending, bool complete,
                    const QString& error) {
            QCOMPARE(id, requestId);
            QCOMPARE(rows.size(), 2);
            QVERIFY(error.isEmpty());
            ++published;
            if (published == 1) {
                QVERIFY(pending);
                QVERIFY(!complete);
            } else {
                QCOMPARE(published, 2);
                QCOMPARE(pending, !terminalReply);
                QCOMPARE(complete, terminalReply);
                source.reset();
            }
        });
        source->search(requestId, {}, {}, {});
        QPointer<BeanBaseClient> client = source->findChild<BeanBaseClient*>();
        QVERIFY(client);
        client->m_linkStateInFlight.insert(firstUrl);
        if (terminalReply)
            client->m_linkStateByUrl.insert(secondUrl, QStringLiteral("live"));
        else
            client->m_linkStateInFlight.insert(secondUrl);
        QTRY_COMPARE_WITH_TIMEOUT(published, 1, 5000);
        QVERIFY(source->findChildren<QNetworkReply*>().isEmpty());

        // The real producer emits into the source, whose result observer
        // deletes the complete source/job/client tree before it returns.
        client->finishLinkStateProbe(firstUrl, QStringLiteral("live"));
        QCOMPARE(published, 2);
        QVERIFY(!source);
        QVERIFY(!client);
        QTRY_VERIFY_WITH_TIMEOUT(storage.isDbWorkIdle(), 5000);
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
        // Delete the jobs before dispatching any queued history completion.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

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
        // Cancellation must neither publish those rows nor strand worker capacity.
        QTRY_COMPARE_WITH_TIMEOUT(source.m_activeHistoryReads, 0, 5000);
        QCOMPARE(results.size(), 1);

        const QString replacementId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.search(replacementId, {}, QStringLiteral("tea replacement"), QStringLiteral("tea"));
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 5000);
        QCOMPARE(results.last().at(0).toString(), replacementId);
        QVERIFY(results.last().at(3).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(source.m_activeHistoryReads, 0, 5000);
    }

    void destroyedSourceDropsFinishedHistoryCallbacks()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("bean-search-destroyed.db"));
        withRawDb(path, "bean_search_destroyed", [&](QSqlDatabase& db) {
            createSearchTables(db);
        });

        Settings settings;
        auto source = std::make_unique<NativeBeanSearchServiceSource>(nullptr, &settings, path);
        int published = 0;
        connect(source.get(), &BeanSearchStateSimpleSource::resultsReady, this,
                [&published] { ++published; });
        source->search(QUuid::createUuid().toString(QUuid::WithoutBraces), {},
                       {}, QStringLiteral("tea"));
        QCOMPARE(source->m_activeHistoryReads, 1);
        source.reset();
        // Join the real worker without delivering its queued completion first.
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QCoreApplication::sendPostedEvents(QCoreApplication::instance(), QEvent::MetaCall);
        QCOMPARE(published, 0);
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
