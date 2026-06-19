#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>

#include "history/equipmentstorage.h"
#include "history/coffeebagstorage.h"

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
};

QTEST_MAIN(tst_Equipment)
#include "tst_equipment.moc"
