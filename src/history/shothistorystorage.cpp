#include "core/diagnosticlogging.h"
#include <optional>
#include "shothistorystorage.h"
#include "shotcomparison.h"
#include "shotwaterresult.h"
#include "core/appsettings.h"
#include "shothistorystorage_internal.h"
#include "ai/profileshapeindex.h"
#include "coffeebagstorage.h"
#include "equipmentstorage.h"
#include "equipmentlogging.h"
#include "core/settings.h"   // Settings::testQSettingsPath() under DECENZA_TESTING
#include "recipestorage.h"
#include "ai/conductance.h"
#include "ai/shotanalysis.h"
#include "ai/shotsummarizer.h"
#include "history/shotbadgeprojection.h"
#include "core/grinderaliases.h"
#include "core/yieldspec.h"
#include "models/shotdatamodel.h"
#include "profile/profile.h"
#include "network/visualizeruploader.h"
#include "network/beanbase_blob.h"
#include "network/visualizersync.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QLocale>
#include <QDebug>
#include <QSettings>
#include <QThread>
#include <QMutex>
#include <algorithm>
#include <array>
#include <tuple>
#include <cmath>
#include "core/dbutils.h"

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

// Internal helpers (use12h, ProfileFrameInfo, AnalysisInputs,
// profileFrameInfoFromJson, prepareAnalysisInputs) are declared in
// shothistorystorage_internal.h and shared with shothistorystorage_serialize.cpp
// and shothistorystorage_queries.cpp.
using decenza::storage::detail::AnalysisInputs;
using decenza::storage::detail::prepareAnalysisInputs;

namespace {

// The PORTAL columns shot save and shot-file import write identically.
void bindPortalColumns(QSqlQuery& query, const PortalTailAnalyzer::PersistedSummary& summary,
                       const QString& mountingMode, const QString& weightSource, bool present)
{
    query.bindValue(":portal_mean_ec", nullIfZero(summary.meanRawEcPerG));
    query.bindValue(":portal_integral_ec_g", nullIfZero(summary.integralRawEcG));
    query.bindValue(":portal_mean_temp_c", nullIfZero(summary.meanTempC));
    // The column predates the rename; it holds the measurement verdict, not a prediction's.
    query.bindValue(":portal_estimate_reliable", summary.measurementReliable);
    query.bindValue(":portal_mounting_mode", mountingMode);
    query.bindValue(":weight_source", weightSource);
    query.bindValue(":portal_present", present);
}

// The reference read repeats on every Settings refresh and History page, so a
// persistent failure is reported once per cause, and its recovery once.
class PortalReadReporter {
public:
    void failed(const QString& cause)
    {
        QMutexLocker lock(&m_mutex);
        if (cause == m_lastFailure) return;
        m_lastFailure = cause;
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "PORTAL calibration read failed:" << cause;
    }
    void succeeded()
    {
        QMutexLocker lock(&m_mutex);
        if (m_lastFailure.isEmpty()) return;
        m_lastFailure.clear();
        DIAG_INFO(STORAGE, "ShotHistoryStorage") << "PORTAL calibration read recovered";
    }

private:
    QMutex m_mutex;
    QString m_lastFailure;
};

PortalReadReporter& portalReadReporter()
{
    static PortalReadReporter reporter;
    return reporter;
}

// Dose and yield feed the thermal-ratio and cup-mass checks, so editing either
// re-derives the stored measurement flag (refreshPortalEligibilityInTransaction).
bool changesPortalEligibility(const QVariantMap& fields)
{
    return fields.contains(QStringLiteral("doseWeight")) || fields.contains(QStringLiteral("finalWeight"));
}

} // namespace

const QString ShotHistoryStorage::DB_CONNECTION_NAME = "ShotHistoryConnection";

ShotHistoryStorage::ShotHistoryStorage(QObject* parent)
    : QObject(parent)
{
}

ShotHistoryStorage::~ShotHistoryStorage()
{
    *m_destroyed = true;
    // Stop the CRUD worker before members vanish. reset() runs ~SerialDbWorker,
    // which quit()s (discarding queued-but-unstarted tasks) and wait()s for the
    // single in-flight task to finish its DB work — all while `this` is still
    // alive. The m_destroyed flag, set just above, suppresses that task's result
    // callback so it can't touch a half-destroyed object.
    m_dbWorker.reset();
    close();
}

void ShotHistoryStorage::runOnDbThread(std::function<void()> task)
{
    if (!m_dbWorker)
        m_dbWorker = std::make_unique<SerialDbWorker>(QStringLiteral("ShotHistoryStorageWorker"));
    m_dbWorker->post(std::move(task));
}

void ShotHistoryStorage::runDetachedDbThread(std::function<void()> body)
{
    // Counted, so isDbWorkIdle() can see it. The counter is a shared_ptr for the
    // same reason m_destroyed is: the thread may outlive `this`, and it must still
    // be able to decrement without touching a destroyed object.
    // The decrement is RAII rather than a trailing statement, so an exception escaping
    // body() cannot latch the counter above zero for the rest of the process. That matters
    // because a stuck counter makes isDbWorkIdle() permanently false, turning any future
    // wait on it — a factory reset about to delete the file, a shutdown drain — from a wait
    // into a hang.
    //
    // NOT covered: thread->start() failing. Qt only warns there, run() never executes, and
    // `finished` never fires, so the QThread is never deleteLater'd and the lambda holding
    // this guard is never destroyed either. Accepted rather than worked around: it means
    // the OS refused a thread, the process has bigger problems, and the tests' failOnWarning
    // would surface Qt's warning immediately. Stated because the first draft of this comment
    // claimed the guard handled it, which is wrong in a way that reads as reassuring.
    struct InFlightGuard {
        std::shared_ptr<std::atomic<int>> counter;
        explicit InFlightGuard(std::shared_ptr<std::atomic<int>> c) : counter(std::move(c)) {
            counter->fetch_add(1, std::memory_order_relaxed);
        }
        InFlightGuard(InFlightGuard&& other) noexcept : counter(std::move(other.counter)) {}
        InFlightGuard(const InFlightGuard&) = delete;
        InFlightGuard& operator=(const InFlightGuard&) = delete;
        ~InFlightGuard() {
            // Release, paired with the acquire in isDbWorkIdle(): a reader that sees zero
            // must also see everything the thread did to the DB file before it. Guarded
            // because a moved-from copy holds nothing.
            if (counter)
                counter->fetch_sub(1, std::memory_order_release);
        }
    };

    QThread* thread = QThread::create(
        [ownedBody = std::move(body), guard = InFlightGuard(m_detachedDbThreads)]() mutable {
            ownedBody();
        });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

bool ShotHistoryStorage::isDbWorkIdle() const
{
    // No worker means nothing was ever posted, which is idle by definition — the
    // worker is created lazily on first use (runOnDbThread).
    //
    // The detached count is the other half, and it used to be missing: the nine
    // read queries spawn one-shot threads that never go through m_dbWorker, so a
    // caller that waited on this was told "idle" while a thread was mid-SELECT.
    // initialize() used to start one itself (the distinct-value cache pre-warm),
    // so EVERY user of this class had one running; that cache is gone, but the
    // detached reads it exposed remain. It surfaced as tst_mcptools_write failing
    // with `disk I/O error Unable to execute statement` — a test's QTemporaryDir
    // deleting the .db out from under the previous test's still-running pre-warm,
    // with the warning landing in whichever test happened to be running next.
    return (!m_dbWorker || m_dbWorker->isIdle())
        && m_detachedDbThreads->load(std::memory_order_acquire) == 0;
}

bool ShotHistoryStorage::isDbWriteWorkIdle() const
{
    return !m_dbWorker || m_dbWorker->isIdle();
}

void ShotHistoryStorage::close()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    // Drop THIS object's handle to the named connection BEFORE removing it.
    // removeDatabase() warns ("connection is still in use, all queries will
    // cease to work") when a live QSqlDatabase still references the connection
    // — and the member m_db is exactly such a reference. Reset it to an
    // invalid database first so removeDatabase runs cleanly.
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(DB_CONNECTION_NAME)) {
        QSqlDatabase::removeDatabase(DB_CONNECTION_NAME);
    }
}

// NULL (an empty table) reads as 1, the version createTables() once seeded.
static std::optional<int> readSchemaVersion(QSqlDatabase& db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT MAX(version) FROM schema_version")) || !q.next())
        return std::nullopt;
    return q.value(0).isNull() ? 1 : q.value(0).toInt();
}

QString ShotHistoryStorage::defaultDatabasePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/shots.db");
}

ShotHistoryStorage::UpgradeFloorProbe ShotHistoryStorage::probeUpgradeFloor(const QString& path)
{
    // Opening a missing file would create it.
    if (!QFileInfo::exists(path))
        return UpgradeFloorProbe::NoDatabase;

    UpgradeFloorProbe result = UpgradeFloorProbe::Unknown;
    withTempDb(path, QStringLiteral("upgrade_floor_probe"), [&](QSqlDatabase& db) {
        QSqlQuery tables(db);
        if (!tables.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' "
                                        "AND name IN ('shots', 'schema_version')")))
            return;
        bool hasShots = false;
        bool hasVersion = false;
        while (tables.next()) {
            const QString name = tables.value(0).toString();
            hasShots = hasShots || name == QLatin1String("shots");
            hasVersion = hasVersion || name == QLatin1String("schema_version");
        }
        if (!hasShots) {
            result = UpgradeFloorProbe::NoDatabase;
            return;
        }
        const std::optional<int> version = hasVersion ? readSchemaVersion(db) : 1;
        if (!version)
            return;
        result = *version >= UpgradeFloor::kSchemaVersion ? UpgradeFloorProbe::Supported
                                                       : UpgradeFloorProbe::BelowFloor;
    });
    return result;
}

bool ShotHistoryStorage::initialize(const QString& dbPath)
{
    m_bagGrindNoticePending = false;
    m_dbPath = dbPath;
    if (m_dbPath.isEmpty()) {
        QDir().mkpath(QFileInfo(defaultDatabasePath()).absolutePath());
        m_dbPath = defaultDatabasePath();
    }

    DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Initializing database at" << m_dbPath;

    // Drop any existing connection (a re-initialize) cleanly first — close()
    // resets m_db before removing so removeDatabase doesn't warn about a
    // still-referenced connection.
    close();

    m_db = QSqlDatabase::addDatabase("QSQLITE", DB_CONNECTION_NAME);
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to open database:" << m_db.lastError().text();
        emit errorOccurred("Failed to open shot history database");
        return false;
    }

    // Enable WAL mode for better concurrent access
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA journal_mode=WAL");
    pragma.exec("PRAGMA foreign_keys=ON");
    // No busy_timeout pragma here on purpose: Qt's SQLite driver already calls
    // sqlite3_busy_timeout(5000) on every connection it opens, overridable only
    // by a QSQLITE_BUSY_TIMEOUT connect option this app never sets. Setting it
    // again would be redundant AND harmful — "PRAGMA busy_timeout = N" returns a
    // row, and Qt leaves a row-returning statement un-reset, so this reused
    // QSqlQuery would hold a read transaction open across createTables(),
    // runMigrations() and the startup WAL checkpoint. foreign_keys returns no
    // row, which is why ending on it leaves the connection clean.

    if (!createTables()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to create tables";
        return false;
    }

    if (!runMigrations()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to run migrations";
        return false;
    }

    // Checkpoint any existing WAL data from previous sessions
    // This ensures all data is in the main .db file
    QSqlQuery walQuery(m_db);
    if (walQuery.exec("PRAGMA wal_checkpoint(TRUNCATE)")) {
        DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Startup WAL checkpoint completed";
    }

    {
        QSqlQuery noticeQuery(m_db);
        if (noticeQuery.exec("SELECT 1 FROM bag_grind_notice WHERE id = 1")) {
            m_bagGrindNoticePending = noticeQuery.next();
            if (m_bagGrindNoticePending)
                DIAG_INFO(STORAGE, "ShotHistoryStorage") << "bag grind upgrade notice pending";
        } else
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to read the bag grind notice state:"
                       << noticeQuery.lastError().text() << "- it will be checked again next launch";
    }

    // Sync count at startup (before UI, acceptable on init)
    {
        QSqlQuery countQuery(m_db);
        if (countQuery.exec("SELECT COUNT(*) FROM shots") && countQuery.next())
            m_totalShots = countQuery.value(0).toInt();
        else
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to count shots at startup:" << countQuery.lastError().text();
    }

    // Seed lastSavedShotId with the newest stored shot so direct readers see
    // a valid id immediately after an app restart. The review page's
    // sticky-sync gate DEPENDS on this (it compares synchronously, no
    // fallback); the Last Shot widget and MCP latest-shot tools keep their
    // own most-recent-by-timestamp DB fallbacks and merely take the fast
    // path now. Empty DB -> NULL -> 0 (unchanged).
    {
        QSqlQuery maxQuery(m_db);
        if (maxQuery.exec("SELECT MAX(id) FROM shots") && maxQuery.next())
            m_lastSavedShotId = maxQuery.value(0).toLongLong();
        else
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to seed lastSavedShotId at startup:"
                       << maxQuery.lastError().text();
    }

    m_ready = true;
    emit readyChanged();

#ifndef DECENZA_TESTING
    // After m_ready, deliberately: the census reads through its own connection
    // on the worker, and queueing it earlier would only widen the window in
    // which a reader could see a half-initialised object.
    //
    // Skipped in test builds, for a sharper reason than "tests don't need it". Posting here CREATES the FIFO
    // worker and increments its outstanding count, and ~SerialDbWorker warns
    // "those writes are being discarded" for anything still queued
    // (core/dbutils.h). A read-only storage never created that worker before,
    // so the warning was unreachable; arming it on every initialize() would
    // make a census that writes NOTHING claim a user lost writes, and would
    // race QTest::failOnWarning() in every test that constructs a storage and
    // lets it fall out of scope without draining. The failure would surface
    // named "ShotHistoryStorageWorker", in whichever test happened to be
    // running — see the same hazard documented in tst_coffeebags.cpp.
    logGrinderCensus();
#endif

    DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Database initialized with" << m_totalShots << "shots";
    return true;
}

bool ShotHistoryStorage::createTables()
{
    QSqlQuery query(m_db);

    // An existing database is not touched here: runMigrations() reads its
    // version first and refuses one below the upgrade floor before anything
    // writes to it.
    if (!query.exec(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'shots'"))) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to inspect the database:" << query.lastError().text();
        return false;
    }
    if (query.next())
        return true;
    query.finish();

    // A fresh database starts at the schema 2.0.7 had (UpgradeFloor::kSchemaVersion)
    // and runMigrations() takes it from there. One transaction, because a shots
    // table without its version row reads as a pre-floor database and would be
    // refused on every later launch. tst_dbmigration's equivalence test holds
    // this DDL to tests/data/db/schema41.db.
    DbWriteTxn txn = DbWriteTxn::begin(m_db, "create schema 41 baseline", 1);
    if (!txn.ok())
        return false;

    const char* const statements[] = {
        R"(CREATE TABLE shots (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            uuid TEXT UNIQUE NOT NULL,
            timestamp INTEGER NOT NULL,
            profile_name TEXT NOT NULL,
            profile_json TEXT,
            beverage_type TEXT DEFAULT 'espresso',
            duration_seconds REAL NOT NULL,
            final_weight REAL,
            dose_weight REAL,
            bean_brand TEXT,
            bean_type TEXT,
            roast_date TEXT,
            roast_level TEXT,
            grinder_setting TEXT,
            drink_tds REAL,
            drink_ey REAL,
            enjoyment INTEGER,
            espresso_notes TEXT,
            bean_notes TEXT,
            barista TEXT,
            profile_notes TEXT,
            visualizer_id TEXT,
            visualizer_url TEXT,
            debug_log TEXT,
            temperature_override REAL,
            yield_override REAL,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            updated_at INTEGER DEFAULT (strftime('%s', 'now')),
            profile_kb_id TEXT,
            channeling_detected INTEGER DEFAULT 0,
            grind_issue_detected INTEGER DEFAULT 0,
            skip_first_frame_detected INTEGER DEFAULT 0,
            pour_truncated_detected INTEGER DEFAULT 0,
            stopped_by TEXT DEFAULT '',
            beanbase_json TEXT,
            bag_id INTEGER,
            frozen_date TEXT,
            defrost_date TEXT,
            beanbase_id TEXT,
            equipment_id INTEGER,
            rpm INTEGER,
            recipe_id INTEGER,
            steam_json TEXT,
            hot_water_json TEXT,
            storage_hint TEXT,
            opened_date TEXT,
            taste_balance TEXT,
            taste_body TEXT,
            yield_mode TEXT,
            yield_anchor_value REAL,
            bean_repair_pending INTEGER NOT NULL DEFAULT 0,
            flow_calibration REAL
        ))",
        R"(CREATE TABLE shot_samples (
            shot_id INTEGER PRIMARY KEY REFERENCES shots(id) ON DELETE CASCADE,
            sample_count INTEGER NOT NULL,
            data_blob BLOB NOT NULL
        ))",
        R"(CREATE TABLE shot_phases (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            shot_id INTEGER NOT NULL REFERENCES shots(id) ON DELETE CASCADE,
            time_offset REAL NOT NULL,
            label TEXT NOT NULL,
            frame_number INTEGER,
            is_flow_mode INTEGER DEFAULT 0,
            transition_reason TEXT DEFAULT ''
        ))",
        R"(CREATE VIRTUAL TABLE shots_fts USING fts5(
            espresso_notes, bean_brand, bean_type, profile_name,
            content='shots', content_rowid='id'
        ))",
        R"(CREATE TRIGGER shots_ai AFTER INSERT ON shots BEGIN
            INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name)
            VALUES (new.id, new.espresso_notes, new.bean_brand, new.bean_type, new.profile_name);
        END)",
        R"(CREATE TRIGGER shots_ad AFTER DELETE ON shots BEGIN
            INSERT INTO shots_fts(shots_fts, rowid, espresso_notes, bean_brand, bean_type, profile_name)
            VALUES ('delete', old.id, old.espresso_notes, old.bean_brand, old.bean_type, old.profile_name);
        END)",
        R"(CREATE TRIGGER shots_au AFTER UPDATE ON shots BEGIN
            INSERT INTO shots_fts(shots_fts, rowid, espresso_notes, bean_brand, bean_type, profile_name)
            VALUES ('delete', old.id, old.espresso_notes, old.bean_brand, old.bean_type, old.profile_name);
            INSERT INTO shots_fts(rowid, espresso_notes, bean_brand, bean_type, profile_name)
            VALUES (new.id, new.espresso_notes, new.bean_brand, new.bean_type, new.profile_name);
        END)",
        "CREATE INDEX idx_shots_timestamp ON shots(timestamp DESC)",
        "CREATE INDEX idx_shots_profile ON shots(profile_name)",
        "CREATE INDEX idx_shots_bean ON shots(bean_brand, bean_type)",
        "CREATE INDEX idx_shots_enjoyment ON shots(enjoyment)",
        "CREATE INDEX idx_shots_profile_kb_id ON shots(profile_kb_id)",
        "CREATE INDEX idx_shots_beanbase_id ON shots(beanbase_id)",
        "CREATE INDEX idx_shots_recipe_id ON shots(recipe_id)",
        "CREATE INDEX idx_shots_equip_grind ON shots(equipment_id, grinder_setting)",
        "CREATE INDEX idx_shots_equip_rpm ON shots(equipment_id, rpm)",
        "CREATE INDEX idx_shot_phases_shot ON shot_phases(shot_id)",
        "CREATE TABLE schema_version (version INTEGER PRIMARY KEY)",
    };
    for (const char* sql : statements) {
        if (!query.exec(QString::fromLatin1(sql))) {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to create the schema baseline:" << query.lastError().text();
            return false;
        }
    }
    if (!CoffeeBagStorage::ensureTableStatic(m_db) || !EquipmentStorage::ensureTablesStatic(m_db)
        || !RecipeStorage::ensureTableStatic(m_db))
        return false;
    if (!query.exec(QStringLiteral("INSERT INTO schema_version (version) VALUES (%1)").arg(UpgradeFloor::kSchemaVersion))) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to stamp the schema baseline:" << query.lastError().text();
        return false;
    }
    if (!txn.commit()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to commit the schema baseline:" << txn.commitError();
        return false;
    }
    return true;
}

namespace {

struct MigrationStep {
    const char* name;
    QString error;
};

// remove-bag-equipment-and-grind design.md "Explain the change": no active
// recipe, no active or in-inventory package, and the active bag is one of at
// least two open coffee bags holding a grind that no live recipe owns. nullopt
// when a query fails; `step` says which.
std::optional<bool> bagDialNoticeEligible(QSqlDatabase& db, qint64 recipeId, qint64 equipmentId,
                                          qint64 bagId, MigrationStep* step)
{
    auto counts = [&](const char* name, const char* sql, const QVariantList& binds)
        -> std::optional<std::pair<qint64, qint64>> {
        *step = {name, {}};
        QSqlQuery q(db);
        if (q.prepare(QString::fromLatin1(sql))) {
            for (const QVariant& value : binds)
                q.addBindValue(value);
            if (q.exec() && q.next())
                return std::make_pair(q.value(0).toLongLong(), q.value(1).toLongLong());
        }
        step->error = q.lastError().text();
        return std::nullopt;
    };
    if (bagId <= 0)
        return false;
    const auto active = counts("active selection",
        "SELECT (SELECT COUNT(*) FROM recipes WHERE id = ?), "
        "(SELECT COUNT(*) FROM equipment_packages WHERE id = ? OR in_inventory = 1)",
        {recipeId, equipmentId});
    if (!active)
        return std::nullopt;
    if (active->first > 0 || active->second > 0)
        return false;
    const auto bags = counts("bag grinds",
        "SELECT COUNT(*), COALESCE(MAX(b.id = ?), 0) FROM coffee_bags b "
        "WHERE b.in_inventory = 1 AND b.kind = 'coffee' "
        "AND TRIM(COALESCE(b.grinder_setting, '')) <> '' "
        "AND NOT EXISTS (SELECT 1 FROM recipes r WHERE r.bag_id = b.id AND r.archived = 0)",
        {bagId});
    if (!bags)
        return std::nullopt;
    return bags->first >= 2 && bags->second == 1;
}

} // namespace

bool ShotHistoryStorage::runMigrations()
{
    QSqlQuery query(m_db);

    // Read the version before anything writes. A database below the upgrade
    // floor is left exactly as it is: it has to pass through
    // UpgradeFloor::kSteppingStoneRelease, which still carries the older steps.
    const std::optional<int> storedVersion = readSchemaVersion(m_db);
    if (!storedVersion) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "schema_version read failed -" << m_db.lastError().text()
                                                 << "- database left unchanged, will retry next launch";
        return false;
    }
    int currentVersion = *storedVersion;
    if (currentVersion < UpgradeFloor::kSchemaVersion) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "database is at schema" << currentVersion
                                                 << "- below the upgrade floor" << UpgradeFloor::kSchemaVersion
                                                 << "; install Decenza" << UpgradeFloor::kSteppingStoneRelease
                                                 << "first. Database left unchanged";
        return false;
    }

    // Duplicate rows from an old INSERT OR IGNORE bug: keep only the highest.
    query.exec("DELETE FROM schema_version WHERE version != (SELECT MAX(version) FROM schema_version)");

    // Holds one row while migration 52's upgrade notice awaits acknowledgement.
    if (!query.exec("CREATE TABLE IF NOT EXISTS bag_grind_notice (id INTEGER PRIMARY KEY CHECK(id = 1))"))
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to create bag_grind_notice:" << query.lastError().text();

    // Helper: check if a column exists in a table
    // Returns nullopt when the PRAGMA itself failed — "I could not find out" is
    // not "the column is absent". Collapsing the two let a busy/locked database
    // report a present column as missing, which then decided a migration's
    // behaviour with no way to tell that it had guessed.
    auto columnPresent = [&](const QString& table, const QString& column) -> std::optional<bool> {
        QSqlQuery q(m_db);
        if (!q.exec(QString("PRAGMA table_info(%1)").arg(table))) {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "PRAGMA table_info(" << table
                       << ") failed -" << q.lastError().text();
            return std::nullopt;
        }
        while (q.next()) {
            if (q.value(1).toString() == column)
                return true;
        }
        return false;
    };

    // Columns and required schema statements share the version-stamp transaction.
    struct NewColumn { QString table, name, type; };
    auto addColumns = [&](int version, const char* label, const QList<NewColumn>& columns,
                          const char* untilComplete, const QStringList& extraStatements = {}) {
        query.finish();
        DbWriteTxn txn = DbWriteTxn::begin(m_db, label, 1);
        if (!txn.ok()) {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "migration" << version
                                                     << "could not start a transaction - will retry next launch";
            return;
        }
        bool ok = true;
        for (const NewColumn& column : columns) {
            const std::optional<bool> present = columnPresent(column.table, column.name);
            if (!present.has_value()) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "migration" << version << "could not inspect" << column.table;
                ok = false;
                break;
            }
            if (!*present && !query.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                                             .arg(column.table, column.name, column.type))) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "migration" << version << "add" << column.table << "."
                                                         << column.name << "failed -" << query.lastError().text();
                ok = false;
                break;
            }
        }
        for (const QString& statement : extraStatements) {
            if (!ok) break;
            ok = query.exec(statement);
            if (!ok)
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "migration" << version << "statement failed -"
                                                         << query.lastError().text();
        }
        if (ok)
            ok = query.exec("DELETE FROM schema_version")
                 && query.exec(QStringLiteral("INSERT INTO schema_version (version) VALUES (%1)").arg(version));
        if (ok && txn.commit()) {
            currentVersion = version;
            // INFO: its failure is a WARN that promises a retry, and the resolution belongs beside it.
            DIAG_INFO(STORAGE, "ShotHistoryStorage") << "migration" << version << "complete";
        } else {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "migration" << version
                                                     << "incomplete - will retry next launch." << untilComplete;
        }
    };

    // Migration 42: per-shot Decent account upload state (add-decent-shot-upload).
    // Unfilled (NULL, or 0 for decent_replace_pending): every existing shot starts
    // "never uploaded".
    if (currentVersion < 42) {
        addColumns(42, "migration 42 decent upload columns", {
            {QStringLiteral("shots"), QStringLiteral("decent_uploaded_at"), QStringLiteral("INTEGER")},
            {QStringLiteral("shots"), QStringLiteral("decent_shot_id"), QStringLiteral("TEXT")},
            {QStringLiteral("shots"), QStringLiteral("decent_serial"), QStringLiteral("TEXT")},
            {QStringLiteral("shots"), QStringLiteral("decent_replace_pending"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
            {QStringLiteral("shots"), QStringLiteral("decent_rejected_status"), QStringLiteral("INTEGER")},
            {QStringLiteral("shots"), QStringLiteral("decent_rejected_at"), QStringLiteral("INTEGER")},
        }, "Decent upload is unavailable until it completes");
    }

    // Migration 43: two-way Visualizer sync (visualizer-two-way-sync).
    // shots.visualizer_dirty holds a VisualizerSync::Field bit per field edited
    // here and not yet sent; visualizer_dirty_seq counts edits, so a send clears
    // only the bits it carried and only if no edit landed meanwhile.
    // coffee_bags.visualizer_seen is what Visualizer was last known to hold, so
    // each side syncs its own changes rather than its state. Unfilled: no shot
    // has an unsent edit, no bag has been seen.
    if (currentVersion >= 42 && currentVersion < 43) {
        addColumns(43, "migration 43 visualizer sync columns", {
            {QStringLiteral("shots"), QStringLiteral("visualizer_dirty"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
            {QStringLiteral("shots"), QStringLiteral("visualizer_dirty_seq"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
            {QStringLiteral("coffee_bags"), QStringLiteral("visualizer_seen"), QStringLiteral("TEXT")},
        }, "Until it completes, editing a shot and reading coffee bags fail");
    }

    // Migration 44 (D15): <dest>_failed_at for both destinations (an upload that
    // failed every attempt), and visualizer_rejected_* to match Decent's
    // migration-42 columns. Unfilled: nothing failed, nothing refused by Visualizer.
    if (currentVersion >= 43 && currentVersion < 44) {
        addColumns(44, "migration 44 upload outcome columns", {
            {QStringLiteral("shots"), QStringLiteral("decent_failed_at"), QStringLiteral("INTEGER")},
            {QStringLiteral("shots"), QStringLiteral("visualizer_failed_at"), QStringLiteral("INTEGER")},
            {QStringLiteral("shots"), QStringLiteral("visualizer_rejected_status"), QStringLiteral("INTEGER")},
            {QStringLiteral("shots"), QStringLiteral("visualizer_rejected_at"), QStringLiteral("INTEGER")},
        }, "Until it completes, upload outcomes are not recorded");
    }

    // Migration 45: shots already on the Decent account were sent with UTC ("Z")
    // times, which decentespresso.com shows as UTC. Mark them replace-pending so
    // Upload missing shots re-sends them with the device's offset. A data fix, so
    // the bump is gated on it.
    if (currentVersion >= 44 && currentVersion < 45) {
        query.finish();
        DbWriteTxn txn = DbWriteTxn::begin(m_db, "migration 45 decent time resend", 1);
        if (!txn.ok()) {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "migration 45 could not start a transaction"
                          " - will retry next launch";
        } else {
            bool ok = query.exec("UPDATE shots SET decent_replace_pending = 1 WHERE decent_uploaded_at IS NOT NULL");
            const int marked = ok ? query.numRowsAffected() : 0;
            ok = ok && query.exec("DELETE FROM schema_version")
                    && query.exec(QStringLiteral("INSERT INTO schema_version (version) VALUES (45)"));
            if (ok && txn.commit()) {
                currentVersion = 45;
                DIAG_INFO(STORAGE, "ShotHistoryStorage") << "migration 45 complete -" << marked
                              << "Decent shot(s) offered for re-upload with local times";
            } else {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "migration 45 incomplete - will retry next launch:"
                              << query.lastError().text();
            }
        }
    }

    // Migration 46 (add-grinder-ugs-calibration): a package's optional fine and
    // coarse calibration anchors, each one of its own shots. Unfilled: no package
    // has anchors. A trigger, not a foreign key, clears a side when its shot is
    // deleted: with foreign_keys on, SQLite refuses every write to a child table
    // whose parent table is missing, which equipment_packages must survive.
    if (currentVersion >= 45 && currentVersion < 46) {
        addColumns(46, "migration 46 calibration anchor columns", {
            {QStringLiteral("equipment_packages"), QStringLiteral("calib_fine_shot_id"), QStringLiteral("INTEGER")},
            {QStringLiteral("equipment_packages"), QStringLiteral("calib_coarse_shot_id"), QStringLiteral("INTEGER")},
        }, "Until it completes, grind calibration anchors cannot be set", {QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS shots_clear_calibration_anchor AFTER DELETE ON shots BEGIN "
            "UPDATE equipment_packages SET calib_fine_shot_id = NULL WHERE calib_fine_shot_id = OLD.id; "
            "UPDATE equipment_packages SET calib_coarse_shot_id = NULL WHERE calib_coarse_shot_id = OLD.id; "
            "END")});
    }

    if (currentVersion >= 46 && currentVersion < 47) {
        addColumns(47, "migration 47 water result", {
            {QStringLiteral("shots"), QStringLiteral("hot_water_result_json"), QStringLiteral("TEXT")},
        }, "Until it completes, added-water results are unavailable");
    }

    // One PORTAL migration; versions 48-50 existed only in contributor previews.
    // Schema-only from main: pre-change shots lack weight provenance, so scanning
    // them could never create a calibration reference.
    if (currentVersion >= 47 && currentVersion < 51) {
        QStringList previewStatements;
        if (currentVersion >= 48)
            previewStatements << QStringLiteral("UPDATE shots SET portal_present=1 WHERE portal_mean_ec IS NOT NULL");
        addColumns(51, "migration 51 portal measurements", {
            {QStringLiteral("shots"), QStringLiteral("portal_mean_ec"), QStringLiteral("REAL")},
            {QStringLiteral("shots"), QStringLiteral("portal_integral_ec_g"), QStringLiteral("REAL")},
            {QStringLiteral("shots"), QStringLiteral("portal_mean_temp_c"), QStringLiteral("REAL")},
            {QStringLiteral("shots"), QStringLiteral("portal_estimate_reliable"), QStringLiteral("INTEGER")},
            {QStringLiteral("shots"), QStringLiteral("portal_mounting_mode"), QStringLiteral("TEXT")},
            {QStringLiteral("shots"), QStringLiteral("weight_source"), QStringLiteral("TEXT")},
            {QStringLiteral("shots"), QStringLiteral("portal_present"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
            // Previews branched before main's 47 and lack its added-water column.
            {QStringLiteral("shots"), QStringLiteral("hot_water_result_json"), QStringLiteral("TEXT")},
        }, "Until it completes, shots cannot be saved or opened", previewStatements);
    }

    // Migration 52 (remove-bag-equipment-and-grind): bags stop carrying a grind
    // or equipment. It reads the active ids from dye/* settings and never writes
    // settings, so a failure rolls back and retries next launch.
    if (currentVersion == 51) {
        query.finish();
#ifdef DECENZA_TESTING
        // An isolated DB must never read the developer's live settings.
        QSettings settings(m_dbPath + QStringLiteral(".upgrade-settings"), QSettings::IniFormat);
#else
        AppSettings settings;
#endif
        const qint64 equipmentId = settings.value("dye/activeEquipmentId", -1).toLongLong();
        const qint64 recipeId = settings.value("dye/activeRecipeId", -1).toLongLong();
        const qint64 bagId = settings.value("dye/activeBagId", -1).toLongLong();
        // Unreadable settings would read as "nothing active" and drop the notice.
        MigrationStep step{"settings read", {}};
        bool notice = false;
        bool ok = settings.status() == QSettings::NoError;
        if (!ok)
            step.error = QStringLiteral("QSettings status %1").arg(int(settings.status()));
        std::optional<DbWriteTxn> txn;
        if (ok) {
            step = {"begin transaction", QStringLiteral("see the transaction line above")};
            txn.emplace(DbWriteTxn::begin(m_db, "migration 52 bag dial removal", 1));
            ok = txn->ok();
        }
        // A fresh install's bags never had a grind, so there is nothing to explain.
        std::optional<bool> bagsHadGrind;
        if (ok) {
            step = {"inspect coffee_bags", QStringLiteral("could not inspect coffee_bags (see above)")};
            bagsHadGrind = columnPresent(QStringLiteral("coffee_bags"), QStringLiteral("grinder_setting"));
            ok = bagsHadGrind.has_value();
        }
        if (ok && *bagsHadGrind) {
            const std::optional<bool> eligible = bagDialNoticeEligible(m_db, recipeId, equipmentId, bagId, &step);
            ok = eligible.has_value();
            notice = eligible.value_or(false);
        }
        if (ok && notice) {
            step = {"record notice", {}};
            ok = query.exec("INSERT OR IGNORE INTO bag_grind_notice (id) VALUES (1)");
            if (!ok)
                step.error = query.lastError().text();
        }
        for (const char* column : {"grinder_setting", "equipment_id", "rpm"}) {
            if (!ok)
                break;
            step = {column, {}};
            const std::optional<bool> present = columnPresent(QStringLiteral("coffee_bags"),
                                                              QLatin1String(column));
            if (!present.has_value()) {
                step.error = QStringLiteral("could not inspect coffee_bags (see above)");
                ok = false;
            } else if (*present && !query.exec(QStringLiteral("ALTER TABLE coffee_bags DROP COLUMN %1")
                                                   .arg(QLatin1String(column)))) {
                step.error = query.lastError().text();
                ok = false;
            }
        }
        if (ok) {
            step = {"schema_version", {}};
            ok = query.exec("DELETE FROM schema_version")
                && query.exec("INSERT INTO schema_version (version) VALUES (52)");
            if (!ok)
                step.error = query.lastError().text();
        }
        if (ok) {
            step = {"commit", {}};
            ok = txn->commit();
            if (!ok)
                step.error = txn->commitError();
        }
        if (ok) {
            currentVersion = 52;
            // INFO: its failure is a WARN that promises a retry, and the resolution belongs beside it.
            DIAG_INFO(STORAGE, "ShotHistoryStorage") << "migration 52 complete - upgrade notice"
                                                     << (notice ? "pending" : "not needed");
        } else {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "migration 52 incomplete at" << step.name << "-"
                                                     << step.error << "- will retry next launch";
        }
    }

    m_schemaVersion = currentVersion;
    return true;
}

QJsonObject ShotHistoryStorage::pointsToJsonObject(const QVector<QPointF>& points)
{
    QJsonArray timeArr, valueArr;
    for (const auto& pt : points) {
        timeArr.append(pt.x());
        valueArr.append(pt.y());
    }
    QJsonObject obj;
    obj["t"] = timeArr;
    obj["v"] = valueArr;
    return obj;
}

QByteArray ShotHistoryStorage::compressSampleData(ShotDataModel* shotData, const QString& phaseSummariesJson)
{
    QJsonObject root;

    root["pressure"] = pointsToJsonObject(shotData->pressureData());
    root["weightSource"] = shotData->weightSource();
    root["portalMountingMode"] = shotData->portalMountingMode();
    if (!shotData->portalSamples().isEmpty())
        root["portalSamples"] = QJsonArray::fromVariantList(shotData->portalSamplesVariant());
    root["flow"] = pointsToJsonObject(shotData->flowData());
    root["temperature"] = pointsToJsonObject(shotData->temperatureData());
    root["pressureGoal"] = pointsToJsonObject(shotData->pressureGoalData());
    root["flowGoal"] = pointsToJsonObject(shotData->flowGoalData());
    root["temperatureGoal"] = pointsToJsonObject(shotData->temperatureGoalData());
    root["temperatureMixGoal"] = pointsToJsonObject(shotData->temperatureMixGoalData());

    root["temperatureMix"] = pointsToJsonObject(shotData->temperatureMixData());
    root["resistance"] = pointsToJsonObject(shotData->resistanceData());
    root["conductance"] = pointsToJsonObject(shotData->conductanceData());
    root["darcyResistance"] = pointsToJsonObject(shotData->darcyResistanceData());
    root["conductanceDerivative"] = pointsToJsonObject(shotData->conductanceDerivativeData());
    root["waterDispensed"] = pointsToJsonObject(shotData->waterDispensedData());

    // Weight data - store cumulative weight for history
    root["weight"] = pointsToJsonObject(shotData->cumulativeWeightData());
    // Also store flow rate from scale for future graph display
    root["weightFlow"] = pointsToJsonObject(shotData->weightData());
    // Weight-based flow rate (g/s) for visualizer export
    root["weightFlowRate"] = pointsToJsonObject(shotData->weightFlowRateData());
    root["weightFlowRateRaw"] = pointsToJsonObject(shotData->weightFlowRateRawData());

    // Phase summaries for UI display (pre-computed by saveShot() via computePhaseSummaries)
    if (!phaseSummariesJson.isEmpty()) {
        root["phaseSummaries"] = QJsonDocument::fromJson(phaseSummariesJson.toUtf8()).array();
    }

    QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return qCompress(json, 9);  // Max compression
}

void ShotHistoryStorage::decompressSampleData(const QByteArray& blob, ShotRecord* record,
                                               QByteArray* outCorrectedBlob)
{
    if (outCorrectedBlob) outCorrectedBlob->clear();

    QByteArray json = qUncompress(blob);
    if (json.isEmpty()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to decompress sample data";
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(json);
    QJsonObject root = doc.object();

    auto arrayToPoints = [](const QJsonObject& obj) {
        QVector<QPointF> points;
        QJsonArray timeArr = obj["t"].toArray();
        QJsonArray valueArr = obj["v"].toArray();
        qsizetype count = qMin(timeArr.size(), valueArr.size());
        points.reserve(count);
        for (qsizetype i = 0; i < count; ++i) {
            points.append(QPointF(timeArr[i].toDouble(), valueArr[i].toDouble()));
        }
        return points;
    };

    record->pressure = arrayToPoints(root["pressure"].toObject());
    record->flow = arrayToPoints(root["flow"].toObject());
    record->temperature = arrayToPoints(root["temperature"].toObject());
    record->pressureGoal = arrayToPoints(root["pressureGoal"].toObject());
    record->flowGoal = arrayToPoints(root["flowGoal"].toObject());
    record->temperatureGoal = arrayToPoints(root["temperatureGoal"].toObject());
    // Absent for shots recorded before the mix goal series existed — leave empty
    // rather than defaulting, so consumers can tell "no data" from "goal was 0".
    if (root.contains("temperatureMixGoal"))
        record->temperatureMixGoal = arrayToPoints(root["temperatureMixGoal"].toObject());
    record->weightSource = root["weightSource"].toString();
    record->portalMountingMode = root["portalMountingMode"].toString();
    int droppedPortalSamples = 0;
    record->portalSamples = PortalSamples::fromVariant(root["portalSamples"].toArray().toVariantList(),
                                                       &droppedPortalSamples);
    if (droppedPortalSamples > 0)
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Dropped" << droppedPortalSamples
            << "malformed PORTAL samples while loading a shot; its chart will show a gap";
    if (root.contains("temperatureMix"))
        record->temperatureMix = arrayToPoints(root["temperatureMix"].toObject());
    if (root.contains("resistance"))
        record->resistance = arrayToPoints(root["resistance"].toObject());
    if (root.contains("conductance"))
        record->conductance = arrayToPoints(root["conductance"].toObject());
    if (root.contains("darcyResistance"))
        record->darcyResistance = arrayToPoints(root["darcyResistance"].toObject());
    if (root.contains("conductanceDerivative"))
        record->conductanceDerivative = arrayToPoints(root["conductanceDerivative"].toObject());
    if (root.contains("waterDispensed"))
        record->waterDispensed = arrayToPoints(root["waterDispensed"].toObject());
    record->weight = arrayToPoints(root["weight"].toObject());
    if (root.contains("weightFlowRate"))
        record->weightFlowRate = arrayToPoints(root["weightFlowRate"].toObject());
    if (root.contains("weightFlowRateRaw"))
        record->weightFlowRateRaw = arrayToPoints(root["weightFlowRateRaw"].toObject());

    // Phase summaries (stored as JSON array in the compressed blob)
    if (root.contains("phaseSummaries")) {
        record->phaseSummariesJson = QString::fromUtf8(
            QJsonDocument(root["phaseSummaries"].toArray()).toJson(QJsonDocument::Compact));
    }

    // Resistance, conductance, Darcy resistance and the conductance derivative
    // are pure functions of pressure/flow — recompute them unconditionally from
    // this shot's own samples rather than trusting whatever formula produced the
    // stored values (recompute-shot-curves-on-load). computeDerivedCurves()
    // no-ops below 3 samples, leaving whatever was just parsed above untouched.
    //
    // Table-driven (one row per curve) rather than four parallel snapshot/compare/
    // write blocks, so a future curve added to this set can't get wired into some
    // of the three spots (snapshot, compare, JSON-key write) while silently missing another.
    struct DerivedCurve {
        const char* jsonKey;
        QVector<QPointF>* field;
        QVector<QPointF> stored;
    };
    std::array<DerivedCurve, 4> curves = {{
        {"resistance", &record->resistance, {}},
        {"conductance", &record->conductance, {}},
        {"darcyResistance", &record->darcyResistance, {}},
        {"conductanceDerivative", &record->conductanceDerivative, {}},
    }};
    for (auto& c : curves) c.stored = *c.field;

    computeDerivedCurves(*record);

    auto curvesMatch = [](const QVector<QPointF>& a, const QVector<QPointF>& b) {
        if (a.size() != b.size()) return false;
        constexpr double kEpsilon = 1e-6;
        for (qsizetype i = 0; i < a.size(); ++i) {
            if (std::abs(a[i].x() - b[i].x()) > kEpsilon) return false;
            if (std::abs(a[i].y() - b[i].y()) > kEpsilon) return false;
        }
        return true;
    };

    bool curvesChanged = false;
    for (const auto& c : curves) {
        if (!curvesMatch(c.stored, *c.field)) { curvesChanged = true; break; }
    }

    if (curvesChanged && outCorrectedBlob) {
        for (const auto& c : curves) root[c.jsonKey] = pointsToJsonObject(*c.field);
        *outCorrectedBlob = qCompress(QJsonDocument(root).toJson(QJsonDocument::Compact), 9);
    }
}

qint64 ShotHistoryStorage::saveShot(ShotDataModel* shotData,
                                     const Profile* profile,
                                     double duration,
                                     double finalWeight,
                                     double doseWeight,
                                     const ShotMetadata& metadata,
                                     const QString& debugLog,
                                     double temperatureOverride,
                                     double targetWeight,
                                     const QString& stoppedBy)
{
    if (!m_ready || m_backupInProgress || !shotData) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Cannot save shot - not ready, backup in progress, or no data";
        emit shotSavedWithUuid(-1, {});
        emit shotSaved(-1);
        return -1;
    }

    // Extract all data from QObject pointers on the main thread into a plain value struct
    ShotSaveData data;
    data.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    data.timestamp = QDateTime::currentSecsSinceEpoch();
    data.profileName = profile ? profile->title() : QStringLiteral("Unknown");
    data.profileJson = profile ? QString::fromUtf8(profile->toJson().toJson(QJsonDocument::Compact)) : QString();
    data.beverageType = profile ? profile->beverageType() : QStringLiteral("espresso");
    data.duration = duration;
    data.finalWeight = finalWeight;
    data.doseWeight = doseWeight;
    data.temperatureOverride = temperatureOverride;
    data.targetWeight = targetWeight;
    data.stoppedBy = stoppedBy;
    data.weightSource = shotData->weightSource();
    data.portalMountingMode = shotData->portalMountingMode();
    data.portalPresent = !shotData->portalSamples().isEmpty();
    data.beanBrand = metadata.beanBrand;
    data.beanType = metadata.beanType;
    data.roastDate = metadata.roastDate;
    data.roastLevel = metadata.roastLevel;
    data.grinderBrand = metadata.grinderBrand;
    data.grinderModel = metadata.grinderModel;
    data.grinderBurrs = metadata.grinderBurrs;
    data.grinderSetting = metadata.grinderSetting;
    data.equipmentId = metadata.equipmentId;
    data.rpm = metadata.rpm;
    data.drinkTds = metadata.drinkTds;
    data.drinkEy = metadata.drinkEy;
    data.espressoEnjoyment = metadata.espressoEnjoyment;
    data.espressoNotes = metadata.espressoNotes;
    data.barista = metadata.barista;
    data.profileNotes = profile ? profile->profileNotes() : QString();
    data.debugLog = debugLog;
    data.beanBaseJson = metadata.beanBaseJson;
    data.bagId = metadata.bagId;
    data.frozenDate = metadata.frozenDate;
    data.defrostDate = metadata.defrostDate;
    data.storageHint = metadata.storageHint;
    data.openedDate = metadata.openedDate;
    data.recipeId = metadata.recipeId;
    data.steamJson = metadata.steamJson;
    data.hotWaterJson = metadata.hotWaterJson;
    data.hotWaterResultJson = metadata.hotWaterResultJson;
    data.yieldMode = metadata.yieldMode;
    data.yieldAnchorValue = metadata.yieldAnchorValue;
    data.flowCalibration = metadata.flowCalibration;

    if (profile) {
        // A TITLE resolution only — see KbResolution::persistableId() for why a
        // shape match must not be written to this column. Nothing regresses
        // relative to main: a title-unresolvable profile stored an empty id
        // there too, and the shape facts still reach the shot on load.
        data.profileKbId = resolveProfileKb(*profile).persistableId();
    }

    // Compute conductance derivative (post-shot Gaussian smoothing) before compression
    shotData->computeConductanceDerivative();

    // Compute quality flags and phase summaries. Uses ShotSummarizer::getAnalysisFlags()
    // for KB flag lookups and ShotAnalysis helpers for detection. Runs on the main
    // thread before data is handed to the background save thread.
    // Build a temporary ShotRecord from the live data to reuse the static helpers.
    {
        ShotRecord tmpRecord;
        tmpRecord.pressure = shotData->pressureData();
        tmpRecord.flow = shotData->flowData();
        tmpRecord.temperature = shotData->temperatureData();
        tmpRecord.weight = shotData->cumulativeWeightData();

        // Extract phase markers into the record
        QVariantList tmpMarkers = shotData->phaseMarkersVariant();
        for (const QVariant& mv : tmpMarkers) {
            QVariantMap m = mv.toMap();
            HistoryPhaseMarker pm;
            pm.time = m["time"].toDouble();
            pm.label = m["label"].toString();
            pm.frameNumber = m["frameNumber"].toInt();
            pm.isFlowMode = m["isFlowMode"].toBool();
            pm.transitionReason = m["transitionReason"].toString();
            tmpRecord.phases.append(pm);
        }

        // Compute phase summaries
        computePhaseSummaries(tmpRecord);
        data.phaseSummariesJson = tmpRecord.phaseSummariesJson;

        // Compute all four quality badges via a single ShotAnalysis::analyzeShot
        // pass and project the booleans from DetectorResults using the
        // documented mapping. This unifies the save-time, load-time, and
        // dialog/AI/MCP cascades on one pipeline — the cascade lives in exactly
        // one place (analyzeShot's body). See docs/SHOT_REVIEW.md §4 for the
        // full mapping table and decenza::deriveBadgesFromAnalysis (in
        // history/shotbadgeprojection.h) for the projection rules.
        const AnalysisInputs inputs = prepareAnalysisInputs(data.profileKbId, data.profileJson);
        // Read the gate off AnalysisInputs rather than deriving it from the
        // persisted id. The id is empty for a profile that resolved by SHAPE
        // (change: resolve-profile-kb-by-shape) — a re-tuned copy of a
        // documented profile, for which Arm 1's structural question is
        // answerable. Deriving from the id would gate Arm 1 off for exactly
        // the profiles this change exists to serve.
        const bool profileKbResolved = inputs.profileKbResolved;
        const auto analysis = ShotAnalysis::analyzeShot(
            tmpRecord.pressure, shotData->flowData(),
            shotData->cumulativeWeightData(),
            shotData->conductanceDerivativeData(),
            tmpRecord.phases, data.beverageType, duration,
            shotData->pressureGoalData(), shotData->flowGoalData(),
            inputs.analysisFlags, inputs.firstFrameSeconds,
            data.targetWeight, data.finalWeight,
            inputs.frameCount, inputs.expertBand,
            profileKbResolved);
        decenza::applyBadgesToTarget(data, analysis.detectors);

        if (data.portalPresent) {
            data.portalSummary = PortalTailAnalyzer::analyzeShot(
                shotData->portalSamples(), tmpRecord.weight, data.weightSource, data.portalMountingMode,
                data.doseWeight, data.finalWeight, shotData->pressureData()).persistedSummary();
        }
    }

    // Compress sample data on main thread (reads QObject data vectors)
    data.compressedSamples = compressSampleData(shotData, data.phaseSummariesJson);
    data.sampleCount = static_cast<int>(shotData->pressureData().size());

    // Extract phase markers on main thread
    QVariantList markers = shotData->phaseMarkersVariant();
    for (const QVariant& markerVar : markers) {
        QVariantMap marker = markerVar.toMap();
        HistoryPhaseMarker pm;
        pm.time = marker["time"].toDouble();
        pm.label = marker["label"].toString();
        pm.frameNumber = marker["frameNumber"].toInt();
        pm.isFlowMode = marker["isFlowMode"].toBool();
        pm.transitionReason = marker["transitionReason"].toString();
        data.phaseMarkers.append(pm);
    }

    // Run DB work on background thread
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, ownedData = std::move(data), destroyed]() {
        qint64 shotId = saveShotStatic(dbPath, ownedData);

        // Capture only the fields needed for logging (avoid copying the large compressedSamples blob)
        const QString shotUuid = ownedData.uuid;
        QString profileName = ownedData.profileName;
        double shotDuration = ownedData.duration;
        int sampleCount = ownedData.sampleCount;
        qsizetype compressedSize = ownedData.compressedSamples.size();

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotId, destroyed,
                                         profileName, shotDuration, sampleCount, compressedSize, shotUuid]() {
            if (*destroyed) {
                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "saveShot callback dropped (object destroyed)";
                return;
            }

            if (shotId > 0) {
                m_lastSavedShotId = shotId;
                refreshTotalShots();

                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Saved shot" << shotId
                         << "- Profile:" << profileName
                         << "- Duration:" << shotDuration << "s"
                         << "- Samples:" << sampleCount
                         << "- Compressed size:" << compressedSize << "bytes";
            } else {
                emit errorOccurred("Failed to save shot to database");
            }

            emit shotSavedWithUuid(shotId, shotUuid);
            emit shotSaved(shotId);
        }, Qt::QueuedConnection);
    });

    return 0;  // Async — actual shotId delivered via shotSaved signal
}

qint64 ShotHistoryStorage::saveShotStatic(const QString& dbPath, const ShotSaveData& data)
{
    if (data.uuid.isEmpty() || data.timestamp <= 0) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "saveShotStatic: invalid data - uuid empty or timestamp zero";
        return -1;
    }

    qint64 shotId = -1;
    withTempDb(dbPath, "shs_save", [&](QSqlDatabase& db) {
        // A transient SQLITE_BUSY/locked from a concurrent writer (the post-shot
        // bags_update stamp, reconciliation drain, daily backup, WAL checkpoint)
        // must not drop a real shot. BEGIN IMMEDIATE (below) lets busy_timeout
        // absorb that contention; the retry loop is a backstop for the rare case a
        // writer is held past busy_timeout — retry the transaction before giving up.
        // attemptSave runs one full INSERT transaction. Returns true on commit;
        // on a lock-induced failure it sets `locked` so the caller retries.
        auto attemptSave = [&](bool& locked) -> bool {
            shotId = -1;
            locked = false;
            // BEGIN IMMEDIATE takes the write lock up front. db.transaction()'s plain
            // BEGIN is DEFERRED — it acquires a read lock first, so the INSERT then has
            // to upgrade while a concurrent writer (e.g. the post-shot bags_update bag
            // stamp) holds the lock, and SQLite returns SQLITE_BUSY immediately without
            // waiting out busy_timeout (deadlock avoidance). With IMMEDIATE the busy
            // handler rides out the brief writer, so the lock no longer surfaces.
            QSqlQuery beginQuery(db);
            if (!beginQuery.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
                locked = isSqliteLockError(beginQuery.lastError());
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to start transaction:" << beginQuery.lastError().text();
                return false;
            }

            QSqlQuery query(db);
            query.prepare(R"(
                INSERT INTO shots (
                    uuid, timestamp, profile_name, profile_json, beverage_type,
                    duration_seconds, final_weight, dose_weight,
                    bean_brand, bean_type, roast_date, roast_level,
                    grinder_setting,
                    equipment_id, rpm,
                    drink_tds, drink_ey, enjoyment, espresso_notes, bean_notes, barista,
                    profile_notes, debug_log,
                    temperature_override, yield_override, yield_mode, yield_anchor_value,
                    profile_kb_id,
                    channeling_detected, grind_issue_detected,
                    skip_first_frame_detected, pour_truncated_detected,
                    stopped_by, beanbase_json, beanbase_id,
                    bag_id, frozen_date, defrost_date, storage_hint, opened_date,
                    recipe_id, steam_json, hot_water_json,
                    flow_calibration, hot_water_result_json,
                    portal_mean_ec, portal_integral_ec_g, portal_mean_temp_c,
                    portal_estimate_reliable, portal_mounting_mode, weight_source, portal_present
                ) VALUES (
                    :uuid, :timestamp, :profile_name, :profile_json, :beverage_type,
                    :duration, :final_weight, :dose_weight,
                    :bean_brand, :bean_type, :roast_date, :roast_level,
                    :grinder_setting,
                    :equipment_id, :rpm,
                    :drink_tds, :drink_ey, :enjoyment, :espresso_notes, :bean_notes, :barista,
                    :profile_notes, :debug_log,
                    :temperature_override, :yield_override, :yield_mode, :yield_anchor_value,
                    :profile_kb_id,
                    :channeling_detected, :grind_issue_detected,
                    :skip_first_frame_detected, :pour_truncated_detected,
                    :stopped_by, :beanbase_json, :beanbase_id,
                    :bag_id, :frozen_date, :defrost_date, :storage_hint, :opened_date,
                    :recipe_id, :steam_json, :hot_water_json,
                    :flow_calibration, :hot_water_result_json,
                    :portal_mean_ec, :portal_integral_ec_g, :portal_mean_temp_c,
                    :portal_estimate_reliable, :portal_mounting_mode, :weight_source, :portal_present
                )
            )");

            query.bindValue(":uuid", data.uuid);
            query.bindValue(":timestamp", data.timestamp);
            query.bindValue(":profile_name", data.profileName);
            query.bindValue(":profile_json", data.profileJson);
            query.bindValue(":beverage_type", data.beverageType);
            query.bindValue(":duration", data.duration);
            query.bindValue(":final_weight", data.finalWeight);
            query.bindValue(":dose_weight", data.doseWeight);
            query.bindValue(":bean_brand", data.beanBrand);
            query.bindValue(":bean_type", data.beanType);
            query.bindValue(":roast_date", data.roastDate);
            query.bindValue(":roast_level", data.roastLevel);
            // Grinder identity (brand/model/burrs) is no longer snapshotted on the
            // shot — it resolves through equipment_id to the package's immutable
            // grinder item (migration 23). Only the per-shot dial-in is stored.
            query.bindValue(":grinder_setting", data.grinderSetting);
            query.bindValue(":equipment_id", data.equipmentId > 0 ? QVariant(data.equipmentId) : QVariant());
            query.bindValue(":rpm", data.rpm > 0 ? QVariant(data.rpm) : QVariant());
            query.bindValue(":drink_tds", data.drinkTds);
            query.bindValue(":drink_ey", data.drinkEy);
            query.bindValue(":enjoyment", data.espressoEnjoyment);
            query.bindValue(":espresso_notes", data.espressoNotes);
            query.bindValue(":bean_notes", QString());
            query.bindValue(":barista", data.barista);
            query.bindValue(":profile_notes", data.profileNotes);
            query.bindValue(":debug_log", data.debugLog);
            query.bindValue(":temperature_override", data.temperatureOverride);
            query.bindValue(":yield_override", data.targetWeight);
            // The anchor that produced the target (intent) rides alongside the
            // resolved grams (outcome). Mode always stores explicitly — 'none'
            // included — so NULL stays the "unconverted" marker for migration
            // 34's backfill and legacy-source imports.
            query.bindValue(":yield_mode", YieldSpec::normalizedMode(data.yieldMode));
            query.bindValue(":yield_anchor_value",
                            data.yieldAnchorValue > 0 ? QVariant(data.yieldAnchorValue) : QVariant());
            // NULL, not 0, when nothing was latched: 0 is not a possible
            // multiplier and 1.0 is, so "unknown" needs its own value.
            query.bindValue(":flow_calibration",
                            data.flowCalibration > 0 ? QVariant(data.flowCalibration) : QVariant());
            query.bindValue(":profile_kb_id", data.profileKbId.isEmpty() ? QVariant() : data.profileKbId);
            query.bindValue(":channeling_detected", data.channelingDetected ? 1 : 0);
            query.bindValue(":grind_issue_detected", data.grindIssueDetected ? 1 : 0);
            query.bindValue(":skip_first_frame_detected", data.skipFirstFrameDetected ? 1 : 0);
            query.bindValue(":pour_truncated_detected", data.pourTruncatedDetected ? 1 : 0);
            query.bindValue(":stopped_by", data.stoppedBy);
            query.bindValue(":beanbase_json", data.beanBaseJson.isEmpty() ? QVariant() : data.beanBaseJson);
            // beanbase_id is the indexed canonical UUID for the history
            // search lane — derived from the same blob the row stores.
            {
                const QString canonicalId = BeanBaseBlob::canonicalId(data.beanBaseJson);
                query.bindValue(":beanbase_id", canonicalId.isEmpty() ? QVariant() : canonicalId);
            }
            query.bindValue(":bag_id", bagIdIsSet(data.bagId) ? QVariant(data.bagId) : QVariant());
            query.bindValue(":frozen_date", data.frozenDate.isEmpty() ? QVariant() : data.frozenDate);
            query.bindValue(":defrost_date", data.defrostDate.isEmpty() ? QVariant() : data.defrostDate);
            query.bindValue(":storage_hint", data.storageHint.isEmpty() ? QVariant() : data.storageHint);
            query.bindValue(":opened_date", data.openedDate.isEmpty() ? QVariant() : data.openedDate);
            query.bindValue(":recipe_id", data.recipeId > 0 ? QVariant(data.recipeId) : QVariant());
            query.bindValue(":steam_json", data.steamJson.isEmpty() ? QVariant() : data.steamJson);
            const QString waterSpec = ShotWaterResult::fromJson(data.hotWaterResultJson)
                .specWithCapturedTarget(data.hotWaterJson);
            query.bindValue(":hot_water_json", waterSpec.isEmpty() ? QVariant() : waterSpec);
            query.bindValue(":hot_water_result_json", data.hotWaterResultJson.isEmpty() ? QVariant() : data.hotWaterResultJson);
            bindPortalColumns(query, data.portalSummary, data.portalMountingMode, data.weightSource,
                              data.portalPresent);

            if (!query.exec()) {
                locked = isSqliteLockError(query.lastError());
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to insert shot:" << query.lastError().text()
                           << "(sqlite code" << query.lastError().nativeErrorCode() << ")";
                QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
                return false;
            }

            shotId = query.lastInsertId().toLongLong();

            // Insert compressed sample data
            query.prepare("INSERT INTO shot_samples (shot_id, sample_count, data_blob) VALUES (:id, :count, :blob)");
            query.bindValue(":id", shotId);
            query.bindValue(":count", data.sampleCount);
            query.bindValue(":blob", data.compressedSamples);

            if (!query.exec()) {
                locked = isSqliteLockError(query.lastError());
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to insert samples:" << query.lastError().text();
                QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
                shotId = -1;
                return false;
            }

            // Insert phase markers
            for (const HistoryPhaseMarker& pm : data.phaseMarkers) {
                query.prepare(R"(
                    INSERT INTO shot_phases (shot_id, time_offset, label, frame_number, is_flow_mode, transition_reason)
                    VALUES (:shot_id, :time, :label, :frame, :flow_mode, :reason)
                )");
                query.bindValue(":shot_id", shotId);
                query.bindValue(":time", pm.time);
                query.bindValue(":label", pm.label);
                query.bindValue(":frame", pm.frameNumber);
                query.bindValue(":flow_mode", pm.isFlowMode ? 1 : 0);
                query.bindValue(":reason", pm.transitionReason);
                query.exec();  // Non-critical if markers fail
            }

            // COMMIT. On a transient lock SQLite keeps the transaction staged, so
            // retry the COMMIT itself rather than rolling back — a ROLLBACK here
            // would discard the inserted row + sample blob + phase markers and force
            // the outer loop to redo the whole insert. (Rare in WAL after BEGIN
            // IMMEDIATE; only a non-lock error or exhausting the commit retries
            // falls through to ROLLBACK + a full-transaction retry.)
            QSqlQuery commitQuery(db);
            for (int commitTry = 1; !commitQuery.exec(QStringLiteral("COMMIT")); ++commitTry) {
                const bool commitLocked = isSqliteLockError(commitQuery.lastError());
                if (!commitLocked || commitTry >= 4) {
                    locked = commitLocked;
                    DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to commit shot:"
                               << commitQuery.lastError().text();
                    QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
                    shotId = -1;
                    return false;
                }
                QThread::msleep(static_cast<unsigned long>(50 * commitTry));
            }

            // Checkpoint WAL
            QSqlQuery walQuery(db);
            walQuery.exec("PRAGMA wal_checkpoint(PASSIVE)");
            return true;
        };

        // Retry only a lock-induced miss; success or a real error (constraint,
        // etc.) exits immediately. Short escalating backoff lets the competing
        // writer finish before the next attempt.
        for (int attempt = 1; attempt <= 4; ++attempt) {
            bool locked = false;
            if (attemptSave(locked) || !locked)
                break;
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "shot save hit a transient lock, retrying ("
                       << attempt << "of 4)";
            QThread::msleep(static_cast<unsigned long>(50 * attempt));
        }
    });

    return shotId;
}

void ShotHistoryStorage::requestUpdateVisualizerInfo(qint64 shotId, const QString& visualizerId, const QString& visualizerUrl)
{
    if (!m_ready) {
        emit visualizerInfoUpdated(shotId, false);
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, shotId, visualizerId, visualizerUrl, destroyed]() {
        bool success = false;
        bool opened = withTempDb(dbPath, "shs_vizupd", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            if (!query.prepare("UPDATE shots SET visualizer_id = :viz_id, visualizer_url = :viz_url, "
                               "updated_at = strftime('%s', 'now') WHERE id = :id")) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to prepare visualizer update:" << query.lastError().text();
                return;
            }
            query.bindValue(":viz_id", visualizerId);
            query.bindValue(":viz_url", visualizerUrl);
            query.bindValue(":id", shotId);
            success = query.exec();
            if (!success)
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to async update visualizer info:" << query.lastError().text();
        });
        if (!opened)
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "requestUpdateVisualizerInfo failed - could not open DB for shot" << shotId;

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotId, success, destroyed]() {
            if (*destroyed) return;
            if (success)
                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Async updated visualizer info for shot" << shotId;
            else
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Async visualizer info update FAILED for shot" << shotId;
            emit visualizerInfoUpdated(shotId, success);
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::runUploadStateWrite(qint64 shotId, const QString& what, const char* ifLost,
                                             std::function<bool(QSqlQuery&)> bindAndExec,
                                             std::function<void(bool)> report)
{
    if (!m_ready) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << what << "write skipped for shot" << shotId
                                                 << "- database not ready;" << ifLost;
        report(false);
        return;
    }
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, destroyed, shotId, what, ifLost, bindAndExec, report]() {
        bool success = false;
        const bool opened = withTempDb(dbPath, "shs_upload", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            success = bindAndExec(query);
            if (!success)
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << what << "write failed for shot" << shotId
                                                         << "-" << query.lastError().text() << ";" << ifLost;
        });
        if (!opened)
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << what << "write: could not open DB for shot" << shotId
                                                     << ";" << ifLost;
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [success, destroyed, report]() {
            if (!*destroyed) report(success);
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::runDecentStateWrite(qint64 shotId, const char* what, const char* ifLost,
                                             std::function<bool(QSqlQuery&)> bindAndExec)
{
    runUploadStateWrite(shotId, QStringLiteral("Decent %1").arg(QLatin1StringView(what)), ifLost,
                        std::move(bindAndExec), [this, shotId](bool success) {
        emit decentUploadStateUpdated(shotId, success);
    });
}

void ShotHistoryStorage::requestRecordUploadOutcome(qint64 shotId, const QString& destination,
                                                    UploadOutcome outcome, int httpStatus)
{
    // Column names are built from the destination, so only known ones are accepted.
    if (destination != QLatin1String("decent") && destination != QLatin1String("visualizer")) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "upload outcome for shot" << shotId
                                                 << "not recorded: unknown destination" << destination;
        emit uploadOutcomeUpdated(shotId, false);
        return;
    }
    QString set;
    switch (outcome) {
    case UploadOutcome::Sent:
        set = QStringLiteral("%1_failed_at = NULL, %1_rejected_at = NULL, %1_rejected_status = NULL");
        break;
    case UploadOutcome::Failed:
        set = QStringLiteral("%1_failed_at = strftime('%s', 'now')");
        break;
    case UploadOutcome::Rejected:
        set = QStringLiteral("%1_rejected_at = strftime('%s', 'now'), %1_rejected_status = :status, %1_failed_at = NULL");
        break;
    }
    const QString sql = QStringLiteral("UPDATE shots SET %1 WHERE id = :id").arg(set.arg(destination));
    runUploadStateWrite(shotId, destination + QStringLiteral(" upload outcome"),
                        "the shot's upload status is out of date until its next upload",
                        [sql, shotId, outcome, httpStatus](QSqlQuery& q) {
        if (!q.prepare(sql)) return false;
        if (outcome == UploadOutcome::Rejected) q.bindValue(":status", httpStatus);
        q.bindValue(":id", shotId);
        return q.exec();
    }, [this, shotId, destination](bool success) {
        emit uploadOutcomeUpdated(shotId, success);
        // The shot pages show Decent's rejection from its upload state.
        if (destination == QLatin1String("decent")) emit decentUploadStateUpdated(shotId, success);
    });
}

void ShotHistoryStorage::requestClearUploadRejections(qint64 shotId)
{
    runUploadStateWrite(shotId, QStringLiteral("upload rejection clear"),
                        "the shot stays out of Upload missing shots until it is uploaded",
                        [shotId](QSqlQuery& q) {
        if (!q.prepare("UPDATE shots SET decent_rejected_at = NULL, decent_rejected_status = NULL, "
                       "visualizer_rejected_at = NULL, visualizer_rejected_status = NULL "
                       "WHERE id = :id AND (decent_rejected_at IS NOT NULL OR visualizer_rejected_at IS NOT NULL)"))
            return false;
        q.bindValue(":id", shotId);
        return q.exec();
    }, [this, shotId](bool success) {
        emit uploadOutcomeUpdated(shotId, success);
        emit decentUploadStateUpdated(shotId, success);
    });
}

void ShotHistoryStorage::requestRecordDecentUpload(qint64 shotId, const QString& serverShotId, const QString& serial,
                                                   bool stillPending)
{
    runDecentStateWrite(shotId, "upload", "the shot is in the Decent account, but an edit will go as a new upload",
                        [shotId, serverShotId, serial, stillPending](QSqlQuery& q) {
        if (!q.prepare("UPDATE shots SET decent_uploaded_at = strftime('%s', 'now'), decent_shot_id = :sid, "
                       "decent_serial = :sn, decent_replace_pending = :pending, decent_rejected_status = NULL, "
                       "decent_rejected_at = NULL WHERE id = :id"))
            return false;
        q.bindValue(":sid", serverShotId);
        q.bindValue(":sn", serial);
        q.bindValue(":pending", stillPending ? 1 : 0);
        q.bindValue(":id", shotId);
        return q.exec();
    });
}

void ShotHistoryStorage::requestMarkDecentReplacePending(qint64 shotId)
{
    runDecentStateWrite(shotId, "replace-pending", "the unsent edit is shown only until the page closes",
                        [shotId](QSqlQuery& q) {
        if (!q.prepare("UPDATE shots SET decent_replace_pending = 1 WHERE id = :id AND decent_uploaded_at IS NOT NULL"))
            return false;
        // Every edit comes here; a shot not on Decent is the common case, and changes nothing.
        q.bindValue(":id", shotId);
        return q.exec();
    });
}

bool ShotHistoryStorage::loadDecentUploadStateStatic(QSqlDatabase& db, qint64 shotId, DecentUploadState* out)
{
    QSqlQuery q(db);
    // A failed prepare must be reported here: exec() would replace its error
    // with "Parameter count mismatch" (qsql_sqlite.cpp:565-567).
    if (!q.prepare("SELECT decent_uploaded_at, decent_shot_id, decent_serial, decent_replace_pending, "
                   "decent_rejected_status, decent_rejected_at FROM shots WHERE id = :id")) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Decent upload state of shot" << shotId
                                                 << "unreadable:" << q.lastError().text();
        return false;
    }
    q.bindValue(":id", shotId);
    if (!q.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Decent upload state of shot" << shotId
                                                 << "unreadable:" << q.lastError().text();
        return false;
    }
    if (!q.next()) return false;
    out->uploadedAt = q.value(0).toLongLong();
    out->serverShotId = q.value(1).toString();
    out->serial = q.value(2).toString();
    out->replacePending = q.value(3).toInt() != 0;
    out->rejectedStatus = q.value(4).toInt();
    out->rejectedAt = q.value(5).toLongLong();
    return true;
}

void ShotHistoryStorage::requestDecentUploadState(qint64 shotId)
{
    requestDecentUploadStateImpl({}, shotId);
}

void ShotHistoryStorage::requestDecentUploadStateForRequest(const QString& requestId, qint64 shotId)
{
    requestDecentUploadStateImpl(requestId, shotId);
}

void ShotHistoryStorage::requestDecentUploadStateImpl(const QString& requestId, qint64 shotId)
{
    if (!m_ready || shotId <= 0) {
        if (!requestId.isEmpty())
            emit decentUploadStateRequestReady(requestId, shotId, {},
                                               !m_ready ? QStringLiteral("notReady")
                                                        : QStringLiteral("invalidShotId"));
        return;
    }
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, shotId, requestId, destroyed]() {
        DecentUploadState state;
        bool found = false;
        const bool opened = withTempDb(dbPath, "shs_decentread", [&](QSqlDatabase& db) {
            found = loadDecentUploadStateStatic(db, shotId, &state);
        });
        if (*destroyed || (requestId.isEmpty() && !found)) return;
        const QString reason = !opened ? QStringLiteral("unavailable")
                             : !found ? QStringLiteral("notFound") : QString();
        QMetaObject::invokeMethod(this, [this, shotId, requestId, state, reason, destroyed]() {
            if (*destroyed) return;
            const QVariantMap result{
                {QStringLiteral("uploaded"), state.uploaded()},
                {QStringLiteral("serverShotId"), state.serverShotId},
                {QStringLiteral("serial"), state.serial},
                {QStringLiteral("replacePending"), state.replacePending},
                {QStringLiteral("rejected"), state.rejected()},
                {QStringLiteral("rejectedStatus"), state.rejectedStatus},
            };
            if (requestId.isEmpty())
                emit decentUploadStateReady(shotId, result);
            else
                emit decentUploadStateRequestReady(requestId, shotId, result, reason);
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::requestClearStaleVisualizerLink(qint64 shotId, const QString& staleVisualizerId)
{
    if (!m_ready || shotId <= 0 || staleVisualizerId.isEmpty()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "stale visualizer link NOT cleared for shot" << shotId
                   << "(not ready or bad args) — dead link may remain on the row";
        return;
    }

    const QString dbPath = m_dbPath;
    runOnDbThread([dbPath, shotId, staleVisualizerId]() {
        bool success = false;
        int rowsChanged = 0;
        bool opened = withTempDb(dbPath, "shs_vizclear", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            // The visualizer_id predicate is the guard: no-op if the row's
            // link was replaced since the stale id was queued.
            if (!query.prepare("UPDATE shots SET visualizer_id = '', visualizer_url = '', "
                               "updated_at = strftime('%s', 'now') "
                               "WHERE id = :id AND visualizer_id = :stale_id")) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to prepare stale link clear:" << query.lastError().text();
                return;
            }
            query.bindValue(":id", shotId);
            query.bindValue(":stale_id", staleVisualizerId);
            success = query.exec();
            if (success)
                rowsChanged = query.numRowsAffected();
            else
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to clear stale visualizer link:" << query.lastError().text();
        });
        if (!opened || !success)
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "stale visualizer link clear FAILED for shot" << shotId
                       << "— dead link" << staleVisualizerId << "remains on the row";
        else if (rowsChanged > 0)
            DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "cleared stale visualizer link" << staleVisualizerId
                     << "on shot" << shotId;
        else
            DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "shot" << shotId << "no longer holds visualizer link"
                     << staleVisualizerId << "— nothing to clear (replaced meanwhile)";
    });
}

void ShotHistoryStorage::requestPendingBeanRepairs()
{
    if (!m_ready) {
        emit pendingBeanRepairsReady(false, {});
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, destroyed]() {
        QVector<BeanRepair> repairs;
        // withTempDb reports whether the DATABASE OPENED, not whether the work
        // inside succeeded — so the query's own verdict has to come out
        // separately. Without this, a failed SELECT (the column missing on a
        // database whose ALTER failed) emitted `ok=true, repairs={}`, which is
        // bit-for-bit "the queue is empty, all good": the user's shots stay
        // renamed forever and every boot reports success.
        bool queryOk = false;
        const bool opened = withTempDb(dbPath, "shs_beanrepair", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            if (!query.exec("SELECT id, visualizer_id, bean_brand, bean_type, beanbase_json, timestamp "
                            "FROM shots WHERE bean_repair_pending = 1 "
                            "AND COALESCE(visualizer_id,'') <> '' ORDER BY timestamp DESC")) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "pending bean-repair query failed:"
                           << query.lastError().text();
                return;
            }
            while (query.next()) {
                const QString blob = query.value(4).toString();
                BeanRepair repair;
                repair.shotId = query.value(0).toLongLong();
                repair.visualizerId = query.value(1).toString();
                repair.beanBrand = query.value(2).toString();
                repair.beanType = query.value(3).toString();
                repair.timestamp = query.value(5).toLongLong();
                // The canonical id goes back up only when it names THIS coffee;
                // otherwise it is cleared, which is what stops the server
                // rewriting the names we are about to send.
                repair.canonicalId = BeanBaseBlob::canonicalId(blob);
                if (BeanBaseBlob::canonicalIdentityConflicts(blob, {repair.beanBrand, repair.beanType}))
                    repair.canonicalId.clear();
                repairs.append(repair);
            }
            queryOk = true;
            // The query's visualizer_id filter mirrors the producer's
            // (markShotsForBeanRepairStatic flags uploaded shots only), so a
            // flagged row without an id cannot exist today. It is kept rather
            // than dropped because the two predicates must agree, and a future
            // producer that forgets the filter should skip such a row here, not
            // send it: an empty id addresses the COLLECTION endpoint.
        });
        const bool ok = opened && queryOk;
        if (!ok)
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "pending bean-repair read FAILED -"
                       << (opened ? "query error" : "could not open DB")
                       << "- no repair will run this session";

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, ok, repairs, destroyed]() {
            if (*destroyed) return;
            if (ok && !repairs.isEmpty())
                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << repairs.size()
                         << "shot(s) queued for Visualizer bean repair";
            emit pendingBeanRepairsReady(ok, repairs);
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::clearBeanRepairPending(qint64 shotId)
{
    if (!m_ready || shotId <= 0) {
        // The server confirmed this shot needs nothing; failing to record that
        // means repairing it again on the next boot, so it is not silent.
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "cannot clear the repair flag on shot" << shotId
                   << "- storage not ready; it will be re-checked next boot";
        return;
    }
    const QString dbPath = m_dbPath;
    runOnDbThread([dbPath, shotId]() {
        const bool opened = withTempDb(dbPath, "shs_beanrepairdone", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            query.prepare("UPDATE shots SET bean_repair_pending = 0 WHERE id = :id");
            query.bindValue(":id", shotId);
            if (!query.exec())
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "could not clear repair flag on shot" << shotId
                           << ":" << query.lastError().text();
            else if (query.numRowsAffected() == 0)
                // A successful UPDATE matching nothing is not a clear: the shot
                // was deleted locally, or the id never existed. Without this the
                // two are indistinguishable, and the second is a producer bug
                // that would otherwise leave a flag set with no trace of why.
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "repair flag clear matched no shot" << shotId
                           << "- deleted locally, or the id was never valid";
        });
        if (!opened)
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "repair flag on shot" << shotId
                       << "not cleared - DB open failed; it will be re-checked next boot";
    });
}

void ShotHistoryStorage::acknowledgeBagGrindNotice()
{
    if (!m_bagGrindNoticePending)
        return;
    m_bagGrindNoticePending = false;
    const QString dbPath = m_dbPath;
    runOnDbThread([dbPath]() {
        const bool opened = withTempDb(dbPath, "shs_baggrindnotice", [](QSqlDatabase& db) {
            QSqlQuery query(db);
            if (query.exec("DELETE FROM bag_grind_notice"))
                DIAG_INFO(STORAGE, "ShotHistoryStorage") << "bag grind upgrade notice acknowledged";
            else
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "could not record the bag grind notice as read:"
                           << query.lastError().text() << "- it will show again next launch";
        });
        if (!opened)
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "bag grind notice not recorded as read - DB open failed;"
                       << "it will show again next launch";
    });
}

void ShotHistoryStorage::requestMostRecentShotId()
{
    requestMostRecentShotIdImpl({});
}

void ShotHistoryStorage::requestMostRecentShotIdForRequest(const QString& requestId)
{
    if (!requestId.isEmpty())
        requestMostRecentShotIdImpl(requestId);
}

void ShotHistoryStorage::requestMostRecentShotIdImpl(const QString& requestId)
{
    if (!m_ready) {
        if (requestId.isEmpty())
            emit mostRecentShotIdReady(-1);
        else
            emit mostRecentShotIdRequestReady(requestId, -1, QStringLiteral("notReady"));
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, requestId, destroyed]() {
        qint64 shotId = -1;
        bool queryOk = false;
        const bool opened = withTempDb(dbPath, "shs_recent", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            queryOk = query.exec("SELECT id FROM shots ORDER BY timestamp DESC LIMIT 1");
            if (queryOk && query.next())
                shotId = query.value(0).toLongLong();
        });
        if (!opened)
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "requestMostRecentShotId failed - could not open DB";

        // The old broadcast intentionally skips open failures so a transient
        // failure cannot masquerade as an empty history. The correlated API
        // reports the failure explicitly to its caller.
        if (*destroyed || (!opened && requestId.isEmpty()))
            return;
        const QString reason = !opened ? QStringLiteral("unavailable")
                             : (!queryOk && !requestId.isEmpty())
                                   ? QStringLiteral("queryFailed") : QString();
        QMetaObject::invokeMethod(this, [this, shotId, requestId, reason, destroyed]() {
            if (*destroyed)
                return;
            if (requestId.isEmpty())
                emit mostRecentShotIdReady(shotId);
            else
                emit mostRecentShotIdRequestReady(requestId, shotId, reason);
        }, Qt::QueuedConnection);
    });
}

qint64 ShotHistoryStorage::previousShotIdStatic(QSqlDatabase& db, qint64 shotId)
{
    QSqlQuery q(db);
    q.prepare("SELECT profile_kb_id, profile_name, equipment_id, timestamp, beverage_type "
              "FROM shots WHERE id = ?");
    q.addBindValue(shotId);
    if (!q.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Previous shot of" << shotId << "unreadable:" << q.lastError().text();
        return 0;
    }
    if (!q.next()) return 0;
    const QString kbId = q.value(0).toString();
    const QString name = q.value(1).toString();
    const qint64 equipmentId = q.value(2).toLongLong();
    const qint64 timestamp = q.value(3).toLongLong();
    const QString beverage = q.value(4).toString();
    // No profile identity at all: an empty name would match every other
    // unnamed shot, which is not "the previous shot on this setup".
    if (kbId.isEmpty() && name.trimmed().isEmpty()) return 0;

    // Same profile the way advisor threads key it — the knowledge-base id
    // when the shot resolved one, else its name — same drink, same package.
    // A row with no kb id (saved before migration 9 added it) is matched by
    // name, so a new shot still finds an old one of the same profile.
    QSqlQuery p(db);
    p.prepare(QStringLiteral(
        "SELECT id FROM shots WHERE %1 AND COALESCE(beverage_type, '') = ? "
        "AND COALESCE(equipment_id, 0) = ? "
        "AND timestamp < ? AND id != ? ORDER BY timestamp DESC LIMIT 1")
        .arg(kbId.isEmpty() ? QStringLiteral("profile_name = ?")
                            : QStringLiteral("(profile_kb_id = ? OR (COALESCE(profile_kb_id, '') = '' "
                                             "AND profile_name = ?))")));
    if (kbId.isEmpty()) {
        p.addBindValue(name);
    } else {
        p.addBindValue(kbId);
        p.addBindValue(name);
    }
    p.addBindValue(beverage);
    p.addBindValue(equipmentId);
    p.addBindValue(timestamp);
    p.addBindValue(shotId);
    if (!p.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Previous shot of" << shotId << "unreadable:" << p.lastError().text();
        return 0;
    }
    return p.next() ? p.value(0).toLongLong() : 0;
}

qint64 ShotHistoryStorage::neighbourShotIdStatic(QSqlDatabase& db, qint64 shotId, bool newer)
{
    QSqlQuery q(db);
    q.prepare(newer ? QStringLiteral("SELECT n.id FROM shots s JOIN shots n ON n.timestamp > s.timestamp OR (n.timestamp = s.timestamp AND n.id > s.id) "
                                     "WHERE s.id = ? ORDER BY n.timestamp ASC, n.id ASC LIMIT 1")
                    : QStringLiteral("SELECT n.id FROM shots s JOIN shots n ON n.timestamp < s.timestamp OR (n.timestamp = s.timestamp AND n.id < s.id) "
                                     "WHERE s.id = ? ORDER BY n.timestamp DESC, n.id DESC LIMIT 1"));
    q.addBindValue(shotId);
    if (!q.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Neighbour of shot" << shotId << "unreadable:" << q.lastError().text();
        return 0;
    }
    return q.next() ? q.value(0).toLongLong() : 0;
}

QVariantMap ShotHistoryStorage::shotOutcomeStatic(QSqlDatabase& db, qint64 shotId)
{
    const ShotRecord shot = loadShotRecordStatic(db, shotId, nullptr, Q_FUNC_INFO);
    if (shot.summary.id == 0) return {};
    QList<ShotProjection> pair;
    const qint64 previousId = previousShotIdStatic(db, shotId);
    QString previousWhen;
    if (previousId > 0) {
        // The previous shot is comparison context: its PORTAL analysis stays uncalibrated.
        const ShotRecord previous = loadShotRecordStatic(db, previousId, nullptr, Q_FUNC_INFO,
                                                         PortalCalibration::Skip);
        if (previous.summary.id != 0) {
            pair << convertShotRecord(previous);
            previousWhen = shortDateTime(previous.summary.timestamp);
        }
    }
    pair << convertShotRecord(shot);
    return QVariantMap{
        { QStringLiteral("previousShotId"), pair.size() > 1 ? previousId : qint64(0) },
        { QStringLiteral("previousDateTime"), previousWhen },
        { QStringLiteral("comparison"), ShotComparison::compare(pair, 0, ShotComparison::MetricRows::Defaults).toVariantMap() },
        { QStringLiteral("portalAnalysis"), pair.last().portalAnalysis },
    };
}

void ShotHistoryStorage::requestShotOutcome(qint64 shotId)
{
    requestShotOutcomeImpl({}, shotId);
}

void ShotHistoryStorage::requestShotOutcomeForRequest(const QString& requestId, qint64 shotId)
{
    requestShotOutcomeImpl(requestId, shotId);
}

void ShotHistoryStorage::requestShotOutcomeImpl(const QString& requestId, qint64 shotId)
{
    if (!m_ready) {
        if (!requestId.isEmpty())
            emit shotOutcomeRequestReady(requestId, shotId, {}, QStringLiteral("notReady"));
        return;
    }
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, shotId, requestId, destroyed]() {
        QVariantMap outcome;
        const bool opened = withTempDb(dbPath, "shs_outcome", [&](QSqlDatabase& db) {
            outcome = shotOutcomeStatic(db, shotId);
        });
        if (*destroyed) return;
        const QString reason = !opened ? QStringLiteral("unavailable")
                             : outcome.isEmpty() ? QStringLiteral("notFound") : QString();
        if (!opened || outcome.isEmpty())
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Shot outcome of" << shotId << "unavailable";
        QMetaObject::invokeMethod(this, [this, shotId, requestId, outcome, reason, destroyed]() {
            if (*destroyed) return;
            if (requestId.isEmpty())
                emit shotOutcomeReady(shotId, outcome);
            else
                emit shotOutcomeRequestReady(requestId, shotId, outcome, reason);
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::requestShot(qint64 shotId)
{
    if (!m_ready) {
        emit shotReady(shotId, ShotProjection());
        return;
    }

    const QString dbPath = m_dbPath;

    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, shotId, destroyed]() {
        ShotRecord record;
        bool badgesPersisted = false;
        const bool opened = withTempDb(dbPath, "shs_shot", [&](QSqlDatabase& db) {
            record = loadShotRecordStatic(db, shotId, &badgesPersisted, Q_FUNC_INFO);
        });
        // On a DB-open failure `record` is default/invalid; do NOT deliver it as a
        // shotReady, which a reader would take as "shot no longer exists". A
        // genuine not-found (db opened, row absent) still emits.
        if (!opened)
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "requestShot — DB open failed for shot"
                       << shotId << "(no shotReady emitted)";

        // Convert to QVariantMap on main thread (touches QML-visible data).
        // shotReady carries the recomputed badges already; shotBadgesUpdated
        // fires only when the load actually rewrote the stored columns, so
        // listeners that care about "this shot just got its badges corrected"
        // (e.g., a future history-list filter that wants to refresh) get a
        // signal without having to re-query.
        if (*destroyed || !opened) return;
        QMetaObject::invokeMethod(this, [this, shotId, ownedRecord = std::move(record), badgesPersisted, destroyed]() {
            if (*destroyed) {
                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "requestShot callback dropped (object destroyed)";
                return;
            }
            emit shotReady(shotId, convertShotRecord(ownedRecord));
            if (badgesPersisted) {
                emit shotBadgesUpdated(shotId,
                    ownedRecord.channelingDetected,
                    ownedRecord.grindIssueDetected,
                    ownedRecord.skipFirstFrameDetected,
                    ownedRecord.pourTruncatedDetected);
            }
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::computeDerivedCurves(ShotRecord& record)
{
    const qsizetype n = qMin(record.pressure.size(), record.flow.size());
    if (n < 3) return;

    // All three formulas live in the Conductance:: namespace and are shared
    // with ShotDataModel's live path, so this recompute and the live path can't
    // drift apart. Conductance (+ its derivative) is additionally shared with
    // tools/shot_eval (offline); shot_eval doesn't compute resistance or Darcy
    // resistance, so that three-way agreement doesn't extend to those two.
    record.conductance = Conductance::fromPressureFlow(record.pressure, record.flow);

    record.resistance.clear();
    record.resistance.reserve(n);
    record.darcyResistance.clear();
    record.darcyResistance.reserve(n);
    for (qsizetype i = 0; i < n; ++i) {
        const double p = record.pressure[i].y();
        const double f = record.flow[i].y();
        record.resistance.append(QPointF(record.pressure[i].x(), Conductance::resistance(p, f)));
        record.darcyResistance.append(
            QPointF(record.pressure[i].x(), Conductance::darcyResistanceSample(p, f)));
    }

    record.conductanceDerivative = Conductance::derivative(record.conductance);
}

void ShotHistoryStorage::computePhaseSummaries(ShotRecord& record)
{
    // Helper: average Y values in a time range
    auto avgInRange = [](const QVector<QPointF>& data, double t0, double t1) {
        double sum = 0;
        int count = 0;
        for (const auto& p : data) {
            if (p.x() >= t0 && p.x() <= t1) {
                sum += p.y();
                ++count;
            }
        }
        return count > 0 ? sum / count : 0.0;
    };

    // Helper: find Y value at or near a time
    auto valueAtTime = [](const QVector<QPointF>& data, double t) {
        if (data.isEmpty()) return 0.0;
        for (qsizetype i = 0; i < data.size(); ++i) {
            if (data[i].x() >= t)
                return data[i].y();
        }
        return data.last().y();
    };

    // Build phase boundaries from markers
    struct PhaseBound { QString name; double start; double end; bool isFlowMode; };
    QVector<PhaseBound> bounds;

    for (qsizetype i = 0; i < record.phases.size(); ++i) {
        const auto& marker = record.phases[i];
        if (marker.label == "End") continue;

        double end = (i + 1 < record.phases.size())
            ? record.phases[i + 1].time
            : (record.pressure.isEmpty() ? 0 : record.pressure.last().x());

        QString phaseName = marker.label;
        if (phaseName == "Start") phaseName = QStringLiteral("Preinfusion");

        bounds.append({phaseName, marker.time, end, marker.isFlowMode});
    }

    // If no usable phases, create single "Extraction" phase
    if (bounds.isEmpty() && !record.pressure.isEmpty()) {
        bounds.append({QStringLiteral("Extraction"), record.pressure.first().x(),
                       record.pressure.last().x(), false});
    }

    QJsonArray phasesArray;
    for (const auto& b : bounds) {
        if (b.end <= b.start) continue;

        QJsonObject phaseObj;
        phaseObj["name"] = b.name;
        phaseObj["duration"] = qRound((b.end - b.start) * 10.0) / 10.0;
        phaseObj["avgPressure"] = qRound(avgInRange(record.pressure, b.start, b.end) * 10.0) / 10.0;
        phaseObj["avgFlow"] = qRound(avgInRange(record.flow, b.start, b.end) * 10.0) / 10.0;
        phaseObj["avgTemperature"] = qRound(avgInRange(record.temperature, b.start, b.end) * 10.0) / 10.0;

        double w0 = valueAtTime(record.weight, b.start);
        double w1 = valueAtTime(record.weight, b.end);
        phaseObj["weightGained"] = qRound((w1 - w0) * 10.0) / 10.0;
        phaseObj["isFlowMode"] = b.isFlowMode;
        phasesArray.append(phaseObj);
    }

    record.phaseSummariesJson = QString::fromUtf8(
        QJsonDocument(phasesArray).toJson(QJsonDocument::Compact));
}

void ShotHistoryStorage::requestPortalCalibration(const QString& requestId, qint64 bagId,
                                                  const QString& beanBrand, const QString& beanType,
                                                  PortalTailAnalyzer::MountingMode mountingMode)
{
    if (!m_ready) {
        emit portalCalibrationReady(requestId, {}, QStringLiteral("notReady"));
        return;
    }
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, requestId, bagId, beanBrand, beanType, mountingMode, destroyed] {
        QVariantMap calibration;
        QString error = QStringLiteral("unavailable");
        withTempDb(dbPath, "shs_portal_calibration", [&](QSqlDatabase& db) {
            const auto points = loadPortalCalibrationPointsStatic(db, bagId, beanBrand, beanType);
            if (!points) {
                error = QStringLiteral("queryFailed");
                return;
            }
            error.clear();
            calibration = PortalTailAnalyzer::fitCalibration(*points, bagId, beanBrand, beanType, 0, mountingMode)
                              .toVariantMap();
            calibration.insert(QStringLiteral("coffee"), (beanBrand + QLatin1Char(' ') + beanType).trimmed());
        });
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, requestId, calibration, error, destroyed] {
            if (*destroyed) return;
            emit portalCalibrationReady(requestId, calibration, error);
        }, Qt::QueuedConnection);
    });
}

std::optional<QVector<PortalTailAnalyzer::CalibrationPoint>>
ShotHistoryStorage::loadPortalCalibrationPointsStatic(QSqlDatabase& db, qint64 bagId,
    const QString& beanBrand, const QString& beanType)
{
    // Unindexed name scan on a real-history copy, M-series Mac, Python sqlite3, 7 runs:
    // 1,339 shots: median 0.52 ms, worst 2.81 ms; 10x: median 8.71 ms, worst 9.65 ms.
    // Reached only for shots with PORTAL data and the owner's Settings panel, on worker threads.
    QVector<PortalTailAnalyzer::CalibrationPoint> out;
    const auto failed = [](const char* step, const QSqlQuery& query) {
        portalReadReporter().failed(QStringLiteral("%1: %2").arg(QLatin1String(step), query.lastError().text()));
        return std::nullopt;
    };
    // Names are matched case-folded in C++ (SQLite LOWER is ASCII-only); SQL
    // then selects the exact stored spellings that matched.
    QStringList coffeeScopes{bagId > 0 ? QStringLiteral("bag_id = :bag") : QStringLiteral("0")};
    QVariantMap coffeeBinds;
    const QString brandKey = PortalTailAnalyzer::coffeeNameKey(beanBrand);
    const QString beanKey = PortalTailAnalyzer::coffeeNameKey(beanType);
    if (!brandKey.isEmpty() && !beanKey.isEmpty()) {
        QSqlQuery names(db);
        if (!names.exec("SELECT DISTINCT bean_brand, bean_type FROM shots WHERE portal_present=1 "
                        "AND (COALESCE(drink_tds,0)>0 OR COALESCE(enjoyment,0)>0)"))
            return failed("name lookup", names);
        while (names.next()) {
            if (PortalTailAnalyzer::coffeeNameKey(names.value(0).toString()) != brandKey
                || PortalTailAnalyzer::coffeeNameKey(names.value(1).toString()) != beanKey)
                continue;
            const QString brandBind = QStringLiteral(":brand%1").arg(coffeeBinds.size());
            const QString beanBind = QStringLiteral(":bean%1").arg(coffeeBinds.size());
            coffeeScopes << QString("(bean_brand=%1 AND bean_type=%2)").arg(brandBind, beanBind);
            coffeeBinds.insert(brandBind, names.value(0));
            coffeeBinds.insert(beanBind, names.value(1));
        }
    }
    QSqlQuery q(db);
    if (!q.prepare(QString(R"(
        SELECT id, bag_id, bean_brand, bean_type,
               portal_mean_ec, drink_tds, dose_weight, final_weight, timestamp, enjoyment,
               portal_estimate_reliable, portal_mounting_mode, equipment_id, profile_name
        FROM shots
        WHERE portal_present = 1
          AND weight_source = :physicalScale
          AND (COALESCE(drink_tds, 0) > 0 OR COALESCE(enjoyment, 0) > 0)
          AND (%1)
        ORDER BY CASE WHEN COALESCE(portal_mean_ec, 0) > 0 AND COALESCE(drink_tds, 0) > 0 THEN 0
                      WHEN COALESCE(portal_mean_ec, 0) > 0 THEN 1
                      ELSE 2 END,
                 timestamp DESC
    )").arg(coffeeScopes.join(" OR "))))
        return failed("prepare", q);
    q.bindValue(":physicalScale",
                PortalTailAnalyzer::weightSourceToString(PortalTailAnalyzer::WeightSource::PhysicalScale));
    if (bagId > 0) q.bindValue(":bag", bagId);
    for (auto it = coffeeBinds.cbegin(); it != coffeeBinds.cend(); ++it)
        q.bindValue(it.key(), it.value());
    if (!q.exec())
        return failed("read", q);

    while (q.next()) {
        PortalTailAnalyzer::CalibrationPoint pt;
        pt.shotId = q.value(0).toLongLong();
        pt.bagId = q.value(1).isNull() ? -1 : q.value(1).toLongLong();
        pt.beanBrand = q.value(2).toString();
        pt.beanType = q.value(3).toString();
        pt.meanRawEcPerG = q.value(4).toDouble();
        pt.measuredTdsPct = q.value(5).toDouble();
        pt.doseWeightG = q.value(6).toDouble();
        pt.finalWeightG = q.value(7).toDouble();
        pt.timestamp = q.value(8).toLongLong();
        pt.enjoyment = q.value(9).toInt();
        pt.measurementReliable = q.value(10).toBool();
        pt.mountingMode = PortalTailAnalyzer::mountingModeFromString(q.value(11).toString());
        pt.equipmentId = q.value(12).toLongLong();
        pt.profileName = q.value(13).toString();
        if (pt.measuredTdsPct > 0.0 || pt.enjoyment > 0)
            out.append(pt);
    }
    portalReadReporter().succeeded();
    return out;
}

ShotRecord ShotHistoryStorage::loadShotRecordStatic(QSqlDatabase& db, qint64 shotId,
                                                     bool* outBadgesPersisted, const char* requestedBy,
                                                     PortalCalibration portalCalibration)
{
    if (outBadgesPersisted) *outBadgesPersisted = false;
    ShotRecord record;

    QSqlQuery query(db);
    // Grinder identity (brand/model/burrs) is resolved by following the shot's
    // equipment_id pointer to its package's grinder item, not from per-shot
    // snapshot columns (add-equipment-packages task 4.1). The legacy
    // grinder_brand/model/burrs columns are dropped in migration 23; this JOIN
    // is the single read path so the snapshot columns can go away. burrs lives
    // in the item's attrs JSON blob (json_extract). A NULL equipment_id (shot
    // with no grinder identity) LEFT-JOINs to NULLs — same empty strings the old
    // columns held.
    if (!query.prepare(R"(
        SELECT s.id, s.uuid, s.timestamp, s.profile_name, s.profile_json,
               s.duration_seconds, s.final_weight, s.dose_weight,
               s.bean_brand, s.bean_type, s.roast_date, s.roast_level,
               eg.brand, eg.model, json_extract(eg.attrs, '$.burrs'), s.grinder_setting,
               s.drink_tds, s.drink_ey, s.enjoyment, s.espresso_notes, s.bean_notes, s.barista,
               s.profile_notes, s.visualizer_id, s.visualizer_url, s.debug_log,
               s.temperature_override, s.yield_override, s.beverage_type, s.profile_kb_id,
               s.channeling_detected, s.grind_issue_detected,
               s.skip_first_frame_detected, s.pour_truncated_detected,
               s.stopped_by, s.beanbase_json,
               s.bag_id, s.frozen_date, s.defrost_date,
               s.equipment_id, s.rpm,
               ep.in_inventory, ep.superseded_by, ep.name,
               eb.brand, eb.model,
               epp.model,
               s.recipe_id, s.steam_json, s.hot_water_json,
               s.storage_hint, s.opened_date,
               s.taste_balance, s.taste_body,
               s.yield_mode, s.yield_anchor_value,
               s.flow_calibration,
               s.hot_water_result_json,
               s.portal_mean_ec, s.portal_integral_ec_g, s.portal_mean_temp_c, s.portal_estimate_reliable,
               s.weight_source, s.portal_mounting_mode
        FROM shots s
        LEFT JOIN equipment_items eg ON eg.package_id = s.equipment_id AND eg.kind = 'grinder'
        LEFT JOIN equipment_items eb ON eb.package_id = s.equipment_id AND eb.kind = 'basket'
        LEFT JOIN equipment_items epp ON epp.package_id = s.equipment_id AND epp.kind = 'puckprep'
        LEFT JOIN equipment_packages ep ON ep.id = s.equipment_id
        WHERE s.id = ?
    )")) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "operation=loadShotRecord requestedBy=" << requestedBy << "shotId=" << shotId
            << "result=prepareFailed sqlError=" << query.lastError().text();
        return record;
    }
    query.bindValue(0, shotId);

    if (!query.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "operation=loadShotRecord requestedBy=" << requestedBy << "shotId=" << shotId
            << "result=queryFailed sqlError=" << query.lastError().text();
        return record;
    }
    if (!query.next()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "operation=loadShotRecord requestedBy=" << requestedBy << "shotId=" << shotId
            << "result=missingRow";
        return record;
    }

    record.summary.id = query.value(0).toLongLong();
    record.summary.uuid = query.value(1).toString();
    record.summary.timestamp = query.value(2).toLongLong();
    record.summary.profileName = query.value(3).toString();
    record.profileJson = query.value(4).toString();
    record.summary.duration = query.value(5).toDouble();
    record.summary.finalWeight = query.value(6).toDouble();
    record.summary.doseWeight = query.value(7).toDouble();
    record.summary.beanBrand = query.value(8).toString();
    record.summary.beanType = query.value(9).toString();
    record.roastDate = query.value(10).toString();
    record.roastLevel = query.value(11).toString();
    record.grinderBrand = query.value(12).toString();
    record.grinderModel = query.value(13).toString();
    record.grinderBurrs = query.value(14).toString();
    record.grinderSetting = query.value(15).toString();
    record.drinkTds = query.value(16).toDouble();
    record.drinkEy = query.value(17).toDouble();
    record.summary.enjoyment = query.value(18).toInt();
    record.espressoNotes = query.value(19).toString();
    record.beanNotes = query.value(20).toString();
    record.barista = query.value(21).toString();
    record.profileNotes = query.value(22).toString();
    record.visualizerId = query.value(23).toString();
    record.visualizerUrl = query.value(24).toString();
    record.debugLog = query.value(25).toString();
    record.temperatureOverride = query.value(26).toDouble();
    record.targetWeight = query.value(27).toDouble();
    record.summary.beverageType = query.value(28).toString();
    record.profileKbId = query.value(29).toString();
    record.channelingDetected = query.value(30).toInt() != 0;
    record.grindIssueDetected = query.value(31).toInt() != 0;
    record.skipFirstFrameDetected = query.value(32).toInt() != 0;
    record.pourTruncatedDetected = query.value(33).toInt() != 0;
    record.stoppedBy = query.value(34).toString();
    record.beanBaseJson = query.value(35).toString();
    record.bagId = query.value(36).isNull() ? -1 : query.value(36).toLongLong();
    record.frozenDate = query.value(37).toString();
    record.defrostDate = query.value(38).toString();
    record.equipmentId = query.value(39).isNull() ? 0 : query.value(39).toLongLong();
    record.rpm = query.value(40).toLongLong();
    // Equipment lineage state for history display (add-equipment-packages 4b.7).
    // A NULL in_inventory means the shot has no linked package (LEFT JOIN miss) —
    // leave the state empty. Otherwise: in inventory = current (""), out of
    // inventory with a superseded_by pointer = "older" (a newer fork exists),
    // out of inventory without one = "retired".
    if (record.equipmentId > 0 && !query.value(41).isNull()) {
        const bool inInventory = query.value(41).toInt() != 0;
        const bool hasSuccessor = !query.value(42).isNull() && query.value(42).toLongLong() > 0;
        if (!inInventory)
            record.equipmentState = hasSuccessor ? QStringLiteral("older") : QStringLiteral("retired");
    }
    // Package display name (col 43) — UI shows this rather than the raw grinder
    // identity. Falls back to brand+model when the package has no custom name.
    record.equipmentName = query.value(43).toString();
    if (record.equipmentName.isEmpty())
        record.equipmentName = (record.grinderBrand + QLatin1Char(' ') + record.grinderModel).trimmed();
    // Basket identity (cols 44/45) resolved through equipment_id (add-basket-
    // equipment); empty when the package has no basket. Specs are derived
    // downstream from BasketAliases, never stored.
    record.basketBrand = query.value(44).toString();
    record.basketModel = query.value(45).toString();
    // Puck-prep canonical flag string (col 46) resolved through equipment_id
    // (add-puckprep-equipment); empty when the package has no puck prep. Flags +
    // distribution are derived downstream (core/puckprep.h), never stored.
    record.puckPrep = query.value(46).toString();
    // Recipe provenance (cols 47/48, add-recipes): NULL = pre-recipe shot.
    // hot_water_json (col 49, finish-recipes-first-class) is the added-hot-water
    // snapshot for promote-from-shot round-trip.
    record.recipeId = query.value(47).isNull() ? -1 : query.value(47).toLongLong();
    record.steamJson = query.value(48).toString();
    record.hotWaterJson = query.value(49).toString();
    // Non-frozen storage lifecycle snapshot (cols 50/51, bean-freshness-
    // followup): the non-frozen analogue of frozen_date/defrost_date. Appended
    // at the end of the SELECT so existing positional reads keep their indices.
    record.storageHint = query.value(50).toString();
    record.openedDate = query.value(51).toString();
    // Structured taste axes (cols 52/53, add-ai-taste-intake): sour|balanced|
    // bitter and thin|medium|heavy. "" = unset. Appended at the end of the SELECT
    // so existing positional reads keep their indices.
    record.tasteBalance = query.value(52).toString();
    record.tasteBody = query.value(53).toString();
    // Yield anchor provenance (cols 54/55, add-yield-ratio-anchor). A NULL
    // mode (a row imported from a pre-34 source before its backfill ran)
    // reads by the legacy rule: absolute when a target was recorded, else
    // none — the exact relabel migration 34 applies.
    record.yieldMode = YieldSpec::normalizedMode(query.value(54).toString());
    record.yieldAnchorValue = query.value(55).toDouble();
    // Appended at the END of the SELECT above (index 56) so every existing
    // positional read keeps its index, the same rule the taste-axis and
    // yield-anchor columns followed. NULL reads as 0.0 = not recorded.
    record.flowCalibration = query.value(56).toDouble();
    record.hotWaterResultJson = query.value(57).toString();
    // PORTAL columns (migration 51), appended by the same rule.
    record.portalSummary.meanRawEcPerG = query.value(58).toDouble();
    record.portalSummary.integralRawEcG = query.value(59).toDouble();
    record.portalSummary.meanTempC = query.value(60).toDouble();
    record.portalSummary.measurementReliable = query.value(61).toBool();
    record.weightSource = query.value(62).toString();
    record.portalMountingMode = query.value(63).toString();
    if (query.value(54).isNull() && record.targetWeight > 0) {
        record.yieldMode = YieldSpec::modeAbsolute();
        record.yieldAnchorValue = record.targetWeight;
    }
    record.summary.hasVisualizerUpload = !record.visualizerId.isEmpty();

    // Snapshot stored badge values before the recompute block overwrites them, so
    // we can detect drift and persist the corrected flags below.
    const bool storedChanneling = record.channelingDetected;
    const bool storedGrindIssue = record.grindIssueDetected;
    const bool storedSkipFirstFrame = record.skipFirstFrameDetected;
    const bool storedPourTruncated = record.pourTruncatedDetected;

    // decompressSampleData() unconditionally recomputes resistance/conductance/
    // darcyResistance/conductanceDerivative from this shot's own pressure/flow
    // (recompute-shot-curves-on-load) — so conductanceDerivative is populated
    // for the badge-recompute block below whenever the shot has enough samples
    // for computeDerivedCurves() to run (its own >=3-sample guard applies here
    // too). When the recompute disagrees with what was stored, correctedBlob
    // comes back non-empty and we persist it below on the same connection.
    QByteArray correctedBlob;
    if (query.prepare("SELECT data_blob FROM shot_samples WHERE shot_id = ?")) {
        query.bindValue(0, shotId);
        if (query.exec() && query.next()) {
            QByteArray blob = query.value(0).toByteArray();
            decompressSampleData(blob, &record, &correctedBlob);
        }
        // Release the read transaction this SELECT is still holding — the
        // single row was consumed above but the statement was never stepped
        // to exhaustion, so it stays "active" and the writes below would try
        // to upgrade a stale WAL read snapshot instead of taking a fresh write
        // lock. That upgrade fails immediately (SQLITE_BUSY_SNAPSHOT) rather
        // than waiting out busy_timeout — see core/dbutils.h's DbWriteTxn doc
        // on the no-active-statement precondition.
        query.finish();
    }

    if (!correctedBlob.isEmpty()) {
        // Both writes land together or not at all: a corrected blob with a
        // stale updated_at would permanently hide the correction from
        // ShotHistoryExporter::exportedFileIsFresh(), which decides purely by
        // comparing an export's mtime against this column, and nothing ever
        // retries a write that already "succeeded" on the blob half.
        DbWriteTxn txn = DbWriteTxn::begin(db, "curve self-heal");
        if (txn.ok()) {
            QSqlQuery curveUpd(db);
            curveUpd.prepare("UPDATE shot_samples SET data_blob = ? WHERE shot_id = ?");
            curveUpd.bindValue(0, correctedBlob);
            curveUpd.bindValue(1, shotId);

            // Bump updated_at so consumers keyed on it (ShotHistoryExporter's
            // exportedFileIsFresh()) know this shot changed and re-derive
            // their own cached output — same reason the badge-persist UPDATE
            // below touches it too.
            QSqlQuery touchUpd(db);
            touchUpd.prepare("UPDATE shots SET updated_at = strftime('%s', 'now') WHERE id = ?");
            touchUpd.bindValue(0, shotId);

            if (curveUpd.exec() && touchUpd.exec()) {
                if (!txn.commit()) {
                    DIAG_WARN(STORAGE, "ShotHistoryStorage") << "loadShotRecordStatic: curve self-heal"
                                  " commit failed for shot" << shotId << txn.commitError();
                }
            } else {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "loadShotRecordStatic: curve self-heal"
                              " failed for shot" << shotId
                           << curveUpd.lastError() << touchUpd.lastError();
            }
        }
    }

    if (query.prepare("SELECT time_offset, label, frame_number, is_flow_mode, transition_reason "
                      "FROM shot_phases WHERE shot_id = ? ORDER BY time_offset")) {
        query.bindValue(0, shotId);
        if (query.exec()) {
            while (query.next()) {
                HistoryPhaseMarker marker;
                marker.time = query.value(0).toDouble();
                marker.label = query.value(1).toString();
                marker.frameNumber = query.value(2).toInt();
                marker.isFlowMode = query.value(3).toInt() != 0;
                marker.transitionReason = query.value(4).toString();
                record.phases.append(marker);
            }
        }
        query.finish();
    }

    if (portalCalibration == PortalCalibration::Include && record.portalSummary.meanRawEcPerG > 0.0
        && PortalTailAnalyzer::hasReferenceProvenance(record.weightSource, record.portalMountingMode)) {
        auto points = loadPortalCalibrationPointsStatic(db, record.bagId,
            record.summary.beanBrand, record.summary.beanType);
        record.portalCalibrationFailed = !points;
        if (points)
            record.portalCalibrationPoints = std::move(*points);
    }

    // Compute phase summaries on-the-fly for legacy shots that lack them
    if (record.phaseSummariesJson.isEmpty() && !record.pressure.isEmpty() && !record.phases.isEmpty()) {
        computePhaseSummaries(record);
    }

    // For shots predating migration 9, profile_kb_id was not stored in the DB.
    // Derive it from the stored profile JSON so that channeling/grind suppression
    // flags still apply to old shots.
    if (record.profileKbId.isEmpty() && !record.profileJson.isEmpty()) {
        QJsonDocument kbDoc = QJsonDocument::fromJson(record.profileJson.toUtf8());
        if (!kbDoc.isNull()) {
            record.profileKbId = ShotSummarizer::computeProfileKbId(
                kbDoc.object()[QStringLiteral("title")].toString(),
                kbDoc.object()[QStringLiteral("legacy_profile_type")].toString());
        }
    }

    // Always recompute every quality badge from the loaded curve data, so that
    // detector improvements take effect on existing shots without a one-shot
    // re-analyze pass. Stored badge values are only authoritative as of save
    // time; the detectors evolve. The channeling sub-block uses
    // conductanceDerivative, which decompressSampleData() above unconditionally
    // recomputes from pressure/flow (recompute-shot-curves-on-load), regardless
    // of migration status — populated whenever the shot has enough samples for
    // computeDerivedCurves() to run (its own >=3-sample guard, shothistorystorage.h),
    // same as before this change for any shot that already had the field.
    // The grind and skip-first-frame sub-blocks need only flow / flowGoal /
    // pressure / phases, which are always available.
    // Compute all four quality badges via a single ShotAnalysis::analyzeShot
    // pass and project the booleans from DetectorResults. The cascade lives in
    // exactly one place (analyzeShot's body) and the badge columns are a
    // deterministic projection — see decenza::deriveBadgesFromAnalysis (in
    // history/shotbadgeprojection.h) and docs/SHOT_REVIEW.md §4 for the
    // full mapping table.
    //
    // analyzeShot tolerates empty / partial inputs (its internal
    // pressure.size() < 10 short-circuit handles aborted shots), so the
    // outer "if (!record.pressure.isEmpty())" guard the per-detector code
    // used to need is no longer required — analyzeShot returns clean
    // defaults for any input shape it can't handle, which the projection
    // helper interprets as "all badges false."
    {
        const AnalysisInputs inputs = prepareAnalysisInputs(record.profileKbId, record.profileJson);
        // Same as the save path: the gate comes from AnalysisInputs, which
        // re-resolves from the shot's own stored profile (by title, then by
        // shape). An empty persisted id no longer means "no context" — it is
        // also what every shape-resolved profile carries.
        const bool profileKbResolved = inputs.profileKbResolved;
        auto analysis = ShotAnalysis::analyzeShot(
            record.pressure, record.flow, record.weight,
            record.conductanceDerivative,
            record.phases, record.summary.beverageType, record.summary.duration,
            record.pressureGoal, record.flowGoal,
            inputs.analysisFlags, inputs.firstFrameSeconds,
            record.targetWeight, record.summary.finalWeight,
            inputs.frameCount, inputs.expertBand,
            profileKbResolved);
        decenza::applyBadgesToTarget(record, analysis.detectors);
        // Cache the AnalysisResult on the ShotRecord so convertShotRecord
        // (called next in the requestShot path) doesn't have to re-run
        // analyzeShot on the same inputs. See cachedAnalysis docstring on
        // ShotRecord for the invalidation contract.
        record.cachedAnalysis = std::move(analysis);
        // Same walk, same reason — see cachedKbDerivedFrom on ShotRecord. Only
        // a SHAPE match is a derivation worth naming: a title match needs no
        // explanation and an ambiguous one has no single entry to name.
        if (inputs.identityFromShape && !inputs.identityKbId.isEmpty())
            record.cachedKbDerivedFrom =
                ShotSummarizer::canonicalNameForKbId(inputs.identityKbId);
    }

    // Persist any drift between the stored badge columns and the recomputed values
    // on the same connection. Loading a shot is the canonical "touched it under the
    // current detector" event — both UI and MCP go through this path — so the DB
    // converges with detector improvements as shots are viewed without needing a
    // separate bulk-resweep migration. The UPDATE is skipped when nothing changed.
    const bool flagsChanged = (storedChanneling != record.channelingDetected
        || storedGrindIssue != record.grindIssueDetected
        || storedSkipFirstFrame != record.skipFirstFrameDetected
        || storedPourTruncated != record.pourTruncatedDetected);
    if (flagsChanged) {
        QSqlQuery upd(db);
        upd.prepare("UPDATE shots SET channeling_detected=:c,"
                    " grind_issue_detected=:g,"
                    " skip_first_frame_detected=:s,"
                    " pour_truncated_detected=:p,"
                    " updated_at = strftime('%s', 'now') WHERE id=:id");
        upd.bindValue(":c", record.channelingDetected ? 1 : 0);
        upd.bindValue(":g", record.grindIssueDetected ? 1 : 0);
        upd.bindValue(":s", record.skipFirstFrameDetected ? 1 : 0);
        upd.bindValue(":p", record.pourTruncatedDetected ? 1 : 0);
        upd.bindValue(":id", shotId);
        if (upd.exec()) {
            if (outBadgesPersisted) *outBadgesPersisted = true;
        } else {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "loadShotRecordStatic: badge persist failed for shot"
                       << shotId << upd.lastError();
        }
    }

    return record;
}

bool ShotHistoryStorage::deleteShotStatic(QSqlDatabase& db, qint64 shotId)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM shots WHERE id = ?");
    query.bindValue(0, shotId);

    if (!query.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to delete shot:" << query.lastError().text();
        return false;
    }

    // Note: no updateTotalShots()/shotDeleted() here.
    // This is only called from the import overwrite path, which handles refresh
    // (refreshTotalShots) after the full batch.
    DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Deleted shot" << shotId;
    return true;
}

void ShotHistoryStorage::deleteShots(const QVariantList& shotIds)
{
    deleteShotsImpl({}, shotIds);
}

void ShotHistoryStorage::deleteShotsForRequest(const QString& requestId,
                                              const QVariantList& shotIds)
{
    deleteShotsImpl(requestId, shotIds);
}

void ShotHistoryStorage::deleteShotsImpl(const QString& requestId,
                                         const QVariantList& shotIds)
{
    if (requestId.isEmpty()) {
        if (!m_ready || shotIds.isEmpty()) return;
    } else if (!m_ready || shotIds.isEmpty()) {
        emit shotsDeleteRequestFinished(requestId, false, shotIds,
                                        !m_ready ? QStringLiteral("database not ready")
                                                 : QStringLiteral("empty selection"));
        return;
    }

    const QString dbPath = m_dbPath;

    // Build placeholders on main thread (pure computation, fast)
    QStringList placeholders;
    placeholders.reserve(shotIds.size());
    for (int i = 0; i < shotIds.size(); ++i)
        placeholders << "?";
    QString sql = "DELETE FROM shots WHERE id IN (" + placeholders.join(",") + ")";

    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, sql, shotIds, requestId, destroyed]() {
        bool success = false;
        QString reason;
        withTempDb(dbPath, "shs_delete", [&](QSqlDatabase& db) {
            if (!db.transaction()) {
                reason = QStringLiteral("could not start transaction");
                return;
            }
            QSqlQuery query(db);
            if (query.prepare(sql)) {
                for (int i = 0; i < shotIds.size(); ++i)
                    query.bindValue(i, shotIds[i].toLongLong());
                if (query.exec()) {
                    db.commit();
                    success = true;
                } else {
                    DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to batch delete shots:" << query.lastError().text();
                    db.rollback();
                    reason = QStringLiteral("database rejected delete");
                }
            } else {
                reason = QStringLiteral("could not prepare delete");
            }
        });

        if (!success && reason.isEmpty())
            reason = QStringLiteral("shot database could not be opened");
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotIds, requestId, success, reason, destroyed]() {
            if (*destroyed) {
                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "deleteShots callback dropped (object destroyed)";
                return;
            }
            if (success) {
                updateTotalShots();
                for (const auto& id : shotIds)
                    emit shotDeleted(id.toLongLong());
                if (requestId.isEmpty())
                    emit shotsDeleted(shotIds);
                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Batch deleted" << shotIds.size() << "shots";
            }
            if (!requestId.isEmpty())
                emit shotsDeleteRequestFinished(requestId, success, shotIds, reason);
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::requestDeleteShot(qint64 shotId)
{
    requestDeleteShotImpl({}, shotId);
}

void ShotHistoryStorage::requestDeleteShotForRequest(const QString& requestId, qint64 shotId)
{
    requestDeleteShotImpl(requestId, shotId);
}

void ShotHistoryStorage::requestDeleteShotImpl(const QString& requestId, qint64 shotId)
{
    if (!m_ready) {
        if (requestId.isEmpty()) {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Cannot delete shot - not ready";
            emit errorOccurred("Cannot delete shot: database not ready");
            emit shotDeleteFinished(shotId, false, QStringLiteral("database not ready"));
        } else {
            emit shotDeleteRequestFinished(requestId, shotId, false,
                                           QStringLiteral("database not ready"));
        }
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    runOnDbThread([this, dbPath, shotId, requestId, destroyed]() {
        bool success = false;
        QString reason;
        // withTempDb's return is CHECKED: when the database will not open the
        // work lambda never runs, so `reason` would stay empty and the tool would
        // emit the dangling "Shot 42 was not deleted — ". An unnamed failure in
        // the change whose whole point is that failures name themselves.
        const bool opened = withTempDb(dbPath, "shs_rdel", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            query.prepare("DELETE FROM shots WHERE id = ?");
            query.bindValue(0, shotId);
            if (!query.exec()) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to async delete shot:" << query.lastError().text();
                reason = QStringLiteral("the database rejected the delete");
            } else if (query.numRowsAffected() == 0) {
                // A DELETE matching nothing is a successful statement. The caller
                // asked for a specific shot to be gone; it was never there.
                reason = QStringLiteral("no shot with that id");
            } else {
                success = true;
            }
        });
        if (!opened)
            reason = QStringLiteral("the shot database could not be opened");

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotId, requestId, success, reason, destroyed]() {
            if (*destroyed) {
                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "deleteShot callback dropped (object destroyed)";
                return;
            }
            if (success) {
                refreshTotalShots();
                emit importedDatabaseChanged();
                emit shotDeleted(shotId);
                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Async deleted shot" << shotId;
            } else if (requestId.isEmpty()) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to async delete shot" << shotId
                           << "-" << reason;
                // User-facing (toast): no internal shot id — logged above.
                emit errorOccurred(QStringLiteral("Couldn't delete the shot — please try again."));
            }
            if (requestId.isEmpty())
                emit shotDeleteFinished(shotId, success, reason);
            else
                emit shotDeleteRequestFinished(requestId, shotId, success, reason);
        }, Qt::QueuedConnection);
    });
}

const QStringList& ShotHistoryStorage::tasteBalanceValues()
{
    static const QStringList values = {QStringLiteral("sour"), QStringLiteral("balanced"), QStringLiteral("bitter")};
    return values;
}

const QStringList& ShotHistoryStorage::tasteBodyValues()
{
    static const QStringList values = {QStringLiteral("thin"), QStringLiteral("medium"), QStringLiteral("heavy")};
    return values;
}

namespace {
const QList<QPair<QString, QString>>& shotMetadataFieldMap()
{
    static const QList<QPair<QString, QString>> fields = {
        {"beanBrand", "bean_brand"}, {"beanType", "bean_type"},
        {"roastDate", "roast_date"}, {"roastLevel", "roast_level"},
        {"grinderSetting", "grinder_setting"}, {"rpm", "rpm"},
        {"equipmentId", "equipment_id"}, {"drinkTds", "drink_tds"},
        {"drinkEy", "drink_ey"}, {"enjoyment", "enjoyment"},
        {"espressoNotes", "espresso_notes"}, {"barista", "barista"},
        {"doseWeight", "dose_weight"}, {"finalWeight", "final_weight"},
        {"beverageType", "beverage_type"}, {"beanBaseJson", "beanbase_json"},
        {"beanBaseId", "beanbase_id"}, {"bagId", "bag_id"},
        {"frozenDate", "frozen_date"}, {"defrostDate", "defrost_date"},
        {"storageHint", "storage_hint"}, {"openedDate", "opened_date"},
        {"tasteBalance", "taste_balance"}, {"tasteBody", "taste_body"}
    };
    return fields;
}
} // namespace

bool ShotHistoryStorage::validateShotMetadataRequest(const QVariantMap& metadata,
                                                     QString* reason)
{
    const auto fail = [reason](const QString& value) {
        if (reason) *reason = value;
        return false;
    };
    if (metadata.isEmpty() || metadata.size() > shotMetadataFieldMap().size())
        return fail(QStringLiteral("invalidFields"));

    qsizetype encodedBytes = 0;
    for (auto it = metadata.cbegin(); it != metadata.cend(); ++it) {
        const auto field = std::find_if(shotMetadataFieldMap().cbegin(), shotMetadataFieldMap().cend(),
            [&it](const auto& pair) { return pair.first == it.key(); });
        if (field == shotMetadataFieldMap().cend())
            return fail(QStringLiteral("invalidFields"));
        const int type = it.value().metaType().id();
        if (type == QMetaType::QString) {
            const qsizetype limit = it.key() == QLatin1String("beanBaseJson") ? 240 * 1024
                : it.key() == QLatin1String("espressoNotes") ? 256 * 1024 : 512;
            const QString text = it.value().toString();
            if (text.size() > limit)
                return fail(QStringLiteral("invalidFields"));
            encodedBytes += text.toUtf8().size();
        } else if (type == QMetaType::Bool || type == QMetaType::Int
                   || type == QMetaType::LongLong || type == QMetaType::UInt
                   || type == QMetaType::ULongLong) {
            encodedBytes += 16;
        } else if (type == QMetaType::Double && std::isfinite(it.value().toDouble())) {
            encodedBytes += 24;
        } else if (it.value().isNull()) {
            encodedBytes += 4;
        } else {
            return fail(QStringLiteral("invalidFields"));
        }
        if (encodedBytes > 256 * 1024)
            return fail(QStringLiteral("invalidFields"));
    }
    if (reason) reason->clear();
    return true;
}

bool ShotHistoryStorage::updateShotMetadataStatic(QSqlDatabase& db, qint64 shotId, const QVariantMap& metadataIn,
                                                  QString* refusal)
{
    DbWriteTxn txn = DbWriteTxn::begin(db, "shot metadata update");
    if (!txn.ok() || !updateShotMetadataInTransaction(db, shotId, metadataIn, refusal))
        return false;
    if (!txn.commit()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Shot metadata commit failed for shot" << shotId
                                                 << ":" << txn.commitError();
        return false;
    }
    return true;
}

bool ShotHistoryStorage::updateShotMetadataInTransaction(QSqlDatabase& db, qint64 shotId, const QVariantMap& metadataIn,
                                                  QString* refusal)
{
    // Sanitize structured-taste values against their allowed sets before use.
    // "" is a valid "unset". Any out-of-set value is dropped (with a warning) so
    // a bad taste key can never write garbage — without failing an update that
    // also carries legitimate fields (add-ai-taste-intake). Non-taste keys pass
    // through untouched.
    QVariantMap metadata = metadataIn;
    // A bag pick sends only the bag id; the shot's bean snapshot comes from the
    // bag here, so the app's Change Beans and the web page write the same fields.
    if (bagIdIsSet(metadata.value(QStringLiteral("bagId"), -1).toLongLong()) && !metadata.contains(QStringLiteral("beanBrand"))) {
        const qint64 bagId = metadata.value(QStringLiteral("bagId")).toLongLong();
        const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, bagId);
        if (bag.id <= 0) {
            const QString why = QStringLiteral("No bag with id %1").arg(bagId);
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "shot" << shotId << "update refused:" << why;
            if (refusal)
                *refusal = why;
            return false;
        }
        metadata.insert(bag.shotSnapshot());
    }
    const auto sanitizeTaste = [&](const QString& key, const QStringList& allowed) {
        if (!metadata.contains(key)) return;
        const QString raw = metadata.value(key).toString();
        if (raw.isEmpty()) return;  // "" = unset/clear, always valid
        // Normalize case/whitespace so a drifted producer ("Balanced", " sour ")
        // is corrected rather than silently discarded. Only a genuinely
        // out-of-set value is dropped (with a warning).
        const QString norm = raw.trimmed().toLower();
        if (allowed.contains(norm)) {
            metadata.insert(key, norm);
        } else {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "dropping invalid" << key << "value" << raw;
            metadata.remove(key);
        }
    };
    sanitizeTaste("tasteBalance", tasteBalanceValues());
    sanitizeTaste("tasteBody", tasteBodyValues());

    // Map camelCase metadata keys to DB column names.
    // Only columns with keys present in the metadata map are updated,
    // so partial updates don't wipe unspecified fields.
    const auto& fieldMap = shotMetadataFieldMap();

    // Build SET clause from only the keys present in the metadata.
    QStringList setClauses;
    for (const auto& [metaKey, dbCol] : fieldMap) {
        if (metadata.contains(metaKey))
            setClauses << QString("%1 = :%1").arg(dbCol);
    }

    if (setClauses.isEmpty()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "No fields to update for shot" << shotId;
        return false;
    }

    const bool refreshPortal = changesPortalEligibility(metadata);
    if (refreshPortal)
        setClauses << "portal_estimate_reliable = 0";

    // Mark each Visualizer field whose value this edit actually changes. Compared
    // in SQL against the row's current value, so a form that re-saves every field
    // marks only what moved, without a read before the write. The seq moves only
    // when something did, so a no-op re-save during a send does not stop that
    // send clearing what it carried. Each comparison binds its own copies
    // (:d_/:s_<col>) rather than reusing :<col>, which would lean on
    // QSQLiteResult's duplicate-placeholder pruning (qsql_sqlite.cpp:465-499).
    auto changedFields = [&](const QString& prefix) {
        QStringList terms;
        for (const auto& [metaKey, dbCol] : fieldMap) {
            const VisualizerSync::Column* column = VisualizerSync::columnNamed(dbCol);
            if (column && metadata.contains(metaKey))
                // IFNULL drops the column's affinity, so the cast compares a
                // number bound as text ("18") as the number it is.
                terms << QString("(CASE WHEN CAST(IFNULL(%1, %2) AS %5) IS NOT CAST(IFNULL(:%4%1, %2) AS %5) "
                                 "THEN %3 ELSE 0 END)")
                             .arg(dbCol, column->numeric ? QStringLiteral("0") : QStringLiteral("''"))
                             .arg(column->field).arg(prefix)
                             .arg(column->numeric ? QStringLiteral("REAL") : QStringLiteral("TEXT"));
        }
        return terms.join(" | ");
    };
    const QString dirtyTerms = changedFields(QStringLiteral("d_"));
    if (!dirtyTerms.isEmpty()) {
        setClauses << QString("visualizer_dirty = visualizer_dirty | %1").arg(dirtyTerms);
        setClauses << QString("visualizer_dirty_seq = visualizer_dirty_seq + (CASE WHEN (%1) != 0 THEN 1 ELSE 0 END)")
                          .arg(changedFields(QStringLiteral("s_")));
    }

    setClauses << "updated_at = strftime('%s', 'now')";

    QString sql = QString("UPDATE shots SET %1 WHERE id = :id").arg(setClauses.join(", "));

    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Metadata update prepare failed:" << query.lastError().text();
        return false;
    }

    // Bind only the columns present in the metadata.
    for (const auto& [metaKey, dbCol] : fieldMap) {
        if (!metadata.contains(metaKey))
            continue;
        QVariant v = metadata.value(metaKey);
        // equipment_id is an FK — an unset/zero re-point clears the link to NULL
        // rather than pointing at a non-existent package 0.
        if (dbCol == QLatin1String("equipment_id") && v.toLongLong() <= 0)
            v = QVariant();
        query.bindValue(QString(":%1").arg(dbCol), v);
        if (VisualizerSync::columnNamed(dbCol)) {
            query.bindValue(QString(":d_%1").arg(dbCol), v);
            query.bindValue(QString(":s_%1").arg(dbCol), v);
        }
    }
    query.bindValue(":id", shotId);

    if (!query.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to update shot metadata:" << query.lastError().text();
        return false;
    }

    // `exec()` succeeding means the statement RAN, not that it changed anything —
    // `WHERE id = 99999` is a perfectly successful UPDATE of nothing. Callers ask
    // "was this shot updated", and until this check they were told yes for a shot
    // that does not exist.
    //
    // Deliberately not a `SELECT` before the write: that races (this runs on a
    // background thread) and costs a second query on a one-query path.
    if (query.numRowsAffected() == 0) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "No shot with id" << shotId << "to update";
        return false;
    }
    return !refreshPortal || refreshPortalEligibilityInTransaction(db, shotId);
}

bool ShotHistoryStorage::refreshPortalEligibilityInTransaction(QSqlDatabase& db, qint64 shotId)
{
    QSqlQuery samples(db);
    if (!samples.prepare("SELECT ss.data_blob, s.dose_weight, s.final_weight FROM shots s "
                         "JOIN shot_samples ss ON ss.shot_id=s.id WHERE s.id=:id AND s.portal_present=1")) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "PORTAL eligibility read failed for shot" << shotId
                                                 << ":" << samples.lastError().text();
        return false;
    }
    samples.bindValue(":id", shotId);
    if (!samples.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "PORTAL eligibility read failed for shot" << shotId
                                                 << ":" << samples.lastError().text();
        return false;
    }
    if (!samples.next())
        return true;
    ShotRecord record;
    decompressSampleData(samples.value(0).toByteArray(), &record);
    if (record.portalSamples.isEmpty())
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Shot" << shotId
            << "is marked as having PORTAL data but none is readable - it leaves calibration";
    const double doseWeightG = samples.value(1).toDouble();
    const double finalWeightG = samples.value(2).toDouble();
    samples.finish();
    const auto result = PortalTailAnalyzer::analyzeShot(record.portalSamples, record.weight, record.weightSource,
        record.portalMountingMode, doseWeightG, finalWeightG, record.pressure);
    QSqlQuery update(db);
    if (!update.prepare("UPDATE shots SET portal_estimate_reliable=:reliable WHERE id=:id")) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "PORTAL eligibility write failed for shot" << shotId
                                                 << ":" << update.lastError().text();
        return false;
    }
    update.bindValue(":reliable", result.persistedSummary().measurementReliable);
    update.bindValue(":id", shotId);
    if (!update.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "PORTAL eligibility write failed for shot" << shotId
                                                 << ":" << update.lastError().text();
        return false;
    }
    return true;
}

bool ShotHistoryStorage::readVisualizerDirtyStatic(QSqlDatabase& db, qint64 shotId, quint32* dirty, qint64* seq)
{
    QSqlQuery q(db);
    if (!q.prepare("SELECT visualizer_dirty, visualizer_dirty_seq FROM shots WHERE id = :id")) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Visualizer edit state of shot" << shotId
                                                 << "unreadable:" << q.lastError().text();
        return false;
    }
    q.bindValue(":id", shotId);
    if (!q.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Visualizer edit state of shot" << shotId
                                                 << "unreadable:" << q.lastError().text();
        return false;
    }
    if (!q.next()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Visualizer edit state of shot" << shotId
                                                 << "unreadable: no such shot";
        return false;
    }
    *dirty = static_cast<quint32>(q.value(0).toLongLong());
    *seq = q.value(1).toLongLong();
    return true;
}

bool ShotHistoryStorage::clearVisualizerDirtyStatic(QSqlDatabase& db, qint64 shotId, quint32 fields, qint64 seq)
{
    QSqlQuery q(db);
    if (!q.prepare("UPDATE shots SET visualizer_dirty = visualizer_dirty & ~:fields "
                   "WHERE id = :id AND visualizer_dirty_seq = :seq")) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "clearing Visualizer edit state failed:" << q.lastError().text();
        return false;
    }
    q.bindValue(":fields", static_cast<qint64>(fields));
    q.bindValue(":id", shotId);
    q.bindValue(":seq", seq);
    if (!q.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "clearing Visualizer edit state failed:" << q.lastError().text();
        return false;
    }
    return true;
}

void ShotHistoryStorage::requestClearVisualizerDirty(qint64 shotId, quint32 fields, qint64 seq)
{
    const QString dbPath = m_dbPath;
    runOnDbThread([dbPath, shotId, fields, seq]() {
        bool ok = false;
        withTempDb(dbPath, "shs_vizclean", [&](QSqlDatabase& db) {
            ok = clearVisualizerDirtyStatic(db, shotId, fields, seq);
        });
        // The fields stay marked: the next update of this shot sends them again.
        if (!ok)
            DIAG_WARN(VISUALIZER, "ShotHistoryStorage") << "shot" << shotId
                << "sent to Visualizer but its edits are still marked unsent - the next update resends them";
    });
}

bool ShotHistoryStorage::applyVisualizerPullStatic(QSqlDatabase& db, qint64 shotId, const QVariantMap& remote,
                                                   QVariantMap* written, QVariantMap* previous)
{
    written->clear();
    previous->clear();
    QStringList columns;
    for (const VisualizerSync::Column& c : VisualizerSync::kColumns)
        columns << QString::fromLatin1(c.column);

    DbWriteTxn txn = DbWriteTxn::begin(db, "Visualizer pull");
    if (!txn.ok())
        return false;
    QVariantMap local;
    quint32 dirty = 0;
    {
        QSqlQuery q(db);
        if (!q.prepare(QString("SELECT %1, visualizer_dirty FROM shots WHERE id = :id").arg(columns.join(", ")))) {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Visualizer pull read failed:" << q.lastError().text();
            return false;
        }
        q.bindValue(":id", shotId);
        if (!q.exec()) {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Visualizer pull read failed:" << q.lastError().text();
            return false;
        }
        if (!q.next())
            return txn.commit();  // deleted here since: nothing to apply
        for (qsizetype i = 0; i < columns.size(); ++i)
            local.insert(QString::fromLatin1(VisualizerSync::kColumns[i].key), q.value(int(i)));
        dirty = static_cast<quint32>(q.value(int(columns.size())).toLongLong());
    }

    const QVariantMap changes = VisualizerSync::shotPullChanges(remote, local, dirty);
    if (changes.isEmpty())
        return txn.commit();

    const bool refreshPortal = changesPortalEligibility(changes);
    QStringList sets;
    if (refreshPortal)
        sets << "portal_estimate_reliable = 0";
    for (auto it = changes.cbegin(); it != changes.cend(); ++it)
        sets << QString("%1 = :%1").arg(QString::fromLatin1(VisualizerSync::columnForKey(it.key())->column));
    sets << "updated_at = strftime('%s', 'now')";
    QSqlQuery q(db);
    if (!q.prepare(QString("UPDATE shots SET %1 WHERE id = :id").arg(sets.join(", ")))) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Visualizer pull write failed:" << q.lastError().text();
        return false;
    }
    for (auto it = changes.cbegin(); it != changes.cend(); ++it)
        q.bindValue(QString(":%1").arg(QString::fromLatin1(VisualizerSync::columnForKey(it.key())->column)), it.value());
    q.bindValue(":id", shotId);
    if (!q.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Visualizer pull write failed:" << q.lastError().text();
        return false;
    }
    q.finish();
    if (refreshPortal && !refreshPortalEligibilityInTransaction(db, shotId))
        return false;
    if (!txn.commit()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Visualizer pull commit failed:" << txn.commitError();
        return false;
    }
    *written = changes;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it)
        previous->insert(it.key(), local.value(it.key()));
    return true;
}

void ShotHistoryStorage::requestApplyVisualizerPull(qint64 shotId, const QVariantMap& remote,
                                                    std::function<void(bool ok)> done)
{
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, shotId, remote, destroyed, ownedDone = std::move(done)]() {
        bool ok = false;
        QVariantMap written;
        QVariantMap previous;
        withTempDb(dbPath, "shs_vizpull", [&](QSqlDatabase& db) {
            ok = applyVisualizerPullStatic(db, shotId, remote, &written, &previous);
        });
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotId, ok, written, previous, destroyed, ownedDone]() {
            if (*destroyed) return;
            if (ok && !written.isEmpty()) {
                DIAG_INFO(VISUALIZER, "ShotHistoryStorage") << "shot" << shotId << "updated from Visualizer:"
                                                            << written.keys().join(QStringLiteral(", "));
                emit historyDataChanged();
                emit shotPulledFromVisualizer(shotId, previous, written);
            }
            if (ownedDone)
                ownedDone(ok);
        }, Qt::QueuedConnection);
    });
}

bool ShotHistoryStorage::shotIdsForVisualizerIdsStatic(QSqlDatabase& db, const QStringList& visualizerIds,
                                                      QHash<QString, qint64>* out)
{
    out->clear();
    if (visualizerIds.isEmpty())
        return true;
    QStringList placeholders;
    for (qsizetype i = 0; i < visualizerIds.size(); ++i)
        placeholders << QStringLiteral("?");
    QSqlQuery q(db);
    if (!q.prepare(QString("SELECT visualizer_id, id FROM shots WHERE visualizer_id IN (%1)")
                       .arg(placeholders.join(", ")))) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "linked-shot lookup failed:" << q.lastError().text();
        return false;
    }
    for (const QString& id : visualizerIds)
        q.addBindValue(id);
    if (!q.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "linked-shot lookup failed:" << q.lastError().text();
        return false;
    }
    while (q.next())
        out->insert(q.value(0).toString(), q.value(1).toLongLong());
    return true;
}

bool ShotHistoryStorage::updateShotWaterResultStatic(QSqlDatabase& db, qint64 shotId,
                                                    const QString& uuid, const QVariantMap& result)
{
    const auto waterResult = ShotWaterResult::fromMap(result);
    const QString json = waterResult.toJson();
    if (shotId <= 0 || uuid.isEmpty() || json.isEmpty()) return false;
    auto txn = DbWriteTxn::begin(db, "shot water result");
    if (!txn.ok()) return false;
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT hot_water_json FROM shots WHERE id = ? AND uuid = ?"));
    query.addBindValue(shotId);
    query.addBindValue(uuid);
    if (!query.exec() || !query.next()) return false;
    const QVariant originalSpec = query.value(0);
    const QString waterSpec = waterResult.specWithCapturedTarget(originalSpec.toString());
    query.finish();
    query.prepare(QStringLiteral("UPDATE shots SET hot_water_result_json = ?, hot_water_json = ?, "
                                 "updated_at = strftime('%s', 'now') WHERE id = ? AND uuid = ?"));
    query.addBindValue(json);
    query.addBindValue(waterSpec == originalSpec.toString() ? originalSpec : QVariant(waterSpec));
    query.addBindValue(shotId);
    query.addBindValue(uuid);
    return query.exec() && query.numRowsAffected() == 1 && txn.commit();
}

void ShotHistoryStorage::requestUpdateShotWaterResult(qint64 shotId, const QString& uuid,
                                                     const QVariantMap& result)
{
    const QVariantMap normalized = ShotWaterResult::fromMap(result).toMap();
    if (!m_ready || normalized.isEmpty()) {
        emit shotWaterResultUpdateFinished(shotId, false);
        emit errorOccurred(QStringLiteral("Couldn't save the added water result."));
        return;
    }
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, shotId, uuid, normalized, destroyed] {
        bool success = false;
        withTempDb(dbPath, "shs_water", [&](QSqlDatabase& db) {
            success = updateShotWaterResultStatic(db, shotId, uuid, normalized);
        });
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotId, normalized, success, destroyed] {
            if (*destroyed) return;
            if (success) {
                emit shotWaterResultUpdated(shotId, normalized);
                emit historyDataChanged();
            } else {
                emit errorOccurred(QStringLiteral("Couldn't save the added water result."));
            }
            emit shotWaterResultUpdateFinished(shotId, success);
        }, Qt::QueuedConnection);
    });
}

void ShotHistoryStorage::requestUpdateShotMetadata(qint64 shotId, const QVariantMap& metadata)
{
    requestUpdateShotMetadataForRequest({}, shotId, metadata);
}

void ShotHistoryStorage::requestUpdateShotMetadataForRequest(const QString& requestId,
                                                             qint64 shotId,
                                                             const QVariantMap& metadata)
{
    if (!m_ready) {
        emit shotMetadataUpdated(shotId, false);
        if (!requestId.isEmpty())
            emit shotMetadataUpdateRequestFinished(requestId, shotId, false,
                                                   QStringLiteral("notReady"));
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    runOnDbThread([this, dbPath, shotId, metadata, requestId, destroyed]() {
        bool success = false;
        withTempDb(dbPath, "shs_rupd", [&](QSqlDatabase& db) {
            success = updateShotMetadataStatic(db, shotId, metadata);
        });

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shotId, success, requestId, destroyed]() {
            if (*destroyed) {
                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "updateMetadata callback dropped (object destroyed)";
                return;
            }
            if (success) {
                // A rating, note, bean or grind edit can move what the history
                // getters and the grind-step derivation return.
                emit historyDataChanged();
            } else {
                // User-facing (surfaced as a toast): no internal shot id, no
                // "metadata" jargon. The id + success are logged at qDebug below.
                emit errorOccurred(QStringLiteral("Couldn't save your shot changes — please try again."));
            }
            emit shotMetadataUpdated(shotId, success);
            if (!requestId.isEmpty())
                emit shotMetadataUpdateRequestFinished(requestId, shotId, success,
                    success ? QString() : QStringLiteral("writeFailed"));
            DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Async updated metadata for shot" << shotId << "success:" << success;
        }, Qt::QueuedConnection);
    });
}

// Note: getDistinctValues / requestDistinct* / requestAutoFavorites* /
// queryGrinderContext all live in shothistorystorage_queries.cpp.

void ShotHistoryStorage::updateTotalShots()
{
    // Rows went away, so history-derived values may have moved.
    emit historyDataChanged();

    // Async: run COUNT on background thread using existing static helper
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, destroyed]() {
        int count = getShotCountStatic(dbPath);
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, count, destroyed]() {
            if (*destroyed) return;
            if (count < 0) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "updateTotalShots: count query failed, keeping previous count" << m_totalShots;
                return;
            }
            if (count != m_totalShots) {
                m_totalShots = count;
                emit totalShotsChanged();
            }
        }, Qt::QueuedConnection);
    });
}

bool ShotHistoryStorage::performDatabaseCopy(const QString& destPath)
{
    // This method assumes caller has:
    // 1. Set m_backupInProgress = true
    // 2. Checked that m_dbPath is valid

    DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Performing database copy to" << destPath;

    // Checkpoint WAL to ensure all data is in main database file
    checkpoint();

    // Close database temporarily to ensure clean copy
    m_db.close();

    // Copy file using platform-specific method
    bool success = false;
#ifdef Q_OS_ANDROID
    // On Android, use Java file API for scoped storage compatibility
    success = QJniObject::callStaticMethod<jboolean>(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "copyFile",
        "(Ljava/lang/String;Ljava/lang/String;)Z",
        QJniObject::fromString(m_dbPath).object<jstring>(),
        QJniObject::fromString(destPath).object<jstring>());
    DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Java copyFile result:" << success;
#else
    // Desktop/iOS: use Qt's QFile::copy
    success = QFile::copy(m_dbPath, destPath);
#endif

    // Reopen database — this is critical, retry if first attempt fails
    if (!m_db.open()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "First reopen attempt failed, retrying:" << m_db.lastError().text();
        // Wait briefly and retry once
        QThread::msleep(100);
        if (!m_db.open()) {
            DIAG_ERROR(STORAGE, "ShotHistoryStorage") << "CRITICAL - Failed to reopen database after backup:" << m_db.lastError().text();
            m_ready = false;
            emit readyChanged();
            emit errorOccurred("Critical: Database connection lost after backup. Please restart the app.");
            return false;
        }
    }

    return success;
}

void ShotHistoryStorage::requestCreateBackup(const QString& destPath)
{
    if (m_backupInProgress) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Backup already in progress";
        emit backupFinished(false, QString());
        return;
    }

    if (m_dbPath.isEmpty()) {
        emit errorOccurred("Database path not set");
        emit backupFinished(false, QString());
        return;
    }

    m_backupInProgress = true;

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    runDetachedDbThread([this, dbPath, destPath, destroyed]() {
        QString resultPath = createBackupStatic(dbPath, destPath);

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, resultPath, destroyed]() {
            if (*destroyed) {
                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "backup callback dropped (object destroyed)";
                return;
            }
            m_backupInProgress = false;
            emit backupFinished(!resultPath.isEmpty(), resultPath);
        }, Qt::QueuedConnection);
    });

}

void ShotHistoryStorage::checkpoint()
{
    if (!m_db.isOpen()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "checkpoint: Database not open";
        return;
    }

    DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Starting checkpoint, dbPath:" << m_dbPath;
    DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Total shots:" << m_totalShots;

    QSqlQuery query(m_db);

    // First, try FULL checkpoint which waits for writers to finish
    if (query.exec("PRAGMA wal_checkpoint(FULL)")) {
        if (query.next()) {
            int busy = query.value(0).toInt();
            int log = query.value(1).toInt();
            int checkpointed = query.value(2).toInt();
            DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "FULL checkpoint - busy:" << busy
                     << "log:" << log << "checkpointed:" << checkpointed;
        }
    } else {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "FULL checkpoint failed:" << query.lastError().text();
    }

    // Then TRUNCATE to clean up WAL file
    if (query.exec("PRAGMA wal_checkpoint(TRUNCATE)")) {
        if (query.next()) {
            int busy = query.value(0).toInt();
            int log = query.value(1).toInt();
            int checkpointed = query.value(2).toInt();
            DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "TRUNCATE checkpoint - busy:" << busy
                     << "log:" << log << "checkpointed:" << checkpointed;
        }
    } else {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "TRUNCATE checkpoint failed:" << query.lastError().text();
    }

    // Verify file size after checkpoint
    QFile dbFile(m_dbPath);
    if (dbFile.exists()) {
        DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "Database file size after checkpoint:" << dbFile.size() << "bytes";
    } else {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Database file does not exist at:" << m_dbPath;
    }

    // Check WAL file
    QFile walFile(m_dbPath + "-wal");
    if (walFile.exists()) {
        DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "WAL file size:" << walFile.size() << "bytes";
    } else {
        DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "No WAL file (expected after successful checkpoint)";
    }
}

void ShotHistoryStorage::requestImportDatabase(const QString& filePath, bool merge)
{
    if (m_importInProgress) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Import already in progress";
        emit errorOccurred("Import already in progress");
        emit importDatabaseFinished(false);
        return;
    }

    if (m_dbPath.isEmpty()) {
        emit errorOccurred("Database not open");
        emit importDatabaseFinished(false);
        return;
    }

    // Clean up file path on main thread (pure string manipulation)
    QString cleanPath = filePath;
    if (cleanPath.startsWith("file:///")) {
        cleanPath = cleanPath.mid(8);  // Remove "file:///"
#ifdef Q_OS_WIN
        // On Windows, file:///C:/path becomes C:/path
#else
        cleanPath = "/" + cleanPath;  // On Unix, need leading /
#endif
    } else if (cleanPath.startsWith("file://")) {
        cleanPath = cleanPath.mid(7);
    }

    m_importInProgress = true;

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    runDetachedDbThread([this, dbPath, cleanPath, merge, destroyed]() {
        bool success = importDatabaseStatic(dbPath, cleanPath, merge);

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, success, destroyed]() {
            if (*destroyed) {
                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "importDatabase callback dropped (object destroyed)";
                return;
            }
            m_importInProgress = false;
            if (success) {
                refreshTotalShots();
            } else {
                emit errorOccurred("Database import failed. The file may be corrupt or the disk may be full.");
            }
            emit importDatabaseFinished(success);
        }, Qt::QueuedConnection);
    });

}

// ============================================================================
// Thread-safe static methods (open their own connections, safe from any thread)
// ============================================================================

QString ShotHistoryStorage::createBackupStatic(const QString& dbPath, const QString& destPath)
{
    // VACUUM INTO writes the database as one read transaction sees it, WAL
    // included, without blocking writers (sqlite.org/lang_vacuum.html#vacuuminto).
    // It refuses an existing destination, hence the removal first.
    if (QFile::exists(destPath))
        QFile::remove(destPath);
    bool written = false;
    const bool opened = withTempDb(dbPath, QStringLiteral("backup"), [&](QSqlDatabase& db) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("VACUUM INTO ?"));
        query.addBindValue(destPath);
        written = query.exec();
        if (!written)
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "createBackupStatic: VACUUM INTO" << destPath
                << "failed:" << query.lastError().text();
    });
    if (!opened || !written) {
        QFile::remove(destPath);
        return QString();
    }
    DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "createBackupStatic: Created backup at" << destPath;
    return destPath;
}

bool ShotHistoryStorage::importDatabaseStatic(const QString& destDbPath, const QString& srcFilePath, bool merge,
                                              ImportResult* outResult)
{
    if (probeUpgradeFloor(srcFilePath) == UpgradeFloorProbe::BelowFloor) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: source is below the upgrade floor"
                                                 << UpgradeFloor::kSchemaVersion << "- nothing imported";
        if (outResult) {
            *outResult = ImportResult();
            outResult->sourceTooOld = true;
        }
        return false;
    }

    const QString connPrefix = QString("import_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);
    const QString srcConnName = connPrefix + "_src";
    const QString destConnName = connPrefix + "_dest";

    bool result = false;
    // Local, then copied out at every exit: the function is a web of
    // `goto cleanup`, and writing straight through the pointer would leave a
    // caller reading half-filled counts from an aborted import.
    ImportResult tally;
    {
        // Open source database
        QSqlDatabase srcDb = QSqlDatabase::addDatabase("QSQLITE", srcConnName);
        srcDb.setDatabaseName(srcFilePath);
        if (!srcDb.open()) {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to open source:" << srcDb.lastError().text();
            srcDb = QSqlDatabase();  // Release connection before removeDatabase
            QSqlDatabase::removeDatabase(srcConnName);
            return false;
        }

        // Open destination database
        QSqlDatabase destDb = QSqlDatabase::addDatabase("QSQLITE", destConnName);
        destDb.setDatabaseName(destDbPath);
        if (!destDb.open()) {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to open dest:" << destDb.lastError().text();
            srcDb.close();
            srcDb = QSqlDatabase();
            destDb = QSqlDatabase();  // Release connections before removeDatabase
            QSqlDatabase::removeDatabase(srcConnName);
            QSqlDatabase::removeDatabase(destConnName);
            return false;
        }

        // Set busy timeout so INSERTs retry on contention with main-thread connection
        QSqlQuery(destDb).exec("PRAGMA busy_timeout = 5000");

        // Verify source has shots table
        int sourceCount = 0;
        {
            QSqlQuery srcCheck(srcDb);
            if (!srcCheck.exec("SELECT COUNT(*) FROM shots")) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: No shots table in source";
                goto cleanup;
            }
            srcCheck.next();
            sourceCount = srcCheck.value(0).toInt();
        }

        DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Source has" << sourceCount << "shots";

        // Begin transaction on destination
        if (!destDb.transaction()) {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to begin transaction:" << destDb.lastError().text();
            goto cleanup;
        }

        if (!merge) {
            // Replace mode: delete all existing data
            QSqlQuery delQuery(destDb);
            if (!delQuery.exec("DELETE FROM shot_phases") ||
                !delQuery.exec("DELETE FROM shot_samples") ||
                !delQuery.exec("DELETE FROM shots")) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to clear data:" << delQuery.lastError().text();
                destDb.rollback();
                goto cleanup;
            }
            DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Cleared existing data for replace";
        }

        {
            // Import equipment packages first so shots and recipes can remap
            // their equipment_id to the new package ids.
            QHash<qint64, qint64> packageIdMap;
            if (!EquipmentStorage::importEquipmentStatic(srcDb, destDb, merge, packageIdMap)) {
                EQUIP_WARN_STDERR("Import", QStringLiteral("database import failed - equipment packages not imported"));
                destDb.rollback();
                goto cleanup;
            }
            // Published for the same reason as shotIdMap: an AI conversation is
            // keyed on the equipment package, so a caller carrying conversations
            // across this import must re-key them through these ids or the
            // restored thread is orphaned (see AIConversation::importConversationsStatic).
            tally.equipmentIdMap = packageIdMap;

            // Import coffee bags next so shots.bag_id can be remapped
            // (bean-bag-inventory): bag row ids change on insert, exactly
            // like shot ids.
            QHash<qint64, qint64> bagIdMap;
            if (!CoffeeBagStorage::importBagsStatic(srcDb, destDb, merge, bagIdMap)) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Bag import failed";
                destDb.rollback();
                goto cleanup;
            }

            // Import recipes after bags so shots.recipe_id can be remapped
            // (finish-recipes-first-class): recipe row ids change on insert,
            // exactly like bag ids. packageIdMap remaps each recipe's
            // equipment_id; bagIdMap remaps its bag_id (an unmatched source
            // bag becomes NULL → stale); the bean identity fields carry
            // verbatim as the relink matching key.
            QHash<qint64, qint64> recipeIdMap;
            if (!RecipeStorage::importRecipesStatic(srcDb, destDb, merge, recipeIdMap, packageIdMap,
                                                    bagIdMap)) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Recipe import failed";
                destDb.rollback();
                goto cleanup;
            }

            // Existing shots for merge mode: uuid -> destination id. The id half
            // is what lets a SKIPPED duplicate still contribute a shotIdMap entry
            // — a reference to that source shot must point at the copy we already
            // have, not be cleared as though the shot were missing.
            //
            // Two guards around this read. A 2026-08-21 field restore that
            // logged "Found 0 existing shots" prompted the audit that found
            // them — but that incident is NOT evidence for either guard, and
            // this comment used to claim it was. `sqlite_sequence` showed the
            // destination's shots table had been dropped and recreated, so it
            // was genuinely empty and the pre-read was right (see design.md;
            // do not go looking for a failing SELECT there). Each guard stands
            // on its own reasoning, stated at each one.
            QHash<QString, qint64> existingByUuid;
            QSet<qint64> occupiedIds;
            if (merge) {
                QSqlQuery uuidQuery(destDb);
                // GUARD 1: a failed read is NOT evidence of an empty destination.
                // This `if` used to have no `else`, so a query that failed for any
                // reason produced an empty set — indistinguishable from a genuinely
                // empty table, and the consequence of confusing the two is inserting
                // a second copy of the entire history.
                if (!uuidQuery.exec("SELECT uuid, id FROM shots")) {
                    tally.integrityFailure = QStringLiteral(
                        "the check for shots already present could not run (%1), so existing "
                        "shots could not be identified. Your existing shots were not changed.")
                        .arg(uuidQuery.lastError().text());
                    DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Aborting -"
                               << tally.integrityFailure;
                    destDb.rollback();
                    goto cleanup;
                }
                while (uuidQuery.next()) {
                    existingByUuid.insert(uuidQuery.value(0).toString(), uuidQuery.value(1).toLongLong());
                    // Which ids are already taken. Free, from the row we are
                    // already reading — see the id-preservation note at the
                    // INSERT for why an incoming shot keeps its own id.
                    occupiedIds.insert(uuidQuery.value(1).toLongLong());
                }

                // GUARD 2: a read that STOPPED EARLY is not evidence either.
                // `next()` returning false is how end-of-rows and a mid-scan
                // error look the same, so exec() succeeding says nothing about
                // whether the whole table was read. A restore is exactly when a
                // mid-scan SQLITE_BUSY is plausible: the backup thread and the
                // app's own connection are on the same file.
                //
                // Count the destination independently and require the two to
                // agree exactly. `shots.uuid` is TEXT UNIQUE NOT NULL (see
                // createTables), so a complete read yields precisely COUNT(*)
                // distinct keys — any shortfall means the scan was truncated,
                // and proceeding would re-insert the shots it never saw.
                //
                // This replaced a weaker `destShotsBefore > 0 && isEmpty()`
                // check. That form was reachable only by a read truncated at
                // row 0 — a strict subset of what the equality catches, since
                // UNIQUE NOT NULL means a COMPLETE read always yields exactly
                // COUNT(*) keys.
                //
                // Merge mode only — replace mode DELETEs first, so empty is expected.
                QSqlQuery countQuery(destDb);
                if (!countQuery.exec("SELECT COUNT(*) FROM shots") || !countQuery.next()) {
                    tally.integrityFailure = QStringLiteral(
                        "the existing shot history could not be counted (%1), so this import "
                        "could not be checked for safety. Your existing shots were not changed.")
                        .arg(countQuery.lastError().text());
                    DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Aborting -"
                               << tally.integrityFailure;
                    destDb.rollback();
                    goto cleanup;
                }
                tally.destShotsBefore = countQuery.value(0).toInt();

                if (existingByUuid.size() != *tally.destShotsBefore) {
                    tally.integrityFailure = QStringLiteral(
                        "this device holds %1 shot(s) but only %2 could be read back (%3), so "
                        "importing would have added a second copy of the rest. Your existing "
                        "shots were not changed.")
                        .arg(*tally.destShotsBefore)
                        .arg(existingByUuid.size())
                        .arg(uuidQuery.lastError().isValid()
                                 ? uuidQuery.lastError().text()
                                 : QStringLiteral("no error was reported"));
                    DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Aborting -"
                               << tally.integrityFailure;
                    destDb.rollback();
                    goto cleanup;
                }

                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Destination holds"
                         << *tally.destShotsBefore << "shot(s);" << existingByUuid.size()
                         << "matched for de-duplication";
            }

            // Import shots
            // Bound straight to the tally rather than copied into it at the
            // success tail: a replace-mode INSERT failure aborts to `cleanup`
            // with shotIdMap already holding every row inserted before it, and
            // a deferred copy left the caller reading four zeros beside a
            // populated map.
            int& imported = tally.imported;
            int& skipped = tally.skipped;
            int& failed = tally.failed;

            // Where a colliding row goes. Past the highest id EITHER side uses,
            // so it can never take an id a later source row is entitled to.
            qint64 nextRelocatedId = 1;
            {
                QSqlQuery maxQuery(srcDb);
                if (maxQuery.exec("SELECT IFNULL(MAX(id), 0) FROM shots") && maxQuery.next())
                    nextRelocatedId = maxQuery.value(0).toLongLong() + 1;
                for (qint64 id : std::as_const(occupiedIds))
                    nextRelocatedId = std::max(nextRelocatedId, id + 1);
            }

            QSqlQuery srcShots(srcDb);
            if (!srcShots.exec("SELECT * FROM shots")) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to query source:" << srcShots.lastError().text();
                destDb.rollback();
                goto cleanup;
            }

            // Every column both databases have is copied by name, so a new shots
            // column crosses restores and transfers with no edit here. Not carried:
            // the row id (kept or relocated below), the row's own bookkeeping
            // timestamps, and the bean-repair flag, which queues work for this
            // device's Visualizer account.
            static const QSet<QString> kNotCarried = {
                QStringLiteral("id"), QStringLiteral("created_at"),
                QStringLiteral("updated_at"), QStringLiteral("bean_repair_pending")};
            static const QSet<QString> kNullAsZero = {
                QStringLiteral("channeling_detected"), QStringLiteral("grind_issue_detected"),
                QStringLiteral("skip_first_frame_detected"), QStringLiteral("pour_truncated_detected")};
            const QSqlRecord srcRecord = srcShots.record();
            QStringList columns{QStringLiteral("id")};
            QList<int> srcIndex{-1};
            {
                QSqlQuery destColumns(destDb);
                if (!destColumns.exec(QStringLiteral("PRAGMA table_info(shots)"))) {
                    DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to read destination columns:"
                                                             << destColumns.lastError().text();
                    destDb.rollback();
                    goto cleanup;
                }
                while (destColumns.next()) {
                    const QString name = destColumns.value(1).toString();
                    const int index = srcRecord.indexOf(name);
                    if (index >= 0 && !kNotCarried.contains(name)) {
                        columns << name;
                        srcIndex << index;
                    }
                }
            }
            const int srcWeightSource = srcRecord.indexOf(QStringLiteral("weight_source"));
            const int srcMounting = srcRecord.indexOf(QStringLiteral("portal_mounting_mode"));
            // Ids into the other storages are remapped, NULL when that row was not
            // imported; a reliable PORTAL estimate needs reference provenance.
            auto carried = [&](const QString& column, const QVariant& value) -> QVariant {
                if (column == QLatin1String("equipment_id")) {
                    const qint64 mapped = packageIdMap.value(value.toLongLong(), 0);
                    return mapped > 0 ? QVariant(mapped) : QVariant();
                }
                if (column == QLatin1String("bag_id")) {
                    const qint64 mapped = bagIdMap.value(value.toLongLong(), -1);
                    return mapped > 0 ? QVariant(mapped) : QVariant();
                }
                if (column == QLatin1String("recipe_id")) {
                    const qint64 mapped = recipeIdMap.value(value.toLongLong(), -1);
                    return (!value.isNull() && mapped > 0) ? QVariant(mapped) : QVariant();
                }
                if (column == QLatin1String("portal_estimate_reliable")) {
                    return value.toBool() && PortalTailAnalyzer::hasReferenceProvenance(
                        srcWeightSource >= 0 ? srcShots.value(srcWeightSource).toString() : QString(),
                        srcMounting >= 0 ? srcShots.value(srcMounting).toString() : QString());
                }
                if (value.isNull() && column == QLatin1String("beverage_type"))
                    return QStringLiteral("espresso");
                if (value.isNull() && kNullAsZero.contains(column))
                    return 0;
                return value;
            };
            QStringList placeholders;
            placeholders.fill(QStringLiteral("?"), columns.size());
            QSqlQuery insert(destDb);
            if (!insert.prepare(QStringLiteral("INSERT INTO shots (%1) VALUES (%2)")
                                    .arg(columns.join(QStringLiteral(", ")), placeholders.join(QStringLiteral(", "))))) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to prepare the shot insert:"
                                                         << insert.lastError().text();
                destDb.rollback();
                goto cleanup;
            }

            QSqlQuery srcSamples(srcDb);
            srcSamples.prepare("SELECT sample_count, data_blob FROM shot_samples WHERE shot_id = ?");
            QSqlQuery insertSample(destDb);
            insertSample.prepare("INSERT INTO shot_samples (shot_id, sample_count, data_blob) VALUES (?, ?, ?)");
            QSqlQuery srcPhases(srcDb);
            srcPhases.prepare("SELECT time_offset, label, frame_number, is_flow_mode, transition_reason FROM shot_phases WHERE shot_id = ?");
            QSqlQuery insertPhase(destDb);
            insertPhase.prepare("INSERT INTO shot_phases (shot_id, time_offset, label, frame_number, is_flow_mode, transition_reason) VALUES (?, ?, ?, ?, ?, ?)");

            while (srcShots.next()) {
                QString uuid = srcShots.value("uuid").toString();
                if (merge) {
                    const auto existing = existingByUuid.constFind(uuid);
                    if (existing != existingByUuid.constEnd()) {
                        // Already here. The source shot still MAPS — to the copy the
                        // destination already had — so a reference to it resolves
                        // instead of being cleared as though the shot were lost.
                        tally.shotIdMap.insert(srcShots.value("id").toLongLong(), *existing);
                        skipped++;
                        continue;
                    }
                }

                // KEEP THE SHOT'S OWN ID wherever it is free.
                //
                // This used to omit `id` entirely, so AUTOINCREMENT renumbered
                // every imported row — and since replace mode clears with
                // `DELETE`, which does not reset sqlite_sequence, restoring your
                // own backup into an empty table moved 1135 shots from ids
                // 1..1172 to 1173..2307. Nothing needed those new numbers. The
                // renumbering is what makes an external reference to a shot go
                // stale, which is the entire defect this file's ImportResult
                // exists to work around; not renumbering removes most of it at
                // the source rather than following it afterwards.
                //
                // Replace mode: the table was just cleared, so every source id
                // is free and the database comes back exactly as backed up.
                // Merge mode: existing rows are never touched, and an incoming
                // shot keeps its id unless that id is already taken — only then
                // does it get a new one, which is what shotIdMap is still for.
                // Binding NULL lets AUTOINCREMENT assign, as before.
                // A relocated row takes an id ABOVE everything either database
                // uses, rather than letting AUTOINCREMENT pick the next free
                // one. Otherwise relocating an early collision consumes an id
                // that a LATER source row still owns: with destination ids 1-2
                // and source ids 1-3, auto-assign moved source 1 to 3, and
                // source 3 — whose own id was free — was pushed to 5. Handing
                // out ids past both maxima cannot collide with a kept id.
                const qint64 srcId = srcShots.value("id").toLongLong();
                const bool keepId = srcId > 0 && !occupiedIds.contains(srcId);
                insert.bindValue(0, keepId ? QVariant(srcId) : QVariant(nextRelocatedId++));
                for (qsizetype i = 1; i < columns.size(); ++i)
                    insert.bindValue(int(i), carried(columns.at(i), srcShots.value(srcIndex.at(i))));

                if (!insert.exec()) {
                    DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to import shot:" << insert.lastError().text();
                    failed++;
                    if (!merge) {
                        // In replace mode, existing data was deleted — abort to rollback
                        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Aborting replace-mode import due to INSERT failure";
                        destDb.rollback();
                        goto cleanup;
                    }
                    continue;
                }

                const qint64 oldId = srcId;
                const qint64 newId = insert.lastInsertId().toLongLong();
                // No `newId <= 0` guard here, deliberately. `shots.id` is
                // INTEGER PRIMARY KEY and the exec() above returned true, so
                // SQLite has assigned a rowid and reports it; there is no
                // honest path to 0. A guard was added here during review and
                // removed again: it `continue`d AFTER a committed INSERT,
                // leaving a shot with no samples and no phases, counted
                // `failed` while present in the database, absent from the map
                // (which means "clear the reference" for a shot that exists),
                // and — in replace mode — skipping the rollback every other
                // insert-side failure performs. It made the log and the
                // database disagree to guard a state the schema forbids.
                //
                // Reserve it so a later source row cannot claim the same id.
                // (A well-formed source cannot contain a duplicate id, but this
                // set is also what the NEXT row's `keepId` test reads, and a
                // source that has been hand-edited should not silently collide.)
                occupiedIds.insert(newId);

                // The renumbering, recorded — now usually an identity mapping.
                // Everything that referenced oldId outside this database has to
                // follow it to newId, which is the same value whenever the id
                // was free.
                tally.shotIdMap.insert(oldId, newId);

                // Import samples
                srcSamples.bindValue(0, oldId);
                if (!srcSamples.exec()) {
                    // Distinct from "this shot has no samples": collapsing the
                    // two imports a graphless shot and counts it a success,
                    // with nothing in the log to explain the missing graph.
                    DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to read samples for shot"
                               << uuid << ":" << srcSamples.lastError().text();
                } else if (srcSamples.next()) {
                    insertSample.bindValue(0, newId);
                    insertSample.bindValue(1, srcSamples.value(0));
                    insertSample.bindValue(2, srcSamples.value(1));
                    if (!insertSample.exec()) {
                        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to import sample for shot"
                                   << uuid << ":" << insertSample.lastError().text();
                    }
                }

                srcPhases.bindValue(0, oldId);
                if (!srcPhases.exec()) {
                    DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to query phases for shot"
                               << uuid << ":" << srcPhases.lastError().text();
                }
                while (srcPhases.next()) {
                    insertPhase.bindValue(0, newId);
                    for (int i = 0; i < 4; ++i)
                        insertPhase.bindValue(i + 1, srcPhases.value(i));
                    insertPhase.bindValue(5, srcPhases.value(4).toString());
                    if (!insertPhase.exec()) {
                        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to import phase for shot"
                                   << uuid << ":" << insertPhase.lastError().text();
                    }
                }

                imported++;
            }

            // After the shot loop: anchors reference shots, so both id maps must be complete.
            if (!EquipmentStorage::importCalibrationAnchorsStatic(srcDb, destDb, tally.equipmentIdMap,
                                                                  tally.shotIdMap)) {
                destDb.rollback();
                goto cleanup;
            }

            // Replace mode restores the DATABASE, so the id sequence should
            // match the backup as well as the rows: DELETE does not reset
            // sqlite_sequence. Merge mode keeps the destination's own sequence.
            if (!merge) {
                QSqlQuery seqFix(destDb);
                if (!seqFix.exec("UPDATE sqlite_sequence SET seq = "
                                 "(SELECT IFNULL(MAX(id), 0) FROM shots) WHERE name = 'shots'"))
                    DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: could not realign"
                               << "sqlite_sequence after replace:" << seqFix.lastError().text();
            }

            if (!destDb.commit()) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Failed to commit:" << destDb.lastError().text();
                destDb.rollback();
                goto cleanup;
            }

            // destShotsBefore is what makes this line answer a question the old
            // one could not: did this merge land in an empty database or a
            // populated one? Both used to print the same thing. It is only
            // measured in merge mode, so say "not measured" rather than print a
            // 0 that reads as "the database was empty".
            DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "importDatabaseStatic: Import complete - destination held"
                     << (tally.destShotsBefore ? QString::number(*tally.destShotsBefore)
                                               : QStringLiteral("(not measured, replace mode)"))
                     << "before;" << imported << "imported," << skipped
                     << "skipped," << failed << "failed";
            result = true;
        }

cleanup:
        srcDb.close();
        destDb.close();
    }
    QSqlDatabase::removeDatabase(srcConnName);
    QSqlDatabase::removeDatabase(destConnName);
    // One copy-out for every path that reaches here.
    //
    // On an INTEGRITY REFUSAL the map is empty — both guards fire before the
    // first INSERT. On a mid-import abort it is NOT: a replace-mode INSERT
    // failure rolls back with shotIdMap already holding every row inserted
    // before it, naming destination ids the rollback erased. Rather than leave
    // that trap for the three callers to each guard differently, the producer
    // clears it below.
    //
    // NOT every path reaches here: the two database-open failures above return
    // directly, before `cleanup`. They are the only exits that leave outResult
    // untouched, which is why every caller must treat its ImportResult as
    // meaningful only when this returned true — and why DataMigrationClient,
    // the one caller holding an ImportResult as a member, resets it at the top
    // of each run rather than trusting it to be overwritten.
    // A failed import's map names ids that do not exist. Clearing it here makes
    // every caller correct by construction, whichever way each phrases its own
    // "do we have a map" test. Counts are kept — they are diagnostic and true.
    if (!result) {
        tally.shotIdMap.clear();
        tally.equipmentIdMap.clear();
    }
    if (outResult)
        *outResult = tally;
    return result;
}

std::optional<QSet<qint64>> ShotHistoryStorage::existingShotIds(const QSet<qint64>& ids) const
{
    // Not ready is NOT "these shots do not exist" — see the header. The caller
    // deletes what does not resolve, so answering "nothing resolves" from a
    // database we never asked would strip every reference. m_ready is genuinely
    // false in the field: it is cleared when the post-backup reopen fails
    // (performDatabaseCopy, shothistorystorage.cpp:3943).
    if (!m_ready) return std::nullopt;
    if (ids.isEmpty()) return QSet<qint64>();

    // Primary-key IN over the distinct ids of one conversation's turns — a
    // couple of dozen at most, selecting only the id column. `EXPLAIN QUERY
    // PLAN` confirms `SEARCH shots USING INTEGER PRIMARY KEY (rowid=?)`, so it
    // touches the index and the matched rows, not the profile_json/debug_log
    // blobs that make a shots scan expensive.
    //
    // Measured against the maintainer's REAL database, pulled from the tablet
    // over /api/backup/shots (1061 shots, 18.1 MB), and against a synthetic 4x
    // copy of it (4244 shots, 45.1 MB). 200 runs per point, ids sampled at
    // random:
    //
    //     1061 shots   8 ids  median 0.178 ms   worst 1.022 ms  (cold cache)
    //                 16 ids  median 0.018 ms   worst 0.226 ms
    //                 32 ids  median 0.026 ms   worst 0.079 ms
    //     4244 shots   8 ids  median 0.016 ms   worst 0.038 ms
    //                 16 ids  median 0.024 ms   worst 0.126 ms
    //                 32 ids  median 0.045 ms   worst 0.150 ms
    //
    // Flat against a 4x row count and a 2.5x file, which is the point: the cost
    // tracks the id count, not the table's bytes. (The 4x copy was made by
    // re-INSERTing the shots table three times over with fresh uuids, carrying
    // every column including the blobs; it grows the file only 2.5x because
    // SQLite reuses freed pages and the index overhead does not triple.)
    //
    // The first row is the odd one and is NOT explained: 0.178 ms is 7-10x every
    // other median, and a median over 200 runs is not a cold-cache artefact the
    // way its 1.022 ms worst case is. That point was the first measured, so a
    // warm-up effect across the whole run is the obvious guess — but it was not
    // isolated, so it is recorded as unexplained rather than explained away.
    // It does not change the decision: 0.178 ms is still far inside budget.
    //
    // Caveat on all of these: taken on the maintainer's Mac via sqlite3 against
    // the tablet's database file, NOT in-app on the tablet's ARM hardware —
    // absolute figures there will be higher. What transfers is the shape (index
    // lookup, flat in table size), which is what decides inline vs threaded.
    //
    // Three callers reach this, none of them a binding or a per-sample path:
    // AIConversation::loadFromStorage via switchConversation (a tap in
    // ConversationOverlay.qml), via AIManager::loadMostRecentConversation at
    // startup, and via the MCP ai_advisor_invoke completion.
    //
    // The startup one used to skip this read entirely, because m_shotHistory is
    // still null in the AIManager constructor and is wired later from
    // MainController. An earlier version of this comment recorded that as the
    // read being "free" at startup. It was not free, it was a HOLE: the most
    // recently used conversation — the one the user is most likely to continue
    // — loaded unrepaired on every launch. AIManager::setShotHistoryStorage now
    // repairs the already-loaded conversation when the wiring arrives, so this
    // read happens once more per launch. That is the cost, and it is the
    // measured one below.
    // Re-derive if this ever moves onto a repeating path.
    QStringList placeholders;
    placeholders.reserve(ids.size());
    for (qsizetype i = 0; i < ids.size(); ++i)
        placeholders << QStringLiteral("?");

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM shots WHERE id IN (%1)")
                  .arg(placeholders.join(QLatin1Char(','))));
    for (qint64 id : ids)
        q.addBindValue(id);

    if (!q.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "existingShotIds: lookup failed, reporting"
                   << "\"could not answer\" rather than \"none exist\":" << q.lastError().text();
        return std::nullopt;
    }
    QSet<qint64> found;
    while (q.next())
        found.insert(q.value(0).toLongLong());
    // A scan that stopped early would under-report, and under-reporting here
    // deletes references. Same reasoning as GUARD 2 in importDatabaseStatic.
    if (q.lastError().isValid()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "existingShotIds: read stopped early, reporting"
                   << "\"could not answer\":" << q.lastError().text();
        return std::nullopt;
    }
    return found;
}

int ShotHistoryStorage::getShotCountStatic(const QString& dbPath)
{
    int count = -1;  // -1 = error (distinguishes from 0 = empty)
    withTempDb(dbPath, "shs_count", [&](QSqlDatabase& db) {
        QSqlQuery query(db);
        if (query.exec("SELECT COUNT(*) FROM shots") && query.next())
            count = query.value(0).toInt();
        else
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "getShotCountStatic: COUNT query failed:" << query.lastError().text();
    });
    return count;
}

qint64 ShotHistoryStorage::importShotRecord(const ShotRecord& record, bool overwriteExisting)
{
    if (!m_ready) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Cannot import - not ready";
        return -1;
    }
    // beginAttempts = 1: this overload runs synchronously on the GUI thread
    // (ShotImporter batches it from a timer), and each further attempt can spend
    // the full busy_timeout blocking the UI with no way to cancel.
    return importShotRecordStatic(m_db, record, overwriteExisting, 1);
}

void ShotHistoryStorage::importShotRecordAsync(const ShotRecord& record, bool overwriteExisting,
                                               std::function<void(qint64)> onDone)
{
    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, record, overwriteExisting,
                   ownedOnDone = std::move(onDone), destroyed]() {
        qint64 result = -1;
        withTempDb(dbPath, "shs_import", [&](QSqlDatabase& db) {
            result = importShotRecordStatic(db, record, overwriteExisting);
        });
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [finish = std::move(ownedOnDone), result, destroyed]() {
            if (*destroyed) return;
            if (finish) finish(result);
        }, Qt::QueuedConnection);
    });
}

qint64 ShotHistoryStorage::importShotRecordStatic(QSqlDatabase& db, const ShotRecord& record,
                                                  bool overwriteExisting, int beginAttempts)
{
    // The dedupe probes below are READ-ONLY: when they match a shot we are
    // replacing they only record its id, and the delete happens later, inside
    // the transaction. That ordering carries the whole correctness argument of
    // this function, so it is worth stating why it is not either of the two
    // shapes this code has had before:
    //
    //  - Deleting where the probe matches, outside a transaction, autocommits
    //    the delete. Any later failure then left the user's old shot destroyed
    //    with no replacement written. That is a real shot loss, reachable on
    //    shipped builds.
    //  - Opening the transaction first and probing inside it fixes that, but
    //    makes every import take the write lock — including the pure-duplicate
    //    case that writes nothing — which turns a "skipped" result into a
    //    "failed" one under contention and serialises bulk imports for no gain.
    //
    // Probing read-only and deleting inside the transaction gets both: a
    // duplicate we are not overwriting returns 0 having taken no lock at all,
    // and everything the import does destroy rolls back with it.
    QList<qint64> replaceIds;   // shots this import replaces; deleted inside the txn
    QSqlQuery query(db);

    // Check for duplicate by Visualizer id first, when the incoming record has
    // one (only the Visualizer recovery import sets it; .shot-file imports leave
    // it empty, so this probe is a no-op for them). This is the strongest key for
    // the sync-gap case: a shot that was pulled ON THIS DEVICE has a local uuid
    // keyed on its filename, so the recovery's visualizer-id-keyed uuid can never
    // match it — but if that local shot was uploaded, its visualizer_id column is
    // set to the same id we're importing, and matches here. Without this, dedupe
    // would fall through to the timestamp+profile_name check, which a shot with a
    // differently-formatted or later-renamed profile title can slip past and
    // re-import as a duplicate row.
    if (!record.visualizerId.isEmpty()) {
        query.prepare("SELECT id FROM shots WHERE visualizer_id = ?");
        query.bindValue(0, record.visualizerId);
        if (query.exec() && query.next()) {
            if (!overwriteExisting)
                return 0;   // already have this exact Visualizer shot, skip
            const qint64 existingId = query.value(0).toLongLong();
            if (!replaceIds.contains(existingId))
                replaceIds.append(existingId);
        }
    }

    // Check for duplicate by UUID
    query.prepare("SELECT id FROM shots WHERE uuid = ?");
    query.bindValue(0, record.summary.uuid);
    if (query.exec() && query.next()) {
        if (!overwriteExisting)
            return 0;   // duplicate found, skip
        const qint64 existingId = query.value(0).toLongLong();
        if (!replaceIds.contains(existingId))
            replaceIds.append(existingId);
    }

    // Also check by timestamp (within 5 seconds) and profile to catch near-duplicates
    query.prepare("SELECT id FROM shots WHERE ABS(timestamp - ?) < 5 AND profile_name = ?");
    query.bindValue(0, record.summary.timestamp);
    query.bindValue(1, record.summary.profileName);
    if (query.exec() && query.next()) {
        if (!overwriteExisting)
            return 0;   // near-duplicate found, skip
        const qint64 existingId = query.value(0).toLongLong();
        if (!replaceIds.contains(existingId))
            replaceIds.append(existingId);
    }

    // Release the probes' statement: it matched a row and was never stepped to
    // exhaustion, so it is still active, and an active statement holds a read
    // transaction open — which makes BEGIN IMMEDIATE fail instantly and
    // unrecoverably. See DbWriteTxn's precondition.
    query.finish();

    DbWriteTxn txn = DbWriteTxn::begin(db, "shot import", beginAttempts);
    if (!txn.ok()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "import could not start a transaction, skipping shot"
                   << record.summary.uuid;
        return -1;
    }

    // Now inside the transaction, so these roll back with everything else.
    for (const qint64 replaceId : std::as_const(replaceIds)) {
        if (!deleteShotStatic(db, replaceId)) {
            DIAG_WARN(STORAGE, "ShotHistoryStorage") << "import could not remove the shot it replaces"
                       << replaceId << "for" << record.summary.uuid
                       << "- aborting rather than leaving a duplicate";
            return -1;
        }
    }

    // Resolve the parsed grinder identity to an equipment package so the
    // imported shot keeps its grinder (the per-shot grinder_brand/model/burrs
    // columns are gone — migration 23; identity lives only on a package now).
    // Find-or-create on grinder identity, mirroring the live shot-save path,
    // then link via equipment_id. Empty identity → no package, NULL link.
    qint64 importEquipmentId = 0;
    if (!(record.grinderBrand.isEmpty() && record.grinderModel.isEmpty() && record.grinderBurrs.isEmpty())) {
        importEquipmentId = EquipmentStorage::findPackageByGrinderIdentityStatic(
            db, record.grinderBrand, record.grinderModel, record.grinderBurrs);
        if (importEquipmentId <= 0) {
            EquipmentPackage pkg;
            importEquipmentId = EquipmentStorage::createPackageWithGrinderStatic(
                db, pkg, record.grinderBrand, record.grinderModel, record.grinderBurrs);
        }
    }

    const PortalTailAnalyzer::PersistedSummary portalSummary = record.portalSamples.isEmpty()
        ? PortalTailAnalyzer::PersistedSummary{}
        : PortalTailAnalyzer::analyzeShot(record.portalSamples, record.weight, record.weightSource,
              record.portalMountingMode, record.summary.doseWeight, record.summary.finalWeight,
              record.pressure).persistedSummary();

    // Insert main shot record
    query.prepare(R"(
        INSERT INTO shots (
            uuid, timestamp, profile_name, profile_json, beverage_type,
            duration_seconds, final_weight, dose_weight,
            bean_brand, bean_type, roast_date, roast_level,
            grinder_setting, equipment_id, rpm,
            drink_tds, drink_ey, enjoyment, espresso_notes, bean_notes, barista,
            taste_balance, taste_body,
            visualizer_id, visualizer_url,
            profile_notes, debug_log,
            temperature_override, yield_override, yield_mode, yield_anchor_value,
            profile_kb_id,
            channeling_detected, grind_issue_detected,
            skip_first_frame_detected, pour_truncated_detected,
            flow_calibration, hot_water_result_json,
            portal_mean_ec, portal_integral_ec_g, portal_mean_temp_c,
            portal_estimate_reliable, portal_mounting_mode, weight_source, portal_present
        ) VALUES (
            :uuid, :timestamp, :profile_name, :profile_json, :beverage_type,
            :duration, :final_weight, :dose_weight,
            :bean_brand, :bean_type, :roast_date, :roast_level,
            :grinder_setting, :equipment_id, :rpm,
            :drink_tds, :drink_ey, :enjoyment, :espresso_notes, :bean_notes, :barista,
            :taste_balance, :taste_body,
            :visualizer_id, :visualizer_url,
            :profile_notes, :debug_log,
            :temperature_override, :yield_override, :yield_mode, :yield_anchor_value,
            :profile_kb_id,
            :channeling_detected, :grind_issue_detected,
            :skip_first_frame_detected, :pour_truncated_detected,
            :flow_calibration, :hot_water_result_json,
            :portal_mean_ec, :portal_integral_ec_g, :portal_mean_temp_c,
            :portal_estimate_reliable, :portal_mounting_mode, :weight_source, :portal_present
        )
    )");

    query.bindValue(":hot_water_result_json", record.hotWaterResultJson.isEmpty() ? QVariant() : record.hotWaterResultJson);
    query.bindValue(":uuid", record.summary.uuid);
    query.bindValue(":timestamp", record.summary.timestamp);
    query.bindValue(":profile_name", record.summary.profileName);
    query.bindValue(":profile_json", record.profileJson);
    query.bindValue(":beverage_type", record.summary.beverageType.isEmpty() ? QStringLiteral("espresso") : record.summary.beverageType);
    query.bindValue(":duration", record.summary.duration);
    query.bindValue(":final_weight", record.summary.finalWeight);
    query.bindValue(":dose_weight", record.summary.doseWeight);
    query.bindValue(":bean_brand", record.summary.beanBrand);
    query.bindValue(":bean_type", record.summary.beanType);
    query.bindValue(":roast_date", record.roastDate);
    query.bindValue(":roast_level", record.roastLevel);
    // Grinder identity is not snapshotted on the shot row (migration 23 dropped
    // the columns); it resolves via equipment_id to the package found/created
    // above from the parsed identity. The grind setting + rpm stay as per-shot
    // dial-in.
    query.bindValue(":grinder_setting", record.grinderSetting);
    query.bindValue(":equipment_id", importEquipmentId > 0 ? QVariant(importEquipmentId) : QVariant());
    query.bindValue(":rpm", record.rpm > 0 ? QVariant(record.rpm) : QVariant());
    query.bindValue(":drink_tds", record.drinkTds);
    query.bindValue(":drink_ey", record.drinkEy);
    query.bindValue(":enjoyment", record.summary.enjoyment);
    query.bindValue(":espresso_notes", record.espressoNotes);
    query.bindValue(":bean_notes", record.beanNotes);
    query.bindValue(":barista", record.barista);
    query.bindValue(":taste_balance", record.tasteBalance);
    query.bindValue(":taste_body", record.tasteBody);
    query.bindValue(":visualizer_id", record.visualizerId);
    query.bindValue(":visualizer_url", record.visualizerUrl);
    query.bindValue(":profile_notes", record.profileNotes);
    query.bindValue(":debug_log", record.debugLog);

    // Bind overrides (always have values - user override or profile default)
    query.bindValue(":temperature_override", record.temperatureOverride);
    query.bindValue(":yield_override", record.targetWeight);
    // Anchor provenance: a record that carries none (external-format imports
    // leave it empty) falls back to the migration-34 relabel of its target.
    {
        QString mode = YieldSpec::normalizedMode(record.yieldMode);
        double anchor = record.yieldAnchorValue;
        if (!YieldSpec::isSet(mode) && record.targetWeight > 0
            && record.yieldMode.isEmpty()) {
            mode = YieldSpec::modeAbsolute();
            anchor = record.targetWeight;
        }
        query.bindValue(":yield_mode", mode);
        query.bindValue(":yield_anchor_value", anchor > 0 ? QVariant(anchor) : QVariant());
        // NULL when the source carried no multiplier — an imported shot from a
        // pre-39 database, or any external format. Never defaulted to 1.0.
        query.bindValue(":flow_calibration",
                        record.flowCalibration > 0 ? QVariant(record.flowCalibration) : QVariant());
    }
    query.bindValue(":profile_kb_id", record.profileKbId.isEmpty() ? QVariant() : record.profileKbId);
    query.bindValue(":channeling_detected", record.channelingDetected ? 1 : 0);
    query.bindValue(":grind_issue_detected", record.grindIssueDetected ? 1 : 0);
    query.bindValue(":skip_first_frame_detected", record.skipFirstFrameDetected ? 1 : 0);
    query.bindValue(":pour_truncated_detected", record.pourTruncatedDetected ? 1 : 0);
    bindPortalColumns(query, portalSummary, record.portalMountingMode, record.weightSource,
                      !record.portalSamples.isEmpty());

    if (!query.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to import shot:" << query.lastError().text();
        return -1;
    }

    qint64 shotId = query.lastInsertId().toLongLong();

    // Compress and insert sample data
    QJsonObject root;
    root["pressure"] = pointsToJsonObject(record.pressure);
    root["flow"] = pointsToJsonObject(record.flow);
    root["temperature"] = pointsToJsonObject(record.temperature);
    root["temperatureMix"] = pointsToJsonObject(record.temperatureMix);
    root["weightSource"] = record.weightSource;
    root["portalMountingMode"] = record.portalMountingMode;
    if (!record.portalSamples.isEmpty())
        root["portalSamples"] = QJsonArray::fromVariantList(PortalSamples::toVariant(record.portalSamples));
    root["resistance"] = pointsToJsonObject(record.resistance);
    root["waterDispensed"] = pointsToJsonObject(record.waterDispensed);
    root["pressureGoal"] = pointsToJsonObject(record.pressureGoal);
    root["flowGoal"] = pointsToJsonObject(record.flowGoal);
    root["temperatureGoal"] = pointsToJsonObject(record.temperatureGoal);
    root["weight"] = pointsToJsonObject(record.weight);
    root["weightFlowRate"] = pointsToJsonObject(record.weightFlowRate);
    if (!record.weightFlowRateRaw.isEmpty())
        root["weightFlowRateRaw"] = pointsToJsonObject(record.weightFlowRateRaw);

    QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QByteArray compressedData = qCompress(json, 9);
    qsizetype sampleCount = record.pressure.size();

    query.prepare("INSERT INTO shot_samples (shot_id, sample_count, data_blob) VALUES (:id, :count, :blob)");
    query.bindValue(":id", shotId);
    query.bindValue(":count", sampleCount);
    query.bindValue(":blob", compressedData);

    if (!query.exec()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "Failed to insert imported samples:" << query.lastError().text();
        return -1;
    }

    // Insert phase markers
    for (const auto& marker : record.phases) {
        query.prepare(R"(
            INSERT INTO shot_phases (shot_id, time_offset, label, frame_number, is_flow_mode, transition_reason)
            VALUES (:shot_id, :time, :label, :frame, :flow_mode, :reason)
        )");
        query.bindValue(":shot_id", shotId);
        query.bindValue(":time", marker.time);
        query.bindValue(":label", marker.label);
        query.bindValue(":frame", marker.frameNumber);
        query.bindValue(":flow_mode", marker.isFlowMode ? 1 : 0);
        query.bindValue(":reason", marker.transitionReason);
        query.exec();  // Non-critical if markers fail
    }

    // A failed COMMIT means nothing was written, so returning shotId here would
    // report an imported shot that does not exist. (The guard has already rolled
    // back by this point, so nothing is left open on the connection.)
    if (!txn.commit()) {
        DIAG_WARN(STORAGE, "ShotHistoryStorage") << "import commit failed for" << record.summary.uuid
                   << "-" << txn.commitError();
        return -1;
    }

    return shotId;
}

void ShotHistoryStorage::refreshTotalShots()
{
    // Shot count moved, so anything derived from history may have moved too.
    emit historyDataChanged();

    // Run COUNT query on background thread to avoid blocking the main thread
    QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runOnDbThread([this, dbPath, destroyed]() {
        int count = getShotCountStatic(dbPath);
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, count, destroyed]() {
            if (*destroyed) {
                DIAG_DEBUG(STORAGE, "ShotHistoryStorage") << "refreshTotalShots callback dropped (object destroyed)";
                return;
            }
            if (count < 0) {
                DIAG_WARN(STORAGE, "ShotHistoryStorage") << "refreshTotalShots: count query failed, keeping previous count" << m_totalShots;
                return;
            }
            if (count != m_totalShots) {
                m_totalShots = count;
                emit totalShotsChanged();
            }
        }, Qt::QueuedConnection);
    });
}
