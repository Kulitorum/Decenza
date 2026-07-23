#include <QtTest>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "history/recipestorage.h"
#include "history/coffeebagstorage.h"

// Recipe storage (add-recipes, recipes-bag-links-ui-polish): CRUD statics,
// variant-map round-trip, inventory MRU + shot-count aggregate + stale flag,
// the delete-vs-archive lifecycle guard, clone provenance, the hard bag link
// (activation uses it directly; the resolver survives only as the relink
// matching helper + migration-29 data pass), and the relink lifecycle
// (roll-on-finish, wake-on-restock, dup-guard).

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

// Two bags of the same bean for the roll scenarios: the first finished (but
// deliberately MRU — the roll must pick the NEWEST open bag, never the most
// recently used), the second open and newer.
static void insertBeanPair(QSqlDatabase& db, qint64* outFinished, qint64* outOpen) {
    CoffeeBag finished; finished.roasterName = "Roaster"; finished.coffeeName = "Guji";
    finished.beanBaseId = "bb-uuid-1"; finished.inInventory = false;
    finished.lastUsedEpoch = 900;
    *outFinished = CoffeeBagStorage::insertBagStatic(db, finished);
    CoffeeBag open = finished; open.inInventory = true; open.lastUsedEpoch = 100;
    *outOpen = CoffeeBagStorage::insertBagStatic(db, open);
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
    r.yieldValue = 40.0;   // yield spec (add-yield-ratio-anchor)
    r.yieldMode = "absolute";
    r.tempOffsetC = -2.5;  // SIGNED: negative offsets must survive the bind
    r.grindPinned = "";  // no grind recorded (a valid state)
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
    void init() { QTest::failOnWarning(); }

    // --- variant map round-trip ---

    void variantMapRoundTrip() {
        Recipe r = sampleRecipe();
        r.bagId = 12;
        r.createdEpoch = 1700000000;  // read-only column still surfaces in the map
        const Recipe back = Recipe::fromVariantMap(r.toVariantMap());
        QCOMPARE(back.bagId, r.bagId);
        QCOMPARE(back.createdEpoch, r.createdEpoch);
        QCOMPARE(back.name, r.name);
        QCOMPARE(back.profileTitle, r.profileTitle);
        QCOMPARE(back.profileJson, r.profileJson);
        QCOMPARE(back.drinkType, r.drinkType);
        QCOMPARE(back.beanBaseId, r.beanBaseId);
        QCOMPARE(back.roasterName, r.roasterName);
        QCOMPARE(back.coffeeName, r.coffeeName);
        QCOMPARE(back.equipmentId, r.equipmentId);
        QCOMPARE(back.doseG, r.doseG);
        QCOMPARE(back.yieldValue, r.yieldValue);
        QCOMPARE(back.yieldMode, r.yieldMode);
        QCOMPARE(back.tempOffsetC, r.tempOffsetC);
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
        QCOMPARE(r.yieldMode, QString("none"));
        QCOMPARE(r.archived, false);
        QVERIFY(r.grindPinned.isEmpty());
        QVERIFY(r.drinkType.isEmpty());
    }

    // --- isRecipeStale (recipe-auto-load) ---
    // Pure decision behind MainController's recipe-auto-load stale check —
    // lives on RecipeStorage rather than MainController specifically so it
    // can be tested here without linking MainController's full subsystem
    // closure.

    void isRecipeStaleEmptyMapIsStale() {
        QVERIFY(RecipeStorage::isRecipeStale(QVariantMap()));
    }

    void isRecipeStaleArchivedIsStale() {
        QVariantMap recipe = sampleRecipe().toVariantMap();
        recipe["archived"] = true;
        QVERIFY(RecipeStorage::isRecipeStale(recipe));
    }

    void isRecipeStaleLiveNonArchivedIsNotStale() {
        QVariantMap recipe = sampleRecipe().toVariantMap();
        recipe["archived"] = false;
        QVERIFY(!RecipeStorage::isRecipeStale(recipe));
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

        // Milk + added water is its own drink: a latte with an Americano-style
        // water pour derives latte_hotwater (checked before milk-alone).
        r.steamJson = milk;
        r.hotWaterJson = waterAfter;
        QCOMPARE(Recipe::deriveDrinkType(r, "espresso"), QString("latte_hotwater"));
        r.hotWaterJson = waterBefore;  // order does not matter once milk is present
        QCOMPARE(Recipe::deriveDrinkType(r, "espresso"), QString("latte_hotwater"));
        // Milk alone (no water) is a plain latte.
        r.hotWaterJson.clear();
        QCOMPARE(Recipe::deriveDrinkType(r, "espresso"), QString("latte"));

        // Tea profile beats milk (the profile type is the strongest signal).
        r.hotWaterJson = waterAfter;  // even with both blocks, tea profile wins
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

    // --- active-name uniqueness lookup (block-duplicate-active-names) ---
    // Two non-archived recipes may not share a name; archiving one frees its name
    // for reuse. Comparison is trimmed and case-insensitive, and excludes the
    // recipe being edited.
    void activeNameUniqueness() {
        withRawDb(freshDbPath(), "namedup", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe r = sampleRecipe();          // name: "Morning capp"
            const qint64 id = RecipeStorage::insertRecipeStatic(db, r);
            QVERIFY(id > 0);

            // Exact, case-insensitive and whitespace-insensitive matches all hit.
            QCOMPARE(RecipeStorage::findRecipeByNameStatic(db, "Morning capp"), id);
            QCOMPARE(RecipeStorage::findRecipeByNameStatic(db, "  MORNING CAPP  "), id);
            // A different name does not.
            QCOMPARE(RecipeStorage::findRecipeByNameStatic(db, "Evening capp"), (qint64)0);
            // A blank name never collides.
            QCOMPARE(RecipeStorage::findRecipeByNameStatic(db, ""), (qint64)0);
            QCOMPARE(RecipeStorage::findRecipeByNameStatic(db, "   "), (qint64)0);
            // The recipe being edited is excluded, so renaming it to a casing
            // variant of its own name is allowed.
            QCOMPARE(RecipeStorage::findRecipeByNameStatic(db, "morning capp", id), (qint64)0);

            // Archiving frees the name for reuse.
            QVERIFY(RecipeStorage::updateRecipeFieldsStatic(db, id, {{"archived", true}}));
            QCOMPARE(RecipeStorage::findRecipeByNameStatic(db, "Morning capp"), (qint64)0);
            // ...and unarchiving makes it collide again.
            QVERIFY(RecipeStorage::updateRecipeFieldsStatic(db, id, {{"archived", false}}));
            QCOMPARE(RecipeStorage::findRecipeByNameStatic(db, "Morning capp"), id);
        });
    }

    // --- active-name uniqueness through the async request* paths ---
    // The helper test above covers the lookup predicate; these cover the guards
    // that use it, the emitted payloads, and the two new signals.

    // A create/clone colliding with an active recipe is refused, and the refusal
    // carries error:"nameInUse" WITHOUT dropping the correlation token every
    // MCP/web listener filters on (losing it would hang them forever).
    void createAndCloneRejectDuplicateName() {
        const QString path = freshDbPath();
        qint64 sourceId = 0;
        withRawDb(path, "namedup_seed", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            sourceId = RecipeStorage::insertRecipeStatic(db, sampleRecipe());  // "Morning capp"
        });
        QVERIFY(sourceId > 0);

        RecipeStorage storage;
        storage.initialize(path);

        {   // Create colliding with an active recipe -> refused, token echoed.
            QSignalSpy spy(&storage, &RecipeStorage::recipeCreated);
            storage.requestCreateRecipe({{"name", "  morning CAPP  "}, {"profileTitle", "P"},
                                         {"requestToken", "tok-create"}});
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(0).toLongLong(), (qint64)-1);
            const QVariantMap created = spy.at(0).at(1).toMap();
            QCOMPARE(created.value("error").toString(), QString("nameInUse"));
            QCOMPARE(created.value("requestToken").toString(), QString("tok-create"));
        }
        {   // Clone under the same name -> refused the same way.
            QSignalSpy spy(&storage, &RecipeStorage::recipeCreated);
            storage.requestCloneRecipe(sourceId, "Morning capp", "tok-clone");
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(0).toLongLong(), (qint64)-1);
            QCOMPARE(spy.at(0).at(1).toMap().value("error").toString(), QString("nameInUse"));
            QCOMPARE(spy.at(0).at(1).toMap().value("requestToken").toString(), QString("tok-clone"));
        }
        {   // A free name still creates.
            QSignalSpy spy(&storage, &RecipeStorage::recipeCreated);
            storage.requestCreateRecipe({{"name", "Evening capp"}, {"profileTitle", "P"}});
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(spy.at(0).at(0).toLongLong() > 0);
        }
        // Exactly one row was added across all three attempts.
        withRawDb(path, "namedup_count", [](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*) FROM recipes"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 2);
        });
    }

    // requestRecipe() must emit a terminal signal even when storage was
    // never initialized (recipe-auto-load), rather than silently dropping
    // the job the way runAsync() does for every other caller — a caller
    // waiting on a specific id would otherwise hang on a pending flag
    // forever with no signal ever arriving.
    void requestRecipeOnUninitializedStorageEmitsRecipeCheckFailed() {
        RecipeStorage storage;  // no initialize() call
        QSignalSpy readySpy(&storage, &RecipeStorage::recipeReady);
        QSignalSpy failedSpy(&storage, &RecipeStorage::recipeCheckFailed);
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("RecipeStorage: requestRecipe on uninitialized storage.*"));
        storage.requestRecipe(42);
        QCOMPARE(failedSpy.count(), 1);
        QCOMPARE(failedSpy.at(0).at(0).toLongLong(), (qint64)42);
        QCOMPARE(readySpy.count(), 0);
    }

    // Re-saving a recipe under the name it ALREADY has must succeed, even when
    // another active recipe shares that name. Without this a pre-existing
    // duplicate could not be edited at all — every save sends `name`, so a guard
    // testing "is name in the patch" disabled both records permanently.
    void unchangedNameOnPreExistingDuplicateStillSaves() {
        const QString path = freshDbPath();
        qint64 idA = 0, idB = 0;
        withRawDb(path, "namedup_pre", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe a = sampleRecipe(); a.name = "Twin";
            Recipe b = sampleRecipe(); b.name = "Twin";   // duplicate predates the rule
            idA = RecipeStorage::insertRecipeStatic(db, a);
            idB = RecipeStorage::insertRecipeStatic(db, b);
        });
        QVERIFY(idA > 0 && idB > 0);

        RecipeStorage storage;
        storage.initialize(path);
        {   // Same name + an unrelated field change -> allowed.
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestUpdateRecipe(idB, {{"name", "Twin"}, {"doseG", 21.0}});
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(1).toBool(), true);
        }
        withRawDb(path, "namedup_pre2", [&](QSqlDatabase& db) {
            const Recipe b = RecipeStorage::loadRecipeStatic(db, idB);
            QCOMPARE(b.name, QString("Twin"));
            QCOMPARE(b.doseG, 21.0);
        });
    }

    // A rename into a collision is refused, rolled back whole (so sibling fields
    // in the same patch are NOT applied), and reports the cause before the
    // terminal status.
    void rejectedRenameRollsBackCleanly() {
        const QString path = freshDbPath();
        qint64 idA = 0, idB = 0;
        withRawDb(path, "namedup_rb", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe a = sampleRecipe(); a.name = "Morning capp";
            Recipe b = sampleRecipe(); b.name = "Evening capp"; b.doseG = 18.0; b.grindPinned = "keep";
            idA = RecipeStorage::insertRecipeStatic(db, a);
            idB = RecipeStorage::insertRecipeStatic(db, b);
        });
        QVERIFY(idA > 0 && idB > 0);

        RecipeStorage storage;
        storage.initialize(path);
        // Order matters to every surface: the reason must precede the status.
        QStringList order;
        connect(&storage, &RecipeStorage::recipeUpdateFailed, this,
                [&order](qint64, const QString& r) { order << ("failed:" + r); });
        connect(&storage, &RecipeStorage::recipeUpdated, this,
                [&order](qint64, bool ok) { order << (ok ? "updated:true" : "updated:false"); });

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("already used by another active recipe"));
        QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
        storage.requestUpdateRecipe(idB, {{"name", "Morning capp"}, {"doseG", 21.0}, {"grindPinned", "changed"}});
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toBool(), false);
        QCOMPARE(order, QStringList({"failed:nameInUse", "updated:false"}));

        // Nothing from that patch survived — proves the rollback ran.
        withRawDb(path, "namedup_rb2", [&](QSqlDatabase& db) {
            const Recipe b = RecipeStorage::loadRecipeStatic(db, idB);
            QCOMPARE(b.name, QString("Evening capp"));
            QCOMPARE(b.doseG, 18.0);
            QCOMPARE(b.grindPinned, QString("keep"));
        });
    }

    // Archiving must always work (it is how a name is freed); restoring into a
    // name an active recipe has taken must not.
    void archiveAlwaysAllowedRestoreIntoCollisionRefused() {
        const QString path = freshDbPath();
        qint64 idActive = 0, idArchived = 0;
        withRawDb(path, "namedup_arch", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe a = sampleRecipe(); a.name = "Twin";
            Recipe b = sampleRecipe(); b.name = "Twin"; b.archived = true;
            idActive = RecipeStorage::insertRecipeStatic(db, a);
            idArchived = RecipeStorage::insertRecipeStatic(db, b);
        });
        QVERIFY(idActive > 0 && idArchived > 0);

        RecipeStorage storage;
        storage.initialize(path);
        {   // Restore into the taken name -> refused.
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("already used by another active recipe"));
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestUnarchiveRecipe(idArchived);
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(1).toBool(), false);
        }
        {   // Archiving the active twin is always allowed...
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestArchiveRecipe(idActive);
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(1).toBool(), true);
        }
        {   // ...and now the name is free, so the restore succeeds.
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestUnarchiveRecipe(idArchived);
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(1).toBool(), true);
        }
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
            // created_at is read-only (COL_EPOCH_RO): the SQL DEFAULT stamped it
            // at insert and it surfaces on load, even though the struct's value
            // was never bound.
            QVERIFY(loaded.createdEpoch > 0);
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
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
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

    // Stale flag on the inventory listing: a recipe whose linked bag is
    // finished (or whose bean was never linked) is stale; an open link or a
    // bean-less recipe is not. Display state only — the rows still list.
    void inventoryStaleFlag() {
        withRawDb(freshDbPath(), "inv_stale", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            createMinimalShots(db);

            CoffeeBag open; open.roasterName = "Roaster"; open.coffeeName = "Guji";
            open.inInventory = true;
            const qint64 openId = CoffeeBagStorage::insertBagStatic(db, open);
            CoffeeBag finished = open; finished.inInventory = false;
            const qint64 finishedId = CoffeeBagStorage::insertBagStatic(db, finished);
            QVERIFY(openId > 0 && finishedId > 0);

            Recipe fresh = sampleRecipe(); fresh.name = "Fresh"; fresh.bagId = openId;
            Recipe stale = sampleRecipe(); stale.name = "Stale"; stale.bagId = finishedId;
            Recipe unlinked = sampleRecipe(); unlinked.name = "Unlinked";  // bean, no bag
            Recipe bare; bare.name = "Bare"; bare.profileTitle = "P";      // no bean at all
            QVERIFY(RecipeStorage::insertRecipeStatic(db, fresh) > 0);
            QVERIFY(RecipeStorage::insertRecipeStatic(db, stale) > 0);
            QVERIFY(RecipeStorage::insertRecipeStatic(db, unlinked) > 0);
            QVERIFY(RecipeStorage::insertRecipeStatic(db, bare) > 0);

            QHash<QString, bool> staleByName;
            for (const InventoryRecipe& e : RecipeStorage::loadInventoryStatic(db, false))
                staleByName.insert(e.recipe.name, e.stale);
            QCOMPARE(staleByName.size(), 4);
            QCOMPARE(staleByName.value("Fresh"), false);
            QCOMPARE(staleByName.value("Stale"), true);
            QCOMPARE(staleByName.value("Unlinked"), true);
            QCOMPARE(staleByName.value("Bare"), false);
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

    // --- migration-29 data pass: bean identity -> open bag, once ---

    void migrateBagLinks() {
        withRawDb(freshDbPath(), "mig29", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));

            CoffeeBag open; open.roasterName = "Roaster"; open.coffeeName = "Guji";
            open.beanBaseId = "bb-uuid-1"; open.inInventory = true;
            const qint64 openId = CoffeeBagStorage::insertBagStatic(db, open);
            CoffeeBag finished = open; finished.inInventory = false;
            const qint64 finishedId = CoffeeBagStorage::insertBagStatic(db, finished);
            QVERIFY(openId > 0 && finishedId > 0);

            // Resolves to the open bag of its bean.
            Recipe resolvable = sampleRecipe();
            const qint64 resolvableId = RecipeStorage::insertRecipeStatic(db, resolvable);
            // No open bag of this bean -> stays unlinked (stale).
            Recipe orphan = sampleRecipe();
            orphan.beanBaseId = "bb-none"; orphan.roasterName = "Else"; orphan.coffeeName = "Where";
            const qint64 orphanId = RecipeStorage::insertRecipeStatic(db, orphan);
            // Already linked (idempotence): the pass must not touch it, even
            // though the resolver would pick the open bag.
            Recipe already = sampleRecipe();
            already.bagId = finishedId;
            const qint64 alreadyId = RecipeStorage::insertRecipeStatic(db, already);
            QVERIFY(resolvableId > 0 && orphanId > 0 && alreadyId > 0);

            RecipeStorage::migrateBagLinksStatic(db);

            QCOMPARE(RecipeStorage::loadRecipeStatic(db, resolvableId).bagId, openId);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, orphanId).bagId, (qint64)0);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, alreadyId).bagId, finishedId);

            // Running it again changes nothing (idempotent).
            RecipeStorage::migrateBagLinksStatic(db);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, resolvableId).bagId, openId);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, alreadyId).bagId, finishedId);
        });
    }

    // --- migration 30: recipe-owned grind (fix-recipe-grind-integrity) ---

    // Inherit-mode rows (empty grind_pinned + a bag link) adopt their bag's
    // current dial once; rows whose bag has no dial (tea bags, never-dialed)
    // are skipped, not stamped with empty strings; already-pinned and
    // bag-less rows stay untouched. Idempotent.
    void migrateGrindOwnership() {
        withRawDb(freshDbPath(), "mig30", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));

            CoffeeBag dialed; dialed.roasterName = "Roaster"; dialed.coffeeName = "Guji";
            dialed.inInventory = true; dialed.grinderSetting = "18"; dialed.rpm = 1200;
            const qint64 dialedId = CoffeeBagStorage::insertBagStatic(db, dialed);
            CoffeeBag undialed; undialed.roasterName = "T"; undialed.coffeeName = "Sencha";
            undialed.inInventory = true;  // no grinderSetting (tea / never dialed)
            const qint64 undialedId = CoffeeBagStorage::insertBagStatic(db, undialed);
            QVERIFY(dialedId > 0 && undialedId > 0);

            Recipe inheriting = sampleRecipe();
            inheriting.bagId = dialedId;
            const qint64 inheritingId = RecipeStorage::insertRecipeStatic(db, inheriting);
            Recipe teaLike = sampleRecipe();
            teaLike.name = "Sencha"; teaLike.bagId = undialedId; teaLike.rpmPinned = 0;
            const qint64 teaLikeId = RecipeStorage::insertRecipeStatic(db, teaLike);
            Recipe pinned = sampleRecipe();
            pinned.name = "Pinned"; pinned.bagId = dialedId;
            pinned.grindPinned = "2.4"; pinned.rpmPinned = 90;
            const qint64 pinnedId = RecipeStorage::insertRecipeStatic(db, pinned);
            Recipe bagless = sampleRecipe();
            bagless.name = "Bagless"; bagless.beanBaseId.clear();
            bagless.roasterName.clear(); bagless.coffeeName.clear();
            const qint64 baglessId = RecipeStorage::insertRecipeStatic(db, bagless);
            QVERIFY(inheritingId > 0 && teaLikeId > 0 && pinnedId > 0 && baglessId > 0);

            QVERIFY(RecipeStorage::migrateGrindOwnershipStatic(db));

            const Recipe adopted = RecipeStorage::loadRecipeStatic(db, inheritingId);
            QCOMPARE(adopted.grindPinned, QString("18"));
            QCOMPARE(adopted.rpmPinned, (qint64)1200);
            QVERIFY(RecipeStorage::loadRecipeStatic(db, teaLikeId).grindPinned.isEmpty());
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, pinnedId).grindPinned, QString("2.4"));
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, pinnedId).rpmPinned, (qint64)90);
            QVERIFY(RecipeStorage::loadRecipeStatic(db, baglessId).grindPinned.isEmpty());

            // Idempotent: a second pass changes nothing (re-dial the bag first
            // to prove the adopted value is not re-copied).
            QSqlQuery redial(db);
            redial.prepare("UPDATE coffee_bags SET grinder_setting = '20' WHERE id = ?");
            redial.addBindValue(dialedId);
            QVERIFY(redial.exec());
            QVERIFY(RecipeStorage::migrateGrindOwnershipStatic(db));
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, inheritingId).grindPinned, QString("18"));
        });
    }

    // rpm edges of the migration-30 pass: a bag with NO dial but a recorded
    // rpm is skipped entirely (grind gates the copy); a dialed bag with NULL
    // rpm zeroes a leftover rpm_pinned (rpm rides with the dial — intended).
    void migrateGrindOwnershipRpmEdges() {
        withRawDb(freshDbPath(), "mig30_rpm", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));

            CoffeeBag rpmOnly; rpmOnly.roasterName = "R"; rpmOnly.coffeeName = "A";
            rpmOnly.inInventory = true; rpmOnly.rpm = 1400;  // no grinderSetting
            const qint64 rpmOnlyId = CoffeeBagStorage::insertBagStatic(db, rpmOnly);
            CoffeeBag noRpm; noRpm.roasterName = "R"; noRpm.coffeeName = "B";
            noRpm.inInventory = true; noRpm.grinderSetting = "18";  // rpm unset
            const qint64 noRpmId = CoffeeBagStorage::insertBagStatic(db, noRpm);
            QVERIFY(rpmOnlyId > 0 && noRpmId > 0);

            Recipe skipped = sampleRecipe();
            skipped.name = "RpmOnlyBag"; skipped.bagId = rpmOnlyId;  // rpmPinned 90 leftover
            const qint64 skippedId = RecipeStorage::insertRecipeStatic(db, skipped);
            Recipe zeroed = sampleRecipe();
            zeroed.name = "NoRpmBag"; zeroed.bagId = noRpmId;  // rpmPinned 90 leftover
            const qint64 zeroedId = RecipeStorage::insertRecipeStatic(db, zeroed);
            QVERIFY(skippedId > 0 && zeroedId > 0);

            QVERIFY(RecipeStorage::migrateGrindOwnershipStatic(db));

            const Recipe s = RecipeStorage::loadRecipeStatic(db, skippedId);
            QVERIFY(s.grindPinned.isEmpty());               // no dial to copy
            QCOMPARE(s.rpmPinned, (qint64)90);              // untouched
            const Recipe z = RecipeStorage::loadRecipeStatic(db, zeroedId);
            QCOMPARE(z.grindPinned, QString("18"));
            QCOMPARE(z.rpmPinned, (qint64)0);               // rpm rides with the dial
        });
    }

    // Import runs the same backfill: a pre-migration-30 source DB's
    // inherit-mode rows adopt their (remapped) bag's dial on arrival.
    void importBackfillsInheritModeGrind() {
        const QString srcPath = freshDbPath();
        const QString destPath = freshDbPath();
        qint64 srcRecipeId = 0;
        withRawDb(srcPath, "mig30_imp_src", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe inheriting = sampleRecipe();
            inheriting.bagId = 40;  // source-side id; remapped below
            srcRecipeId = RecipeStorage::insertRecipeStatic(db, inheriting);
            QVERIFY(srcRecipeId > 0);
        });
        qint64 destBagId = 0;
        withRawDb(destPath, "mig30_imp_dest", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            CoffeeBag bag; bag.roasterName = "Roaster"; bag.coffeeName = "Guji";
            bag.inInventory = true; bag.grinderSetting = "18"; bag.rpm = 1200;
            destBagId = CoffeeBagStorage::insertBagStatic(db, bag);
            QVERIFY(destBagId > 0);
        });
        withRawDb(srcPath, "mig30_imp_src2", [&](QSqlDatabase& srcDb) {
            withRawDb(destPath, "mig30_imp_dest2", [&](QSqlDatabase& destDb) {
                QHash<qint64, qint64> idMap;
                const QHash<qint64, qint64> bagIdMap{{40, destBagId}};
                QVERIFY(RecipeStorage::importRecipesStatic(srcDb, destDb, /*merge=*/false,
                                                           idMap, {}, bagIdMap));
                const Recipe imported =
                    RecipeStorage::loadRecipeStatic(destDb, idMap.value(srcRecipeId));
                QCOMPARE(imported.grindPinned, QString("18"));
                QCOMPARE(imported.rpmPinned, (qint64)1200);
            });
        });
    }

    // Import must preserve the source's original created_at (recipe-list-
    // organization): "Date created" ordering has to survive transfer / backup
    // restore. created_at is read-only, so the INSERT lets the DEFAULT stamp
    // import-time — importRecipesStatic re-stamps it from the source row.
    void importPreservesCreatedAt() {
        const QString srcPath = freshDbPath();
        const QString destPath = freshDbPath();
        const qint64 pastEpoch = 1500000000;  // a fixed date well before "now"
        qint64 srcWithDate = 0, srcNullDate = 0;
        withRawDb(srcPath, "created_src", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe a = sampleRecipe(); a.name = "Has date";
            srcWithDate = RecipeStorage::insertRecipeStatic(db, a);
            QVERIFY(srcWithDate > 0);
            Recipe b = sampleRecipe(); b.name = "No date";
            srcNullDate = RecipeStorage::insertRecipeStatic(db, b);
            QVERIFY(srcNullDate > 0);
            // Force one row's created_at to a fixed past value (the DEFAULT
            // stamped ~now at insert), and clear the other's to NULL to exercise
            // the guard's else-branch.
            QSqlQuery u(db);
            u.prepare("UPDATE recipes SET created_at = :c WHERE id = :id");
            u.bindValue(":c", pastEpoch); u.bindValue(":id", srcWithDate);
            QVERIFY(u.exec());
            QSqlQuery n(db);
            n.prepare("UPDATE recipes SET created_at = NULL WHERE id = :id");
            n.bindValue(":id", srcNullDate);
            QVERIFY(n.exec());
        });
        withRawDb(destPath, "created_dest", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            // The post-import grind-ownership backfill queries coffee_bags.
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
        });
        withRawDb(srcPath, "created_src2", [&](QSqlDatabase& srcDb) {
            withRawDb(destPath, "created_dest2", [&](QSqlDatabase& destDb) {
                QHash<qint64, qint64> idMap;
                QVERIFY(RecipeStorage::importRecipesStatic(srcDb, destDb, /*merge=*/false,
                                                           idMap, {}, {}));
                // The dated recipe keeps its ORIGINAL created_at, not import-time.
                QCOMPARE(RecipeStorage::loadRecipeStatic(destDb, idMap.value(srcWithDate)).createdEpoch,
                         pastEpoch);
                // The NULL-date recipe still lands with a valid DEFAULT (never 0).
                QVERIFY(RecipeStorage::loadRecipeStatic(destDb, idMap.value(srcNullDate)).createdEpoch > 0);
            });
        });
    }

    // The import backfill is scoped to the rows the import inserted: a
    // pre-existing LOCAL recipe whose grind was deliberately cleared (a
    // supported state post-migration-30) must survive an unrelated import
    // untouched — a table-wide re-run would stamp the bag's dial back on.
    void importDoesNotClobberDeliberatelyEmptyLocalGrind() {
        const QString srcPath = freshDbPath();
        const QString destPath = freshDbPath();
        withRawDb(srcPath, "mig30_keep_src", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe unrelated = sampleRecipe();
            unrelated.name = "Unrelated import"; unrelated.grindPinned = "2.4";
            QVERIFY(RecipeStorage::insertRecipeStatic(db, unrelated) > 0);
        });
        qint64 localId = 0;
        withRawDb(destPath, "mig30_keep_dest", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            CoffeeBag bag; bag.roasterName = "Roaster"; bag.coffeeName = "Guji";
            bag.inInventory = true; bag.grinderSetting = "18";
            const qint64 bagId = CoffeeBagStorage::insertBagStatic(db, bag);
            QVERIFY(bagId > 0);
            Recipe local = sampleRecipe();
            local.name = "Deliberately grind-less"; local.bagId = bagId;
            local.rpmPinned = 0;  // grind explicitly cleared, bag dialed
            localId = RecipeStorage::insertRecipeStatic(db, local);
            QVERIFY(localId > 0);
        });
        withRawDb(srcPath, "mig30_keep_src2", [&](QSqlDatabase& srcDb) {
            withRawDb(destPath, "mig30_keep_dest2", [&](QSqlDatabase& destDb) {
                QHash<qint64, qint64> idMap;
                QVERIFY(RecipeStorage::importRecipesStatic(srcDb, destDb, /*merge=*/true,
                                                           idMap, {}, {}));
                QVERIFY(RecipeStorage::loadRecipeStatic(destDb, localId).grindPinned.isEmpty());
            });
        });
    }

    // Yield-spec import conversion (add-yield-ratio-anchor): a pre-34 source
    // (yield_g but no yield_mode) converts on import — yield_g > 0 becomes
    // {value, absolute}, else "none" — producing the same specs the local
    // migration would have.
    void importConvertsLegacyYieldG() {
        const QString srcPath = freshDbPath();
        const QString destPath = freshDbPath();
        withRawDb(srcPath, "y34_src", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("ALTER TABLE recipes DROP COLUMN yield_value"));
            QVERIFY(q.exec("ALTER TABLE recipes DROP COLUMN yield_mode"));
            QVERIFY(q.exec("INSERT INTO recipes (name, yield_g) VALUES ('LegacyYield', 40.0)"));
            QVERIFY(q.exec("INSERT INTO recipes (name) VALUES ('LegacyNone')"));
        });
        withRawDb(destPath, "y34_dest", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
        });
        withRawDb(srcPath, "y34_src2", [&](QSqlDatabase& srcDb) {
            withRawDb(destPath, "y34_dest2", [&](QSqlDatabase& destDb) {
                QHash<qint64, qint64> idMap;
                QVERIFY(RecipeStorage::importRecipesStatic(srcDb, destDb, /*merge=*/false,
                                                           idMap, {}, {}));
                QSqlQuery q(destDb);
                QVERIFY(q.exec("SELECT yield_value, yield_mode FROM recipes WHERE name = 'LegacyYield'"));
                QVERIFY(q.next());
                QCOMPARE(q.value(0).toDouble(), 40.0);
                QCOMPARE(q.value(1).toString(), QStringLiteral("absolute"));
                QVERIFY(q.exec("SELECT yield_mode FROM recipes WHERE name = 'LegacyNone'"));
                QVERIFY(q.next());
                QCOMPARE(q.value(1).isValid() ? q.value(0).toString() : q.value(0).toString(),
                         QStringLiteral("none"));
            });
        });
    }

    // A ≥34 source imports its spec VERBATIM and its dead yield_g is ignored
    // — reconverting would resurrect a yield the user has since changed to a
    // ratio (the staged-conversion discipline of the temperature migration).
    void importNeverReconvertsFromDeadYieldG() {
        const QString srcPath = freshDbPath();
        const QString destPath = freshDbPath();
        withRawDb(srcPath, "y34v_src", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            // A recipe migrated from yield_g 40 then changed to a 1:2 ratio:
            // the dead column still holds 40.
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO recipes (name, yield_g, yield_value, yield_mode) "
                           "VALUES ('NowRatio', 40.0, 2.0, 'ratio')"));
        });
        withRawDb(destPath, "y34v_dest", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
        });
        withRawDb(srcPath, "y34v_src2", [&](QSqlDatabase& srcDb) {
            withRawDb(destPath, "y34v_dest2", [&](QSqlDatabase& destDb) {
                QHash<qint64, qint64> idMap;
                QVERIFY(RecipeStorage::importRecipesStatic(srcDb, destDb, /*merge=*/false,
                                                           idMap, {}, {}));
                QSqlQuery q(destDb);
                QVERIFY(q.exec("SELECT yield_value, yield_mode FROM recipes WHERE name = 'NowRatio'"));
                QVERIFY(q.next());
                QCOMPARE(q.value(0).toDouble(), 2.0);
                QCOMPARE(q.value(1).toString(), QStringLiteral("ratio"));
            });
        });
    }

    // Create-time grind default (fix-recipe-grind-integrity): a bag-linked
    // create map that OMITS grind adopts the bag's current dial — the
    // non-interactive surfaces' (MCP/web) equivalent of the wizard's editable
    // prefill; an explicitly EMPTY grind is a deliberate "no grind".
    void createDefaultsGrindFromBag() {
        const QString path = freshDbPath();
        qint64 bagId = 0;
        withRawDb(path, "create_grind_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            CoffeeBag bag; bag.roasterName = "Roaster"; bag.coffeeName = "Guji";
            bag.beanBaseId = "bb-uuid-1"; bag.inInventory = true;
            bag.grinderSetting = "18"; bag.rpm = 1200;
            bagId = CoffeeBagStorage::insertBagStatic(db, bag);
        });
        QVERIFY(bagId > 0);

        RecipeStorage storage;
        storage.initialize(path);

        // Omitted grind -> adopts the bag's dial (grind + rpm).
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeCreated);
            QVariantMap map{{"name", "Adopts"}, {"profileTitle", "P"}, {"bagId", bagId}};
            storage.requestCreateRecipe(map);
            QTRY_COMPARE(spy.count(), 1);
            const QVariantMap created = spy.at(0).at(1).toMap();
            QCOMPARE(created.value("grindPinned").toString(), QString("18"));
            QCOMPARE(created.value("rpmPinned").toLongLong(), (qint64)1200);
        }

        // Explicitly empty grind -> stays empty (deliberate "no grind").
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeCreated);
            QVariantMap map{{"name", "Deliberate"}, {"profileTitle", "P"},
                            {"bagId", bagId}, {"grindPinned", QString()}};
            storage.requestCreateRecipe(map);
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(spy.at(0).at(1).toMap().value("grindPinned").toString().isEmpty());
        }

        // Explicit grind with rpm omitted -> no rpm adoption either: the
        // default is all-or-nothing, gated on grind being omitted.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeCreated);
            QVariantMap map{{"name", "Explicit grind"}, {"profileTitle", "P"},
                            {"bagId", bagId}, {"grindPinned", "2.6"}};
            storage.requestCreateRecipe(map);
            QTRY_COMPARE(spy.count(), 1);
            const QVariantMap created = spy.at(0).at(1).toMap();
            QCOMPARE(created.value("grindPinned").toString(), QString("2.6"));
            QCOMPARE(created.value("rpmPinned").toLongLong(), (qint64)0);
        }
    }

    // --- import: bag_id remaps through the bag id-map ---

    // --- temp-offset migration (recipe-relative-temp-offset) ---

    // Migration 31's data pass: rows whose temp_offset_c is NULL convert
    // their legacy absolute against the profile (catalog map first, embedded
    // JSON fallback, unresolvable → 0); already-converted rows are untouched.
    void convertLegacyTempOffsets() {
        withRawDb(freshDbPath(), "cvt", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QSqlQuery q(db);
            // Four legacy rows (temp_offset_c NULL = unconverted):
            // by catalog map, by embedded JSON, unresolvable, and no override.
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, temp_offset_c, temp_override_c) "
                           "VALUES ('ByMap', 'Known', NULL, 87.0)"));
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, profile_json, temp_offset_c, temp_override_c) "
                           "VALUES ('ByJson', 'Gone', '{\"espresso_temperature\":\"92\"}', NULL, 90.0)"));
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, temp_offset_c, temp_override_c) "
                           "VALUES ('Lost', 'Nowhere', NULL, 85.0)"));
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, temp_offset_c, temp_override_c) "
                           "VALUES ('NoPin', 'Known', NULL, 0)"));
            // An already-converted row must NOT be reconverted from its dead
            // absolute (the user reset it to 0 after migrating).
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, temp_offset_c, temp_override_c) "
                           "VALUES ('Cleared', 'Known', 0, 87.0)"));

            const QHash<QString, double> temps{{QStringLiteral("Known"), 90.0}};
            // "Lost"/"Nowhere" is this test's deliberate unresolvable-profile
            // case (see comment above) — expect the warning it logs.
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression("could not resolve profile"));
            QVERIFY(RecipeStorage::convertLegacyTempOffsetsStatic(db, temps));

            auto offsetOf = [&](const QString& name) {
                QSqlQuery r(db);
                r.prepare("SELECT temp_offset_c FROM recipes WHERE name = :n");
                r.bindValue(":n", name);
                if (!r.exec() || !r.next()) return -999.0;
                return r.value(0).isNull() ? -998.0 : r.value(0).toDouble();
            };
            QCOMPARE(offsetOf("ByMap"), -3.0);
            QCOMPARE(offsetOf("ByJson"), -2.0);
            QCOMPARE(offsetOf("Lost"), 0.0);   // unresolvable → pin dropped
            QCOMPARE(offsetOf("NoPin"), 0.0);
            QCOMPARE(offsetOf("Cleared"), 0.0); // untouched (was non-NULL)

            // Idempotent: a second pass changes nothing.
            QVERIFY(RecipeStorage::convertLegacyTempOffsetsStatic(db, temps));
            QCOMPARE(offsetOf("ByMap"), -3.0);
        });
    }

    // A legacy-version source (no temp_offset_c column) stages its absolute
    // into the dest's dead column and marks the row unconverted; the deferred
    // pass then produces the same offset the local migration would have.
    void importLegacyTempSourceStagesAndConverts() {
        const QString srcPath = freshDbPath();
        const QString destPath = freshDbPath();
        withRawDb(srcPath, "lts_src", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("ALTER TABLE recipes DROP COLUMN temp_offset_c"));  // pre-31 schema
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, temp_override_c) "
                           "VALUES ('Legacy', 'Known', 87.0)"));
        });
        withRawDb(destPath, "lts_dest", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            // importRecipesStatic's post-import grind-ownership backfill
            // queries coffee_bags on the dest DB (see migrateGrindOwnershipStatic);
            // without the table it logs a warning even though this test has
            // no bag-linked rows to migrate.
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
        });
        withRawDb(srcPath, "lts_src2", [&](QSqlDatabase& srcDb) {
            withRawDb(destPath, "lts_dest2", [&](QSqlDatabase& destDb) {
                QHash<qint64, qint64> idMap;
                QVERIFY(RecipeStorage::importRecipesStatic(srcDb, destDb, /*merge=*/false,
                                                           idMap, {}, {}));
                // Staged: unconverted marker + the absolute in the dead column.
                QSqlQuery r(destDb);
                QVERIFY(r.exec("SELECT temp_offset_c, temp_override_c FROM recipes"));
                QVERIFY(r.next());
                QVERIFY(r.value(0).isNull());
                QCOMPARE(r.value(1).toDouble(), 87.0);

                const QHash<QString, double> temps{{QStringLiteral("Known"), 90.0}};
                QVERIFY(RecipeStorage::convertLegacyTempOffsetsStatic(destDb, temps));
                QSqlQuery r2(destDb);
                QVERIFY(r2.exec("SELECT temp_offset_c FROM recipes"));
                QVERIFY(r2.next());
                QCOMPARE(r2.value(0).toDouble(), -3.0);
            });
        });
    }

    // Clearing an offset through the normal update path must write an
    // EXPLICIT 0, never NULL — NULL is the migration's "unconverted" marker,
    // and a NULL here would let the conversion pass resurrect the dead
    // absolute the user deliberately cleared. This pins bindDblSigned being
    // wired to temp_offset_c (COL_DBL_SIGNED): "harmonizing" it back to the
    // null-collapsing COL_DBL is exactly the refactor this test fails on.
    void clearingOffsetWritesExplicitZeroNeverNull() {
        withRawDb(freshDbPath(), "clr0", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QSqlQuery q(db);
            // A migrated row: converted offset −3, dead absolute still parked.
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, temp_offset_c, temp_override_c) "
                           "VALUES ('Pinned', 'Known', -3.0, 87.0)"));
            const qint64 id = q.lastInsertId().toLongLong();

            QVERIFY(RecipeStorage::updateRecipeFieldsStatic(db, id, {{"tempOffsetC", 0.0}}));
            QSqlQuery r(db);
            QVERIFY(r.exec(QString("SELECT temp_offset_c FROM recipes WHERE id = %1").arg(id)));
            QVERIFY(r.next());
            QVERIFY(!r.value(0).isNull());       // explicit 0, NOT the unconverted marker
            QCOMPARE(r.value(0).toDouble(), 0.0);

            // And the conversion pass must not resurrect the dead absolute.
            const QHash<QString, double> temps{{QStringLiteral("Known"), 90.0}};
            QVERIFY(RecipeStorage::convertLegacyTempOffsetsStatic(db, temps));
            QSqlQuery r2(db);
            QVERIFY(r2.exec(QString("SELECT temp_offset_c FROM recipes WHERE id = %1").arg(id)));
            QVERIFY(r2.next());
            QCOMPARE(r2.value(0).toDouble(), 0.0);
        });
    }

    // Merge-mode import: a legacy-source row that dedup-matches an existing
    // dest recipe maps to the existing row WITHOUT staging — the local
    // recipe's (possibly deliberately cleared) offset must survive. Staging
    // belongs to inserted rows only.
    void mergeMatchedLegacyRowDoesNotStage() {
        const QString srcPath = freshDbPath();
        const QString destPath = freshDbPath();
        withRawDb(srcPath, "mrg_src", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("ALTER TABLE recipes DROP COLUMN temp_offset_c"));  // pre-31 schema
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, roaster_name, coffee_name, "
                           "temp_override_c) VALUES ('Twin', 'P', 'R', 'C', 87.0)"));
        });
        withRawDb(destPath, "mrg_dest", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QSqlQuery q(db);
            // Same identity, offset deliberately cleared to 0 locally.
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, roaster_name, coffee_name, "
                           "temp_offset_c, temp_override_c) VALUES ('Twin', 'P', 'R', 'C', 0, 87.0)"));
        });
        withRawDb(srcPath, "mrg_src2", [&](QSqlDatabase& srcDb) {
            withRawDb(destPath, "mrg_dest2", [&](QSqlDatabase& destDb) {
                QHash<qint64, qint64> idMap;
                QVERIFY(RecipeStorage::importRecipesStatic(srcDb, destDb, /*merge=*/true,
                                                           idMap, {}, {}));
                QSqlQuery r(destDb);
                QVERIFY(r.exec("SELECT COUNT(*), MIN(temp_offset_c) FROM recipes"));
                QVERIFY(r.next());
                QCOMPARE(r.value(0).toInt(), 1);         // matched, not duplicated
                QVERIFY(!r.value(1).isNull());           // no unconverted marker planted
                QCOMPARE(r.value(1).toDouble(), 0.0);    // the cleared offset survived
            });
        });
    }

    // A ≥31 source whose OWN deferred pass hadn't completed (backup exported
    // mid-window) carries NULL-offset rows with the absolute still in the
    // dead column. Those rows must import as UNCONVERTED — flattening the
    // NULL to an explicit 0 would silently destroy a still-recoverable pin.
    void importUnconvertedRowFromCurrentSourceStages() {
        const QString srcPath = freshDbPath();
        const QString destPath = freshDbPath();
        withRawDb(srcPath, "unc_src", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, temp_offset_c, temp_override_c) "
                           "VALUES ('Unconverted', 'Known', NULL, 87.0)"));
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, temp_offset_c, temp_override_c) "
                           "VALUES ('Converted', 'Known', -2.0, 87.0)"));
        });
        withRawDb(destPath, "unc_dest", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            // See importLegacyTempSourceStagesAndConverts: the post-import
            // grind-ownership backfill queries coffee_bags on the dest DB.
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
        });
        withRawDb(srcPath, "unc_src2", [&](QSqlDatabase& srcDb) {
            withRawDb(destPath, "unc_dest2", [&](QSqlDatabase& destDb) {
                QHash<qint64, qint64> idMap;
                QVERIFY(RecipeStorage::importRecipesStatic(srcDb, destDb, /*merge=*/false,
                                                           idMap, {}, {}));
                const QHash<QString, double> temps{{QStringLiteral("Known"), 90.0}};
                QVERIFY(RecipeStorage::convertLegacyTempOffsetsStatic(destDb, temps));
                QSqlQuery r(destDb);
                QVERIFY(r.exec("SELECT name, temp_offset_c FROM recipes ORDER BY name"));
                QVERIFY(r.next());
                QCOMPARE(r.value(0).toString(), QStringLiteral("Converted"));
                QCOMPARE(r.value(1).toDouble(), -2.0);   // verbatim, never reconverted
                QVERIFY(r.next());
                QCOMPARE(r.value(0).toString(), QStringLiteral("Unconverted"));
                QCOMPARE(r.value(1).toDouble(), -3.0);   // staged, then converted (87 − 90)
            });
        });
    }

    // A current-version source imports its offset VERBATIM — its dead
    // temp_override_c (still holding the pre-migration absolute) must never
    // resurrect an offset the user has since cleared.
    void importCurrentSourceKeepsOffsetVerbatim() {
        const QString srcPath = freshDbPath();
        const QString destPath = freshDbPath();
        withRawDb(srcPath, "cur_src", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QSqlQuery q(db);
            // Offset deliberately cleared to 0; stale absolute in the dead column.
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, temp_offset_c, temp_override_c) "
                           "VALUES ('Cleared', 'Known', 0, 87.0)"));
            QVERIFY(q.exec("INSERT INTO recipes (name, profile_title, temp_offset_c, temp_override_c) "
                           "VALUES ('Minus2', 'Known', -2.0, 87.0)"));
        });
        withRawDb(destPath, "cur_dest", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            // See importLegacyTempSourceStagesAndConverts: the post-import
            // grind-ownership backfill queries coffee_bags on the dest DB.
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
        });
        withRawDb(srcPath, "cur_src2", [&](QSqlDatabase& srcDb) {
            withRawDb(destPath, "cur_dest2", [&](QSqlDatabase& destDb) {
                QHash<qint64, qint64> idMap;
                QVERIFY(RecipeStorage::importRecipesStatic(srcDb, destDb, /*merge=*/false,
                                                           idMap, {}, {}));
                // Conversion pass runs after every import — it must find
                // nothing to do (no NULL markers from a current source).
                const QHash<QString, double> temps{{QStringLiteral("Known"), 90.0}};
                QVERIFY(RecipeStorage::convertLegacyTempOffsetsStatic(destDb, temps));
                QSqlQuery r(destDb);
                QVERIFY(r.exec("SELECT name, temp_offset_c FROM recipes ORDER BY name"));
                QVERIFY(r.next());
                QCOMPARE(r.value(0).toString(), QStringLiteral("Cleared"));
                QCOMPARE(r.value(1).toDouble(), 0.0);
                QVERIFY(r.next());
                QCOMPARE(r.value(0).toString(), QStringLiteral("Minus2"));
                QCOMPARE(r.value(1).toDouble(), -2.0);
            });
        });
    }

    void importRemapsBagId() {
        const QString srcPath = freshDbPath();
        const QString destPath = freshDbPath();
        qint64 srcLinkedId = 0, srcDanglingId = 0;
        withRawDb(srcPath, "imp_src", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe linked = sampleRecipe(); linked.name = "Linked"; linked.bagId = 40;
            Recipe dangling = sampleRecipe(); dangling.name = "Dangling"; dangling.bagId = 77;
            srcLinkedId = RecipeStorage::insertRecipeStatic(db, linked);
            srcDanglingId = RecipeStorage::insertRecipeStatic(db, dangling);
            QVERIFY(srcLinkedId > 0 && srcDanglingId > 0);
        });
        withRawDb(destPath, "imp_dest", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
        });
        withRawDb(srcPath, "imp_src2", [&](QSqlDatabase& srcDb) {
            withRawDb(destPath, "imp_dest2", [&](QSqlDatabase& destDb) {
                QHash<qint64, qint64> idMap;
                const QHash<qint64, qint64> bagIdMap{{40, 9}};  // 77 absent -> NULL
                QVERIFY(RecipeStorage::importRecipesStatic(srcDb, destDb, /*merge=*/false,
                                                           idMap, {}, bagIdMap));
                QCOMPARE(RecipeStorage::loadRecipeStatic(destDb, idMap.value(srcLinkedId)).bagId,
                         (qint64)9);
                QCOMPARE(RecipeStorage::loadRecipeStatic(destDb, idMap.value(srcDanglingId)).bagId,
                         (qint64)0);
            });
        });
    }

    // --- save-time normalization (create/update) ---

    // requestCreateRecipe normalizes the bag link once at save time: bean
    // identity without a bag resolves to that bean's open bag (older MCP/web
    // clients); a bag without identity adopts the bag's identity fields (the
    // relink matching key must always be populated); an unknown bag id drops
    // the link with a warning but still creates. The emitted map echoes the
    // caller's requestToken — the correlation contract for the broadcast
    // recipeCreated signal.
    void createNormalizesBagLink() {
        const QString path = freshDbPath();
        qint64 openBagId = 0;
        withRawDb(path, "create_norm_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            CoffeeBag bag; bag.roasterName = "Roaster"; bag.coffeeName = "Guji";
            bag.beanBaseId = "bb-uuid-1"; bag.inInventory = true;
            openBagId = CoffeeBagStorage::insertBagStatic(db, bag);
        });
        QVERIFY(openBagId > 0);

        RecipeStorage storage;
        storage.initialize(path);

        // Identity only -> resolved to the open bag; token echoed.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeCreated);
            QVariantMap map = sampleRecipe().toVariantMap();
            map.remove("id");
            map.insert("requestToken", "tok-identity");
            storage.requestCreateRecipe(map);
            QTRY_COMPARE(spy.count(), 1);
            const QVariantMap created = spy.at(0).at(1).toMap();
            QCOMPARE(created.value("requestToken").toString(), QString("tok-identity"));
            QCOMPARE(created.value("bagId").toLongLong(), openBagId);
        }

        // Bag only -> adopts the bag's identity fields.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeCreated);
            QVariantMap map{{"name", "Bag only"}, {"profileTitle", "P"},
                            {"bagId", openBagId}};
            storage.requestCreateRecipe(map);
            QTRY_COMPARE(spy.count(), 1);
            const QVariantMap created = spy.at(0).at(1).toMap();
            QCOMPARE(created.value("bagId").toLongLong(), openBagId);
            QCOMPARE(created.value("beanBaseId").toString(), QString("bb-uuid-1"));
            QCOMPARE(created.value("roasterName").toString(), QString("Roaster"));
            QCOMPARE(created.value("coffeeName").toString(), QString("Guji"));
        }

        // Unknown bag id -> link dropped (warned), recipe still created.
        {
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression("unknown bag id"));
            QSignalSpy spy(&storage, &RecipeStorage::recipeCreated);
            QVariantMap map{{"name", "Dangling"}, {"profileTitle", "P"},
                            {"bagId", (qint64)999999}};
            storage.requestCreateRecipe(map);
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(spy.at(0).at(0).toLongLong() > 0);
            QCOMPARE(spy.at(0).at(1).toMap().value("bagId").toLongLong(), (qint64)0);
        }
    }

    // Clone echoes the requestToken too (same correlation contract).
    void cloneEchoesRequestToken() {
        const QString path = freshDbPath();
        qint64 sourceId = 0;
        withRawDb(path, "clone_tok_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            sourceId = RecipeStorage::insertRecipeStatic(db, sampleRecipe());
        });
        RecipeStorage storage;
        storage.initialize(path);
        QSignalSpy spy(&storage, &RecipeStorage::recipeCreated);
        storage.requestCloneRecipe(sourceId, "Copy", "tok-clone");
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toMap().value("requestToken").toString(),
                 QString("tok-clone"));
    }

    // A bagId patch adopts the new bag's identity (the manual re-point);
    // explicit identity fields in the same patch win over adoption; a
    // DANGLING bagId drops the link field and applies the rest (the web
    // editor re-sends the stored bagId on every save — a bag deleted on
    // another surface must not turn a rename into a failure).
    void updateBagIdPatch() {
        const QString path = freshDbPath();
        qint64 recipeId = 0, bagAId = 0, bagBId = 0;
        withRawDb(path, "update_patch_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            CoffeeBag a; a.roasterName = "Roaster"; a.coffeeName = "Guji";
            a.beanBaseId = "bb-uuid-1"; a.inInventory = true;
            bagAId = CoffeeBagStorage::insertBagStatic(db, a);
            CoffeeBag b; b.roasterName = "Other"; b.coffeeName = "Yirg";
            b.beanBaseId = "bb-uuid-2"; b.inInventory = true;
            bagBId = CoffeeBagStorage::insertBagStatic(db, b);
            Recipe r = sampleRecipe(); r.bagId = bagAId;
            recipeId = RecipeStorage::insertRecipeStatic(db, r);
        });
        QVERIFY(recipeId > 0 && bagAId > 0 && bagBId > 0);

        RecipeStorage storage;
        storage.initialize(path);

        // Re-point to bag B: identity follows the bag.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestRelinkRecipeToBag(recipeId, bagBId);
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(1).toBool(), true);
        }
        withRawDb(path, "update_patch_check1", [&](QSqlDatabase& db) {
            const Recipe r = RecipeStorage::loadRecipeStatic(db, recipeId);
            QCOMPARE(r.bagId, bagBId);
            QCOMPARE(r.beanBaseId, QString("bb-uuid-2"));
            QCOMPARE(r.roasterName, QString("Other"));
            QCOMPARE(r.coffeeName, QString("Yirg"));
        });

        // Explicit identity in the same patch wins over adoption.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestUpdateRecipe(recipeId,
                {{"bagId", bagAId}, {"roasterName", "Custom"}});
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(1).toBool(), true);
        }
        withRawDb(path, "update_patch_check2", [&](QSqlDatabase& db) {
            const Recipe r = RecipeStorage::loadRecipeStatic(db, recipeId);
            QCOMPARE(r.bagId, bagAId);
            QCOMPARE(r.roasterName, QString("Custom"));      // explicit wins
            QCOMPARE(r.beanBaseId, QString("bb-uuid-1"));    // adopted
        });

        // Dangling bagId: the link field drops, the rest applies.
        {
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression("unknown bag id"));
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestUpdateRecipe(recipeId,
                {{"bagId", (qint64)999999}, {"name", "Renamed"}});
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(1).toBool(), true);
        }
        // Dangling bagId as the ONLY field: a clean no-op success.
        {
            QTest::ignoreMessage(QtWarningMsg,
                QRegularExpression("unknown bag id"));
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            storage.requestUpdateRecipe(recipeId, {{"bagId", (qint64)888888}});
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(1).toBool(), true);
        }
        withRawDb(path, "update_patch_check3", [&](QSqlDatabase& db) {
            const Recipe r = RecipeStorage::loadRecipeStatic(db, recipeId);
            QCOMPARE(r.name, QString("Renamed"));
            QCOMPARE(r.bagId, bagAId);  // existing link untouched
        });
    }

    // --- relink lifecycle (recipe-bag-lifecycle) ---

    void rollOnFinishHappyPath() {
        withRawDb(freshDbPath(), "roll_happy", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            qint64 finishedId = 0, openId = 0;
            insertBeanPair(db, &finishedId, &openId);
            QVERIFY(finishedId > 0 && openId > 0);

            Recipe r1 = sampleRecipe(); r1.name = "One"; r1.bagId = finishedId;
            Recipe r2 = sampleRecipe(); r2.name = "Two"; r2.bagId = finishedId;
            r2.profileTitle = "Different profile";  // no dup collision
            const qint64 id1 = RecipeStorage::insertRecipeStatic(db, r1);
            const qint64 id2 = RecipeStorage::insertRecipeStatic(db, r2);

            qint64 target = -1;
            const QVector<qint64> moved =
                RecipeStorage::relinkForFinishedBagStatic(db, finishedId, &target);
            QCOMPARE(target, openId);
            QCOMPARE(moved.size(), 2);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, id1).bagId, openId);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, id2).bagId, openId);
        });
    }

    void rollOnFinishDupGuard() {
        withRawDb(freshDbPath(), "roll_dup", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            qint64 finishedId = 0, openId = 0;
            insertBeanPair(db, &finishedId, &openId);

            // A deliberate comparison pair: same profile + drink type on both
            // bags. The roll would collapse it — the finished side stays put.
            Recipe onFinished = sampleRecipe(); onFinished.name = "Old bag"; onFinished.bagId = finishedId;
            Recipe onOpen = sampleRecipe(); onOpen.name = "New bag"; onOpen.bagId = openId;
            const qint64 finishedRecipeId = RecipeStorage::insertRecipeStatic(db, onFinished);
            QVERIFY(RecipeStorage::insertRecipeStatic(db, onOpen) > 0);

            const QVector<qint64> moved =
                RecipeStorage::relinkForFinishedBagStatic(db, finishedId);
            QVERIFY(moved.isEmpty());
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, finishedRecipeId).bagId, finishedId);
        });
    }

    void rollDifferentDrinkTypeIsNotDup() {
        withRawDb(freshDbPath(), "roll_type", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            qint64 finishedId = 0, openId = 0;
            insertBeanPair(db, &finishedId, &openId);

            // Same profile, DIFFERENT drink type — not a duplicate.
            Recipe latte = sampleRecipe(); latte.name = "Latte"; latte.bagId = finishedId;
            latte.drinkType = "latte";
            Recipe espresso = sampleRecipe(); espresso.name = "Espresso"; espresso.bagId = openId;
            espresso.drinkType = "espresso"; espresso.steamJson.clear();
            const qint64 latteId = RecipeStorage::insertRecipeStatic(db, latte);
            QVERIFY(RecipeStorage::insertRecipeStatic(db, espresso) > 0);

            const QVector<qint64> moved =
                RecipeStorage::relinkForFinishedBagStatic(db, finishedId);
            QCOMPARE(moved.size(), 1);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, latteId).bagId, openId);
        });
    }

    void rollNoSuccessorKeepsLink() {
        withRawDb(freshDbPath(), "roll_none", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            CoffeeBag only; only.roasterName = "Roaster"; only.coffeeName = "Guji";
            only.beanBaseId = "bb-uuid-1"; only.inInventory = false;
            const qint64 bagId = CoffeeBagStorage::insertBagStatic(db, only);
            Recipe r = sampleRecipe(); r.bagId = bagId;
            const qint64 recipeId = RecipeStorage::insertRecipeStatic(db, r);

            qint64 target = -1;
            const QVector<qint64> moved =
                RecipeStorage::relinkForFinishedBagStatic(db, bagId, &target);
            QVERIFY(moved.isEmpty());
            QCOMPARE(target, (qint64)-1);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, recipeId).bagId, bagId);
        });
    }

    void wakeOnRestock() {
        withRawDb(freshDbPath(), "wake", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            CoffeeBag gone; gone.roasterName = "Roaster"; gone.coffeeName = "Guji";
            gone.beanBaseId = "bb-uuid-1"; gone.inInventory = false;
            const qint64 goneId = CoffeeBagStorage::insertBagStatic(db, gone);

            // Stale recipe of that bean; a pinned grind must survive the move.
            Recipe stale = sampleRecipe(); stale.bagId = goneId;
            stale.grindPinned = "2.4"; stale.rpmPinned = 90;
            const qint64 staleId = RecipeStorage::insertRecipeStatic(db, stale);
            // A different bean's stale recipe must NOT wake.
            Recipe otherBean = sampleRecipe(); otherBean.name = "Other";
            otherBean.beanBaseId = "bb-other"; otherBean.roasterName = "Else";
            otherBean.coffeeName = "Where";
            const qint64 otherId = RecipeStorage::insertRecipeStatic(db, otherBean);

            CoffeeBag restock = gone; restock.inInventory = true;
            const qint64 newId = CoffeeBagStorage::insertBagStatic(db, restock);

            const QVector<qint64> moved =
                RecipeStorage::relinkForRestockedBagStatic(db, newId);
            QCOMPARE(moved.size(), 1);
            QCOMPARE(moved.first(), staleId);
            const Recipe woken = RecipeStorage::loadRecipeStatic(db, staleId);
            QCOMPARE(woken.bagId, newId);
            // Relink never rewrites grind: the pin is untouched.
            QCOMPARE(woken.grindPinned, QString("2.4"));
            QCOMPARE(woken.rpmPinned, (qint64)90);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, otherId).bagId, (qint64)0);
        });
    }

    void wakeTwinsMruFirst() {
        withRawDb(freshDbPath(), "wake_twins", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            // Two stale TWINS (same profile + drink type) of the same bean:
            // only the most recently used one wakes; the dup-guard holds the
            // other back for the next restock.
            Recipe older = sampleRecipe(); older.name = "Older"; older.lastUsedEpoch = 100;
            Recipe newer = sampleRecipe(); newer.name = "Newer"; newer.lastUsedEpoch = 500;
            const qint64 olderId = RecipeStorage::insertRecipeStatic(db, older);
            const qint64 newerId = RecipeStorage::insertRecipeStatic(db, newer);

            CoffeeBag bag; bag.roasterName = "Roaster"; bag.coffeeName = "Guji";
            bag.beanBaseId = "bb-uuid-1"; bag.inInventory = true;
            const qint64 bagId = CoffeeBagStorage::insertBagStatic(db, bag);

            const QVector<qint64> moved =
                RecipeStorage::relinkForRestockedBagStatic(db, bagId);
            QCOMPARE(moved.size(), 1);
            QCOMPARE(moved.first(), newerId);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, newerId).bagId, bagId);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, olderId).bagId, (qint64)0);
        });
    }

    // Roll matching falls back to case-insensitive roaster+coffee when the
    // bean has no canonical id (the common non-Bean-Base path), and among
    // SEVERAL open bags picks the NEWEST (highest id), not the MRU one.
    void rollIdentityFallbackPicksNewest() {
        withRawDb(freshDbPath(), "roll_ident", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            CoffeeBag finished; finished.roasterName = "ROASTER"; finished.coffeeName = "guji";
            finished.inInventory = false;   // no beanBaseId anywhere
            const qint64 finishedId = CoffeeBagStorage::insertBagStatic(db, finished);
            CoffeeBag older; older.roasterName = "Roaster"; older.coffeeName = "Guji";
            older.inInventory = true; older.lastUsedEpoch = 900;  // MRU but older
            const qint64 olderId = CoffeeBagStorage::insertBagStatic(db, older);
            CoffeeBag newest = older; newest.lastUsedEpoch = 100;
            const qint64 newestId = CoffeeBagStorage::insertBagStatic(db, newest);
            QVERIFY(finishedId > 0 && olderId > 0 && newestId > 0);

            Recipe r = sampleRecipe(); r.beanBaseId.clear(); r.bagId = finishedId;
            const qint64 recipeId = RecipeStorage::insertRecipeStatic(db, r);

            qint64 target = -1;
            const QVector<qint64> moved =
                RecipeStorage::relinkForFinishedBagStatic(db, finishedId, &target);
            QCOMPARE(moved.size(), 1);
            QCOMPARE(target, newestId);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, recipeId).bagId, newestId);
        });
    }

    // Wake guards: a bag NOT in inventory never wakes anything, and identity
    // matching is case-insensitive.
    void wakeGuardsAndCaseInsensitivity() {
        withRawDb(freshDbPath(), "wake_guards", [](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            Recipe stale = sampleRecipe();
            stale.beanBaseId.clear();
            stale.roasterName = "ROASTER"; stale.coffeeName = "GUJI";
            const qint64 staleId = RecipeStorage::insertRecipeStatic(db, stale);

            // Out-of-inventory bag: no wake.
            CoffeeBag closed; closed.roasterName = "Roaster"; closed.coffeeName = "Guji";
            closed.inInventory = false;
            const qint64 closedId = CoffeeBagStorage::insertBagStatic(db, closed);
            QVERIFY(RecipeStorage::relinkForRestockedBagStatic(db, closedId).isEmpty());
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, staleId).bagId, (qint64)0);

            // Open bag, different casing: wakes.
            CoffeeBag open = closed; open.inInventory = true;
            const qint64 openId = CoffeeBagStorage::insertBagStatic(db, open);
            const QVector<qint64> moved = RecipeStorage::relinkForRestockedBagStatic(db, openId);
            QCOMPARE(moved.size(), 1);
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, staleId).bagId, openId);
        });
    }

    // The async wrappers' observable contract: recipesRelinked (with the
    // moved ids + target bag id + display name) and recipesChanged fire when
    // something moved; NOTHING fires when nothing moved — the toast and the
    // active-cache refresh both hang off exactly this payload.
    void relinkWrappersSignalContract() {
        const QString path = freshDbPath();
        qint64 finishedId = 0, openId = 0, recipeId = 0;
        withRawDb(path, "wrap_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            createMinimalShots(db);  // the drain read below joins shot counts
            insertBeanPair(db, &finishedId, &openId);
            Recipe r = sampleRecipe(); r.bagId = finishedId;
            recipeId = RecipeStorage::insertRecipeStatic(db, r);
        });
        QVERIFY(recipeId > 0);

        RecipeStorage storage;
        storage.initialize(path);

        // Moves: one relinked emission naming the target bag, + recipesChanged.
        {
            QSignalSpy relinked(&storage, &RecipeStorage::recipesRelinked);
            QSignalSpy changed(&storage, &RecipeStorage::recipesChanged);
            storage.requestRelinkForFinishedBag(finishedId);
            QTRY_COMPARE(relinked.count(), 1);
            const auto args = relinked.at(0);
            QCOMPARE(args.at(0).toList().size(), 1);
            QCOMPARE(args.at(0).toList().first().toLongLong(), recipeId);
            QCOMPARE(args.at(1).toLongLong(), openId);
            QCOMPARE(args.at(2).toString(), QString("Guji"));
            QCOMPARE(changed.count(), 1);
        }

        // Nothing left to move: silent (no phantom toast).
        {
            QSignalSpy relinked(&storage, &RecipeStorage::recipesRelinked);
            QSignalSpy changed(&storage, &RecipeStorage::recipesChanged);
            storage.requestRelinkForFinishedBag(finishedId);
            // Drain the worker with an unrelated read so the relink job has
            // definitely completed before asserting silence.
            QSignalSpy inv(&storage, &RecipeStorage::inventoryReady);
            storage.requestInventory();
            QTRY_COMPARE(inv.count(), 1);
            QCOMPARE(relinked.count(), 0);
            QCOMPARE(changed.count(), 0);
        }
    }

    // --- activation bundle ---

    // requestRecipeForActivation loads recipe + resolved bag in ONE pass and is
    // the terminus of every activation caller (idle pill, RecipesPage, MCP,
    // web). Its three documented contracts — happy path, missing recipe emits
    // an EMPTY map (activation must fail cleanly, NOT hang), and bean-less /
    // no-linked-bag emits linkedBagId == -1 — are each load-bearing.
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
        // is untouched. The rejection logs a qWarning naming the recipe; that's
        // the behaviour under test, so mark it expected (TESTING.md).
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
                QString("RecipeStorage: rejecting update that would strand recipe %1 .*").arg(espId)));
            storage.requestUpdateRecipe(espId, {{"profileTitle", QString()}});
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(!spy.at(0).at(1).toBool());  // failed
        }
        // Removing the hot-water block from a profile-less recipe is rejected
        // too (the case the per-surface profileTitle guard misses entirely).
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
                QString("RecipeStorage: rejecting update that would strand recipe %1 .*").arg(hwId)));
            storage.requestUpdateRecipe(hwId, {{"hotWaterJson", QString()}});
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(!spy.at(0).at(1).toBool());
        }
        // A hint-only patch (nothing persistable) fails cleanly, not silently.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
                QString("RecipeStorage: update for recipe %1 carried no persistable fields").arg(espId)));
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
                                 QStringLiteral("latte"), QStringLiteral("latte_hotwater"),
                                 QStringLiteral("tea"), QStringLiteral("tea_hotwater")})
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

    // Adding a hot-water block to a stored latte (via an MCP/web update that
    // omits drinkType) re-derives to latte_hotwater — the milk+water name.
    void updateRederivesLatteToLatteHotWater() {
        const QString path = freshDbPath();
        qint64 id = 0;
        withRawDb(path, "lattehw_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            Recipe r;
            r.name = "Flat white"; r.profileTitle = "Espresso 1.0";
            r.drinkType = "latte"; r.steamJson = "{\"hasMilk\":true}";
            id = RecipeStorage::insertRecipeStatic(db, r);
        });
        RecipeStorage storage;
        storage.initialize(path);
        QSignalSpy spy(&storage, &RecipeStorage::recipeUpdated);
        storage.requestUpdateRecipe(id,
            {{"hotWaterJson", "{\"hasWater\":true,\"vesselName\":\"Cup\",\"order\":\"after\"}"}});
        QTRY_COMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(1).toBool());
        withRawDb(path, "lattehw_verify", [&](QSqlDatabase& db) {
            QCOMPARE(RecipeStorage::loadRecipeStatic(db, id).drinkType,
                     QString("latte_hotwater"));
        });
    }

    void requestRecipeForActivation() {
        const QString path = freshDbPath();
        qint64 recipeId = 0, bareId = 0, staleId = 0, mruTrapId = 0;
        qint64 linkedBagId = 0, finishedBagId = 0;
        withRawDb(path, "act_setup", [&](QSqlDatabase& db) {
            QVERIFY(RecipeStorage::ensureTableStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            // Two open bags of the same bean: the recipe links the OLDER,
            // less recently used one — activation must apply exactly it (no
            // MRU resolution, the whole point of the hard bag link).
            CoffeeBag linked; linked.roasterName = "Roaster"; linked.coffeeName = "Guji";
            linked.beanBaseId = "bb-uuid-1"; linked.inInventory = true;
            linked.grinderSetting = "2.4"; linked.lastUsedEpoch = 100;
            linkedBagId = CoffeeBagStorage::insertBagStatic(db, linked);
            CoffeeBag mruTwin = linked; mruTwin.grinderSetting = "3.0";
            mruTwin.lastUsedEpoch = 900;
            QVERIFY(CoffeeBagStorage::insertBagStatic(db, mruTwin) > 0);
            // A finished bag for the stale case.
            CoffeeBag finished = linked; finished.inInventory = false;
            finished.grinderSetting = "1.8";
            finishedBagId = CoffeeBagStorage::insertBagStatic(db, finished);

            Recipe r = sampleRecipe();
            r.bagId = linkedBagId;
            recipeId = RecipeStorage::insertRecipeStatic(db, r);
            Recipe stale = sampleRecipe(); stale.name = "Stale";
            stale.bagId = finishedBagId;
            staleId = RecipeStorage::insertRecipeStatic(db, stale);
            // Bean identity but NO bag link (unresolved migration): no
            // MRU resolution happens at activation anymore.
            Recipe unlinked = sampleRecipe(); unlinked.name = "Unlinked";
            mruTrapId = RecipeStorage::insertRecipeStatic(db, unlinked);
            Recipe bare; bare.name = "Bare";  // no bean link
            bareId = RecipeStorage::insertRecipeStatic(db, bare);
        });
        QVERIFY(recipeId > 0 && bareId > 0 && staleId > 0 && mruTrapId > 0
                && linkedBagId > 0 && finishedBagId > 0);

        RecipeStorage storage;
        storage.initialize(path);

        // Happy path: recipe map + the LINKED bag (not the MRU twin), one pass.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeActivationReady);
            storage.requestRecipeForActivation(recipeId);
            QTRY_COMPARE(spy.count(), 1);
            const auto args = spy.at(0);
            QCOMPARE(args.at(0).toLongLong(), recipeId);
            QCOMPARE(args.at(1).toMap().value("name").toString(), QString("Morning capp"));
            QCOMPARE(args.at(2).toLongLong(), linkedBagId);
            QCOMPARE(args.at(3).toMap().value("grinderSetting").toString(), QString("2.4"));
        }

        // Stale recipe: the FINISHED bag applies fully — full bag map with
        // its grind, no error, no substitute bag.
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeActivationReady);
            storage.requestRecipeForActivation(staleId);
            QTRY_COMPARE(spy.count(), 1);
            const auto args = spy.at(0);
            QCOMPARE(args.at(2).toLongLong(), finishedBagId);
            QCOMPARE(args.at(3).toMap().value("grinderSetting").toString(), QString("1.8"));
            QCOMPARE(args.at(3).toMap().value("inInventory").toBool(), false);
        }

        // Missing recipe: emits with an EMPTY recipe map (caller fails cleanly).
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeActivationReady);
            storage.requestRecipeForActivation(999999);
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(spy.at(0).at(1).toMap().isEmpty());
            QCOMPARE(spy.at(0).at(2).toLongLong(), (qint64)-1);
        }

        // Bean identity but no bag link: bagId == -1 — activation performs
        // NO identity resolution (the retired MRU behavior must stay dead).
        {
            QSignalSpy spy(&storage, &RecipeStorage::recipeActivationReady);
            storage.requestRecipeForActivation(mruTrapId);
            QTRY_COMPARE(spy.count(), 1);
            QVERIFY(!spy.at(0).at(1).toMap().isEmpty());
            QCOMPARE(spy.at(0).at(2).toLongLong(), (qint64)-1);
            QVERIFY(spy.at(0).at(3).toMap().isEmpty());
        }

        // Bean-less recipe: valid recipe, bagId == -1, empty bag map.
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
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
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
