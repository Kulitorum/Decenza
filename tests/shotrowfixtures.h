#pragma once

// Shared DB fixtures: stand up a real SQLite shot row from a designated
// initialiser, and run work against a scoped raw connection.
//
// Extracted from tst_dialing_blocks.cpp when a second test file needed the same
// seeding. `withRawDb` had by then been hand-copied into four more test files
// (tst_dbmigration, tst_equipment, tst_recipestorage, tst_coffeebags) and had
// already drifted two ways: all four used a bare `db.open()` with no assertion,
// so a failed open ran the work body against a closed handle and every query
// silently did nothing (only tst_dialing_blocks, the source here, checked it);
// and only tst_dbmigration and tst_coffeebags set `PRAGMA foreign_keys = ON`, so
// tst_equipment and tst_recipestorage were not enforcing the constraints the
// production `withTempDb()` enforces. The version here is the union — it asserts
// the open AND sets the pragma — and the copies are gone. See CLAUDE.md's rule
// about centralising anything produced at more than one site.
//
// Include from a QTest translation unit: withRawDb uses QVERIFY2, and
// insertShot's failure path emits a qWarning that QTest::failOnWarning() will
// surface.

#include <QtTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "history/shothistorystorage.h"
#include "history/shotprojection.h"
#include "history/equipmentstorage.h"

namespace ShotRowFixtures {

// One shot's input fields. Keep this near-identical to ShotSaveData so
// the parameter list is grep-able from the production save path.
// Every member carries an explicit default initializer, including the QStrings.
// A bare `QString x;` has no default member initializer, so GCC's
// -Wmissing-field-initializers (part of -Wextra) fires on every designated
// initialisation below that omits it — and omitting most fields is the entire
// point of these fixtures. clang does not warn here, so this only showed up on
// the Linux build.
struct ShotRow {
    QString uuid{};
    qint64 timestamp = 0;
    QString profileName{};
    QString profileKbId{};
    QString beverageType = QStringLiteral("espresso");
    double duration = 30.0;
    double finalWeight = 36.0;
    double doseWeight = 18.0;
    QString beanBrand{};
    QString beanType{};
    QString roastLevel{};
    QString grinderBrand{};
    QString grinderModel{};
    QString grinderBurrs{};
    // Present = the package is keyed on grinder AND basket, so two rows sharing
    // a grinder but differing here land in two packages.
    QString basketBrand{};
    QString basketModel{};
    QString grinderSetting{};
    // Grinder RPM → shots.rpm. 0 by default, which is also what "not
    // recorded" looks like, so existing fixtures are unaffected and the
    // adherence comparison skips the field exactly as it does in the wild.
    qint64 rpm = 0;
    int enjoyment = 0;
    QString espressoNotes{};
    // Issue #1158: profile recipe snapshot + SAW target. Empty/0 by
    // default so existing fixtures are unaffected (pourControl /
    // targetWeightG simply stay absent, exactly as before this PR).
    QString profileJson{};
    double targetWeight = 0.0;  // → shots.yield_override
    // #1164 finding #3: per-shot temperature override → shots
    // .temperature_override. 0 by default so existing fixtures are
    // unaffected (the field stays absent / hoist-neutral, as before).
    double temperatureOverride = 0.0;
    // #1161: why the shot ended → shots.stopped_by. "" by default so
    // existing fixtures are unaffected (sparse-omitted from the blocks).
    QString stoppedBy{};
    // Bean storage lifecycle snapshot (bean-freshness-followup) → shots
    // frozen_date/defrost_date/storage_hint/opened_date. "" by default so
    // existing fixtures are unaffected (sparse-omitted from the blocks).
    QString frozenDate{};
    QString defrostDate{};
    QString storageHint{};
    QString openedDate{};
};

// Run work with a scoped raw SQLite connection on `path`. The connection is
// removed deterministically when `work` returns so Qt does not warn about open
// connections.
template<typename Work>
void withRawDb(const QString& path, const QString& connName, Work&& work)
{
    // The open check does NOT use QVERIFY2 directly: that expands to `return`,
    // which would skip removeDatabase() below and leave the name registered. The
    // next addDatabase() with the same name then warns "duplicate connection
    // name", and QTest::failOnWarning() turns that into a second, misleading
    // failure in an unrelated test. Capture the result, always remove, then assert.
    QString openError;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(path);
        if (!db.open())
            openError = db.lastError().text();
        else {
            QSqlQuery (db).exec(QStringLiteral("PRAGMA foreign_keys = ON"));
            work(db);
        }
    }
    QSqlDatabase::removeDatabase(connName);
    QVERIFY2(openError.isEmpty(), qPrintable(openError));
}

// Find-or-create by grinder AND basket. Not
// EquipmentStorage::findPackageByGrinderIdentityStatic(), which matches the
// grinder alone and so cannot express two baskets on one grinder.
inline qint64 findOrCreatePackage(QSqlDatabase& db, const ShotRow& r)
{
    const bool hasGrinder = !(r.grinderBrand.isEmpty() && r.grinderModel.isEmpty()
                              && r.grinderBurrs.isEmpty());
    const bool hasBasket = !(r.basketBrand.isEmpty() && r.basketModel.isEmpty());
    if (!hasGrinder && !hasBasket)
        return 0;  // no identity at all -> equipment_id stays NULL

    QSqlQuery find(db);
    find.prepare(QStringLiteral(
        "SELECT p.id FROM equipment_packages p "
        "LEFT JOIN equipment_items g ON g.package_id = p.id AND g.kind = 'grinder' "
        "LEFT JOIN equipment_items b ON b.package_id = p.id AND b.kind = 'basket' "
        // COALESCE on the BOUND side too: an unset ShotRow field is a null
        // QString, which Qt binds as SQL NULL, and `x = NULL` is never true --
        // so without this the lookup never matches and every shot forks its own
        // package.
        "WHERE COALESCE(g.brand,'') = COALESCE(?,'') "
        "  AND COALESCE(g.model,'') = COALESCE(?,'') "
        "  AND COALESCE(json_extract(g.attrs,'$.burrs'),'') = COALESCE(?,'') "
        "  AND COALESCE(b.brand,'') = COALESCE(?,'') "
        "  AND COALESCE(b.model,'') = COALESCE(?,'')"));
    find.addBindValue(r.grinderBrand);
    find.addBindValue(r.grinderModel);
    find.addBindValue(r.grinderBurrs);
    find.addBindValue(r.basketBrand);
    find.addBindValue(r.basketModel);
    if (!find.exec()) {
        qWarning() << "findOrCreatePackage: lookup failed:" << find.lastError().text();
        return 0;
    }
    if (find.next())
        return find.value(0).toLongLong();

    EquipmentPackage pkg;
    const qint64 id = EquipmentStorage::createPackageWithGrinderStatic(
        db, pkg, r.grinderBrand, r.grinderModel, r.grinderBurrs);
    if (id <= 0) {
        qWarning() << "findOrCreatePackage: package create failed";
        return 0;
    }
    if (hasBasket && !EquipmentStorage::setBasketItemStatic(db, id, r.basketBrand, r.basketModel))
        qWarning() << "findOrCreatePackage: basket set failed for package" << id;
    return id;
}

inline qint64 insertShot(QSqlDatabase& db, const ShotRow& r)
{
    // Grinder identity is no longer a per-shot column (migration 23) — it
    // resolves through equipment_id to a package's grinder item. Mirror the
    // production save path: find-or-create a package for this row's grinder
    // identity and link the shot to it. The per-shot grind setting stays on the
    // row. An empty identity leaves equipment_id NULL.
    const qint64 equipmentId = findOrCreatePackage(db, r);

    // shots.uuid is NOT NULL and a default QString binds as NULL, so every
    // ShotRow literal had to set it by hand. An empty uuid could never insert,
    // so filling it cannot change a passing test. Counter, not QUuid: fixtures
    // should be deterministic.
    QString uuid = r.uuid;
    if (uuid.isEmpty()) {
        static int autoUuid = 0;
        uuid = QStringLiteral("fixture-auto-%1").arg(++autoUuid);
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO shots (
            uuid, timestamp, profile_name, beverage_type,
            duration_seconds, final_weight, dose_weight,
            bean_brand, bean_type, roast_level,
            grinder_setting, rpm, equipment_id,
            enjoyment, espresso_notes, profile_kb_id,
            profile_json, yield_override, temperature_override, stopped_by,
            frozen_date, defrost_date, storage_hint, opened_date
        ) VALUES (
            :uuid, :timestamp, :profile_name, :beverage_type,
            :duration, :final_weight, :dose_weight,
            :bean_brand, :bean_type, :roast_level,
            :grinder_setting, :rpm, :equipment_id,
            :enjoyment, :espresso_notes, :profile_kb_id,
            :profile_json, :yield_override, :temperature_override, :stopped_by,
            :frozen_date, :defrost_date, :storage_hint, :opened_date
        )
    )"));
    q.bindValue(":uuid", uuid);
    q.bindValue(":timestamp", r.timestamp);
    q.bindValue(":profile_name", r.profileName);
    q.bindValue(":beverage_type", r.beverageType);
    q.bindValue(":duration", r.duration);
    q.bindValue(":final_weight", r.finalWeight);
    q.bindValue(":dose_weight", r.doseWeight);
    q.bindValue(":bean_brand", r.beanBrand);
    q.bindValue(":bean_type", r.beanType);
    q.bindValue(":roast_level", r.roastLevel);
    q.bindValue(":grinder_setting", r.grinderSetting);
    // NULL when unset, matching a shot with no recorded RPM — a 0 here
    // would still read as "not recorded" downstream, but NULL is what the
    // app actually writes.
    q.bindValue(":rpm", r.rpm > 0 ? QVariant(r.rpm) : QVariant());
    q.bindValue(":equipment_id", equipmentId > 0 ? QVariant(equipmentId) : QVariant());
    q.bindValue(":enjoyment", r.enjoyment);
    q.bindValue(":espresso_notes", r.espressoNotes);
    q.bindValue(":profile_kb_id", r.profileKbId.isEmpty() ? QVariant() : r.profileKbId);
    q.bindValue(":profile_json", r.profileJson);
    q.bindValue(":yield_override", r.targetWeight);
    q.bindValue(":temperature_override", r.temperatureOverride);
    q.bindValue(":stopped_by", r.stoppedBy);
    q.bindValue(":frozen_date", r.frozenDate.isEmpty() ? QVariant() : r.frozenDate);
    q.bindValue(":defrost_date", r.defrostDate.isEmpty() ? QVariant() : r.defrostDate);
    q.bindValue(":storage_hint", r.storageHint.isEmpty() ? QVariant() : r.storageHint);
    q.bindValue(":opened_date", r.openedDate.isEmpty() ? QVariant() : r.openedDate);
    if (!q.exec ()) {
        qWarning() << "insertShot failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

// The AdviceScope for a DB whose shots all sit in one equipment package.
// Warns when there is more than one: picking either by convention is how a
// scoped read ends up tested against a pool it never uses.
inline AdviceScope soleScope(QSqlDatabase& db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT DISTINCT COALESCE(equipment_id, 0) FROM shots ORDER BY 1"))) {
        qWarning() << "soleScope: query failed:" << q.lastError().text();
        return AdviceScope(0);
    }
    QList<qint64> buckets;
    while (q.next())
        buckets.append(q.value(0).toLongLong());
    if (buckets.isEmpty())
        return AdviceScope(0);   // no shots yet: nothing to be ambiguous about
    if (buckets.size() != 1) {
        qWarning() << "soleScope: expected exactly one equipment bucket, found"
                   << buckets.size() << buckets
                   << "-- name the intended one with AdviceScope(id) instead";
        return AdviceScope(buckets.isEmpty() ? 0 : buckets.first());
    }
    return AdviceScope(buckets.first());
}

inline ShotProjection projectionForShot(QSqlDatabase& db, qint64 shotId)
{
    return ShotHistoryStorage::convertShotRecord(
        ShotHistoryStorage::loadShotRecordStatic(db, shotId));
}

// Schema introspection. Previously hand-copied byte-for-byte into tst_dbmigration
// and tst_coffeebags; hasIndex existed once and belongs with its siblings.
// NOTE: several tests assert the NEGATIVE (QVERIFY(!hasColumn(...)) after a
// column is dropped). A bare `return false` on failure would make those pass
// against a typo'd table name or a broken connection, so the failure path warns —
// QTest::failOnWarning() then turns it into a hard failure instead of a silent
// pass. SQLite answers PRAGMA table_info(<missing table>) with an empty result
// set rather than an error, so the table is checked explicitly.
inline bool hasTable(QSqlDatabase& db, const QString& table);

inline bool hasColumn(QSqlDatabase& db, const QString& table, const QString& column)
{
    if (!hasTable(db, table)) {
        qWarning() << "hasColumn: no such table" << table;
        return false;
    }
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        qWarning() << "hasColumn: PRAGMA table_info failed for" << table
                   << "-" << q.lastError().text();
        return false;
    }
    while (q.next()) {
        if (q.value(1).toString() == column)
            return true;
    }
    return false;
}

inline bool hasTable(QSqlDatabase& db, const QString& table)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name=?"));
    q.addBindValue(table);
    return q.exec() && q.next();
}

inline bool hasIndex(QSqlDatabase& db, const QString& indexName)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type='index' AND name=?"));
    q.addBindValue(indexName);
    return q.exec() && q.next();
}

// Initialize, close, and let background DB work drain.
//
// Drains on the real condition (isDbWorkIdle) rather than a fixed sleep. The
// three hand-copied versions this replaces each spun `20 x msleep(25)` — half a
// second per call, and a timer standing in for a condition, which CLAUDE.md
// forbids precisely because it breaks on a slow device.
inline void initAndCloseStorage(const QString& path, ShotHistoryStorage& storage)
{
    // close() resets its m_db handle before removeDatabase, so there is no
    // "connection still in use" warning to ignore here.
    QVERIFY(storage.initialize(path));
    storage.close();
    QTRY_VERIFY(storage.isDbWorkIdle());
}

}  // namespace ShotRowFixtures
