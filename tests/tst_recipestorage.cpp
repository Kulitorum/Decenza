#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "history/recipestorage.h"
#include "history/coffeebagstorage.h"

// Recipe storage (add-recipes): CRUD statics, variant-map round-trip,
// inventory MRU + shot-count aggregate, the delete-vs-archive lifecycle
// guard, clone provenance, and bean-link resolution to the current open bag.

template<typename Work>
static void withRawDb(const QString& path, const QString& connName, Work&& work) {
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(path);
        db.open();
        work(db);
    }
    QSqlDatabase::removeDatabase(connName);
}

// Minimal shots table carrying just the columns the recipe queries read
// (the shot-count aggregate and the delete guard both key on recipe_id).
static void createMinimalShots(QSqlDatabase& db) {
    QSqlQuery(db).exec(R"(
        CREATE TABLE shots (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            recipe_id INTEGER,
            steam_json TEXT
        )
    )");
}

static qint64 insertShotForRecipe(QSqlDatabase& db, qint64 recipeId) {
    QSqlQuery q(db);
    q.prepare("INSERT INTO shots (recipe_id) VALUES (?)");
    q.addBindValue(recipeId);
    q.exec();
    return q.lastInsertId().toLongLong();
}

static Recipe sampleRecipe() {
    Recipe r;
    r.name = "Morning capp";
    r.profileTitle = "D-Flow / default";
    r.profileJson = "{\"title\":\"D-Flow / default\"}";
    r.beanBaseId = "bb-uuid-1";
    r.roasterName = "Roaster";
    r.coffeeName = "Guji";
    r.equipmentId = 7;
    r.doseG = 18.0;
    r.yieldG = 40.0;
    r.tempOverrideC = 92.5;
    r.grindPinned = "";  // inherits
    r.steamJson = "{\"hasMilk\":true,\"milkWeightG\":150,\"pitcherName\":\"Small\","
                  "\"pitcherVolumeMl\":350,\"temperatureC\":140,\"flow\":1.2,\"timeoutSec\":40}";
    r.createdFromShotId = 42;
    return r;
}

class tst_RecipeStorage : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_seq = 0;
    QString freshDbPath() { return m_dir.filePath(QString("rec_%1.db").arg(++m_seq)); }

private slots:

    // --- variant map round-trip ---

    void variantMapRoundTrip() {
        const Recipe r = sampleRecipe();
        const Recipe back = Recipe::fromVariantMap(r.toVariantMap());
        QCOMPARE(back.name, r.name);
        QCOMPARE(back.profileTitle, r.profileTitle);
        QCOMPARE(back.profileJson, r.profileJson);
        QCOMPARE(back.beanBaseId, r.beanBaseId);
        QCOMPARE(back.roasterName, r.roasterName);
        QCOMPARE(back.coffeeName, r.coffeeName);
        QCOMPARE(back.equipmentId, r.equipmentId);
        QCOMPARE(back.doseG, r.doseG);
        QCOMPARE(back.yieldG, r.yieldG);
        QCOMPARE(back.tempOverrideC, r.tempOverrideC);
        QCOMPARE(back.grindPinned, r.grindPinned);
        QCOMPARE(back.steamJson, r.steamJson);
        QCOMPARE(back.archived, r.archived);
        QCOMPARE(back.createdFromShotId, r.createdFromShotId);
    }

    void variantMapAbsentKeysKeepDefaults() {
        const Recipe r = Recipe::fromVariantMap({{"name", "Only name"}});
        QCOMPARE(r.name, QString("Only name"));
        QCOMPARE(r.equipmentId, (qint64)0);
        QCOMPARE(r.doseG, 0.0);
        QCOMPARE(r.archived, false);
        QVERIFY(r.grindPinned.isEmpty());
    }

    // --- insert / load / update statics ---

    void insertLoadRoundTrip() {
        withRawDb(freshDbPath(), "rt", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            const qint64 id = RecipeStorage::insertRecipeStatic(db, sampleRecipe());
            QVERIFY(id > 0);
            const Recipe loaded = RecipeStorage::loadRecipeStatic(db, id);
            QVERIFY(loaded.isValid());
            QCOMPARE(loaded.name, QString("Morning capp"));
            QCOMPARE(loaded.doseG, 18.0);
            QCOMPARE(loaded.steamJson, sampleRecipe().steamJson);
            QCOMPARE(loaded.createdFromShotId, (qint64)42);
            // Minimal recipe: only a name (profile optional at the storage
            // layer — required-ness is enforced by the composer/tools).
            Recipe minimal;
            minimal.name = "Bare";
            const qint64 id2 = RecipeStorage::insertRecipeStatic(db, minimal);
            QVERIFY(id2 > 0);
            QVERIFY(RecipeStorage::loadRecipeStatic(db, id2).isValid());
        });
    }

    void loadMissingReturnsInvalid() {
        withRawDb(freshDbPath(), "miss", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(!RecipeStorage::loadRecipeStatic(db, 999).isValid());
        });
    }

    void updateFields() {
        withRawDb(freshDbPath(), "upd", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            const qint64 id = RecipeStorage::insertRecipeStatic(db, sampleRecipe());

            QVERIFY(RecipeStorage::updateRecipeFieldsStatic(db, id, {
                {"doseG", 17.5}, {"grindPinned", "2.4"}, {"name", "Evening capp"}}));
            Recipe r = RecipeStorage::loadRecipeStatic(db, id);
            QCOMPARE(r.doseG, 17.5);
            QCOMPARE(r.grindPinned, QString("2.4"));
            QCOMPARE(r.name, QString("Evening capp"));

            // Unknown keys are ignored; a map of only unknown keys updates nothing.
            QVERIFY(!RecipeStorage::updateRecipeFieldsStatic(db, id, {{"bogus", 1}}));

            // Empty string collapses to NULL (insert-equivalent coercion):
            // clearing the pin returns the recipe to inherit.
            QVERIFY(RecipeStorage::updateRecipeFieldsStatic(db, id, {{"grindPinned", ""}}));
            QVERIFY(RecipeStorage::loadRecipeStatic(db, id).grindPinned.isEmpty());

            // Missing row: no rows affected -> false.
            QVERIFY(!RecipeStorage::updateRecipeFieldsStatic(db, 999, {{"doseG", 20.0}}));
        });
    }

    // --- inventory: archived filter, MRU order, shot-count aggregate ---

    void inventoryFiltersAndOrders() {
        withRawDb(freshDbPath(), "inv", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            createMinimalShots(db);

            Recipe a = sampleRecipe(); a.name = "A"; a.lastUsedEpoch = 100;
            Recipe b = sampleRecipe(); b.name = "B"; b.lastUsedEpoch = 300;
            Recipe c = sampleRecipe(); c.name = "C"; c.lastUsedEpoch = 200; c.archived = true;
            const qint64 idA = RecipeStorage::insertRecipeStatic(db, a);
            const qint64 idB = RecipeStorage::insertRecipeStatic(db, b);
            const qint64 idC = RecipeStorage::insertRecipeStatic(db, c);
            QVERIFY(idA > 0 && idB > 0 && idC > 0);

            insertShotForRecipe(db, idB);
            insertShotForRecipe(db, idB);

            const QVector<InventoryRecipe> active = RecipeStorage::loadInventoryStatic(db, false);
            QCOMPARE(active.size(), 2);
            QCOMPARE(active[0].recipe.name, QString("B"));  // MRU first
            QCOMPARE(active[0].shotCount, (qint64)2);
            QCOMPARE(active[1].recipe.name, QString("A"));
            QCOMPARE(active[1].shotCount, (qint64)0);

            const QVector<InventoryRecipe> archived = RecipeStorage::loadInventoryStatic(db, true);
            QCOMPARE(archived.size(), 1);
            QCOMPARE(archived[0].recipe.name, QString("C"));
        });
    }

    // --- bean-link resolution ---

    void resolveOpenBag() {
        withRawDb(freshDbPath(), "bag", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));

            CoffeeBag oldBag;
            oldBag.roasterName = "Roaster"; oldBag.coffeeName = "Guji";
            oldBag.beanBaseId = "bb-uuid-1";
            oldBag.inInventory = false;  // finished
            oldBag.lastUsedEpoch = 100;
            const qint64 finishedId = CoffeeBagStorage::insertBagStatic(db, oldBag);

            CoffeeBag newBag = oldBag;
            newBag.inInventory = true;
            newBag.lastUsedEpoch = 200;
            const qint64 openId = CoffeeBagStorage::insertBagStatic(db, newBag);
            QVERIFY(finishedId > 0 && openId > 0);

            // Canonical id resolves to the OPEN bag, not the finished one.
            Recipe r = sampleRecipe();
            QCOMPARE(RecipeStorage::resolveOpenBagStatic(db, r), openId);

            // Identity fallback (no canonical id), case-insensitive.
            r.beanBaseId.clear();
            r.roasterName = "ROASTER"; r.coffeeName = "guji";
            QCOMPARE(RecipeStorage::resolveOpenBagStatic(db, r), openId);

            // No open bag of the bean -> -1 (display state, not an error).
            Recipe other = sampleRecipe();
            other.beanBaseId = "bb-uuid-other";
            other.roasterName = "Elsewhere"; other.coffeeName = "Nope";
            QCOMPARE(RecipeStorage::resolveOpenBagStatic(db, other), (qint64)-1);

            // Bean-less recipe -> -1.
            Recipe bare;
            bare.name = "Bare";
            QCOMPARE(RecipeStorage::resolveOpenBagStatic(db, bare), (qint64)-1);
        });
    }

    // --- async lifecycle: delete guard + clone provenance ---

    void deleteGuardAndClone() {
        const QString path = freshDbPath();
        qint64 usedId = 0, unusedId = 0;
        withRawDb(path, "life_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            createMinimalShots(db);
            Recipe used = sampleRecipe(); used.name = "Used";
            Recipe unused = sampleRecipe(); unused.name = "Unused";
            usedId = RecipeStorage::insertRecipeStatic(db, used);
            unusedId = RecipeStorage::insertRecipeStatic(db, unused);
            insertShotForRecipe(db, usedId);
        });
        QVERIFY(usedId > 0 && unusedId > 0);

        RecipeStorage storage;
        storage.initialize(path);

        // Used recipe refuses deletion (archive is the only exit).
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeDeleted);
            storage.requestDeleteRecipe(usedId);
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(0).toLongLong(), usedId);
            QCOMPARE(spy.at(0).at(1).toBool(), false);
        }

        // Unused recipe deletes fine.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeDeleted);
            storage.requestDeleteRecipe(unusedId);
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(1).toBool(), true);
        }

        // Clone: all fields copied, provenance points at the source recipe,
        // the source's golden-shot link is NOT copied.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeCreated);
            storage.requestCloneRecipe(usedId, "Anna's latte");
            QTRY_COMPARE(spy.count(), 1);
            const qint64 cloneId = spy.at(0).at(0).toLongLong();
            QVERIFY(cloneId > 0);
            const QVariantMap clone = spy.at(0).at(1).toMap();
            QCOMPARE(clone.value("name").toString(), QString("Anna's latte"));
            QCOMPARE(clone.value("doseG").toDouble(), 18.0);
            QCOMPARE(clone.value("steamJson").toString(), sampleRecipe().steamJson);
            QCOMPARE(clone.value("clonedFromRecipeId").toLongLong(), usedId);
            QCOMPARE(clone.value("createdFromShotId").toLongLong(), (qint64)0);
        }

        // Archive: recipe leaves the active inventory but stays loadable.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestArchiveRecipe(usedId);
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(1).toBool(), true);
        }
        withRawDb(path, "life_verify", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::loadRecipeStatic(db, usedId).archived);
            const QVector<InventoryRecipe> active = RecipeStorage::loadInventoryStatic(db, false);
            for (const InventoryRecipe& e : active)
                QVERIFY(e.recipe.id != usedId);
        });
    }
};

QTEST_MAIN(tst_RecipeStorage)
#include "tst_recipestorage.moc"
