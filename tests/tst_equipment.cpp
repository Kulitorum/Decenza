#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>

#include "history/equipmentstorage.h"
#include "history/coffeebagstorage.h"
#include "core/puckprep.h"

// Equipment packages: the grind/rpm split heuristic, rpmCapable derivation,
// package CRUD, identity dedup, and the migration-22 data step
// (add-equipment-packages).

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

            // Merge: a USED package edited to the fork's identity merges into it.
            EquipmentPackage s;
            const qint64 S = EquipmentStorage::createPackageWithGrinderStatic(db, s, "Mazzer", "Major", "83mm");
            addShot(S);
            const qint64 merged = EquipmentStorage::supersedeOrEditGrinderStatic(db, S, "Turin", "DF83V", "83mm DLC flat");
            QCOMPARE(merged, fork);  // repointed to the existing matching package
            QCOMPARE(EquipmentStorage::loadPackageStatic(db, S).supersededBy, fork);
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

            // Derive-at-read: the view map carries the flags + distribution.
            EquipmentPackageView v;
            v.package = EquipmentStorage::loadPackageStatic(db, g1);
            v.grinder = EquipmentStorage::loadGrinderItemStatic(db, g1);
            v.puckPrep = pp;
            const QVariantMap m = v.toVariantMap();
            QCOMPARE(m.value("puckPrep_wdt").toBool(), true);
            QCOMPARE(m.value("puckPrep_shaker").toBool(), true);
            QCOMPARE(m.value("puckPrep_puckScreen").toBool(), false);
            QCOMPARE(m.value("puckPrepDistribution").toString(), QString("thorough"));
            QCOMPARE(m.value("puckPrepCanonical").toString(), QString("shaker,wdt"));

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
        });
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
