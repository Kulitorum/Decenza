#include <QtTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <memory>
#include <limits>

#include "core/settings_dye.h"
#include "core/appsettings.h"
#include "history/equipmentstorage.h"
#include "history/recipestorage.h"
#include "history/coffeebagstorage.h"
#include "remote/recipeservice_qml.h"
#include "remote/recipeservicesource.h"
#include "remote/equipmentservice_qml.h"
#include "remote/equipmentservicesource.h"
#include "rep_equipmentservice_source.h"
#include "shotrowfixtures.h"

using ShotRowFixtures::withRawDb;

namespace {
void createMinimalShots(QSqlDatabase& db)
{
    QSqlQuery query(db);
    QVERIFY(query.exec(R"(
        CREATE TABLE shots (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            grinder_brand TEXT, grinder_model TEXT, grinder_burrs TEXT,
            grinder_setting TEXT, equipment_id INTEGER, rpm INTEGER, recipe_id INTEGER
        )
    )"));
}

struct RemoveSqlConnection final
{
    QString name;
    ~RemoveSqlConnection() { QSqlDatabase::removeDatabase(name); }
};
}

class PendingEquipmentSource final : public EquipmentStateSimpleSource
{
    Q_OBJECT
public:
    using EquipmentStateSimpleSource::setInventoryReady;
    using EquipmentStateSimpleSource::setLoading;

    int calls = 0;
    QString lastRequestId;
    QString lastOperation;
    QVariantMap lastFields;
    qint64 lastId = -1;
    QStringList archivedReads;
    QStringList grindReads;

    void refresh() override {}
    void execute(QString requestId, QString operation, qint64 id, QVariantMap fields, QVariantMap, bool) override
    {
        ++calls;
        lastRequestId = std::move(requestId);
        lastOperation = std::move(operation);
        lastFields = std::move(fields);
        lastId = id;
        // Deliberately leave this correlated operation in-flight until the client
        // disconnects. The facade must fail it once and must never replay it.
    }
    void requestPackage(QString, qint64) override {}
    void requestArchived(QString requestId) override { archivedReads.append(requestId); }
    void requestGrindContext(QString requestId, QString, QVariantMap) override
    { grindReads.append(requestId); }
};

class tst_EquipmentService : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;

private slots:
    void init()
    {
        QTest::failOnWarning();
        AppSettings settings;
        settings.clear();
    }
    void cleanup() { EquipmentService::setPublishedNode(nullptr); RecipeService::setPublishedNode(nullptr); }

    void inventoryAndOptionsUseNativeProjection()
    {
        QVERIFY(m_dir.isValid());
        const QString path = m_dir.filePath(QStringLiteral("equipment.db"));
        withRawDb(path, "equipment_service_fixture", [](QSqlDatabase& db) {
            createMinimalShots(db);
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
        });

        EquipmentStorage storage;
        storage.initialize(path);
        SettingsDye dye;
        NativeEquipmentServiceSource source(&storage, &dye, nullptr);

        QTRY_VERIFY_WITH_TIMEOUT(source.inventoryReady(), 15000);
        QVERIFY(!source.loading());
        QVERIFY(source.errorMessage().isEmpty());
        QVERIFY(source.packages().isEmpty());
        const QVariantMap seed = source.createSeed();
        for (const QString& key : {QStringLiteral("grinderBrand"), QStringLiteral("grinderModel"),
                                   QStringLiteral("grinderBurrs"), QStringLiteral("basketBrand"),
                                   QStringLiteral("basketModel"), QStringLiteral("puckPrepCanonical")})
            QVERIFY2(seed.contains(key), qPrintable(key));
        const QVariantMap options = source.options();
        for (const QString& key : {QStringLiteral("grinderBrands"),
                                   QStringLiteral("grinderHistoryBrands"),
                                   QStringLiteral("grinderModels"), QStringLiteral("grinderBurrs"),
                                   QStringLiteral("basketBrands"), QStringLiteral("basketModels"),
                                   QStringLiteral("basketSummaries"), QStringLiteral("puckPrepFlags")})
            QVERIFY2(options.contains(key), qPrintable(key));

        source.refresh();
        QVERIFY(source.inventoryReady()); // A pending refresh does not invalidate the last complete read.
        QTRY_VERIFY_WITH_TIMEOUT(!source.loading(), 15000);
        QVERIFY(source.inventoryReady());
    }

    void createUpdateConflictAndScopedDetail()
    {
        QVERIFY(m_dir.isValid());
        const QString path = m_dir.filePath(QStringLiteral("operations.db"));
        withRawDb(path, "equipment_service_operations", [](QSqlDatabase& db) {
            createMinimalShots(db);
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
        });

        EquipmentStorage storage;
        storage.initialize(path);
        SettingsDye dye;
        NativeEquipmentServiceSource source(&storage, &dye, nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(source.inventoryReady(), 15000);

        QSignalSpy operationSpy(&source, &EquipmentStateSource::operationCompleted);
        QSignalSpy packageSpy(&source, &EquipmentStateSource::packageResult);
        const QVariantMap fields{
            {QStringLiteral("name"), QStringLiteral("Basket setup")},
            {QStringLiteral("basketBrand"), QStringLiteral("IMS")},
            {QStringLiteral("basketModel"), QStringLiteral("Competition")}
        };
        source.execute(QStringLiteral("create-1"), QStringLiteral("create"), -1,
                       fields, {}, false);
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.size(), 1, 15000);
        const QList<QVariant> created = operationSpy.takeFirst();
        QCOMPARE(created.at(0).toString(), QStringLiteral("create-1"));
        QVERIFY(created.at(1).toBool());
        const qint64 id = created.at(2).toLongLong();
        QVERIFY(id > 0);
        const auto packageExists = [&source, id] {
            for (const QVariant& value : source.packages()) {
                if (value.toMap().value(QStringLiteral("id")).toLongLong() == id)
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(packageExists(), 15000);

        const auto rowForId = [&source, id] {
            for (const QVariant& item : source.packages()) {
                const QVariantMap row = item.toMap();
                if (row.value(QStringLiteral("id")).toLongLong() == id)
                    return row;
            }
            return QVariantMap{};
        };
        const QVariantMap original = rowForId();
        QVERIFY(!original.isEmpty());
        const QVariantMap expected{
            {QStringLiteral("name"), original.value(QStringLiteral("name")).toString()},
            {QStringLiteral("grinderBrand"), original.value(QStringLiteral("grinderBrand")).toString()},
            {QStringLiteral("grinderModel"), original.value(QStringLiteral("grinderModel")).toString()},
            {QStringLiteral("grinderBurrs"), original.value(QStringLiteral("grinderBurrs")).toString()},
            {QStringLiteral("basketBrand"), original.value(QStringLiteral("basketBrand")).toString()},
            {QStringLiteral("basketModel"), original.value(QStringLiteral("basketModel")).toString()},
            {QStringLiteral("puckPrepCanonical"), original.value(QStringLiteral("puckPrepCanonical")).toString()}
        };
        source.execute(QStringLiteral("update-1"), QStringLiteral("update"), id,
                       {{QStringLiteral("name"), QStringLiteral("Renamed basket setup")}},
                       expected, false);
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.size(), 1, 15000);
        const QList<QVariant> updated = operationSpy.takeFirst();
        QCOMPARE(updated.at(0).toString(), QStringLiteral("update-1"));
        QVERIFY(updated.at(1).toBool());
        QCOMPARE(updated.at(2).toLongLong(), id);
        QTRY_COMPARE_WITH_TIMEOUT(rowForId().value(QStringLiteral("name")).toString(),
                                  QStringLiteral("Renamed basket setup"), 15000);

        source.execute(QStringLiteral("conflict-1"), QStringLiteral("update"), id,
                       {{QStringLiteral("name"), QStringLiteral("Stale overwrite")}},
                       expected, false);
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.size(), 1, 15000);
        const QList<QVariant> conflict = operationSpy.takeFirst();
        QCOMPARE(conflict.at(0).toString(), QStringLiteral("conflict-1"));
        QVERIFY(!conflict.at(1).toBool());
        QCOMPARE(conflict.at(3).toString(), QStringLiteral("conflict"));

        source.requestPackage(QStringLiteral("detail-1"), id);
        QTRY_COMPARE_WITH_TIMEOUT(packageSpy.size(), 1, 15000);
        const QList<QVariant> detail = packageSpy.takeFirst();
        QCOMPARE(detail.at(0).toString(), QStringLiteral("detail-1"));
        QCOMPARE(detail.at(1).toLongLong(), id);
        QVERIFY(detail.at(2).toMap().contains(QStringLiteral("basketBrand")));
        QVERIFY(detail.at(3).toString().isEmpty());
    }

    void nativeInProcessReplicaIsReady()
    {
        const QString path = m_dir.filePath(QStringLiteral("in_process.db"));
        withRawDb(path, "equipment_service_in_process", [](QSqlDatabase& db) {
            createMinimalShots(db);
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe referencedRecipe;
            referencedRecipe.name = QStringLiteral("Counted recipe");
            referencedRecipe.profileTitle = QStringLiteral("Count profile");
            QVERIFY(RecipeStorage::insertRecipeStatic(db, referencedRecipe) > 0);
        });
        EquipmentStorage storage;
        storage.initialize(path);
        RecipeStorage recipeStorage;
        recipeStorage.initialize(path);
        SettingsDye dye;
        NativeEquipmentServiceSource source(&storage, &dye, nullptr);
        NativeRecipeServiceSource recipeSource(&recipeStorage, &dye, {}, {});
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("equipment-test:///native")),
                                QRemoteObjectHost::AllowExternalRegistration));
        QVERIFY(host.enableRemoting<EquipmentStateSourceAPI>(&source));
        QVERIFY(host.enableRemoting<RecipeStateSourceAPI>(&recipeSource));
        EquipmentService::setPublishedNode(&host);
        RecipeService::setPublishedNode(&host);
        QQmlEngine engine;
        EquipmentService* service = EquipmentServiceForeign::create(&engine, &engine);
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 15000);
        QVERIFY(service->errorMessage().isEmpty());
        QVERIFY(service->packages().isEmpty());
        QSignalSpy reads(service, &EquipmentService::inventoryReady);
        QSignalSpy changes(service, &EquipmentService::packagesChanged);
        service->requestInventory();
        QTRY_VERIFY_WITH_TIMEOUT(!reads.isEmpty(), 5000);
        QCOMPARE(changes.size(), 0); // Unchanged rows must not invalidate themselves again.
        RecipeService* recipes = RecipeServiceForeign::create(&engine, &engine);
        QTRY_VERIFY_WITH_TIMEOUT(recipes->ready(), 15000);
        QVERIFY(recipes->errorMessage().isEmpty());
        QSignalSpy recipeReads(recipes, &RecipeService::recipeReady);
        QSignalSpy failedReads(recipes, &RecipeService::recipeCheckFailed);
        recipes->requestRecipe(999);
        QTRY_COMPARE_WITH_TIMEOUT(recipeReads.size(), 1, 15000);
        QCOMPARE(recipeReads.first().at(0).toLongLong(), 999);
        QVERIFY(recipeReads.first().at(1).toMap().isEmpty());
        QVERIFY(failedReads.isEmpty()); // The wizard distinguishes a deleted row from a read failure.
        QSignalSpy profileCount(recipes, &RecipeService::countRecipesUsingProfileReady);
        const QString countRequest = recipes->requestCountRecipesUsingProfile(
            QStringLiteral("Count profile"));
        QVERIFY(!countRequest.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(profileCount.size(), 1, 15000);
        QCOMPARE(profileCount.first().at(0).toString(), countRequest);
        QCOMPARE(profileCount.first().at(1).toString(), QStringLiteral("Count profile"));
        QCOMPARE(profileCount.first().at(2).toInt(), 1);
        QVERIFY(profileCount.first().at(3).toString().isEmpty());
        QSignalSpy equipmentDefaults(recipes, &RecipeService::lastEquipmentForDrinkTypeReady);
        QSignalSpy equipmentDefaultFailures(recipes, &RecipeService::lastEquipmentForDrinkTypeFailed);
        QSignalSpy disconnectedCount(recipes, &RecipeService::countRecipesUsingProfileReady);
        recipes->setNode(nullptr);
        const QString disconnectedCountId = recipes->requestCountRecipesUsingProfile(
            QStringLiteral("Count profile"));
        QTRY_COMPARE_WITH_TIMEOUT(disconnectedCount.size(), 1, 5000);
        QCOMPARE(disconnectedCount.first().at(0).toString(), disconnectedCountId);
        QCOMPARE(disconnectedCount.first().at(2).toInt(), -1);
        QCOMPARE(disconnectedCount.first().at(3).toString(), QStringLiteral("disconnected"));
        recipes->requestLastEquipmentForDrinkType(QStringLiteral("espresso"));
        QTRY_COMPARE_WITH_TIMEOUT(equipmentDefaultFailures.size(), 1, 5000);
        QCOMPARE(equipmentDefaultFailures.first().at(0).toString(), QStringLiteral("espresso"));
        QVERIFY(equipmentDefaults.isEmpty()); // A read failure must not masquerade as “no saved equipment”.
    }

    void tcpClientsReceiveOnlyTheirCorrelatedResults()
    {
        const QString path = m_dir.filePath(QStringLiteral("tcp_clients.db"));
        withRawDb(path, "equipment_service_tcp_clients", [](QSqlDatabase& db) {
            createMinimalShots(db);
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
        });
        EquipmentStorage storage;
        storage.initialize(path);
        SettingsDye dye;
        NativeEquipmentServiceSource source(&storage, &dye, nullptr);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<EquipmentStateSourceAPI>(&source));

        QRemoteObjectNode node1;
        QRemoteObjectNode node2;
        QVERIFY(node1.connectToNode(host.hostUrl()));
        QVERIFY(node2.connectToNode(host.hostUrl()));
        QQmlEngine engine1;
        EquipmentService* first = EquipmentServiceForeign::create(&engine1, &engine1);
        first->setNode(&node1);
        QTRY_VERIFY_WITH_TIMEOUT(first->ready(), 5000);
        QQmlEngine engine2;
        EquipmentService* second = EquipmentServiceForeign::create(&engine2, &engine2);
        second->setNode(&node2);
        QTRY_VERIFY_WITH_TIMEOUT(second->ready(), 5000);

        QSignalSpy firstCompleted(first, &EquipmentService::operationCompleted);
        QSignalSpy secondCompleted(second, &EquipmentService::operationCompleted);
        const QString firstId = QStringLiteral("tcp-create-first");
        const QString secondId = QStringLiteral("tcp-create-second");
        first->createPackage({{QStringLiteral("name"), QStringLiteral("First basket")},
                              {QStringLiteral("basketBrand"), QStringLiteral("IMS")},
                              {QStringLiteral("basketModel"), QStringLiteral("First")}}, false, firstId);
        second->createPackage({{QStringLiteral("name"), QStringLiteral("Second basket")},
                                {QStringLiteral("basketBrand"), QStringLiteral("IMS")},
                                {QStringLiteral("basketModel"), QStringLiteral("Second")}}, false, secondId);
        QTRY_COMPARE_WITH_TIMEOUT(firstCompleted.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(secondCompleted.size(), 1, 15000);
        QCOMPARE(firstCompleted.first().at(0).toString(), firstId);
        QCOMPARE(secondCompleted.first().at(0).toString(), secondId);
        QVERIFY(firstCompleted.first().at(1).toBool());
        QVERIFY(secondCompleted.first().at(1).toBool());
        QVERIFY(firstCompleted.first().at(2).toLongLong() != secondCompleted.first().at(2).toLongLong());
        QTRY_COMPARE_WITH_TIMEOUT(first->packages().size(), 2, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(second->packages().size(), 2, 15000);
    }

    void recipeClientsShareStorageAndConflictRules()
    {
        const QString path = m_dir.filePath(QStringLiteral("recipe_clients.db"));
        withRawDb(path, "recipe_service_fixture", [](QSqlDatabase& db) {
            createMinimalShots(db);
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            QVERIFY(RecipeStorage::ensureTableStatic(db));
        });
        RecipeStorage storage;
        storage.initialize(path);
        SettingsDye dye;
        int deactivationCalls = 0;
        NativeRecipeServiceSource source(&storage, &dye, {}, [&] {
            ++deactivationCalls;
            dye.setActiveRecipeId(-1);
        });
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<RecipeStateSourceAPI>(&source));
        QRemoteObjectNode firstNode, secondNode;
        QVERIFY(firstNode.connectToNode(host.hostUrl()));
        QVERIFY(secondNode.connectToNode(host.hostUrl()));
        QQmlEngine firstEngine, secondEngine;
        auto* first = RecipeServiceForeign::create(&firstEngine, &firstEngine);
        auto* second = RecipeServiceForeign::create(&secondEngine, &secondEngine);
        first->setNode(&firstNode);
        second->setNode(&secondNode);
        QTRY_VERIFY_WITH_TIMEOUT(first->ready() && second->ready(), 5000);
        QSignalSpy created(first, &RecipeService::recipeCreated);
        QSignalSpy secondCreated(second, &RecipeService::recipeCreated);
        Recipe seed;
        seed.name = QStringLiteral("Service recipe");
        seed.profileTitle = QStringLiteral("Test profile");
        seed.drinkType = QStringLiteral("espresso");
        auto fields = seed.editableSnapshot();
        fields.remove(QStringLiteral("archived"));
        fields.insert(QStringLiteral("requestToken"), QStringLiteral("create-local"));
        first->requestCreateRecipe(fields);
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 1, 5000);
        const qint64 id = created.first().at(0).toLongLong();
        QVERIFY(id > 0);
        QCOMPARE(created.first().at(1).toMap().value(QStringLiteral("requestToken")).toString(),
                 QStringLiteral("create-local"));
        QCOMPARE(secondCreated.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(second->recipes().size(), 1, 5000);
        const auto original = second->editableSnapshot(second->recipes().first().toMap());
        QCOMPARE(original.size(), 18);
        QSignalSpy firstUpdated(first, &RecipeService::recipeUpdated);
        QSignalSpy secondUpdated(second, &RecipeService::recipeUpdated);
        QSignalSpy conflict(second, &RecipeService::recipeUpdateFailed);
        QSignalSpy firstOperations(first, &RecipeService::operationCompleted);
        QSignalSpy secondOperations(second, &RecipeService::operationCompleted);
        const QString updateId = first->requestUpdateRecipe(id,
            {{QStringLiteral("name"), QStringLiteral("New name")}}, original);
        QVERIFY(!updateId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(firstUpdated.size(), 1, 5000);
        QVERIFY(firstUpdated.first().at(1).toBool());
        QCOMPARE(firstOperations.first().at(0).toString(), updateId);
        QVERIFY(secondOperations.isEmpty());
        const QString staleUpdateId = second->requestUpdateRecipe(id,
            {{QStringLiteral("name"), QStringLiteral("Stale name")}}, original);
        QVERIFY(staleUpdateId != updateId);
        QTRY_COMPARE_WITH_TIMEOUT(secondUpdated.size(), 1, 5000);
        QVERIFY(!secondUpdated.first().at(1).toBool());
        QCOMPARE(secondOperations.first().at(0).toString(), staleUpdateId);
        QCOMPARE(conflict.first().at(1).toString(), QStringLiteral("conflict"));
        QTRY_COMPARE_WITH_TIMEOUT(second->recipes().first().toMap().value(QStringLiteral("name")).toString(),
                                  QStringLiteral("New name"), 5000);
        firstUpdated.clear();

        // A stale archive CAS must not turn off the currently active recipe.
        dye.setActiveRecipeId(static_cast<int>(id));
        QSignalSpy sourceOperations(&source, &RecipeStateSource::operationCompleted);
        source.execute(QStringLiteral("stale-archive"), QStringLiteral("archive"), id, {}, original);
        QTRY_COMPARE_WITH_TIMEOUT(sourceOperations.size(), 1, 5000);
        QCOMPARE(sourceOperations.first().at(0).toString(), QStringLiteral("stale-archive"));
        QVERIFY(!sourceOperations.first().at(2).toBool());
        QCOMPARE(sourceOperations.first().at(4).toString(), QStringLiteral("conflict"));
        QCOMPARE(dye.activeRecipeId(), static_cast<int>(id));
        QCOMPARE(deactivationCalls, 0);
        QVERIFY(firstUpdated.isEmpty()); // The direct source request belongs to neither client.

        first->requestArchiveRecipe(id);
        QTRY_COMPARE_WITH_TIMEOUT(firstUpdated.size(), 1, 5000);
        QVERIFY(firstUpdated.first().at(1).toBool());
        QCOMPARE(dye.activeRecipeId(), -1);
        QCOMPARE(deactivationCalls, 1);
        QTRY_COMPARE_WITH_TIMEOUT(second->archivedRecipes().size(), 1, 5000);
        QVERIFY(second->recipes().isEmpty());
        firstUpdated.clear();
        QTRY_COMPARE_WITH_TIMEOUT(first->archivedRecipes().size(), 1, 5000);
        first->requestUnarchiveRecipe(id);
        QTRY_COMPARE_WITH_TIMEOUT(firstUpdated.size(), 1, 5000);
        QVERIFY(firstUpdated.first().at(1).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(second->recipes().size(), 1, 5000);
        withRawDb(path, "recipe_service_reference", [id](QSqlDatabase& db) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO shots(recipe_id) VALUES (?)");
            query.addBindValue(id);
            QVERIFY(query.exec());
        });
        QSignalSpy deleted(first, &RecipeService::recipeDeleted);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*refusing to delete recipe.*with linked shots.*")));
        dye.setActiveRecipeId(static_cast<int>(id));
        first->requestDeleteRecipe(id);
        QTRY_COMPARE_WITH_TIMEOUT(deleted.size(), 1, 5000);
        QVERIFY(!deleted.first().at(1).toBool());
        QCOMPARE(dye.activeRecipeId(), static_cast<int>(id));
        QCOMPARE(deactivationCalls, 1);
        QCOMPARE(second->recipes().size(), 1);
    }

    void recipeCloneActivationAndBagRelinkUseNativeRules()
    {
        const QString path = m_dir.filePath(QStringLiteral("recipe_clone_relink.db"));
        qint64 originalBagId = 0;
        qint64 replacementBagId = 0;
        qint64 recipeId = 0;
        withRawDb(path, "recipe_service_clone_relink", [&](QSqlDatabase& db) {
            createMinimalShots(db);
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            QVERIFY(RecipeStorage::ensureTableStatic(db));

            CoffeeBag originalBag;
            originalBag.roasterName = QStringLiteral("Old Roaster");
            originalBag.coffeeName = QStringLiteral("Old Coffee");
            originalBag.beanBaseId = QStringLiteral("old-bean-id");
            originalBagId = CoffeeBagStorage::insertBagStatic(db, originalBag);
            QVERIFY(originalBagId > 0);

            CoffeeBag replacementBag;
            replacementBag.roasterName = QStringLiteral("New Roaster");
            replacementBag.coffeeName = QStringLiteral("New Coffee");
            replacementBag.beanBaseId = QStringLiteral("new-bean-id");
            replacementBagId = CoffeeBagStorage::insertBagStatic(db, replacementBag);
            QVERIFY(replacementBagId > 0);

            Recipe seed;
            seed.name = QStringLiteral("Original recipe");
            seed.profileTitle = QStringLiteral("Test profile");
            seed.drinkType = QStringLiteral("espresso");
            seed.bagId = originalBagId;
            seed.beanBaseId = originalBag.beanBaseId;
            seed.roasterName = originalBag.roasterName;
            seed.coffeeName = originalBag.coffeeName;
            seed.createdFromShotId = 731;
            seed.doseG = 18.5;
            seed.yieldMode = QStringLiteral("ratio");
            seed.yieldValue = 2.1;
            seed.grindPinned = QStringLiteral("4.2");
            seed.rpmPinned = 780;
            recipeId = RecipeStorage::insertRecipeStatic(db, seed);
            QVERIFY(recipeId > 0);
        });

        RecipeStorage storage;
        storage.initialize(path);
        SettingsDye dye;
        NativeRecipeServiceSource* sourcePtr = nullptr;
        qint64 activationCallbackId = -1;
        bool completeActivationInline = false;
        bool acceptActivation = false;
        NativeRecipeServiceSource source(&storage, &dye,
            [&](qint64 id) {
                activationCallbackId = id;
                if (completeActivationInline && sourcePtr) {
                    if (acceptActivation)
                        dye.setActiveRecipeId(static_cast<int>(id));
                    sourcePtr->completeActivation(id, acceptActivation,
                        acceptActivation ? QString() : QStringLiteral("Missing profile"));
                }
            }, {});
        sourcePtr = &source;

        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<RecipeStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        RecipeService* service = RecipeServiceForeign::create(&engine, &engine);
        service->setNode(&node);
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 5000);

        QSignalSpy created(service, &RecipeService::recipeCreated);
        QSignalSpy operations(service, &RecipeService::operationCompleted);
        service->requestCloneRecipe(recipeId, QStringLiteral("Cloned recipe"),
                                    QStringLiteral("clone-test-token"));
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 1, 15000);
        const qint64 cloneId = created.first().at(0).toLongLong();
        QVERIFY(cloneId > 0);
        const QVariantMap clone = created.first().at(1).toMap();
        QCOMPARE(clone.value(QStringLiteral("requestToken")).toString(),
                 QStringLiteral("clone-test-token"));
        QCOMPARE(clone.value(QStringLiteral("name")).toString(), QStringLiteral("Cloned recipe"));
        QCOMPARE(clone.value(QStringLiteral("clonedFromRecipeId")).toLongLong(), recipeId);
        QCOMPARE(clone.value(QStringLiteral("createdFromShotId")).toLongLong(), 0);
        QCOMPARE(clone.value(QStringLiteral("bagId")).toLongLong(), originalBagId);
        QCOMPARE(clone.value(QStringLiteral("grindPinned")).toString(), QStringLiteral("4.2"));
        QCOMPARE(clone.value(QStringLiteral("rpmPinned")).toLongLong(), 780);
        QTRY_VERIFY_WITH_TIMEOUT(service->recipes().size() == 2, 15000);

        // Activation remains pending until the host callback completes it. A completion
        // for another recipe must not satisfy this request; refusal is reported to this client.
        QSignalSpy activated(service, &RecipeService::recipeActivated);
        const QString refusedActivationId = service->requestActivateRecipe(cloneId);
        QTRY_COMPARE_WITH_TIMEOUT(activationCallbackId, cloneId, 5000);
        QVERIFY(activated.isEmpty());
        source.completeActivation(cloneId + 1000, true);
        QCoreApplication::processEvents();
        QVERIFY(activated.isEmpty());
        source.completeActivation(cloneId, false, QStringLiteral("Missing profile"));
        QTRY_COMPARE_WITH_TIMEOUT(activated.size(), 1, 5000);
        QCOMPARE(activated.first().at(0).toLongLong(), cloneId);
        QVERIFY(!activated.first().at(1).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!operations.isEmpty(), 5000);
        QCOMPARE(operations.takeFirst().at(1).toString(), QStringLiteral("clone"));
        QTRY_VERIFY_WITH_TIMEOUT(!operations.isEmpty(), 5000);
        const QList<QVariant> refused = operations.takeFirst();
        QCOMPARE(refused.at(0).toString(), refusedActivationId);
        QCOMPARE(refused.at(1).toString(), QStringLiteral("activate"));
        QVERIFY(!refused.at(2).toBool());
        QCOMPARE(refused.at(4).toString(), QStringLiteral("activationFailed"));
        QCOMPARE(dye.activeRecipeId(), -1);

        // The injected native activation callback can also complete successfully without
        // starting a machine or touching host hardware in this isolated fixture.
        completeActivationInline = true;
        acceptActivation = true;
        const QString acceptedActivationId = service->requestActivateRecipe(cloneId);
        QVERIFY(acceptedActivationId != refusedActivationId);
        QTRY_COMPARE_WITH_TIMEOUT(activated.size(), 2, 5000);
        QVERIFY(activated.last().at(1).toBool());
        QCOMPARE(dye.activeRecipeId(), static_cast<int>(cloneId));
        QTRY_VERIFY_WITH_TIMEOUT(!operations.isEmpty(), 5000);
        const QList<QVariant> accepted = operations.takeFirst();
        QCOMPARE(accepted.at(0).toString(), acceptedActivationId);
        QCOMPARE(accepted.at(1).toString(), QStringLiteral("activate"));
        QVERIFY(accepted.at(2).toBool());

        auto recipeRow = [service, recipeId] {
            for (const QVariant& value : service->recipes()) {
                const QVariantMap row = value.toMap();
                if (row.value(QStringLiteral("id")).toLongLong() == recipeId)
                    return row;
            }
            return QVariantMap{};
        };
        QTRY_VERIFY_WITH_TIMEOUT(!recipeRow().isEmpty(), 5000);

        service->requestRelinkRecipeToBag(recipeId, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!operations.isEmpty(), 5000);
        const QList<QVariant> invalidRelink = operations.takeFirst();
        QCOMPARE(invalidRelink.at(1).toString(), QStringLiteral("relink"));
        QVERIFY(!invalidRelink.at(2).toBool());
        QCOMPARE(invalidRelink.at(4).toString(), QStringLiteral("invalidRequest"));
        QCOMPARE(recipeRow().value(QStringLiteral("bagId")).toLongLong(), originalBagId);

        service->requestRelinkRecipeToBag(recipeId, replacementBagId);
        QTRY_VERIFY_WITH_TIMEOUT(!operations.isEmpty(), 15000);
        const QList<QVariant> relinked = operations.takeFirst();
        QCOMPARE(relinked.at(1).toString(), QStringLiteral("relink"));
        QVERIFY(relinked.at(2).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(recipeRow().value(QStringLiteral("bagId")).toLongLong(),
                                  replacementBagId, 15000);
        const QVariantMap afterRelink = recipeRow();
        QCOMPARE(afterRelink.value(QStringLiteral("beanBaseId")).toString(),
                 QStringLiteral("new-bean-id"));
        QCOMPARE(afterRelink.value(QStringLiteral("roasterName")).toString(),
                 QStringLiteral("New Roaster"));
        QCOMPARE(afterRelink.value(QStringLiteral("coffeeName")).toString(),
                 QStringLiteral("New Coffee"));
        QCOMPARE(afterRelink.value(QStringLiteral("profileTitle")).toString(),
                 QStringLiteral("Test profile"));
        QCOMPARE(afterRelink.value(QStringLiteral("doseG")).toDouble(), 18.5);
        QCOMPARE(afterRelink.value(QStringLiteral("yieldMode")).toString(), QStringLiteral("ratio"));
        QCOMPARE(afterRelink.value(QStringLiteral("yieldValue")).toDouble(), 2.1);
        QCOMPARE(afterRelink.value(QStringLiteral("grindPinned")).toString(), QStringLiteral("4.2"));
        QCOMPARE(afterRelink.value(QStringLiteral("rpmPinned")).toLongLong(), 780);
    }

    void recipeDisconnectDoesNotReplayAcceptedMutation()
    {
        const QString path = m_dir.filePath(QStringLiteral("recipe_disconnect.db"));
        qint64 recipeId = 0;
        withRawDb(path, "recipe_service_disconnect", [&recipeId](QSqlDatabase& db) {
            createMinimalShots(db);
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe seed;
            seed.name = QStringLiteral("Disconnect recipe");
            seed.profileTitle = QStringLiteral("Test profile");
            seed.drinkType = QStringLiteral("espresso");
            recipeId = RecipeStorage::insertRecipeStatic(db, seed);
            QVERIFY(recipeId > 0);
        });

        RecipeStorage storage;
        storage.initialize(path);
        SettingsDye dye;
        NativeRecipeServiceSource source(&storage, &dye, {}, {});
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<RecipeStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine engine;
        RecipeService* service = RecipeServiceForeign::create(&engine, &engine);
        service->setNode(&node);
        QTRY_VERIFY_WITH_TIMEOUT(service->ready() && service->recipes().size() == 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(storage.isDbWorkIdle(), 5000);

        const QVariantMap original = service->editableSnapshot(service->recipes().first().toMap());
        QVERIFY(!original.isEmpty());
        QSignalSpy sourceOperations(&source, &RecipeStateSource::operationCompleted);
        QSignalSpy clientOperations(service, &RecipeService::operationCompleted);

        // Hold SQLite's write lock so the real RecipeStorage worker remains in-flight while
        // the facade disconnects. The accepted host write may finish, but reconnect must not
        // submit it a second time or replace the facade's terminal disconnect result.
        const QString lockConnectionName = QStringLiteral("recipe_disconnect_lock");
        RemoveSqlConnection removeLockConnection{lockConnectionName};
        {
            QSqlDatabase lockDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), lockConnectionName);
            lockDb.setDatabaseName(path);
            QVERIFY(lockDb.open());
            {
                QSqlQuery lockQuery(lockDb);
                QVERIFY2(lockQuery.exec(QStringLiteral("BEGIN IMMEDIATE")),
                         qPrintable(lockQuery.lastError().text()));

                service->requestUpdateRecipe(recipeId,
                    {{QStringLiteral("name"), QStringLiteral("Accepted while disconnected")}},
                    original);
                QTRY_VERIFY_WITH_TIMEOUT(!storage.isDbWorkIdle(), 5000);
                service->setNode(nullptr);
                QTRY_COMPARE_WITH_TIMEOUT(clientOperations.size(), 1, 5000);
                QCOMPARE(clientOperations.first().at(1).toString(), QStringLiteral("update"));
                QVERIFY(!clientOperations.first().at(2).toBool());
                QVERIFY(clientOperations.first().at(4).toString().contains(QStringLiteral("Connection lost")));

                QVERIFY2(lockQuery.exec(QStringLiteral("ROLLBACK")),
                         qPrintable(lockQuery.lastError().text()));
            }
            lockDb.close();
        }

        QTRY_VERIFY_WITH_TIMEOUT(storage.isDbWorkIdle(), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(sourceOperations.size(), 1, 5000);
        QCOMPARE(sourceOperations.first().at(1).toString(), QStringLiteral("update"));
        QVERIFY(sourceOperations.first().at(2).toBool());

        service->setNode(&node);
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(service->recipes().first().toMap()
                                      .value(QStringLiteral("name")).toString(),
                                  QStringLiteral("Accepted while disconnected"), 5000);
        QCOMPARE(sourceOperations.size(), 1);
        QCOMPARE(clientOperations.size(), 1);
    }

    void archiveRestoreUsesScopedCanonicalOperation()
    {
        const QString path = m_dir.filePath(QStringLiteral("equipment_restore.db"));
        qint64 id = -1;
        withRawDb(path, "equipment_restore_seed", [&id](QSqlDatabase& db) {
            createMinimalShots(db);
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage package;
            package.name = QStringLiteral("Restorable equipment");
            id = EquipmentStorage::insertPackageStatic(db, package);
            QVERIFY(id > 0);
        });
        EquipmentStorage storage;
        storage.initialize(path);
        SettingsDye dye;
        NativeEquipmentServiceSource source(&storage, &dye, nullptr);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<EquipmentStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine firstEngine;
        QQmlEngine secondEngine;
        auto* first = EquipmentServiceForeign::create(&firstEngine, &firstEngine);
        auto* second = EquipmentServiceForeign::create(&secondEngine, &secondEngine);
        first->setNode(&node);
        second->setNode(&node);
        QTRY_VERIFY_WITH_TIMEOUT(first->ready() && second->ready(), 5000);
        QSignalSpy operations(first, &EquipmentService::operationCompleted);
        QSignalSpy otherOperations(second, &EquipmentService::operationCompleted);
        QSignalSpy legacyUpdates(&storage, &EquipmentStorage::packageUpdated);
        first->requestMarkRemoved(id);
        QTRY_COMPARE_WITH_TIMEOUT(operations.size(), 1, 15000);
        QVERIFY(operations.takeFirst().at(1).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(first->packages().isEmpty() && second->packages().isEmpty(), 5000);
        QSignalSpy archived(first, &EquipmentService::archivedReady);
        QSignalSpy otherArchived(second, &EquipmentService::archivedReady);
        first->requestArchived();
        QTRY_COMPARE_WITH_TIMEOUT(archived.size(), 1, 15000);
        const QVariantList rows = archived.first().at(0).toList();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value(QStringLiteral("id")).toLongLong(), id);
        QVERIFY(otherArchived.isEmpty());
        QSignalSpy invalidated(second, &EquipmentService::packagesChanged);
        bool renamed = false;
        storage.requestUpdatePackageWithCompletion(id,
            {{QStringLiteral("name"), QStringLiteral("Renamed archive")}}, &storage,
            [&renamed](qint64, qint64, bool success, const QString&) { renamed = success; });
        QTRY_VERIFY_WITH_TIMEOUT(renamed, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!invalidated.isEmpty(), 5000);
        second->requestArchived();
        QTRY_COMPARE_WITH_TIMEOUT(otherArchived.size(), 1, 15000);
        QCOMPARE(otherArchived.first().at(0).toList().first().toMap()
                     .value(QStringLiteral("name")).toString(), QStringLiteral("Renamed archive"));
        first->requestRestorePackage(id);
        QTRY_COMPARE_WITH_TIMEOUT(operations.size(), 1, 15000);
        QVERIFY(operations.takeFirst().at(1).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(second->packages().size(), 1, 5000);
        first->requestRestorePackage(id);
        QTRY_COMPARE_WITH_TIMEOUT(operations.size(), 1, 15000);
        const auto refused = operations.takeFirst();
        QVERIFY(!refused.at(1).toBool());
        QCOMPARE(refused.at(3).toString(), QStringLiteral("notArchived"));
        QVERIFY(legacyUpdates.isEmpty());
        QVERIFY(otherOperations.isEmpty());
    }

    void calibrationSourceChecksPackageAndRejectsMalformedQueries()
    {
        const QString path = m_dir.filePath(QStringLiteral("equipment_calibration.db"));
        ShotHistoryStorage history;
        QVERIFY(history.initialize(path));
        qint64 shotId = 0;
        qint64 packageId = 0;
        withRawDb(path, "equipment_calibration_seed", [&](QSqlDatabase& db) {
            ShotRowFixtures::ShotRow row;
            row.grinderBrand = QStringLiteral("Niche");
            row.grinderModel = QStringLiteral("Zero");
            row.grinderSetting = QStringLiteral("4.0");
            shotId = ShotRowFixtures::insertShot(db, row);
            QVERIFY(shotId > 0);
            packageId = ShotRowFixtures::findOrCreatePackage(db, row);
            QVERIFY(packageId > 0);
            QVERIFY(EquipmentStorage::setCalibrationAnchorStatic(db, packageId,
                                                                 QStringLiteral("fine"), shotId));
        });
        EquipmentStorage storage;
        storage.initialize(path);
        SettingsDye dye;
        NativeEquipmentServiceSource source(&storage, &dye, &history);
        QTRY_VERIFY_WITH_TIMEOUT(source.inventoryReady(), 15000);
        QSignalSpy operations(&source, &EquipmentStateSource::operationCompleted);
        QSignalSpy changes(&source, &EquipmentStateSource::grindCalibrationChanged);
        source.execute(QStringLiteral("moved-shot"), QStringLiteral("setGrindAnchor"), shotId,
                       {{QStringLiteral("side"), QStringLiteral("fine")},
                        {QStringLiteral("expectedPackageId"), packageId + 1}}, {}, false);
        QTRY_COMPARE_WITH_TIMEOUT(operations.size(), 1, 15000);
        QCOMPARE(operations.takeFirst().at(3).toString(), QStringLiteral("notInPackage"));
        QVERIFY(changes.isEmpty());
        source.execute(QStringLiteral("clear-anchor"), QStringLiteral("clearGrindAnchor"), packageId,
                       {{QStringLiteral("side"), QStringLiteral("fine")}}, {}, false);
        QTRY_COMPARE_WITH_TIMEOUT(operations.size(), 1, 15000);
        QVERIFY(operations.takeFirst().at(1).toBool());
        QCOMPARE(changes.size(), 1);
        withRawDb(path, "equipment_calibration_check", [packageId](QSqlDatabase& db) {
            QCOMPARE(EquipmentStorage::loadCalibrationAnchorsStatic(db, packageId).fineShotId, 0);
        });

        QSignalSpy reads(&source, &EquipmentStateSource::grindContextResult);
        const QVariantList invalidIds{QStringLiteral("7"), true, 1.5, 0,
                                     std::numeric_limits<double>::infinity(), 9007199254740992.0};
        int request = 0;
        for (const QVariant& invalidId : invalidIds) {
            source.requestGrindContext(QStringLiteral("invalid-read-%1").arg(++request),
                QStringLiteral("calibration"), {{QStringLiteral("packageId"), invalidId}});
            QCOMPARE(reads.size(), 1);
            QCOMPARE(reads.takeFirst().at(2).toString(), QStringLiteral("invalidRequest"));
            source.execute(QStringLiteral("invalid-mark-%1").arg(request),
                QStringLiteral("setGrindAnchor"), shotId,
                {{QStringLiteral("side"), QStringLiteral("fine")},
                 {QStringLiteral("expectedPackageId"), invalidId}}, {}, false);
            QCOMPARE(operations.size(), 1);
            QCOMPARE(operations.takeFirst().at(3).toString(), QStringLiteral("invalidRequest"));
        }
        QVariantMap query{{QStringLiteral("packageId"), packageId},
                          {QStringLiteral("beanBrand"), QStringLiteral("Roaster")},
                          {QStringLiteral("beanType"), QStringLiteral("Coffee")},
                          {QStringLiteral("roastDate"), QString()},
                          {QStringLiteral("profileTitle"), true}};
        source.requestGrindContext(QStringLiteral("invalid-query-type"), QStringLiteral("suggestion"), query);
        QCOMPARE(reads.size(), 1);
        QCOMPARE(reads.takeFirst().at(2).toString(), QStringLiteral("invalidRequest"));
        query[QStringLiteral("profileTitle")] = QString(4097, QLatin1Char('x'));
        source.requestGrindContext(QStringLiteral("invalid-query-length"), QStringLiteral("suggestion"), query);
        QCOMPARE(reads.size(), 1);
        QCOMPARE(reads.takeFirst().at(2).toString(), QStringLiteral("invalidRequest"));
        query[QStringLiteral("profileTitle")] = QStringLiteral("Profile");
        query[QStringLiteral("extra")] = true;
        source.requestGrindContext(QStringLiteral("invalid-query-key"), QStringLiteral("suggestion"), query);
        QCOMPARE(reads.size(), 1);
        QCOMPARE(reads.takeFirst().at(2).toString(), QStringLiteral("invalidRequest"));
    }

    void grindRequestsAreCorrelatedAndDisconnectDoesNotReplay()
    {
        PendingEquipmentSource source;
        source.setLoading(false);
        source.setInventoryReady(true);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<EquipmentStateSourceAPI>(&source));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        QQmlEngine firstEngine;
        QQmlEngine secondEngine;
        auto* first = EquipmentServiceForeign::create(&firstEngine, &firstEngine);
        auto* second = EquipmentServiceForeign::create(&secondEngine, &secondEngine);
        first->setNode(&node);
        second->setNode(&node);
        QTRY_VERIFY_WITH_TIMEOUT(first->ready() && second->ready(), 5000);
        QSignalSpy calibration(first, &EquipmentService::grindCalibrationReady);
        QSignalSpy otherCalibration(second, &EquipmentService::grindCalibrationReady);
        QSignalSpy suggestion(first, &EquipmentService::grindSuggestionReady);
        first->requestGrindCalibration(7);
        first->requestGrindSuggestion(7, QStringLiteral("Roaster"), QStringLiteral("Coffee"),
                                     QStringLiteral("2026-10-01"), QStringLiteral("Profile"));
        QTRY_COMPARE_WITH_TIMEOUT(source.grindReads.size(), 2, 5000);
        emit source.grindContextResult(QStringLiteral("foreign-request"), {}, {});
        emit source.grindContextResult(source.grindReads.at(0),
                                      {{QStringLiteral("hasShots"), true}}, {});
        emit source.grindContextResult(source.grindReads.at(1),
                                      {{QStringLiteral("setting"), QStringLiteral("4.2")}}, {});
        QTRY_COMPARE_WITH_TIMEOUT(calibration.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(suggestion.size(), 1, 5000);
        QCOMPARE(calibration.first().at(0).toLongLong(), 7);
        const QVariantMap result = suggestion.first().at(0).toMap();
        QCOMPARE(result.value(QStringLiteral("packageId")).toLongLong(), 7);
        QCOMPARE(result.value(QStringLiteral("beanBrand")).toString(), QStringLiteral("Roaster"));
        QCOMPARE(result.value(QStringLiteral("profileTitle")).toString(), QStringLiteral("Profile"));
        QCOMPARE(result.value(QStringLiteral("setting")).toString(), QStringLiteral("4.2"));
        QVERIFY(otherCalibration.isEmpty());

        QSignalSpy marked(first, &EquipmentService::grindAnchorMarked);
        QSignalSpy otherMarked(second, &EquipmentService::grindAnchorMarked);
        first->requestSetGrindAnchor(42, QStringLiteral("fine"), 7);
        QTRY_COMPARE_WITH_TIMEOUT(source.calls, 1, 5000);
        QCOMPARE(source.lastOperation, QStringLiteral("setGrindAnchor"));
        QCOMPARE(source.lastId, 42);
        QCOMPARE(source.lastFields.value(QStringLiteral("expectedPackageId")).toLongLong(), 7);
        emit source.operationCompleted(source.lastRequestId, false, 7, QStringLiteral("notInPackage"));
        QTRY_COMPARE_WITH_TIMEOUT(marked.size(), 1, 5000);
        QCOMPARE(marked.first().at(0).toLongLong(), 42);
        QCOMPARE(marked.first().at(1).toString(), QStringLiteral("fine"));
        QCOMPARE(marked.first().at(2).toString(), QStringLiteral("notInPackage"));
        QVERIFY(otherMarked.isEmpty());

        QSignalSpy cleared(first, &EquipmentService::grindAnchorCleared);
        QSignalSpy archivedFailures(first, &EquipmentService::archivedFailed);
        first->requestClearGrindAnchor(7, QStringLiteral("coarse"));
        first->requestArchived();
        first->requestGrindCalibration(7);
        QTRY_COMPARE_WITH_TIMEOUT(source.calls, 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(source.archivedReads.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(source.grindReads.size(), 3, 5000);
        const QString pendingWrite = source.lastRequestId;
        first->setNode(nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(cleared.size(), 1, 5000);
        QVERIFY(!cleared.first().at(2).toBool());
        QCOMPARE(cleared.first().at(1).toString(), QStringLiteral("coarse"));
        QTRY_COMPARE_WITH_TIMEOUT(archivedFailures.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(calibration.size(), 2, 5000);
        QCOMPARE(calibration.last().at(1).toMap().value(QStringLiteral("error")).toString(),
                 QStringLiteral("disconnected"));
        first->setNode(&node);
        QTRY_VERIFY_WITH_TIMEOUT(first->ready(), 5000);
        emit source.operationCompleted(pendingWrite, true, 7, {});
        emit source.grindContextResult(source.grindReads.last(), {}, {});
        emit source.archivedResult(source.archivedReads.last(), {}, {});
        QCoreApplication::processEvents();
        QCOMPARE(source.calls, 2);
        QCOMPARE(source.grindReads.size(), 3);
        QCOMPARE(source.archivedReads.size(), 1);
        QCOMPARE(cleared.size(), 1);
        QCOMPARE(calibration.size(), 2);
    }

    void commonOperationTrackerRejectsLateRepliesAndReentrantDisconnect()
    {
        ServiceOperation operation;
        QSignalSpy completed(&operation, &ServiceOperation::completed);
        const auto first = operation.begin(QStringLiteral("update"), 1, QStringLiteral("first"), true);
        QVERIFY(!first.isEmpty());
        operation.disconnectPending();
        const auto second = operation.begin(QStringLiteral("delete"), 2, QStringLiteral("second"), true);
        QVERIFY(!second.isEmpty());
        operation.finish(first, true, 1, {});
        QVERIFY(operation.busy());
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), QStringLiteral("first"));
        QVERIFY(!completed.first().at(3).toBool());
        operation.finish(second, true, 2, {});
        QCOMPARE(completed.size(), 2);
        QVERIFY(!operation.busy());
        const auto completedId = operation.begin(QStringLiteral("update"), 4, QStringLiteral("old"), true);
        QString nextId;
        const auto reentrant = connect(&operation, &ServiceOperation::busyChanged, &operation, [&] {
            if (!operation.busy()) nextId = operation.begin(QStringLiteral("update"), 5, QStringLiteral("new"), true);
        });
        operation.finish(completedId, false, 4, QStringLiteral("old error"));
        QVERIFY(!nextId.isEmpty());
        QVERIFY(operation.error().isEmpty());
        disconnect(reentrant);
        operation.finish(nextId, true, 5, {});
        completed.clear();
        connect(&operation, &ServiceOperation::busyChanged, &operation, [&operation] {
            if (operation.busy()) operation.disconnectPending();
        });
        QVERIFY(operation.begin(QStringLiteral("update"), 3, QStringLiteral("reentrant"), true).isEmpty());
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(!operation.busy());
    }

    void tcpDisconnectFailsPendingOperationWithoutReplay()
    {
        PendingEquipmentSource source;
        source.setLoading(false);
        source.setInventoryReady(true);
        QRemoteObjectHost host;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<EquipmentStateSourceAPI>(&source));

        auto node = std::make_unique<QRemoteObjectNode>();
        QVERIFY(node->connectToNode(host.hostUrl()));
        QQmlEngine engine;
        EquipmentService* service = EquipmentServiceForeign::create(&engine, &engine);
        service->setNode(node.get());
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 5000);
        QSignalSpy completed(service, &EquipmentService::operationCompleted);
        const QString requestId = QStringLiteral("pending-no-replay");
        service->createPackage({{QStringLiteral("name"), QStringLiteral("Uncertain")}}, false, requestId);
        QTRY_COMPARE_WITH_TIMEOUT(source.calls, 1, 5000);
        QVERIFY(!source.lastRequestId.isEmpty());
        QVERIFY(service->busy());

        node.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!service->ready(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 3000);
        QCOMPARE(completed.first().at(0).toString(), requestId);
        QVERIFY(!completed.first().at(1).toBool());
        QVERIFY(completed.first().at(3).toString().contains(QStringLiteral("unknown"), Qt::CaseInsensitive));
        QVERIFY(!service->busy());

        node = std::make_unique<QRemoteObjectNode>();
        QVERIFY(node->connectToNode(host.hostUrl()));
        service->setNode(node.get());
        QTRY_VERIFY_WITH_TIMEOUT(service->ready(), 5000);
        QTest::qWait(100);
        QCOMPARE(source.calls, 1);
        QCOMPARE(completed.size(), 1);
    }
};

QTEST_GUILESS_MAIN(tst_EquipmentService)
#include "tst_equipmentservice.moc"
