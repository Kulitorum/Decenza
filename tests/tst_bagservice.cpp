#include <QtTest>
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QStringList>
#include <QSettings>
#include <QDate>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <memory>
#include <algorithm>

#include "core/settings.h"
#include "core/settings_dye.h"
#include "history/coffeebagstorage.h"
#include "remote/bagservice_qml.h"
#include "remote/bagservicesource.h"
#include "rep_bagservice_source.h"

namespace {
bool prepareBagDatabase(const QString& path, qint64* linkedBagId)
{
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                     QStringLiteral("bag_service_fixture"));
        db.setDatabaseName(path);
        ok = db.open();
        if (ok) {
            QSqlQuery shots(db);
            ok = shots.exec(QStringLiteral("CREATE TABLE shots (id INTEGER PRIMARY KEY, bag_id INTEGER)"))
                && CoffeeBagStorage::ensureTableStatic(db);
            if (ok && linkedBagId) {
                CoffeeBag bag;
                bag.roasterName = QStringLiteral("Fixture roaster");
                bag.coffeeName = QStringLiteral("Linked bag");
                *linkedBagId = CoffeeBagStorage::insertBagStatic(db, bag);
                QSqlQuery shot(db);
                shot.prepare(QStringLiteral("INSERT INTO shots (bag_id) VALUES (:bagId)"));
                shot.bindValue(QStringLiteral(":bagId"), *linkedBagId);
                ok = *linkedBagId > 0 && shot.exec();
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("bag_service_fixture"));
    return ok;
}

void clearDyeSettings()
{
    QSettings settings(Settings::testQSettingsPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("dye"));
    settings.sync();
}
} // namespace

class PendingBagSource final : public BagStateSimpleSource
{
public:
    using BagStateSimpleSource::setInventoryReady;

    struct Call {
        QString id;
        QString operation;
        qint64 bagId;
        QVariantMap expectedEditable;
        qint64 expectedActiveBagId = -1;
    };
    QList<Call> calls;
    QStringList finishedCountRequestIds;

    void refresh(QString) override {}
    void refreshFinished(QString) override {}
    void refreshFinishedCount(QString requestId) override
    { finishedCountRequestIds.append(std::move(requestId)); }
    void requestBag(QString, qint64) override {}
    void execute(QString requestId, QString operation, qint64 bagId,
                 QVariantMap, bool, QVariantMap expectedEditable,
                 qint64 expectedActiveBagId) override
    {
        calls.append({std::move(requestId), std::move(operation), bagId,
                      std::move(expectedEditable), expectedActiveBagId});
    }
};

class tst_BagService final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void cleanup() { BagService::setPublishedNode(nullptr); }

    void activationWaitsForCorrelatedCompletionAndNeverReplays()
    {
        PendingBagSource source;
        source.setInventoryReady(true);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<BagStateSourceAPI>(&source));

        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        auto* service = BagServiceForeign::create(&engine, &engine);
        service->setNode(&node);
        const std::unique_ptr<BagStateReplica> replica(node.acquire<BagStateReplica>());
        QTRY_VERIFY_WITH_TIMEOUT(replica->isReplicaValid() && service->ready(), 5000);
        QSignalSpy activated(service, &BagService::bagActivated);

        service->requestActivateBag(42);
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 1, 5000);
        QCOMPARE(source.calls.first().operation, QStringLiteral("activate"));
        QCOMPARE(source.calls.first().bagId, qint64(42));
        const auto selectedBag = QVariantMap{{QStringLiteral("id"), 42},
                                             {QStringLiteral("coffeeName"), QStringLiteral("Test coffee")}};
        emit source.operationCompleted(source.calls.first().id, QStringLiteral("activate"),
                                       42, true, selectedBag, {});
        QTRY_COMPARE_WITH_TIMEOUT(activated.size(), 1, 5000);
        QCOMPARE(activated.first().at(0).toLongLong(), qint64(42));
        QVERIFY(activated.first().at(1).toBool());
        QCOMPARE(activated.first().at(2).toMap(), selectedBag);

        service->requestActivateBag(43);
        QTRY_COMPARE_WITH_TIMEOUT(source.calls.size(), 2, 5000);
        const QString pendingId = source.calls.last().id;
        QCOMPARE(source.calls.last().bagId, qint64(43));
        service->setNode(nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(activated.size(), 2, 5000);
        QCOMPARE(activated.last().at(0).toLongLong(), qint64(43));
        QVERIFY(!activated.last().at(1).toBool());
        QVERIFY(activated.last().at(3).toString().contains(QStringLiteral("Connection lost")));

        // An old source reply after disconnect cannot report success or cause a replay.
        emit source.operationCompleted(pendingId, QStringLiteral("activate"), 43, true, {}, {});
        QCoreApplication::processEvents();
        QCOMPARE(activated.size(), 2);
        QCOMPARE(source.calls.size(), 2);
    }

    void finishedBagCountReadFailsOnDisconnectAndIgnoresLateReply()
    {
        PendingBagSource source;
        source.setInventoryReady(true);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<BagStateSourceAPI>(&source));

        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        auto* service = BagServiceForeign::create(&engine, &engine);
        service->setNode(&node);
        const std::unique_ptr<BagStateReplica> replica(node.acquire<BagStateReplica>());
        QTRY_VERIFY_WITH_TIMEOUT(replica->isReplicaValid() && service->ready(), 5000);

        QSignalSpy ready(service, &BagService::finishedBagCountReady);
        QSignalSpy failed(service, &BagService::finishedBagCountFailed);
        service->requestFinishedBagCount();
        QTRY_COMPARE_WITH_TIMEOUT(source.finishedCountRequestIds.size(), 1, 5000);
        emit source.finishedBagCountResult(source.finishedCountRequestIds.constFirst(),
                                           false, 0, QStringLiteral("readFailed"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);

        service->requestFinishedBagCount();
        QTRY_COMPARE_WITH_TIMEOUT(source.finishedCountRequestIds.size(), 2, 5000);
        const QString requestId = source.finishedCountRequestIds.last();

        service->setNode(nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 2, 5000);
        QCOMPARE(ready.size(), 0);

        // The old host response is ignored after the page has been told the
        // count read terminated; it cannot restore stale shelf state.
        emit source.finishedBagCountResult(requestId, true, 12, {});
        QCoreApplication::processEvents();
        QCOMPARE(failed.size(), 2);
        QCOMPARE(ready.size(), 0);

        service->requestFinishedBagCount();
        QCOMPARE(failed.size(), 3);
        QCOMPARE(ready.size(), 0);
    }

    void nativeSourceSerializesCreateUpdateActivationAndDeleteRules()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qint64 linkedBagId = -1;
        const QString databasePath = directory.filePath(QStringLiteral("bags.sqlite"));
        QVERIFY(prepareBagDatabase(databasePath, &linkedBagId));
        QVERIFY(linkedBagId > 0);
        clearDyeSettings();

        CoffeeBagStorage storage;
        storage.initialize(databasePath);
        SettingsDye dye;
        dye.setBagStorage(&storage);
        NativeBagServiceSource source(&storage, &dye);

        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<BagStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        auto* service = BagServiceForeign::create(&engine, &engine);
        service->setNode(&node);
        const std::unique_ptr<BagStateReplica> replica(node.acquire<BagStateReplica>());
        QTRY_VERIFY_WITH_TIMEOUT(replica->isReplicaValid() && service->ready(), 5000);

        QSignalSpy created(service, &BagService::bagCreated);
        QSignalSpy updated(service, &BagService::bagUpdated);
        QSignalSpy activated(service, &BagService::bagActivated);
        QSignalSpy deleted(service, &BagService::bagDeleted);
        QSignalSpy failed(service, &BagService::operationFailed);

        service->requestCreateBag({
            {QStringLiteral("roasterName"), QStringLiteral("Fixture roaster")},
            {QStringLiteral("coffeeName"), QStringLiteral("Created bag")}
        });
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 1, 5000);
        const qint64 createdId = created.first().at(0).toLongLong();
        QVERIFY(createdId > 0);
        QCOMPARE(created.first().at(1).toMap().value(QStringLiteral("coffeeName")).toString(),
                 QStringLiteral("Created bag"));

        service->requestUpdateBag(createdId,
                                  {{QStringLiteral("coffeeName"), QStringLiteral("Updated bag")}});
        QTRY_COMPARE_WITH_TIMEOUT(updated.size(), 1, 5000);
        QCOMPARE(updated.first().at(0).toLongLong(), createdId);
        QVERIFY(updated.first().at(1).toBool());
        const auto hasUpdatedBag = [service, createdId] {
            const QVariantList rows = service->bags();
            return std::any_of(rows.cbegin(), rows.cend(), [createdId](const QVariant& value) {
                const QVariantMap row = value.toMap();
                return row.value(QStringLiteral("id")).toLongLong() == createdId
                    && row.value(QStringLiteral("coffeeName")).toString() == QStringLiteral("Updated bag");
            });
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasUpdatedBag(), 5000);

        service->requestActivateBag(createdId);
        QTRY_COMPARE_WITH_TIMEOUT(activated.size(), 1, 5000);
        QCOMPARE(activated.first().at(0).toLongLong(), createdId);
        QVERIFY(activated.first().at(1).toBool());
        QCOMPARE(dye.activeBagId(), static_cast<int>(createdId));
        QTRY_COMPARE_WITH_TIMEOUT(service->activeBagId(), createdId, 5000);
        const auto activeCreatedBag = [service, createdId] {
            for (const QVariant& value : service->bags()) {
                const QVariantMap row = value.toMap();
                if (row.value(QStringLiteral("id")).toLongLong() == createdId)
                    return row.value(QStringLiteral("isActive")).toBool();
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(activeCreatedBag(), 5000);

        // Invalid activation IDs are correlated failures and leave the active bag unchanged.
        service->requestActivateBag(0);
        QTRY_COMPARE_WITH_TIMEOUT(activated.size(), 2, 5000);
        QCOMPARE(activated.last().at(0).toLongLong(), qint64(0));
        QVERIFY(!activated.last().at(1).toBool());
        QCOMPARE(dye.activeBagId(), static_cast<int>(createdId));

        // A bag referenced by history cannot be deleted; an unreferenced one can.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("refusing to delete bag"));
        service->requestDeleteBag(linkedBagId);
        QTRY_COMPARE_WITH_TIMEOUT(deleted.size(), 1, 5000);
        QCOMPARE(deleted.first().at(0).toLongLong(), linkedBagId);
        QVERIFY(!deleted.first().at(1).toBool());
        QVERIFY(failed.size() >= 1);
        QCOMPARE(failed.last().at(2).toString(), QStringLiteral("hasShots"));

        service->requestDeleteBag(createdId);
        QTRY_COMPARE_WITH_TIMEOUT(deleted.size(), 2, 5000);
        QCOMPARE(deleted.last().at(0).toLongLong(), createdId);
        QVERIFY(deleted.last().at(1).toBool());

        // A missing positive ID is a not-found result; non-positive IDs are rejected before storage.
        service->requestDeleteBag(900000);
        QTRY_COMPARE_WITH_TIMEOUT(deleted.size(), 3, 5000);
        QVERIFY(!deleted.last().at(1).toBool());
        QCOMPARE(failed.last().at(2).toString(), QStringLiteral("notFound"));
        service->requestDeleteBag(-1);
        QTRY_COMPARE_WITH_TIMEOUT(deleted.size(), 4, 5000);
        QVERIFY(!deleted.last().at(1).toBool());
        QCOMPARE(failed.last().at(2).toString(), QStringLiteral("invalidRequest"));
    }

    void nativeSourceGuardsYieldUpdateByActiveBagAndStoredFields()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qint64 linkedBagId = -1;
        const QString databasePath = directory.filePath(QStringLiteral("bags-yield-cas.sqlite"));
        QVERIFY(prepareBagDatabase(databasePath, &linkedBagId));
        clearDyeSettings();

        CoffeeBagStorage storage;
        storage.initialize(databasePath);
        SettingsDye dye;
        dye.setBagStorage(&storage);
        NativeBagServiceSource source(&storage, &dye);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<BagStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        auto* service = BagServiceForeign::create(&engine, &engine);
        service->setNode(&node);
        const std::unique_ptr<BagStateReplica> replica(node.acquire<BagStateReplica>());
        QTRY_VERIFY_WITH_TIMEOUT(replica->isReplicaValid() && service->ready(), 5000);

        QSignalSpy created(service, &BagService::bagCreated);
        QSignalSpy activated(service, &BagService::bagActivated);
        QSignalSpy updated(service, &BagService::bagUpdated);
        QSignalSpy failed(service, &BagService::operationFailed);
        QSignalSpy completed(service, &BagService::operationCompleted);
        service->requestCreateBag({
            {QStringLiteral("roasterName"), QStringLiteral("Fixture roaster")},
            {QStringLiteral("coffeeName"), QStringLiteral("Yield guarded")},
            {QStringLiteral("yieldValue"), 30.0},
            {QStringLiteral("yieldMode"), QStringLiteral("absolute")}
        });
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 1, 5000);
        const qint64 bagId = created.first().first().toLongLong();
        QVERIFY(bagId > 0);
        service->requestActivateBag(bagId);
        QTRY_COMPARE_WITH_TIMEOUT(activated.size(), 1, 5000);
        QVERIFY(activated.first().at(1).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(dye.activeBagYieldValue(), 30.0, 5000);
        QCOMPARE(dye.activeBagYieldMode(), QStringLiteral("absolute"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 5000);
        completed.clear();

        const QVariantMap opened{{QStringLiteral("yieldValue"), 30.0},
                                 {QStringLiteral("yieldMode"), QStringLiteral("absolute")}};
        QSignalSpy committedRows(&storage, &CoffeeBagStorage::bagUpdateCommitted);
        const qsizetype committedBeforeUpdate = committedRows.size();
        const QString successRequestId = service->requestUpdateBag(bagId,
            {{QStringLiteral("yieldValue"), 45.0}, {QStringLiteral("yieldMode"), QStringLiteral("ratio")}},
            false, opened, bagId);
        QTRY_COMPARE_WITH_TIMEOUT(updated.size(), 1, 5000);
        QVERIFY(updated.last().at(1).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        QCOMPARE(completed.first().at(0).toString(), successRequestId);
        QCOMPARE(completed.first().at(1).toString(), QStringLiteral("update"));
        QCOMPARE(completed.first().at(2).toLongLong(), bagId);
        QVERIFY(completed.first().at(3).toBool());
        QCOMPARE(committedRows.size(), committedBeforeUpdate + 1);
        // The scoped completion is sent only after the exact committed row
        // refreshed SettingsDye; there is no follow-up DB read to race this.
        QCOMPARE(dye.activeBagYieldValue(), 45.0);
        QCOMPARE(dye.activeBagYieldMode(), QStringLiteral("ratio"));

        // A stale dialog from the same selected bag cannot overwrite the new spec.
        const QString staleRequestId = service->requestUpdateBag(bagId,
            {{QStringLiteral("yieldValue"), 31.0}, {QStringLiteral("yieldMode"), QStringLiteral("absolute")}},
            false, opened, bagId);
        QTRY_COMPARE_WITH_TIMEOUT(updated.size(), 2, 5000);
        QVERIFY(!updated.last().at(1).toBool());
        QCOMPARE(failed.last().at(2).toString(), QStringLiteral("staleData"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 5000);
        QCOMPARE(completed.last().at(0).toString(), staleRequestId);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("staleData"));
        QCOMPARE(dye.activeBagYieldValue(), 45.0);

        // The captured active-bag identity is checked before dispatch as well.
        dye.setActiveBagId(static_cast<int>(linkedBagId));
        const QString wrongActiveBagRequestId = service->requestUpdateBag(bagId,
            {{QStringLiteral("yieldValue"), 32.0}, {QStringLiteral("yieldMode"), QStringLiteral("absolute")}},
            false, {{QStringLiteral("yieldValue"), 45.0},
                    {QStringLiteral("yieldMode"), QStringLiteral("ratio")}}, bagId);
        QTRY_COMPARE_WITH_TIMEOUT(updated.size(), 3, 5000);
        QVERIFY(!updated.last().at(1).toBool());
        QCOMPARE(failed.last().at(2).toString(), QStringLiteral("staleData"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 3, 5000);
        QCOMPARE(completed.last().at(0).toString(), wrongActiveBagRequestId);
        QVERIFY(!completed.last().at(3).toBool());

        QTRY_VERIFY_WITH_TIMEOUT(storage.isDbWorkIdle(), 5000);
        bool inspected = false;
        storage.requestBagWithCompletion(bagId, &storage,
            [&](qint64 resultId, bool success, const QVariantMap& bag, const QString&) {
                QCOMPARE(resultId, bagId);
                QVERIFY(success);
                QCOMPARE(bag.value(QStringLiteral("yieldValue")).toDouble(), 45.0);
                QCOMPARE(bag.value(QStringLiteral("yieldMode")).toString(), QStringLiteral("ratio"));
                inspected = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(inspected, 5000);

        // The ordinary Beans editor can save yield fields on an inactive bag.
        // Active-bag CAS is opt-in for BrewDialog and must not change that flow.
        const QString inactiveEditId = service->requestUpdateBag(bagId,
            {{QStringLiteral("yieldValue"), 52.0},
             {QStringLiteral("yieldMode"), QStringLiteral("absolute")} });
        QTRY_COMPARE_WITH_TIMEOUT(updated.size(), 4, 5000);
        QVERIFY(updated.last().at(1).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 4, 5000);
        QCOMPARE(completed.last().at(0).toString(), inactiveEditId);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(dye.activeBagId(), static_cast<int>(linkedBagId));

        const QString malformedYieldId = service->requestUpdateBag(bagId,
            {{QStringLiteral("yieldValue"), QStringLiteral("not-a-number")},
             {QStringLiteral("yieldMode"), QStringLiteral("absolute")} });
        QTRY_COMPARE_WITH_TIMEOUT(updated.size(), 5, 5000);
        QVERIFY(!updated.last().at(1).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 5, 5000);
        QCOMPARE(completed.last().at(0).toString(), malformedYieldId);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidFields"));

        bool finalRowRead = false;
        storage.requestBagWithCompletion(bagId, &storage,
            [&](qint64 resultId, bool success, const QVariantMap& bag, const QString&) {
                QCOMPARE(resultId, bagId);
                QVERIFY(success);
                QCOMPARE(bag.value(QStringLiteral("yieldValue")).toDouble(), 52.0);
                QCOMPARE(bag.value(QStringLiteral("yieldMode")).toString(), QStringLiteral("absolute"));
                finalRowRead = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(finalRowRead, 5000);


    }

    void nativeSourceReadsAndAppliesBagLifecycleTransitions()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qint64 linkedBagId = -1;
        const QString databasePath = directory.filePath(QStringLiteral("bags-lifecycle.sqlite"));
        QVERIFY(prepareBagDatabase(databasePath, &linkedBagId));
        QVERIFY(linkedBagId > 0);
        clearDyeSettings();

        CoffeeBagStorage storage;
        storage.initialize(databasePath);
        SettingsDye dye;
        dye.setBagStorage(&storage);
        NativeBagServiceSource source(&storage, &dye);

        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<BagStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        auto* service = BagServiceForeign::create(&engine, &engine);
        service->setNode(&node);
        const std::unique_ptr<BagStateReplica> replica(node.acquire<BagStateReplica>());
        QTRY_VERIFY_WITH_TIMEOUT(replica->isReplicaValid() && service->ready(), 5000);

        auto findBag = [linkedBagId](const QVariantList& rows) -> QVariantMap {
            for (const QVariant& value : rows) {
                const QVariantMap row = value.toMap();
                if (row.value(QStringLiteral("id")).toLongLong() == linkedBagId)
                    return row;
            }
            return {};
        };

        QVariantMap activeRow = findBag(service->bags());
        QVERIFY(!activeRow.isEmpty());
        QCOMPARE(activeRow.value(QStringLiteral("shotCount")).toLongLong(), qint64(1));
        QVERIFY(activeRow.value(QStringLiteral("inInventory")).toBool());
        QVERIFY(!activeRow.value(QStringLiteral("isActive")).toBool());

        QSignalSpy bagReady(service, &BagService::bagReady);
        service->requestBag(linkedBagId);
        QTRY_COMPARE_WITH_TIMEOUT(bagReady.size(), 1, 5000);
        QCOMPARE(bagReady.first().at(0).toLongLong(), linkedBagId);
        QCOMPARE(bagReady.first().at(1).toMap().value(QStringLiteral("coffeeName")).toString(),
                 QStringLiteral("Linked bag"));

        QSignalSpy finishedCount(service, &BagService::finishedBagCountReady);
        service->requestFinishedBagCount();
        QTRY_COMPARE_WITH_TIMEOUT(finishedCount.size(), 1, 5000);
        QCOMPARE(finishedCount.first().at(0).toInt(), 0);

        QSignalSpy bagFinished(service, &BagService::bagFinished);
        QSignalSpy updated(service, &BagService::bagUpdated);
        service->requestMarkEmpty(linkedBagId);
        QTRY_COMPARE_WITH_TIMEOUT(updated.size(), 1, 5000);
        QVERIFY(updated.last().at(1).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(findBag(service->bags()).isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(bagFinished.size(), 1, 5000);
        QCOMPARE(bagFinished.first().at(0).toLongLong(), linkedBagId);

        QSignalSpy finishedReady(service, &BagService::finishedBagsReady);
        service->requestFinishedBags();
        QTRY_COMPARE_WITH_TIMEOUT(finishedReady.size(), 1, 5000);
        QCOMPARE(findBag(finishedReady.first().first().toList()).value(QStringLiteral("shotCount")).toLongLong(),
                 qint64(1));
        service->requestFinishedBagCount();
        QTRY_COMPARE_WITH_TIMEOUT(finishedCount.size(), 2, 5000);
        QCOMPARE(finishedCount.last().at(0).toInt(), 1);

        QSignalSpy restocked(service, &BagService::bagRestocked);
        service->requestUpdateBag(linkedBagId, {{QStringLiteral("inInventory"), true}});
        QTRY_COMPARE_WITH_TIMEOUT(updated.size(), 2, 5000);
        QVERIFY(updated.last().at(1).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!findBag(service->bags()).isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(restocked.size(), 1, 5000);
        QCOMPARE(restocked.first().at(0).toLongLong(), linkedBagId);
        service->requestFinishedBags();
        QTRY_COMPARE_WITH_TIMEOUT(finishedReady.size(), 2, 5000);
        QVERIFY(finishedReady.last().first().toList().isEmpty());
        service->requestFinishedBagCount();
        QTRY_COMPARE_WITH_TIMEOUT(finishedCount.size(), 3, 5000);
        QCOMPARE(finishedCount.last().at(0).toInt(), 0);

        // Freeze and thaw use the same canonical date update path as the native
        // inventory card. Clearing frozenDate must clear defrostDate as well.
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        service->requestUpdateBag(linkedBagId, {{QStringLiteral("frozenDate"), today}});
        QTRY_COMPARE_WITH_TIMEOUT(updated.size(), 3, 5000);
        QVERIFY(updated.last().at(1).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(findBag(service->bags()).value(QStringLiteral("frozenDate")).toString(),
                                  today, 5000);

        service->requestUpdateBag(linkedBagId, {{QStringLiteral("frozenDate"), QString()}});
        QTRY_COMPARE_WITH_TIMEOUT(updated.size(), 4, 5000);
        QVERIFY(updated.last().at(1).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(findBag(service->bags()).value(QStringLiteral("frozenDate")).toString().isEmpty(),
                                 5000);
        QCOMPARE(findBag(service->bags()).value(QStringLiteral("defrostDate")).toString(), QString());
    }
};

QTEST_MAIN(tst_BagService)
#include "tst_bagservice.moc"
