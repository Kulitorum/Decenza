#include <QtTest>
#include <QRegularExpression>
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
    r.drinkType = "latte";  // migration-28 field; round-trips through COL_STR
    r.beanBaseId = "bb-uuid-1";
    r.roasterName = "Roaster";
    r.coffeeName = "Guji";
    r.equipmentId = 7;
    r.doseG = 18.0;
    r.yieldG = 40.0;
    r.tempOverrideC = 92.5;
    r.grindPinned = "";  // inherits
    r.rpmPinned = 90;    // migration-26 field; round-trips through COL_EPOCH
    // Real steam-block key shape (see MainController::currentSteamSpecJson).
    r.steamJson = "{\"hasMilk\":true,\"milkWeightG\":150,\"pitcherName\":\"Small\","
                  "\"durationSec\":40,\"flow\":120,\"temperatureC\":140}";
    r.createdFromShotId = 42;
    r.clonedFromRecipeId = 3;
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
        QCOMPARE(back.drinkType, r.drinkType);
        QCOMPARE(back.beanBaseId, r.beanBaseId);
        QCOMPARE(back.roasterName, r.roasterName);
        QCOMPARE(back.coffeeName, r.coffeeName);
        QCOMPARE(back.equipmentId, r.equipmentId);
        QCOMPARE(back.doseG, r.doseG);
        QCOMPARE(back.yieldG, r.yieldG);
        QCOMPARE(back.tempOverrideC, r.tempOverrideC);
        QCOMPARE(back.grindPinned, r.grindPinned);
        QCOMPARE(back.rpmPinned, r.rpmPinned);
        QCOMPARE(back.steamJson, r.steamJson);
        QCOMPARE(back.archived, r.archived);
        QCOMPARE(back.createdFromShotId, r.createdFromShotId);
        QCOMPARE(back.clonedFromRecipeId, r.clonedFromRecipeId);
    }

    void variantMapAbsentKeysKeepDefaults() {
        const Recipe r = Recipe::fromVariantMap({{"name", "Only name"}});
        QCOMPARE(r.name, QString("Only name"));
        QCOMPARE(r.equipmentId, (qint64)0);
        QCOMPARE(r.doseG, 0.0);
        QCOMPARE(r.archived, false);
        QVERIFY(r.grindPinned.isEmpty());
        QVERIFY(r.drinkType.isEmpty());
    }

    // --- drink-type derivation + hot-water gate (add-recipe-wizard-tea) ---

    void hotWaterActiveGate() {
        QVERIFY(!Recipe::hotWaterActive(QString()));
        QVERIFY(!Recipe::hotWaterActive("not json"));
        QVERIFY(!Recipe::hotWaterActive("{\"hasWater\":false,\"vesselName\":\"Cup\"}"));
        QVERIFY(Recipe::hotWaterActive("{\"hasWater\":true,\"vesselName\":\"Cup\"}"));
    }

    void saveValidationRule() {
        const QString water = "{\"hasWater\":true,\"vesselName\":\"Cup\"}";
        // Name + profile: the classic case.
        QVERIFY(Recipe::saveValidationPasses("Capp", "D-Flow", QString()));
        // Name + hot-water block, no profile: hot-water tea.
        QVERIFY(Recipe::saveValidationPasses("Earl Grey", QString(), water));
        // No profile and no hot water: rejected on every surface.
        QVERIFY(!Recipe::saveValidationPasses("Broken", QString(), QString()));
        // hasWater:false does not excuse a missing profile; no name never passes.
        QVERIFY(!Recipe::saveValidationPasses("Broken", "",
                                              "{\"hasWater\":false}"));
        QVERIFY(!Recipe::saveValidationPasses("  ", "D-Flow", QString()));
    }

    void deriveDrinkTypeMatrix() {
        const QString waterAfter  = "{\"hasWater\":true,\"vesselName\":\"Cup\",\"order\":\"after\"}";
        const QString waterBefore = "{\"hasWater\":true,\"vesselName\":\"Cup\",\"order\":\"before\"}";
        const QString milk        = "{\"hasMilk\":true,\"milkWeightG\":150}";

        Recipe r;
        r.profileTitle = "Some profile";

        // Bare espresso profile; empty beverage_type is espresso.
        QCOMPARE(Recipe::deriveDrinkType(r, ""), QString("espresso"));
        QCOMPARE(Recipe::deriveDrinkType(r, "espresso"), QString("espresso"));

        // Profile beverage_type routes filter and tea.
        QCOMPARE(Recipe::deriveDrinkType(r, "filter"), QString("filter"));
        QCOMPARE(Recipe::deriveDrinkType(r, "pourover"), QString("filter"));
        QCOMPARE(Recipe::deriveDrinkType(r, "Tea_Portafilter "), QString("tea"));

        // Hot-water order splits americano / long black; missing order = after.
        r.hotWaterJson = waterAfter;
        QCOMPARE(Recipe::deriveDrinkType(r, "espresso"), QString("americano"));
        r.hotWaterJson = waterBefore;
        QCOMPARE(Recipe::deriveDrinkType(r, "espresso"), QString("long_black"));
        r.hotWaterJson = "{\"hasWater\":true,\"vesselName\":\"Cup\"}";
        QCOMPARE(Recipe::deriveDrinkType(r, "espresso"), QString("americano"));

        // Milk wins over added water (a milk drink with a splash is still
        // a milk drink) — the documented ambiguous-combination rule.
        r.steamJson = milk;
        r.hotWaterJson = waterAfter;
        QCOMPARE(Recipe::deriveDrinkType(r, "espresso"), QString("latte"));
        r.hotWaterJson.clear();
        QCOMPARE(Recipe::deriveDrinkType(r, "espresso"), QString("latte"));

        // Tea profile beats milk (the profile type is the strongest signal).
        QCOMPARE(Recipe::deriveDrinkType(r, "tea_portafilter"), QString("tea"));

        // Profile-less + hot water = hot-water tea; with a profile it is not.
        Recipe hw;
        hw.hotWaterJson = waterAfter;
        QCOMPARE(Recipe::deriveDrinkType(hw, ""), QString("tea_hotwater"));
        hw.profileTitle = "Some profile";
        QCOMPARE(Recipe::deriveDrinkType(hw, "espresso"), QString("americano"));
    }

    void lastEquipmentForDrinkType() {
        withRawDb(freshDbPath(), "lastequip", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            const auto make = [&](const QString& type, qint64 equipId, qint64 lastUsed,
                                  bool archived = false) {
                Recipe r;
                r.name = QString("%1-%2").arg(type).arg(lastUsed);
                r.profileTitle = "P";
                r.drinkType = type;
                r.equipmentId = equipId;
                r.lastUsedEpoch = lastUsed;
                r.archived = archived;
                QVERIFY(RecipeStorage::insertRecipeStatic(db, r) > 0);
            };
            make("espresso", 1, 100);
            make("espresso", 2, 200);          // most recent espresso package
            make("tea",      5, 300);
            make("tea",      6, 400, true);    // archived: ignored
            make("filter",   0, 500);          // no equipment: ignored

            QCOMPARE(RecipeStorage::lastEquipmentForDrinkTypeStatic(db, "espresso"), qint64(2));
            QCOMPARE(RecipeStorage::lastEquipmentForDrinkTypeStatic(db, "tea"), qint64(5));
            QCOMPARE(RecipeStorage::lastEquipmentForDrinkTypeStatic(db, "filter"), qint64(0));
            QCOMPARE(RecipeStorage::lastEquipmentForDrinkTypeStatic(db, "latte"), qint64(0));
            QCOMPARE(RecipeStorage::lastEquipmentForDrinkTypeStatic(db, ""), qint64(0));
        });
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
            QCOMPARE(loaded.rpmPinned, (qint64)90);
            QCOMPARE(loaded.steamJson, sampleRecipe().steamJson);
            QCOMPARE(loaded.createdFromShotId, (qint64)42);
            // rpm_pinned is COL_EPOCH: updating to 0 clears it to NULL (the
            // pin-clearing path MainController relies on when the override is
            // turned off), and it reloads as 0.
            QVERIFY(RecipeStorage::updateRecipeFieldsStatic(db, id, {{"rpmPinned", 0}}));
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, id).rpmPinned, (qint64)0);
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

    // Hot-water block (finish-recipes-first-class): the opt-in water-vessel
    // snapshot that makes an Americano possible round-trips through the kCols
    // column set exactly like the steam block.
    void hotWaterBlockRoundTrip() {
        withRawDb(freshDbPath(), "hw", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            // Fresh-DB schema converges: ensureTableStatic includes the column.
            bool hasHotWaterCol = false;
            QSqlQuery info(db);
            QVERIFY(info.exec("PRAGMA table_info(recipes)"));
            while (info.next()) {
                if (info.value(1).toString() == "hot_water_json") { hasHotWaterCol = true; break; }
            }
            QVERIFY(hasHotWaterCol);

            Recipe r;
            r.name = "Americano";
            r.profileTitle = "Filter 2.0";
            r.hotWaterJson = "{\"hasWater\":true,\"vesselName\":\"Cup\",\"volume\":120,"
                             "\"mode\":\"volume\",\"flowRate\":40,\"temperatureC\":90,\"order\":\"after\"}";
            const qint64 id = RecipeStorage::insertRecipeStatic(db, r);
            QVERIFY(id > 0);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, id).hotWaterJson, r.hotWaterJson);

            // Update round-trips through the kCols update map; empty clears to NULL.
            QVERIFY(RecipeStorage::updateRecipeFieldsStatic(db, id, {{"hotWaterJson", QString()}}));
            QVERIFY(RecipeStorage::loadRecipeStatic(db, id).hotWaterJson.isEmpty());
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
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression("ignoring unknown update key"));
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

            // Canonical-miss fallthrough: a recipe whose beanBaseId matches NO
            // bag but whose roaster+coffee identity matches an open bag must
            // fall through to the identity pass (real case: bags created before
            // Bean Base linking). If someone made a present-but-unmatched
            // canonical id return -1, older bags would silently lose their bean.
            Recipe canonMiss = sampleRecipe();
            canonMiss.beanBaseId = "bb-uuid-nonexistent";  // matches no bag
            QCOMPARE(RecipeStorage::resolveOpenBagStatic(db, canonMiss), openId);
        });
    }

    void resolveOpenBagMruTieBreak() {
        withRawDb(freshDbPath(), "bag_mru", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            // Two OPEN bags of the same bean -> the most recently used wins.
            CoffeeBag a; a.roasterName = "Roaster"; a.coffeeName = "Guji";
            a.beanBaseId = "bb-uuid-1"; a.inInventory = true; a.lastUsedEpoch = 100;
            CoffeeBag b = a; b.lastUsedEpoch = 500;
            const qint64 olderId = CoffeeBagStorage::insertBagStatic(db, a);
            const qint64 newerId = CoffeeBagStorage::insertBagStatic(db, b);
            QVERIFY(olderId > 0 && newerId > 0);
            QCOMPARE(RecipeStorage::resolveOpenBagStatic(db, sampleRecipe()), newerId);
        });
    }

    // --- activation bundle ---

    // requestRecipeForActivation loads recipe + resolved bag in ONE pass and is
    // the terminus of every activation caller (idle pill, RecipesPage, MCP,
    // web). Its three documented contracts — happy path, missing recipe emits
    // an EMPTY map (activation must fail cleanly, NOT hang), and bean-less /
    // no-open-bag emits openBagId == -1 — are each load-bearing.
    // drink_type follows the blocks on updates that change them without
    // setting it (MCP/web edits, steam stamps) — and a stored profile-derived
    // type (tea/filter) survives a block stamp when the profile didn't change.
    void updateRederivesDrinkType() {
        const QString path = freshDbPath();
        qint64 espressoId = 0, teaId = 0;
        withRawDb(path, "rederive_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe e; e.name = "Straight"; e.profileTitle = "P"; e.drinkType = "espresso";
            espressoId = RecipeStorage::insertRecipeStatic(db, e);
            Recipe t; t.name = "Sencha"; t.profileTitle = "Tea portafilter/Sencha";
            t.drinkType = "tea";
            teaId = RecipeStorage::insertRecipeStatic(db, t);
        });
        QVERIFY(espressoId > 0 && teaId > 0);

        RecipeStorage storage;
        storage.initialize(path);

        // Adding a hot-water block to an espresso re-derives to americano.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestUpdateRecipe(espressoId, {{"hotWaterJson",
                "{\"hasWater\":true,\"vesselName\":\"Cup\",\"order\":\"after\"}"}});
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(spy.at(0).at(1).toBool());
        }
        // A steam stamp on a tea recipe must NOT turn it into a latte.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestUpdateRecipe(teaId, {{"steamJson",
                "{\"hasMilk\":true,\"milkWeightG\":150}"}});
            QTRY_COMPARE(spy.count(), 1);
        }
        // An explicit drinkType from the caller always wins (no re-derive).
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestUpdateRecipe(espressoId, {
                {"hotWaterJson", "{\"hasWater\":true,\"vesselName\":\"Cup\",\"order\":\"before\"}"},
                {"drinkType", "americano"}});
            QTRY_COMPARE(spy.count(), 1);
        }
        withRawDb(path, "rederive_check", [&](QSqlDatabase& db) {
            // First update re-derived; third kept the caller's explicit value
            // (americano, despite the order flipping to "before").
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, espressoId).drinkType,
                     QString("americano"));
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, teaId).drinkType, QString("tea"));
        });
    }

    void updateRederivesDrinkTypeWithBeverageHint() {
        // The MCP/web update path attaches a transient profileBeverageType
        // hint (installed profiles embed no JSON; the catalog is main-thread
        // only). requestUpdateRecipe strips the hint before the column write
        // and uses it for the drink-type re-derivation — live-caught bug:
        // without it, switching a recipe to an installed tea profile derived
        // "espresso".
        const QString path = freshDbPath();
        qint64 id = 0;
        withRawDb(path, "hint_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe r;
            r.name = "Becomes tea";
            r.profileTitle = "Default";
            r.drinkType = "espresso";
            id = RecipeStorage::insertRecipeStatic(db, r);
        });
        QVERIFY(id > 0);

        RecipeStorage storage;
        storage.initialize(path);

        QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
        storage.requestUpdateRecipe(id, {
            {"profileTitle", "Tea portafilter/black tea"},
            {"profileBeverageType", "tea_portafilter"}});
        QTRY_COMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(1).toBool());

        withRawDb(path, "hint_verify", [&](QSqlDatabase& db) {
            const Recipe updated = RecipeStorage::loadRecipeStatic(db, id);
            QCOMPARE(updated.profileTitle, QString("Tea portafilter/black tea"));
            QCOMPARE(updated.drinkType, QString("tea"));
            // The hint is transient — updateRecipeFieldsStatic never saw it
            // as a column (an unknown key would have been warned + skipped;
            // storage round-trip proves no such column landed).
        });
    }

    // The storage layer enforces the save invariant against the RESULTING row,
    // so every surface (wizard, MCP, web) inherits it — the web update route
    // shipped without the MCP tool's early guard, and neither surface caught
    // removing the hot-water block from an already profile-less recipe.
    void updateCannotStrandRecipe() {
        const QString path = freshDbPath();
        qint64 espId = 0, hwId = 0;
        withRawDb(path, "strand_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe esp;
            esp.name = "Espresso"; esp.profileTitle = "Default"; esp.drinkType = "espresso";
            espId = RecipeStorage::insertRecipeStatic(db, esp);
            Recipe hw;  // profile-less hot-water tea
            hw.name = "Earl Grey"; hw.drinkType = "tea_hotwater";
            hw.hotWaterJson = "{\"hasWater\":true,\"vesselName\":\"Mug\"}";
            hwId = RecipeStorage::insertRecipeStatic(db, hw);
        });
        QVERIFY(espId > 0 && hwId > 0);

        RecipeStorage storage;
        storage.initialize(path);

        // Clearing the profile on a non-hot-water recipe is rejected — the row
        // is untouched.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestUpdateRecipe(espId, {{"profileTitle", QString()}});
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(!spy.at(0).at(1).toBool());  // failed
        }
        // Removing the hot-water block from a profile-less recipe is rejected
        // too (the case the per-surface profileTitle guard misses entirely).
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestUpdateRecipe(hwId, {{"hotWaterJson", QString()}});
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(!spy.at(0).at(1).toBool());
        }
        // A hint-only patch (nothing persistable) fails cleanly, not silently.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestUpdateRecipe(espId, {{"profileBeverageType", "espresso"}});
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(!spy.at(0).at(1).toBool());
        }
        // Both rows survived the rejected updates intact.
        withRawDb(path, "strand_verify", [&](QSqlDatabase& db) {
            const Recipe esp = RecipeStorage::loadRecipeStatic(db, espId);
            QCOMPARE(esp.profileTitle, QString("Default"));
            QVERIFY(Recipe::saveValidationPasses(esp.name, esp.profileTitle, esp.hotWaterJson));
            const Recipe hw = RecipeStorage::loadRecipeStatic(db, hwId);
            QVERIFY(Recipe::hotWaterActive(hw.hotWaterJson));
        });
    }

    void isKnownDrinkTypeVocabulary() {
        for (const QString& t : {QStringLiteral("espresso"), QStringLiteral("filter"),
                                 QStringLiteral("americano"), QStringLiteral("long_black"),
                                 QStringLiteral("latte"), QStringLiteral("tea"),
                                 QStringLiteral("tea_hotwater")})
            QVERIFY2(Recipe::isKnownDrinkType(t), qPrintable(t));
        QVERIFY(!Recipe::isKnownDrinkType("Tea"));          // case
        QVERIFY(!Recipe::isKnownDrinkType("cappuccino"));   // typo
        QVERIFY(!Recipe::isKnownDrinkType(""));
    }

    // The stored-filter arm of the update re-derivation: a block stamp that
    // leaves neither milk nor water active must keep drink_type "filter", not
    // slide to "espresso" (the tea arm is covered above).
    void updateRederivesPreservesFilter() {
        const QString path = freshDbPath();
        qint64 id = 0;
        withRawDb(path, "filter_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe r;
            r.name = "Pour"; r.profileTitle = "Filter 2.0"; r.drinkType = "filter";
            id = RecipeStorage::insertRecipeStatic(db, r);
        });
        RecipeStorage storage;
        storage.initialize(path);
        QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
        // A steam stamp with hasMilk:false touches a block without changing the
        // profile — re-derivation must fall back to the stored filter category.
        storage.requestUpdateRecipe(id, {{"steamJson", "{\"hasMilk\":false}"}});
        QTRY_COMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(1).toBool());
        withRawDb(path, "filter_verify", [&](QSqlDatabase& db) {
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, id).drinkType, QString("filter"));
        });
    }

    void requestRecipeForActivation() {
        const QString path = freshDbPath();
        qint64 recipeId = 0, bareId = 0, openBagId = 0;
        withRawDb(path, "act_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            CoffeeBag bag; bag.roasterName = "Roaster"; bag.coffeeName = "Guji";
            bag.beanBaseId = "bb-uuid-1"; bag.inInventory = true; bag.grinderSetting = "2.4";
            openBagId = CoffeeBagStorage::insertBagStatic(db, bag);
            recipeId = RecipeStorage::insertRecipeStatic(db, sampleRecipe());
            Recipe bare; bare.name = "Bare";  // no bean link
            bareId = RecipeStorage::insertRecipeStatic(db, bare);
        });
        QVERIFY(recipeId > 0 && bareId > 0 && openBagId > 0);

        RecipeStorage storage;
        storage.initialize(path);

        // Happy path: recipe map + resolved open bag + full bag map, one pass.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeActivationReady);
            storage.requestRecipeForActivation(recipeId);
            QTRY_COMPARE(spy.count(), 1);
            const auto args = spy.at(0);
            QCOMPARE(args.at(0).toLongLong(), recipeId);
            QCOMPARE(args.at(1).toMap().value("name").toString(), QString("Morning capp"));
            QCOMPARE(args.at(2).toLongLong(), openBagId);
            QCOMPARE(args.at(3).toMap().value("grinderSetting").toString(), QString("2.4"));
        }

        // Missing recipe: emits with an EMPTY recipe map (caller fails cleanly).
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeActivationReady);
            storage.requestRecipeForActivation(999999);
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(spy.at(0).at(1).toMap().isEmpty());
            QCOMPARE(spy.at(0).at(2).toLongLong(), (qint64)-1);
        }

        // Bean-less recipe: valid recipe, openBagId == -1, empty bag map.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeActivationReady);
            storage.requestRecipeForActivation(bareId);
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(!spy.at(0).at(1).toMap().isEmpty());
            QCOMPARE(spy.at(0).at(2).toLongLong(), (qint64)-1);
            QVERIFY(spy.at(0).at(3).toMap().isEmpty());
        }
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

        // Used recipe refuses deletion (archive is the only exit). The
        // refusal qWarning fires on the worker thread before recipeDeleted is
        // queued back, so it lands (and is matched) before the QTRY returns.
        {
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression("refusing to delete recipe"));
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
