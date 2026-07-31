#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>

#include "history/equipmentstorage.h"
#include "history/coffeebagstorage.h"
#include "core/puckprep.h"
#include "shotrowfixtures.h"

// Equipment packages: the grind/rpm split heuristic, rpmCapable derivation,
// package CRUD, identity dedup, and the migration-22 data step
// (add-equipment-packages).

using ShotRowFixtures::withRawDb;

// Minimal shots table carrying just the columns the migration reads/writes.
static void createMinimalShots(QSqlDatabase& db) {
    QSqlQuery(db).exec(R"(
        CREATE TABLE shots (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            grinder_brand TEXT, grinder_model TEXT, grinder_burrs TEXT,
            grinder_setting TEXT, equipment_id INTEGER, rpm INTEGER
        )
    )");
}

static qint64 insertShot(QSqlDatabase& db, const QString& brand, const QString& model,
                         const QString& burrs, const QString& setting) {
    QSqlQuery q(db);
    q.prepare("INSERT INTO shots (grinder_brand, grinder_model, grinder_burrs, grinder_setting) "
              "VALUES (?, ?, ?, ?)");
    q.addBindValue(brand); q.addBindValue(model); q.addBindValue(burrs); q.addBindValue(setting);
    q.exec();
    return q.lastInsertId().toLongLong();
}

class tst_Equipment : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_seq = 0;
    QString freshDbPath() { return m_dir.filePath(QString("eq_%1.db").arg(++m_seq)); }

private slots:
    void init() { QTest::failOnWarning(); }
    // --- splitGrindAndRpm ---
    void splitGrindRpm_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("grind");
        QTest::addColumn<qint64>("rpm");
        QTest::newRow("annotated")     << "24 1400rpm" << "24"        << (qint64)1400;
        QTest::newRow("spaced")        << "2.4 1400 rpm" << "2.4"     << (qint64)1400;
        QTest::newRow("caps")          << "24 1400RPM" << "24"        << (qint64)1400;
        QTest::newRow("compound")      << "1+4"       << "1+4"        << (qint64)0;
        QTest::newRow("clicks")        << "24 clicks" << "24 clicks"  << (qint64)0;
        QTest::newRow("plain")         << "24"        << "24"         << (qint64)0;
        QTest::newRow("empty")         << ""          << ""           << (qint64)0;
        QTest::newRow("rpm_only")      << "1400rpm"   << ""           << (qint64)1400;
        QTest::newRow("rpm_word_no_digits") << "rpm"  << "rpm"        << (qint64)0;
        QTest::newRow("rpm_not_trailing")   << "1400 rpm extra" << "1400 rpm extra" << (qint64)0;
        QTest::newRow("trailing_ws")   << "24 1400 rpm " << "24"      << (qint64)1400;
    }
    void splitGrindRpm() {
        QFETCH(QString, input);
        QFETCH(QString, grind);
        QFETCH(qint64, rpm);
        QString outGrind; qint64 outRpm = -1;
        EquipmentStorage::splitGrindAndRpm(input, outGrind, outRpm);
        QCOMPARE(outGrind, grind);
        QCOMPARE(outRpm, rpm);
    }

    // --- rpmCapable derivation ---
    void rpmCapable() {
        // Registry variableRpm grinder.
        QVERIFY(EquipmentStorage::deriveRpmCapable("Turin", "DF83V"));
        // Registry non-variable grinder.
        QVERIFY(!EquipmentStorage::deriveRpmCapable("Niche", "Zero"));
        // Custom grinder not in the registry -> shown (true).
        QVERIFY(EquipmentStorage::deriveRpmCapable("Acme", "Imaginary 9000"));
    }

    // --- package create + load ---
    void createAndLoadPackage() {
        const QString path = freshDbPath();
        withRawDb(path, "eq_create", [](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage pkg;
            pkg.lastGrindSetting = "24";
            pkg.lastRpm = 1400;
            const qint64 id = EquipmentStorage::createPackageWithGrinderStatic(
                db, pkg, "Turin", "DF83V", "83mm flat steel");
            QVERIFY(id > 0);

            const EquipmentPackage loaded = EquipmentStorage::loadPackageStatic(db, id);
            QVERIFY(loaded.isValid());
            QCOMPARE(loaded.lastGrindSetting, QString("24"));
            QCOMPARE(loaded.lastRpm, (qint64)1400);

            const EquipmentItem grinder = EquipmentStorage::loadGrinderItemStatic(db, id);
            QVERIFY(grinder.isValid());
            QCOMPARE(grinder.brand, QString("Turin"));
            QCOMPARE(grinder.model, QString("DF83V"));
            QCOMPARE(grinder.burrs, QString("83mm flat steel"));
            QVERIFY(grinder.rpmCapable);  // DF83V is variableRpm
        });
    }

    // --- grinder-less (basket-only) packages: tea setups (add-recipe-wizard-tea) ---
    void basketOnlyPackage() {
        const QString path = freshDbPath();
        withRawDb(path, "eq_teabasket", [](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage pkg;
            const qint64 id = EquipmentStorage::createPackageWithGrinderStatic(
                db, pkg, "", "", "", "Decent", "Tea Basket");
            QVERIFY(id > 0);

            // No grinder item row; display name fell back to the basket.
            QVERIFY(!EquipmentStorage::loadGrinderItemStatic(db, id).isValid());
            const EquipmentItem basket = EquipmentStorage::loadBasketItemStatic(db, id);
            QVERIFY(basket.isValid());
            QCOMPARE(basket.brand, QString("Decent"));
            QCOMPARE(EquipmentStorage::loadPackageStatic(db, id).name,
                     QString("Decent Tea Basket"));

            // Dedup matches the grinder-less identity (empty grinder + basket)…
            QCOMPARE(EquipmentStorage::findPackageByGrinderIdentityStatic(
                         db, "", "", "", 0, "decent", "tea basket"), id);
            // …and does NOT match a different basket or a grinder-carrying query.
            QCOMPARE(EquipmentStorage::findPackageByGrinderIdentityStatic(
                         db, "", "", "", 0, "VST", "18g"), (qint64)0);
            QCOMPARE(EquipmentStorage::findPackageByGrinderIdentityStatic(
                         db, "Niche", "Zero", "", 0, "Decent", "Tea Basket"), (qint64)0);

            // In-place identity edit works with no grinder row (unused package):
            // changing the basket keeps the same id and stays grinder-less.
            const qint64 edited = EquipmentStorage::supersedeOrEditStatic(
                db, id, "", "", "", "Weber", "Unibasket 18g", "");
            QCOMPARE(edited, id);
            QCOMPARE(EquipmentStorage::loadBasketItemStatic(db, id).brand, QString("Weber"));
            QVERIFY(!EquipmentStorage::loadGrinderItemStatic(db, id).isValid());

            // Adding a grinder to a grinder-less package inserts the item.
            const qint64 withGrinder = EquipmentStorage::supersedeOrEditStatic(
                db, id, "Niche", "Zero", "63mm conical", "Weber", "Unibasket 18g", "");
            QCOMPARE(withGrinder, id);
            QCOMPARE(EquipmentStorage::loadGrinderItemStatic(db, id).brand, QString("Niche"));
        });
    }

    // --- identity dedup lookup ---
    void identityDedup() {
        const QString path = freshDbPath();
        withRawDb(path, "eq_dedup", [](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage pkg;
            const qint64 id = EquipmentStorage::createPackageWithGrinderStatic(
                db, pkg, "Niche", "Zero", "63mm conical");
            QVERIFY(id > 0);
            // Case-insensitive identity match.
            QCOMPARE(EquipmentStorage::findPackageByGrinderIdentityStatic(db, "niche", "zero", "63mm conical"), id);
            // Different burrs -> no match.
            QCOMPARE(EquipmentStorage::findPackageByGrinderIdentityStatic(db, "Niche", "Zero", "other"), (qint64)0);
            // Unknown grinder -> no match.
            QCOMPARE(EquipmentStorage::findPackageByGrinderIdentityStatic(db, "X", "Y", ""), (qint64)0);
        });
    }

    // --- active-name uniqueness lookup (block-duplicate-active-names) ---
    // Two in-inventory packages may not share a name; a name freed by removing a
    // package from inventory becomes reusable. Comparison is trimmed and
    // case-insensitive, and excludes the package being edited.
    void activeNameUniqueness() {
        const QString path = freshDbPath();
        withRawDb(path, "eq_namedup", [](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage a;
            a.name = "Espresso setup";
            const qint64 idA = EquipmentStorage::createPackageWithGrinderStatic(
                db, a, "Niche", "Zero", "63mm conical");
            QVERIFY(idA > 0);

            // Exact, case-insensitive and whitespace-insensitive matches all hit.
            QCOMPARE(EquipmentStorage::findPackageByNameStatic(db, "Espresso setup"), idA);
            QCOMPARE(EquipmentStorage::findPackageByNameStatic(db, "  espresso SETUP  "), idA);
            // A different name does not.
            QCOMPARE(EquipmentStorage::findPackageByNameStatic(db, "Filter setup"), (qint64)0);
            // A blank name is derived from brand+model and never collides.
            QCOMPARE(EquipmentStorage::findPackageByNameStatic(db, ""), (qint64)0);
            QCOMPARE(EquipmentStorage::findPackageByNameStatic(db, "   "), (qint64)0);
            // The package being edited is excluded, so renaming it to a casing
            // variant of its own name is allowed.
            QCOMPARE(EquipmentStorage::findPackageByNameStatic(db, "espresso setup", idA), (qint64)0);

            // Removing it from inventory frees the name for reuse.
            QVERIFY(EquipmentStorage::updatePackageFieldsStatic(db, idA, {{"inInventory", false}}));
            QCOMPARE(EquipmentStorage::findPackageByNameStatic(db, "Espresso setup"), (qint64)0);
        });
    }

    // --- active-name uniqueness through the async request* paths ---

    // Re-saving a package under the name it ALREADY has must succeed, even when
    // another in-inventory package shares that name. Blank names are derived and
    // persisted at creation ("{brand} {model}"), so same-grinder/different-basket
    // packages legitimately collide; a guard testing the submitted name alone
    // locked the user out of editing gear they never named.
    void unchangedDerivedNameStillSaves() {
        const QString path = freshDbPath();
        qint64 idA = 0, idB = 0;
        withRawDb(path, "eq_pre", [&](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage a, b;   // both blank -> both derive "Niche Zero"
            idA = EquipmentStorage::createPackageWithGrinderStatic(
                db, a, "Niche", "Zero", "63mm conical", "Decent", "18g Ridged", "shaker");
            idB = EquipmentStorage::createPackageWithGrinderStatic(
                db, b, "Niche", "Zero", "63mm conical", "Decent", "18g Ridged", "shaker,rdt");
            QCOMPARE(EquipmentStorage::loadPackageStatic(db, idA).name, QString("Niche Zero"));
            QCOMPARE(EquipmentStorage::loadPackageStatic(db, idB).name, QString("Niche Zero"));
        });
        QVERIFY(idA > 0 && idB > 0 && idA != idB);

        EquipmentStorage storage;
        storage.initialize(path);
        {   // Editing one, resubmitting its own derived name -> allowed.
            QSignalSpy spy(&storage, &EquipmentStorage::packageUpdated);
            storage.requestUpdatePackage(idB, {{"name", "Niche Zero"}, {"lastGrindSetting", "8.25"}});
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(spy.at(0).at(1).toBool(), true);
        }
        withRawDb(path, "eq_pre2", [&](QSqlDatabase& db) {
            QCOMPARE(EquipmentStorage::loadPackageStatic(db, idB).lastGrindSetting, QString("8.25"));
        });
    }

    // A rename into a collision is refused BEFORE anything is written: no fork,
    // no supersede, no field applied, and the reason precedes the terminal status.
    void rejectedRenameIsCleanNoOp() {
        const QString path = freshDbPath();
        qint64 idA = 0, idB = 0;
        withRawDb(path, "eq_rename", [&](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage a; a.name = "Espresso setup";
            EquipmentPackage b; b.name = "Filter setup";
            idA = EquipmentStorage::createPackageWithGrinderStatic(db, a, "Niche", "Zero", "63mm conical");
            idB = EquipmentStorage::createPackageWithGrinderStatic(db, b, "Turin", "DF83V", "83mm flat");
        });
        QVERIFY(idA > 0 && idB > 0);

        EquipmentStorage storage;
        storage.initialize(path);
        QStringList order;
        connect(&storage, &EquipmentStorage::packageUpdateFailed, this,
                [&order](qint64, const QString& r) { order << ("failed:" + r); });
        connect(&storage, &EquipmentStorage::packageUpdated, this,
                [&order](qint64, bool ok) { order << (ok ? "updated:true" : "updated:false"); });

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("rejecting rename of package"));
        QSignalSpy spy(&storage, &EquipmentStorage::packageUpdated);
        // Rename onto A's name AND edit the identity in the same patch.
        storage.requestUpdatePackage(idB, {{"name", "Espresso setup"}, {"grinderModel", "DF64"}});
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toLongLong(), idB);   // no fork id
        QCOMPARE(spy.at(0).at(1).toBool(), false);
        QCOMPARE(order, QStringList({"failed:nameInUse", "updated:false"}));

        withRawDb(path, "eq_rename2", [&](QSqlDatabase& db) {
            const EquipmentPackage b = EquipmentStorage::loadPackageStatic(db, idB);
            QCOMPARE(b.name, QString("Filter setup"));      // name untouched
            QVERIFY(b.inInventory);                          // not retired by a supersede
            QCOMPARE(EquipmentStorage::loadGrinderItemStatic(db, idB).model, QString("DF83V"));
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*) FROM equipment_packages"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 2);                 // no new row minted
        });
    }

    // --- copy-on-write immutability + merge on identity edit ---
    void copyOnWriteAndMerge() {
        const QString path = freshDbPath();
        withRawDb(path, "eq_cow", [](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            createMinimalShots(db);

            auto bagEq = [&](qint64 id) {
                QSqlQuery q(db); q.prepare("SELECT equipment_id FROM coffee_bags WHERE id=?");
                q.addBindValue(id); q.exec(); q.next(); return q.value(0).toLongLong();
            };
            auto shotEq = [&](qint64 id) {
                QSqlQuery q(db); q.prepare("SELECT equipment_id FROM shots WHERE id=?");
                q.addBindValue(id); q.exec(); q.next(); return q.value(0).toLongLong();
            };
            auto addBag = [&](qint64 eq) {
                QSqlQuery q(db);
                q.prepare("INSERT INTO coffee_bags (roaster_name, equipment_id, in_inventory) VALUES ('R', ?, 1)");
                q.addBindValue(eq); q.exec(); return q.lastInsertId().toLongLong();
            };
            auto addShot = [&](qint64 eq) {
                QSqlQuery q(db);
                q.prepare("INSERT INTO shots (equipment_id) VALUES (?)");
                q.addBindValue(eq); q.exec(); return q.lastInsertId().toLongLong();
            };

            // Package P, used by a shot and pointed at by a bag.
            EquipmentPackage base;
            const qint64 P = EquipmentStorage::createPackageWithGrinderStatic(db, base, "Turin", "DF83V", "83mm flat steel");
            const qint64 bag = addBag(P);
            const qint64 shot = addShot(P);

            // Edit burrs on a USED package -> fork (copy-on-write).
            const qint64 fork = EquipmentStorage::supersedeOrEditGrinderStatic(db, P, "Turin", "DF83V", "83mm DLC flat");
            QVERIFY(fork > 0 && fork != P);
            // Old P retired + lineage; new fork current.
            const EquipmentPackage oldP = EquipmentStorage::loadPackageStatic(db, P);
            QVERIFY(!oldP.inInventory);
            QCOMPARE(oldP.supersededBy, fork);
            QCOMPARE(oldP.name, QString("Turin DF83V"));  // name persisted
            const EquipmentPackage newP = EquipmentStorage::loadPackageStatic(db, fork);
            QCOMPARE(newP.name, QString("Turin DF83V"));  // name preserved on fork
            QCOMPARE(EquipmentStorage::loadGrinderItemStatic(db, fork).burrs, QString("83mm DLC flat"));
            // Bag repointed to the fork; shot stays on the old package (history).
            QCOMPARE(bagEq(bag), fork);
            QCOMPARE(shotEq(shot), P);

            // Unchanged identity -> no-op (no new fork).
            QCOMPARE(EquipmentStorage::supersedeOrEditGrinderStatic(db, fork, "Turin", "DF83V", "83mm DLC flat"), fork);

            // Unused package edits in place (same id).
            EquipmentPackage q2;
            const qint64 Q = EquipmentStorage::createPackageWithGrinderStatic(db, q2, "Niche", "Zero", "63mm conical");
            QCOMPARE(EquipmentStorage::supersedeOrEditGrinderStatic(db, Q, "Niche", "Zero", "swapped"), Q);
            QCOMPARE(EquipmentStorage::loadGrinderItemStatic(db, Q).burrs, QString("swapped"));

            // ENRICHMENT (#1713): filling in a component that was EMPTY on a USED
            // package is recording gear it always had, not a swap — edit in place,
            // keep the id, keep the shots. A fork here retires the package the whole
            // history hangs off and the grinder reads as brand new.
            EquipmentPackage e;
            const qint64 E = EquipmentStorage::createPackageWithGrinderStatic(db, e, "Eureka", "Mignon Single Dose", "");
            const qint64 eShot = addShot(E);
            QCOMPARE(EquipmentStorage::supersedeOrEditGrinderStatic(db, E, "Eureka", "Mignon Single Dose", "Lebrew Sweet"), E);
            QCOMPARE(EquipmentStorage::loadGrinderItemStatic(db, E).burrs, QString("Lebrew Sweet"));
            QVERIFY(EquipmentStorage::loadPackageStatic(db, E).inInventory);   // not retired
            QCOMPARE(shotEq(eShot), E);                                        // history still attached

            // ...but CHANGING a component that had a value is a real swap and still
            // forks, so the earlier shots keep the burrs they were actually pulled on.
            const qint64 eFork = EquipmentStorage::supersedeOrEditGrinderStatic(db, E, "Eureka", "Mignon Single Dose", "Lebrew Sharp");
            QVERIFY(eFork > 0 && eFork != E);
            QCOMPARE(shotEq(eShot), E);

            // Enrichment across the whole tuple: an empty basket + puck prep named
            // for the first time on a used package stays in place too.
            EquipmentPackage f;
            const qint64 F = EquipmentStorage::createPackageWithGrinderStatic(db, f, "Niche", "Duo", "");
            addShot(F);
            QCOMPARE(EquipmentStorage::supersedeOrEditStatic(db, F, "Niche", "Duo", "83mm",
                                                             "IMS", "Superfine 18g", QStringLiteral("wdt")), F);
            QCOMPARE(EquipmentStorage::loadBasketItemStatic(db, F).model, QString("Superfine 18g"));
            QCOMPARE(EquipmentStorage::loadPuckPrepItemStatic(db, F).model, QStringLiteral("wdt"));

            // Adding a technique to an EXISTING puck prep is a change, not enrichment:
            // the earlier shots really were pulled with the shorter routine.
            const qint64 fFork = EquipmentStorage::supersedeOrEditStatic(db, F, "Niche", "Duo", "83mm",
                                                                         "IMS", "Superfine 18g",
                                                                         PuckPrep::recanonical(QStringLiteral("wdt,rdt")));
            QVERIFY(fFork > 0 && fFork != F);

            // A grinder-less (basket-only tea) package GAINING a grinder is a real
            // identity change, not enrichment: its shots were pulled with nothing
            // ground, and they must not start reporting a grinder.
            EquipmentPackage tea;
            const qint64 T = EquipmentStorage::createPackageWithGrinderStatic(
                db, tea, "", "", "", "IMS", "Superfine 18g", QString());
            const qint64 teaShot = addShot(T);
            const qint64 teaFork = EquipmentStorage::supersedeOrEditStatic(
                db, T, "Niche", "Zero", "", "IMS", "Superfine 18g", QString());
            QVERIFY(teaFork > 0 && teaFork != T);
            QCOMPARE(shotEq(teaShot), T);   // the tea shot keeps its grinder-less package

            // Clearing a component is a change, not enrichment -> forks as well.
            EquipmentPackage g;
            const qint64 G = EquipmentStorage::createPackageWithGrinderStatic(db, g, "Mazzer", "Philos", "83mm");
            addShot(G);
            const qint64 gFork = EquipmentStorage::supersedeOrEditGrinderStatic(db, G, "Mazzer", "Philos", "");
            QVERIFY(gFork > 0 && gFork != G);

            // Merge: a USED package edited to the fork's identity merges into it.
            EquipmentPackage s;
            const qint64 S = EquipmentStorage::createPackageWithGrinderStatic(db, s, "Mazzer", "Major", "83mm");
            addShot(S);
            const qint64 merged = EquipmentStorage::supersedeOrEditGrinderStatic(db, S, "Turin", "DF83V", "83mm DLC flat");
            QCOMPARE(merged, fork);  // repointed to the existing matching package
            QCOMPARE(EquipmentStorage::loadPackageStatic(db, S).supersededBy, fork);
        });
    }

    // --- explicit two-id merge (the repair for a wrongly split grinder, #1713) ---
    void mergePackagesById() {
        const QString path = freshDbPath();
        withRawDb(path, "eq_merge_ids", [](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            createMinimalShots(db);
            auto addShot = [&](qint64 eq) {
                QSqlQuery q(db); q.prepare("INSERT INTO shots (equipment_id) VALUES (?)");
                q.addBindValue(eq); q.exec(); return q.lastInsertId().toLongLong();
            };
            auto addBag = [&](qint64 eq) {
                QSqlQuery q(db);
                q.prepare("INSERT INTO coffee_bags (roaster_name, equipment_id, in_inventory) VALUES ('R', ?, 1)");
                q.addBindValue(eq); q.exec(); return q.lastInsertId().toLongLong();
            };
            auto shotEq = [&](qint64 id) {
                QSqlQuery q(db); q.prepare("SELECT equipment_id FROM shots WHERE id=?");
                q.addBindValue(id); q.exec(); q.next(); return q.value(0).toLongLong();
            };
            auto pkgExists = [&](qint64 id) {
                QSqlQuery q(db); q.prepare("SELECT COUNT(*) FROM equipment_packages WHERE id=?");
                q.addBindValue(id); q.exec(); q.next(); return q.value(0).toInt() > 0;
            };

            // The shape a pre-enrichment fork left behind: OLD holds the history and
            // is retired, NEW is live and has the burrs. Undoing it means merging the
            // live NEW into the retired OLD, so a retired target must be accepted.
            EquipmentPackage o;
            const qint64 old_ = EquipmentStorage::createPackageWithGrinderStatic(db, o, "Eureka", "Mignon Single Dose", "stock");
            const qint64 oldShot = addShot(old_);
            // A real burr CHANGE still forks (enrichment only covers empty -> value),
            // which is how the two-package state under repair gets built here.
            const qint64 fresh = EquipmentStorage::supersedeOrEditGrinderStatic(db, old_, "Eureka", "Mignon Single Dose", "Lebrew Sweet");
            QVERIFY(fresh > 0 && fresh != old_);
            const qint64 newShot = addShot(fresh);
            const qint64 bag = addBag(fresh);

            const EquipmentMergeResult r = EquipmentStorage::mergePackagesStatic(db, fresh, old_);
            QVERIFY2(r.ok, qPrintable(r.error));
            QCOMPARE(r.shotsMoved, (qint64)1);
            QCOMPARE(r.bagsMoved, (qint64)1);
            QCOMPARE(r.recipesMoved, (qint64)0);          // no recipes table in this fixture
            QCOMPARE(shotEq(newShot), old_);              // history united on the survivor
            QCOMPARE(shotEq(oldShot), old_);
            QSqlQuery bagQ(db);
            bagQ.prepare("SELECT equipment_id FROM coffee_bags WHERE id=?");
            bagQ.addBindValue(bag);
            QVERIFY(bagQ.exec() && bagQ.next());
            QCOMPARE(bagQ.value(0).toLongLong(), old_);
            QVERIFY(!pkgExists(fresh));                   // source gone, no dangling references
            const EquipmentPackage survivor = EquipmentStorage::loadPackageStatic(db, old_);
            QVERIFY(survivor.inInventory);                // revived: it holds the history now
            QCOMPARE(survivor.supersededBy, (qint64)0);   // and no longer points at the deleted fork

            // Refusals are clean no-ops with a machine-readable reason.
            QCOMPARE(EquipmentStorage::mergePackagesStatic(db, old_, old_).error, QString("samePackage"));
            QCOMPARE(EquipmentStorage::mergePackagesStatic(db, 99999, old_).error, QString("sourceNotFound"));
            QCOMPARE(EquipmentStorage::mergePackagesStatic(db, old_, 99999).error, QString("targetNotFound"));
            QVERIFY(pkgExists(old_));

            // A third package superseded BY the source follows it to the target
            // rather than being left pointing at a deleted row.
            EquipmentPackage a;
            const qint64 keep = EquipmentStorage::createPackageWithGrinderStatic(db, a, "Mazzer", "Kold", "83mm");
            addShot(keep);
            const qint64 keepFork = EquipmentStorage::supersedeOrEditGrinderStatic(db, keep, "Mazzer", "Kold", "83mm titanium");
            QVERIFY(keepFork > 0 && keepFork != keep);
            QVERIFY(EquipmentStorage::mergePackagesStatic(db, keepFork, old_).ok);
            QCOMPARE(EquipmentStorage::loadPackageStatic(db, keep).supersededBy, old_);
        });
    }

    // --- one-time heal of pre-enrichment forks (migration 35) ---
    void healEnrichmentForks() {
        const QString path = freshDbPath();
        withRawDb(path, "eq_heal", [](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            createMinimalShots(db);
            auto addShot = [&](qint64 eq) {
                QSqlQuery q(db); q.prepare("INSERT INTO shots (equipment_id) VALUES (?)");
                q.addBindValue(eq); q.exec(); return q.lastInsertId().toLongLong();
            };
            auto shotEq = [&](qint64 id) {
                QSqlQuery q(db); q.prepare("SELECT equipment_id FROM shots WHERE id=?");
                q.addBindValue(id); q.exec(); q.next(); return q.value(0).toLongLong();
            };
            auto pkgExists = [&](qint64 id) {
                QSqlQuery q(db); q.prepare("SELECT COUNT(*) FROM equipment_packages WHERE id=?");
                q.addBindValue(id); q.exec(); q.next(); return q.value(0).toInt() > 0;
            };
            // Build a fork the OLD way — the enrichment rule would not create one,
            // so the split state is staged by hand, exactly as an upgraded database
            // carries it.
            auto stageEnrichmentFork = [&](const QString& brand, const QString& model,
                                           const QString& burrs) {
                EquipmentPackage older;
                const qint64 o = EquipmentStorage::createPackageWithGrinderStatic(db, older, brand, model, "");
                EquipmentPackage newer;
                const qint64 n = EquipmentStorage::createPackageWithGrinderStatic(db, newer, brand, model, burrs);
                QSqlQuery q(db);
                q.prepare("UPDATE equipment_packages SET in_inventory = 0, superseded_by = ? WHERE id = ?");
                q.addBindValue(n); q.addBindValue(o);
                q.exec();
                return QPair<qint64, qint64>(o, n);
            };

            const auto [old1, new1] = stageEnrichmentFork("Eureka", "Mignon Single Dose", "Lebrew Sweet");
            const qint64 oldShot = addShot(old1);
            const qint64 newShot = addShot(new1);

            // A real burr SWAP has the same lineage shape but named burrs on BOTH
            // sides — it must survive untouched.
            EquipmentPackage sA;
            const qint64 swapOld = EquipmentStorage::createPackageWithGrinderStatic(db, sA, "Niche", "Zero", "63mm conical");
            addShot(swapOld);
            const qint64 swapNew = EquipmentStorage::supersedeOrEditGrinderStatic(db, swapOld, "Niche", "Zero", "63mm titanium");
            QVERIFY(swapNew > 0 && swapNew != swapOld);

            // Two lookalike packages with NO lineage between them are two grinders
            // the user owns, not a fork.
            EquipmentPackage tw1, tw2;
            const qint64 twinA = EquipmentStorage::createPackageWithGrinderStatic(db, tw1, "Mazzer", "Philos", "");
            const qint64 twinB = EquipmentStorage::createPackageWithGrinderStatic(db, tw2, "Mazzer", "Philos", "83mm flat");
            addShot(twinA);

            QHash<qint64, qint64> remap;
            qsizetype healed = -1;
            QVERIFY(EquipmentStorage::healEnrichmentForksStatic(db, &remap, &healed));
            QCOMPARE(healed, (qsizetype)1);
            QCOMPARE(remap.value(old1), new1);

            // The fork is united on the survivor; nothing else moved.
            QVERIFY(!pkgExists(old1));
            QCOMPARE(shotEq(oldShot), new1);
            QCOMPARE(shotEq(newShot), new1);
            QVERIFY(EquipmentStorage::loadPackageStatic(db, new1).inInventory);
            QVERIFY(pkgExists(swapOld));                  // burr swap untouched
            QVERIFY(pkgExists(swapNew));
            QVERIFY(pkgExists(twinA));                    // no lineage, no heal
            QVERIFY(pkgExists(twinB));

            // Re-running finds nothing: the migration is safe to retry.
            QHash<qint64, qint64> remap2;
            qsizetype healed2 = -1;
            QVERIFY(EquipmentStorage::healEnrichmentForksStatic(db, &remap2, &healed2));
            QCOMPARE(healed2, (qsizetype)0);
            QVERIFY(remap2.isEmpty());

            // A CHAIN — burrs named, then the basket named on top — collapses whole,
            // and the remap follows the id all the way to the survivor rather than
            // stopping at the intermediate package.
            const auto [chainOld, chainMid] = stageEnrichmentFork("Turin", "DF83V", "83mm flat");
            const qint64 chainShot = addShot(chainOld);
            EquipmentPackage last;
            const qint64 chainNew = EquipmentStorage::createPackageWithGrinderStatic(
                db, last, "Turin", "DF83V", "83mm flat");
            QSqlQuery link(db);
            link.prepare("UPDATE equipment_packages SET in_inventory = 0, superseded_by = ? WHERE id = ?");
            link.addBindValue(chainNew); link.addBindValue(chainMid);
            QVERIFY(link.exec());
            // chainMid -> chainNew differ in nothing at all, so only the burrs link
            // heals; the identical pair is left to the user (merge is theirs to run).
            QHash<qint64, qint64> remap3;
            qsizetype healed3 = -1;
            QVERIFY(EquipmentStorage::healEnrichmentForksStatic(db, &remap3, &healed3));
            QCOMPARE(healed3, (qsizetype)1);
            QCOMPARE(remap3.value(chainOld), chainMid);
            QCOMPARE(shotEq(chainShot), chainMid);
            // ...and the middle package stays RETIRED. It is superseded by chainNew,
            // so reviving it would put a stale duplicate — same derived name as its
            // own successor — back in the inventory, which is what the repair is
            // supposed to remove.
            const EquipmentPackage mid = EquipmentStorage::loadPackageStatic(db, chainMid);
            QVERIFY(!mid.inInventory);
            QCOMPARE(mid.supersededBy, chainNew);
        });
    }

    // --- merge sub-branches + name derivation ---
    void mergeAndNameEdges() {
        const QString path = freshDbPath();
        withRawDb(path, "eq_edges", [](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            createMinimalShots(db);
            auto addShot = [&](qint64 eq) {
                QSqlQuery q(db); q.prepare("INSERT INTO shots (equipment_id) VALUES (?)");
                q.addBindValue(eq); q.exec(); return q.lastInsertId().toLongLong();
            };
            auto pkgExists = [&](qint64 id) {
                QSqlQuery q(db); q.prepare("SELECT COUNT(*) FROM equipment_packages WHERE id=?");
                q.addBindValue(id); q.exec(); q.next(); return q.value(0).toInt() > 0;
            };

            EquipmentPackage t;
            const qint64 target = EquipmentStorage::createPackageWithGrinderStatic(db, t, "Niche", "Zero", "63mm conical");

            // Unused source merged into an existing target → source hard-deleted.
            EquipmentPackage s;
            const qint64 src = EquipmentStorage::createPackageWithGrinderStatic(db, s, "Mazzer", "Major", "83mm");
            QCOMPARE(EquipmentStorage::supersedeOrEditGrinderStatic(db, src, "Niche", "Zero", "63mm conical"), target);
            QVERIFY(!pkgExists(src));  // unused source physically removed, not just retired

            // Editing into a RETIRED package's identity must fork, not resurrect it.
            EquipmentPackage u;
            const qint64 used = EquipmentStorage::createPackageWithGrinderStatic(db, u, "Turin", "DF83V", "83mm flat steel");
            addShot(used);
            { QSqlQuery q(db); q.prepare("UPDATE equipment_packages SET in_inventory=0 WHERE id=?");
              q.addBindValue(target); q.exec(); }
            const qint64 forked = EquipmentStorage::supersedeOrEditGrinderStatic(db, used, "Niche", "Zero", "63mm conical");
            QVERIFY(forked > 0 && forked != used && forked != target);  // did not merge into the retired package

            // Name derived from a partial identity (brand only).
            EquipmentPackage p;
            const qint64 pid = EquipmentStorage::createPackageWithGrinderStatic(db, p, "Turin", "", "");
            QCOMPARE(EquipmentStorage::loadPackageStatic(db, pid).name, QString("Turin"));
        });
    }

    // --- basket: optional second item + derive-at-read + setBasketItemStatic ---
    void basketOptionalAndDerive() {
        const QString path = freshDbPath();
        withRawDb(path, "eq_basket", [](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));

            // Grinder-only package: no basket item.
            EquipmentPackage p0;
            const qint64 g0 = EquipmentStorage::createPackageWithGrinderStatic(db, p0, "Niche", "Zero", "63mm conical");
            QVERIFY(g0 > 0);
            QVERIFY(!EquipmentStorage::loadBasketItemStatic(db, g0).isValid());

            // Regression: a grinder-only lookup (basket params default to a NULL
            // QString) MUST find a no-basket package. Without the bind-side
            // IFNULL(:bbrand,'') in findPackageByGrinderIdentityStatic the null
            // binds as SQL NULL, '' = NULL is never true, and the package is missed
            // — re-creating it as a duplicate. Pin the fix here (basketIdentityWidening
            // can't: its packages all have baskets, so it returns 0 either way).
            QCOMPARE(EquipmentStorage::findPackageByGrinderIdentityStatic(db, "Niche", "Zero", "63mm conical"), g0);

            // setBasketItemStatic inserts when the package has no basket yet, and a
            // clear on an already-basketless package is a success no-op (true), not
            // a failure (the return-contract the edit-in-place rollback depends on).
            QVERIFY(EquipmentStorage::setBasketItemStatic(db, g0, "", ""));  // no-op -> true
            QVERIFY(EquipmentStorage::setBasketItemStatic(db, g0, "VST", "18g"));  // insert -> true
            QCOMPARE(EquipmentStorage::loadBasketItemStatic(db, g0).model, QString("18g"));
            QVERIFY(EquipmentStorage::setBasketItemStatic(db, g0, "", ""));  // clear existing -> true
            QVERIFY(!EquipmentStorage::loadBasketItemStatic(db, g0).isValid());

            // Package with a registry basket (created in one call).
            EquipmentPackage p1;
            const qint64 g1 = EquipmentStorage::createPackageWithGrinderStatic(
                db, p1, "Turin", "DF83V", "83mm flat steel", "Decent", "18g Ridgeless");
            QVERIFY(g1 > 0);
            const EquipmentItem b = EquipmentStorage::loadBasketItemStatic(db, g1);
            QVERIFY(b.isValid());
            QCOMPARE(b.kind, QString("basket"));
            QCOMPARE(b.brand, QString("Decent"));
            QCOMPARE(b.model, QString("18g Ridgeless"));

            // Derive-at-read: the view's variant map carries REGISTRY specs (the
            // basket item itself stores none).
            EquipmentPackageView v;
            v.package = EquipmentStorage::loadPackageStatic(db, g1);
            v.grinder = EquipmentStorage::loadGrinderItemStatic(db, g1);
            v.basket = b;
            const QVariantMap m = v.toVariantMap();
            QCOMPARE(m.value("basketBrand").toString(), QString("Decent"));
            QCOMPARE(m.value("basketWallProfile").toString(), QString("straight"));
            QVERIFY(m.value("basketPrecision").toBool());
            QVERIFY(m.value("basketDoseMaxG").toDouble() > 0);

            // A custom (off-registry) basket resolves to identity only — no specs.
            EquipmentPackage p2;
            const qint64 g2 = EquipmentStorage::createPackageWithGrinderStatic(
                db, p2, "Niche", "Zero", "63mm", "Acme", "Imaginary Basket");
            EquipmentPackageView v2;
            v2.package = EquipmentStorage::loadPackageStatic(db, g2);
            v2.grinder = EquipmentStorage::loadGrinderItemStatic(db, g2);
            v2.basket = EquipmentStorage::loadBasketItemStatic(db, g2);
            const QVariantMap m2 = v2.toVariantMap();
            QCOMPARE(m2.value("basketBrand").toString(), QString("Acme"));
            QVERIFY(!m2.contains("basketWallProfile"));  // unknown specs omitted

            // setBasketItemStatic: clear removes the item; set re-adds.
            QVERIFY(EquipmentStorage::setBasketItemStatic(db, g1, "", ""));
            QVERIFY(!EquipmentStorage::loadBasketItemStatic(db, g1).isValid());
            QVERIFY(EquipmentStorage::setBasketItemStatic(db, g1, "VST", "18g"));
            QCOMPARE(EquipmentStorage::loadBasketItemStatic(db, g1).model, QString("18g"));
        });
    }

    // --- basket participates in package identity (dedup + copy-on-write) ---
    void basketIdentityWidening() {
        const QString path = freshDbPath();
        withRawDb(path, "eq_basket_id", [](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            createMinimalShots(db);
            auto addShot = [&](qint64 eq) {
                QSqlQuery q(db); q.prepare("INSERT INTO shots (equipment_id) VALUES (?)");
                q.addBindValue(eq); q.exec(); return q.lastInsertId().toLongLong();
            };

            // Same grinder, different basket → distinct packages.
            EquipmentPackage a, bp;
            const qint64 A = EquipmentStorage::createPackageWithGrinderStatic(db, a, "Turin", "DF83V", "83mm", "VST", "18g");
            const qint64 B = EquipmentStorage::createPackageWithGrinderStatic(db, bp, "Turin", "DF83V", "83mm", "Weber", "Unibasket 18g");
            QVERIFY(A > 0 && B > 0 && A != B);

            // Full-identity dedup: grinder AND basket must match.
            QCOMPARE(EquipmentStorage::findPackageByGrinderIdentityStatic(db, "Turin", "DF83V", "83mm", 0, "VST", "18g"), A);
            // Same grinder, wrong basket → not A.
            QVERIFY(EquipmentStorage::findPackageByGrinderIdentityStatic(db, "Turin", "DF83V", "83mm", 0, "VST", "20g") != A);
            // "No basket" is a distinct identity: grinder-only query matches neither.
            QCOMPARE(EquipmentStorage::findPackageByGrinderIdentityStatic(db, "Turin", "DF83V", "83mm"), (qint64)0);

            // Changing the basket on a USED package forks (copy-on-write).
            addShot(A);
            const qint64 fork = EquipmentStorage::supersedeOrEditStatic(db, A, "Turin", "DF83V", "83mm", "IMS", "Competition 18g", QString());
            QVERIFY(fork > 0 && fork != A);
            QCOMPARE(EquipmentStorage::loadBasketItemStatic(db, fork).brand, QString("IMS"));
            QCOMPARE(EquipmentStorage::loadPackageStatic(db, A).supersededBy, fork);

            // The grinder-only wrapper PRESERVES the basket.
            EquipmentPackage cpk;
            const qint64 C = EquipmentStorage::createPackageWithGrinderStatic(db, cpk, "Mazzer", "Major", "83mm", "VST", "20g");
            const qint64 c2 = EquipmentStorage::supersedeOrEditGrinderStatic(db, C, "Mazzer", "Major V2", "83mm");
            QCOMPARE(c2, C);  // unused → edit in place
            QCOMPARE(EquipmentStorage::loadBasketItemStatic(db, C).model, QString("20g"));  // basket preserved
        });
    }

    // --- puck prep: canonical helper, optional item, derive-at-read, set contract ---
    void puckPrepOptionalAndDerive() {
        // Canonical is order-independent + sorted (the identity key).
        auto canon = [](bool wdt, bool shaker, bool puckScreen, bool paper, bool rdt) {
            QVariantMap m;
            m["wdt"] = wdt; m["shaker"] = shaker; m["puckScreen"] = puckScreen;
            m["paperFilter"] = paper; m["rdt"] = rdt;
            return PuckPrep::canonical(m);
        };
        QCOMPARE(canon(true, true, false, false, false), QString("shaker,wdt"));
        QCOMPARE(canon(false, false, false, false, false), QString(""));
        QCOMPARE(PuckPrep::distribution("shaker,wdt"), QString("thorough"));
        QCOMPARE(PuckPrep::distribution("shaker"), QString("thorough"));     // shaker == WDT (equal weight)
        QCOMPARE(PuckPrep::distribution("rdt"), QString("light"));           // anti-static only
        QCOMPARE(PuckPrep::distribution("puckScreen"), QString("none"));     // no active distribution

        // recanonical: idempotent, re-sorts an unsorted string, drops unknown tokens.
        QCOMPARE(PuckPrep::recanonical("shaker,wdt"), QString("shaker,wdt"));
        QCOMPARE(PuckPrep::recanonical("wdt,shaker"), QString("shaker,wdt"));   // re-sorted
        QCOMPARE(PuckPrep::recanonical("wdt,bogus, shaker "), QString("shaker,wdt")); // unknown dropped
        QCOMPARE(PuckPrep::recanonical(""), QString(""));

        // canonicalMerged: partial overrides on a current string keep the untouched
        // flags — the load-bearing data-preservation path for partial (MCP/dialog)
        // edits. A regression here silently wipes a user's other flags.
        auto over = [](const QString& key, bool v) { QVariantMap m; m["puckPrep_" + key] = v; return m; };
        QCOMPARE(PuckPrep::canonicalMerged("shaker,wdt", over("paperFilter", true)),
                 QString("paperFilter,shaker,wdt"));               // added, re-sorted, others kept
        QCOMPARE(PuckPrep::canonicalMerged("shaker,wdt", over("wdt", false)),
                 QString("shaker"));                               // one cleared, rest kept
        QCOMPARE(PuckPrep::canonicalMerged("", over("wdt", true)), QString("wdt"));  // from empty
        QCOMPARE(PuckPrep::canonicalMerged("rdt,wdt", QVariantMap()), QString("rdt,wdt")); // empty map = no change
        // mapTouches gates the edit: a puckPrep_* key trips it; unrelated keys / {} don't.
        QVERIFY(PuckPrep::mapTouches(over("wdt", true)));
        QVERIFY(!PuckPrep::mapTouches(QVariantMap{{"basketBrand", "VST"}}));
        QVERIFY(!PuckPrep::mapTouches(QVariantMap()));

        const QString path = freshDbPath();
        withRawDb(path, "eq_puck", [&](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));

            // Create a package with puck prep in one call.
            EquipmentPackage p1;
            const qint64 g1 = EquipmentStorage::createPackageWithGrinderStatic(
                db, p1, "Turin", "DF83V", "83mm", QString(), QString(), "shaker,wdt");
            QVERIFY(g1 > 0);
            const EquipmentItem pp = EquipmentStorage::loadPuckPrepItemStatic(db, g1);
            QVERIFY(pp.isValid());
            QCOMPARE(pp.kind, QString("puckprep"));
            QCOMPARE(pp.model, QString("shaker,wdt"));  // canonical in `model`

            // Derive-at-read: the view map carries the flags + canonical string
            // (distribution is AI-only and excluded — asserted below).
            EquipmentPackageView v;
            v.package = EquipmentStorage::loadPackageStatic(db, g1);
            v.grinder = EquipmentStorage::loadGrinderItemStatic(db, g1);
            v.puckPrep = pp;
            const QVariantMap m = v.toVariantMap();
            QCOMPARE(m.value("puckPrep_wdt").toBool(), true);
            QCOMPARE(m.value("puckPrep_shaker").toBool(), true);
            QCOMPARE(m.value("puckPrep_puckScreen").toBool(), false);
            QCOMPARE(m.value("puckPrepCanonical").toString(), QString("shaker,wdt"));
            // distribution is AI-only — deliberately NOT in the QML-facing map.
            QVERIFY(!m.contains("puckPrepDistribution"));

            // Grinder-only package: no puckprep item; map omits the fields.
            EquipmentPackage p0;
            const qint64 g0 = EquipmentStorage::createPackageWithGrinderStatic(db, p0, "Niche", "Zero", "63mm");
            QVERIFY(!EquipmentStorage::loadPuckPrepItemStatic(db, g0).isValid());

            // setPuckPrepItemStatic: insert, no-op-clear (true), update, clear.
            QVERIFY(EquipmentStorage::setPuckPrepItemStatic(db, g0, ""));        // no-op → true
            QVERIFY(EquipmentStorage::setPuckPrepItemStatic(db, g0, "wdt"));     // insert → true
            QCOMPARE(EquipmentStorage::loadPuckPrepItemStatic(db, g0).model, QString("wdt"));
            QVERIFY(EquipmentStorage::setPuckPrepItemStatic(db, g0, "rdt,wdt")); // update → true
            QCOMPARE(EquipmentStorage::loadPuckPrepItemStatic(db, g0).model, QString("rdt,wdt"));
            QVERIFY(EquipmentStorage::setPuckPrepItemStatic(db, g0, ""));        // clear → true
            QVERIFY(!EquipmentStorage::loadPuckPrepItemStatic(db, g0).isValid());
        });
    }

    // --- puck prep participates in package identity (dedup + copy-on-write) ---
    void puckPrepIdentityWidening() {
        const QString path = freshDbPath();
        withRawDb(path, "eq_puck_id", [](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            createMinimalShots(db);
            auto addShot = [&](qint64 eq) {
                QSqlQuery q(db); q.prepare("INSERT INTO shots (equipment_id) VALUES (?)");
                q.addBindValue(eq); q.exec(); return q.lastInsertId().toLongLong();
            };

            // Same grinder, different puck prep → distinct packages.
            EquipmentPackage a, b;
            const qint64 A = EquipmentStorage::createPackageWithGrinderStatic(db, a, "Turin", "DF83V", "83mm", QString(), QString(), "wdt");
            const qint64 B = EquipmentStorage::createPackageWithGrinderStatic(db, b, "Turin", "DF83V", "83mm", QString(), QString(), "shaker,wdt");
            QVERIFY(A > 0 && B > 0 && A != B);

            // Full-identity dedup keys on puck prep too.
            QCOMPARE(EquipmentStorage::findPackageByGrinderIdentityStatic(db, "Turin", "DF83V", "83mm", 0, QString(), QString(), "wdt"), A);
            // An UNSORTED query arg still matches the canonical-stored value (the
            // lookup re-canonicalizes its bind), so dedup can't be defeated by order.
            QCOMPARE(EquipmentStorage::findPackageByGrinderIdentityStatic(db, "Turin", "DF83V", "83mm", 0, QString(), QString(), "wdt,shaker"), B);
            // "No puck prep" is a distinct value: a grinder+basket-only query matches neither.
            QCOMPARE(EquipmentStorage::findPackageByGrinderIdentityStatic(db, "Turin", "DF83V", "83mm"), (qint64)0);

            // Changing puck prep on a USED package forks.
            addShot(A);
            const qint64 fork = EquipmentStorage::supersedeOrEditStatic(db, A, "Turin", "DF83V", "83mm", QString(), QString(), "rdt,wdt");
            QVERIFY(fork > 0 && fork != A);
            QCOMPARE(EquipmentStorage::loadPuckPrepItemStatic(db, fork).model, QString("rdt,wdt"));
            QCOMPARE(EquipmentStorage::loadPackageStatic(db, A).supersededBy, fork);

            // The grinder-only wrapper PRESERVES puck prep.
            EquipmentPackage cpk;
            const qint64 C = EquipmentStorage::createPackageWithGrinderStatic(db, cpk, "Mazzer", "Major", "83mm", QString(), QString(), "wdt");
            const qint64 c2 = EquipmentStorage::supersedeOrEditGrinderStatic(db, C, "Mazzer", "Major V2", "83mm");
            QCOMPARE(c2, C);  // unused → edit in place
            QCOMPARE(EquipmentStorage::loadPuckPrepItemStatic(db, C).model, QString("wdt"));  // preserved

            // Partial edit through the same storage path requestUpdatePackage / the
            // MCP equipment_update use (canonicalMerged): a single-flag override on an
            // unused package preserves the rest.
            EquipmentPackage dpk;
            const qint64 D = EquipmentStorage::createPackageWithGrinderStatic(db, dpk, "Niche", "Zero", "63mm", QString(), QString(), "shaker,wdt");
            const QString merged = PuckPrep::canonicalMerged(EquipmentStorage::loadPuckPrepItemStatic(db, D).model,
                                                             QVariantMap{{"puckPrep_paperFilter", true}});
            QCOMPARE(EquipmentStorage::supersedeOrEditStatic(db, D, "Niche", "Zero", "63mm", QString(), QString(), merged), D);
            QCOMPARE(EquipmentStorage::loadPuckPrepItemStatic(db, D).model, QString("paperFilter,shaker,wdt"));
        });
    }

    // --- puck prep in device-transfer dedup: same gear + same prep merges,
    //     same gear + different prep stays distinct (no basket either side). ---
    void puckPrepImportDedup() {
        const QString srcPath = freshDbPath();
        const QString dstPath = freshDbPath();

        qint64 srcDiff = -1, srcSame = -1;
        withRawDb(srcPath, "impp_src", [&](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage a, b;
            srcDiff = EquipmentStorage::createPackageWithGrinderStatic(db, a, "Niche", "Zero", "63mm", QString(), QString(), "wdt");
            srcSame = EquipmentStorage::createPackageWithGrinderStatic(db, b, "Mazzer", "Major", "83mm", QString(), QString(), "shaker,wdt");
        });
        QVERIFY(srcDiff > 0 && srcSame > 0);

        qint64 dstDiff = -1, dstSame = -1;
        withRawDb(dstPath, "impp_dst", [&](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage a, b;
            dstDiff = EquipmentStorage::createPackageWithGrinderStatic(db, a, "Niche", "Zero", "63mm", QString(), QString(), "shaker"); // diff prep
            dstSame = EquipmentStorage::createPackageWithGrinderStatic(db, b, "Mazzer", "Major", "83mm", QString(), QString(), "shaker,wdt"); // same
        });
        QVERIFY(dstDiff > 0 && dstSame > 0);

        QHash<qint64, qint64> idMap;
        {
            QSqlDatabase src = QSqlDatabase::addDatabase("QSQLITE", "impp_s");
            src.setDatabaseName(srcPath); QVERIFY(src.open());
            QSqlDatabase dst = QSqlDatabase::addDatabase("QSQLITE", "impp_d");
            dst.setDatabaseName(dstPath); QVERIFY(dst.open());
            QVERIFY(EquipmentStorage::importEquipmentStatic(src, dst, /*merge*/ true, idMap));
            src = QSqlDatabase(); dst = QSqlDatabase();
        }
        QSqlDatabase::removeDatabase("impp_s");
        QSqlDatabase::removeDatabase("impp_d");

        // Different prep -> imported as a NEW dest package, not merged onto dstDiff.
        QVERIFY(idMap.value(srcDiff) > 0 && idMap.value(srcDiff) != dstDiff);
        // Same grinder + same prep -> merged onto the existing dest package.
        QCOMPARE(idMap.value(srcSame), dstSame);
    }

    // --- migration data step: split + dedup-into-packages + link ---
    void migrationSplitsAndLinks() {
        const QString path = freshDbPath();
        withRawDb(path, "eq_migrate", [](QSqlDatabase& db) {
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            createMinimalShots(db);

            // Two bags: a Turin (matches current settings) and a Niche.
            QSqlQuery q(db);
            q.exec("INSERT INTO coffee_bags (roaster_name, coffee_name, grinder_brand, grinder_model, "
                   "grinder_burrs, grinder_setting, in_inventory) VALUES "
                   "('R','A','Turin','DF83V','83mm flat steel','24 1400rpm',1)");
            const qint64 bag1 = q.lastInsertId().toLongLong();
            q.exec("INSERT INTO coffee_bags (roaster_name, coffee_name, grinder_brand, grinder_model, "
                   "grinder_burrs, grinder_setting, in_inventory) VALUES "
                   "('R','B','Niche','Zero','63mm conical','12',1)");
            const qint64 bag2 = q.lastInsertId().toLongLong();

            // Shots: two Turin (one annotated), reusing the same identity.
            const qint64 shot1 = insertShot(db, "Turin", "DF83V", "83mm flat steel", "25 1350rpm");
            const qint64 shot2 = insertShot(db, "Turin", "DF83V", "83mm flat steel", "26");

            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            QVERIFY(EquipmentStorage::migrateFromGrinderColumnsStatic(
                db, "Turin", "DF83V", "83mm flat steel", "24 1400rpm"));

            // Exactly two packages: Turin (default) + Niche. Turin identity is
            // shared by the default seed, bag1, shot1, shot2 (NOT split by grind).
            QVERIFY(q.exec("SELECT COUNT(*) FROM equipment_packages"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 2);

            // bag1 + both Turin shots resolve to the same package.
            auto eqId = [&](const QString& table, qint64 id) -> qint64 {
                QSqlQuery e(db);
                e.prepare(QString("SELECT equipment_id FROM %1 WHERE id = ?").arg(table));
                e.addBindValue(id);
                return (e.exec() && e.next()) ? e.value(0).toLongLong() : -1;
            };
            const qint64 turinPkg = eqId("coffee_bags", bag1);
            QVERIFY(turinPkg > 0);
            QCOMPARE(eqId("shots", shot1), turinPkg);
            QCOMPARE(eqId("shots", shot2), turinPkg);
            QVERIFY(eqId("coffee_bags", bag2) > 0);
            QVERIFY(eqId("coffee_bags", bag2) != turinPkg);

            // Grind/rpm split applied to the annotated rows; plain rows untouched.
            auto cell = [&](const QString& table, const QString& col, qint64 id) -> QVariant {
                QSqlQuery e(db);
                e.prepare(QString("SELECT %1 FROM %2 WHERE id = ?").arg(col, table));
                e.addBindValue(id);
                return (e.exec() && e.next()) ? e.value(0) : QVariant();
            };
            QCOMPARE(cell("coffee_bags", "grinder_setting", bag1).toString(), QString("24"));
            QCOMPARE(cell("coffee_bags", "rpm", bag1).toInt(), 1400);
            QCOMPARE(cell("shots", "grinder_setting", shot1).toString(), QString("25"));
            QCOMPARE(cell("shots", "rpm", shot1).toInt(), 1350);
            QCOMPARE(cell("shots", "grinder_setting", shot2).toString(), QString("26"));
            QVERIFY(cell("shots", "rpm", shot2).isNull());

            // The Turin package is rpm-capable and seeded from current settings.
            const EquipmentItem g = EquipmentStorage::loadGrinderItemStatic(db, turinPkg);
            QVERIFY(g.rpmCapable);
            const EquipmentPackage p = EquipmentStorage::loadPackageStatic(db, turinPkg);
            QCOMPARE(p.lastGrindSetting, QString("24"));
            QCOMPARE(p.lastRpm, (qint64)1400);
        });
    }

    // Device-transfer import (add-equipment-packages task 2.8):
    // importEquipmentStatic copies packages + items with id-remap, MERGES an
    // in-inventory source package onto an existing dest package of the same
    // grinder identity (no duplicate), imports superseded (historical) packages
    // as new rows, and remaps the superseded_by lineage pointer through the id
    // map. Source ids must NOT survive verbatim into dest.
    void importEquipmentRemap() {
        const QString srcPath = freshDbPath();
        const QString dstPath = freshDbPath();

        // SOURCE: an in-inventory Niche (will merge into dest), plus a Turin
        // lineage — an in-inventory "new" fork and a soft-deleted "old" package
        // whose superseded_by points at the fork.
        qint64 srcNiche = -1, srcTurinNew = -1, srcTurinOld = -1;
        withRawDb(srcPath, "imp_src_build", [&](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage n;
            srcNiche = EquipmentStorage::createPackageWithGrinderStatic(db, n, "Niche", "Zero", "63mm conical");
            EquipmentPackage t1;
            srcTurinNew = EquipmentStorage::createPackageWithGrinderStatic(db, t1, "Turin", "DF83V", "83mm DLC flat");
            EquipmentPackage t0;
            srcTurinOld = EquipmentStorage::createPackageWithGrinderStatic(db, t0, "Turin", "DF83V", "83mm flat steel");
            QSqlQuery q(db);
            q.prepare("UPDATE equipment_packages SET in_inventory = 0, superseded_by = ? WHERE id = ?");
            q.addBindValue(srcTurinNew); q.addBindValue(srcTurinOld);
            QVERIFY(q.exec());
        });
        QVERIFY(srcNiche > 0 && srcTurinNew > 0 && srcTurinOld > 0);

        // DEST: a pre-existing in-inventory Niche/Zero/63mm conical the source
        // Niche must merge into.
        qint64 dstNiche = -1;
        withRawDb(dstPath, "imp_dst_build", [&](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage n;
            dstNiche = EquipmentStorage::createPackageWithGrinderStatic(db, n, "Niche", "Zero", "63mm conical");
        });
        QVERIFY(dstNiche > 0);

        // Import (merge mode) with both connections open simultaneously.
        QHash<qint64, qint64> idMap;
        {
            QSqlDatabase src = QSqlDatabase::addDatabase("QSQLITE", "imp_src");
            src.setDatabaseName(srcPath);
            QVERIFY(src.open());
            QSqlDatabase dst = QSqlDatabase::addDatabase("QSQLITE", "imp_dst");
            dst.setDatabaseName(dstPath);
            QVERIFY(dst.open());
            QVERIFY(EquipmentStorage::importEquipmentStatic(src, dst, /*merge*/ true, idMap));
            src = QSqlDatabase();
            dst = QSqlDatabase();
        }
        QSqlDatabase::removeDatabase("imp_src");
        QSqlDatabase::removeDatabase("imp_dst");

        withRawDb(dstPath, "imp_verify", [&](QSqlDatabase& db) {
            // Merge: the source Niche mapped onto the existing dest Niche.
            QCOMPARE(idMap.value(srcNiche), dstNiche);
            QSqlQuery c(db);
            QVERIFY(c.exec("SELECT COUNT(*) FROM equipment_items WHERE kind='grinder' AND brand='Niche'") && c.next());
            QCOMPARE(c.value(0).toInt(), 1);  // no duplicate Niche package/item

            // Both Turin packages imported as NEW rows (ids remapped, not verbatim).
            const qint64 dTurinNew = idMap.value(srcTurinNew);
            const qint64 dTurinOld = idMap.value(srcTurinOld);
            QVERIFY(dTurinNew > 0 && dTurinOld > 0);

            // The old Turin is soft-deleted with superseded_by remapped to the
            // NEW dest id — lineage preserved across the transfer.
            const EquipmentPackage oldP = EquipmentStorage::loadPackageStatic(db, dTurinOld);
            QVERIFY(!oldP.inInventory);
            QCOMPARE(oldP.supersededBy, dTurinNew);

            // Items rode along with their grinder identity.
            QCOMPARE(EquipmentStorage::loadGrinderItemStatic(db, dTurinNew).burrs, QString("83mm DLC flat"));
            QCOMPARE(EquipmentStorage::loadGrinderItemStatic(db, dTurinOld).burrs, QString("83mm flat steel"));
        });
    }

    // Import dedup keys on the FULL identity (grinder + basket): a source package
    // merges into a dest package only when BOTH match. Same grinder + DIFFERENT
    // basket must NOT merge, or the basket identity is silently lost in transfer.
    void importEquipmentBasketDedup() {
        const QString srcPath = freshDbPath();
        const QString dstPath = freshDbPath();

        qint64 srcDiff = -1, srcSame = -1;
        withRawDb(srcPath, "impb_src", [&](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage a, b;
            srcDiff = EquipmentStorage::createPackageWithGrinderStatic(db, a, "Niche", "Zero", "63mm", "VST", "18g");
            srcSame = EquipmentStorage::createPackageWithGrinderStatic(db, b, "Mazzer", "Major", "83mm", "VST", "18g");
        });
        QVERIFY(srcDiff > 0 && srcSame > 0);

        qint64 dstDiff = -1, dstSame = -1;
        withRawDb(dstPath, "impb_dst", [&](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
            EquipmentPackage a, b;
            // Same grinder as srcDiff but a DIFFERENT basket -> must stay distinct.
            dstDiff = EquipmentStorage::createPackageWithGrinderStatic(db, a, "Niche", "Zero", "63mm", "Weber", "20g Unibasket");
            // Same grinder AND basket as srcSame -> must merge.
            dstSame = EquipmentStorage::createPackageWithGrinderStatic(db, b, "Mazzer", "Major", "83mm", "VST", "18g");
        });
        QVERIFY(dstDiff > 0 && dstSame > 0);

        QHash<qint64, qint64> idMap;
        {
            QSqlDatabase src = QSqlDatabase::addDatabase("QSQLITE", "impb_s");
            src.setDatabaseName(srcPath); QVERIFY(src.open());
            QSqlDatabase dst = QSqlDatabase::addDatabase("QSQLITE", "impb_d");
            dst.setDatabaseName(dstPath); QVERIFY(dst.open());
            QVERIFY(EquipmentStorage::importEquipmentStatic(src, dst, /*merge*/ true, idMap));
            src = QSqlDatabase(); dst = QSqlDatabase();
        }
        QSqlDatabase::removeDatabase("impb_s");
        QSqlDatabase::removeDatabase("impb_d");

        // Different basket -> imported as a NEW dest package, NOT merged onto dstDiff.
        QVERIFY(idMap.value(srcDiff) > 0 && idMap.value(srcDiff) != dstDiff);
        // Same grinder + basket -> merged onto the existing dest package.
        QCOMPARE(idMap.value(srcSame), dstSame);
    }

    // A source DB with no equipment tables (transfer from a pre-equipment app
    // version) yields an empty map and succeeds — bags/shots then null their
    // equipment_id rather than mislinking.
    void importEquipmentFromPreEquipmentSource() {
        const QString srcPath = freshDbPath();
        const QString dstPath = freshDbPath();
        withRawDb(srcPath, "imp_old_src", [&](QSqlDatabase& db) {
            QSqlQuery(db).exec("CREATE TABLE shots (id INTEGER PRIMARY KEY)");  // no equipment tables
        });
        withRawDb(dstPath, "imp_old_dst", [&](QSqlDatabase& db) {
            QVERIFY(EquipmentStorage::ensureTablesStatic(db));
        });
        QHash<qint64, qint64> idMap;
        {
            QSqlDatabase src = QSqlDatabase::addDatabase("QSQLITE", "imp_old_s");
            src.setDatabaseName(srcPath);
            QVERIFY(src.open());
            QSqlDatabase dst = QSqlDatabase::addDatabase("QSQLITE", "imp_old_d");
            dst.setDatabaseName(dstPath);
            QVERIFY(dst.open());
            QVERIFY(EquipmentStorage::importEquipmentStatic(src, dst, /*merge*/ true, idMap));
            src = QSqlDatabase();
            dst = QSqlDatabase();
        }
        QSqlDatabase::removeDatabase("imp_old_s");
        QSqlDatabase::removeDatabase("imp_old_d");
        QVERIFY(idMap.isEmpty());
    }
};

QTEST_MAIN(tst_Equipment)
#include "tst_equipment.moc"
