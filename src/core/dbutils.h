#pragma once

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>
#include <QObject>
#include <QString>
#include <QDebug>

#include <atomic>
#include <functional>
#include <memory>

// True when a statement failed because someone else holds the lock rather than
// for a reason retrying cannot fix. This is what separates "wait and try again"
// from "give up": a constraint violation must never be retried.
//
// The TEXT match is the primary detector, not a backstop — do not delete it as
// "fragile string matching". Qt enables SQLite's EXTENDED result codes by default
// (qsql_sqlite.cpp, `useExtendedResultCodes`, cleared only by a connect option
// this app never sets), and the failure this whole helper exists to catch — the
// WAL read→write upgrade — reports SQLITE_BUSY_SNAPSHOT = 517, not 5. So
// nativeErrorCode() is "517" and the numeric checks below miss it entirely; what
// catches it is sqlite3_errmsg's "database is locked". The numeric checks cover
// the plain SQLITE_BUSY (5) / SQLITE_LOCKED (6) cases and driver paths that
// report no text. QSqlError::text() is the database message with Qt's driver text
// appended.
inline bool isSqliteLockError(const QSqlError& e) {
    const QString code = e.nativeErrorCode();
    return code == QLatin1String("5") || code == QLatin1String("6")
        || e.text().contains(QLatin1String("locked"), Qt::CaseInsensitive)
        || e.text().contains(QLatin1String("busy"), Qt::CaseInsensitive);
}

// Owns a write transaction for its scope: rolls back on any exit that is not an
// explicit commit(). Construct with DbWriteTxn::begin() and check ok().
//
// Use this, NOT QSqlDatabase::transaction(), for any transaction that reads
// before it writes. Qt's SQLite driver issues a plain DEFERRED BEGIN (see
// QSQLiteDriver::beginTransaction()), which takes a read lock for the first
// SELECT and then has to UPGRADE it for the first write — and if another
// connection wrote in between, SQLite fails that upgrade IMMEDIATELY rather than
// waiting out busy_timeout. (In WAL, which this database uses, the read snapshot
// is stale and waiting could never succeed, so the busy handler is not consulted
// at all.) The PRAGMA busy_timeout that withTempDb sets is simply not reached, so
// the write fails on the first brush with contention.
//
// That is not theoretical here: four storages (shots, bags, equipment, recipes)
// share shots.db, each on its own worker thread, so their writes overlap by
// design — ordering is guaranteed per storage, not globally. It cost a real
// recipe save twice in a three-day log ("database is locked", and a "Couldn't
// save to the recipe" banner) before this existed.
//
// begin() issues BEGIN IMMEDIATE, which takes the write lock up front. That is
// what lets busy_timeout absorb the contention, and it concentrates the
// contention in begin(): no other writer can exist once the lock is held, so the
// body never has to retry the read→write upgrade. COMMIT is the one statement
// that can still come back busy (see the commit retry in
// ShotHistoryStorage::saveShotStatic, which keeps its own hand-rolled loop
// because it retries the whole INSERT, not just the begin) — so check commit().
//
// WHY A GUARD and not a status code: the obligation is "terminate this exactly
// once, and only if you started it". As a returned enum that was caller
// discipline, and it was already wrong at one of four sites within a day —
// shot import checked for one failure value and fell through on the others,
// running the whole body with no transaction, which silently turned its
// rollbacks into no-ops. Here an early return, a missed branch and a thrown
// exception all roll back, and a discarded guard is a compile error.
//
// A nested begin (a caller that already opened a transaction) is a FAILURE here,
// not something to adopt. Adopting would mean an inner rejection path leaves its
// partial writes staged in the outer transaction — the caller's rollback is not
// ours to perform, and not performing one is how a half-applied identity edit
// would reach a commit. Prefer restructuring so a transaction has one owner.
//
// PRECONDITION: no statement may be active on `db`. An unfinished QSqlQuery holds
// a read transaction open, and SQLite skips the busy handler while the connection
// sits in one — so BEGIN IMMEDIATE fails instantly and every retry fails the same
// way, however idle the database is. A SELECT that returned rows and was not
// stepped to exhaustion counts: call QSqlQuery::finish() first. (COUNT(*) always
// returns a row, so a count probe ALWAYS needs it.)
//
// COST: each failed attempt can itself wait the full busy_timeout (5 s via
// withTempDb) before returning, so the default bounds the wait at roughly 10 s of
// a blocked worker thread, not the ~50 ms the backoff suggests. Callers on the
// GUI thread should pass attempts = 1.
class [[nodiscard]] DbWriteTxn {
public:
    // Takes the write lock, retrying while another writer holds it. Check ok().
    static DbWriteTxn begin(QSqlDatabase& db, const char* what, int attempts = 2) {
        QSqlQuery q(db);
        for (int attempt = 1; ; ++attempt) {
            if (q.exec(QStringLiteral("BEGIN IMMEDIATE")))
                return DbWriteTxn(db);
            const QSqlError err = q.lastError();
            if (!isSqliteLockError(err)) {
                // A caller that already opened a transaction gets "cannot start a
                // transaction within a transaction" — a SQLITE_ERROR with no
                // distinct result code, so the message is the only signal there is.
                if (err.text().contains(QLatin1String("within a transaction"), Qt::CaseInsensitive))
                    qWarning() << "DbWriteTxn:" << what
                               << "is nested inside a transaction owned by an outer frame";
                else
                    qWarning() << "DbWriteTxn:" << what << "failed to begin:" << err.text();
                return DbWriteTxn();
            }
            if (attempt >= attempts) {
                qWarning() << "DbWriteTxn:" << what << "could not take the write lock after"
                           << attempts << "attempts:" << err.text();
                DbWriteTxn failed;
                failed.m_lockTimedOut = true;
                return failed;
            }
            // Backoff on top of the busy_timeout each attempt already waited out:
            // a backstop for a writer held past it.
            QThread::msleep(static_cast<unsigned long>(50 * attempt));
        }
    }

    ~DbWriteTxn() {
        // A failing rollback here means the transaction we thought we owned was
        // already terminated by someone else — in practice, a caller that
        // committed with the raw QSqlDatabase::commit() instead of commit()
        // below, which leaves this guard armed. That happened at two sites
        // within a day of the class existing, silently, because the return was
        // discarded. Logging it makes the next occurrence fail loudly instead:
        // the test suite runs with QTest::failOnWarning(), so any storage test
        // touching such a path now breaks, at every present and future site,
        // without anyone having to write a test for it.
        //
        // It also catches the genuinely dangerous case: on a long-lived
        // connection a failed ROLLBACK leaves the write transaction open, and
        // every later write on that connection silently joins it.
        if (m_db && !m_db->rollback())
            qWarning() << "DbWriteTxn: ROLLBACK failed — the transaction was already ended by "
                          "someone else (a raw db.commit()?), or is still open:"
                       << m_db->lastError().text();
    }

    DbWriteTxn(DbWriteTxn&& other) noexcept
        : m_db(other.m_db), m_lockTimedOut(other.m_lockTimedOut),
          m_commitError(std::move(other.m_commitError)) { other.m_db = nullptr; }
    DbWriteTxn(const DbWriteTxn&) = delete;
    DbWriteTxn& operator=(const DbWriteTxn&) = delete;
    DbWriteTxn& operator=(DbWriteTxn&&) = delete;

    // True when we hold a transaction. False means nothing was started and the
    // caller must abandon the work — never proceed unwrapped.
    bool ok() const { return m_db != nullptr; }

    // True when ok() is false BECAUSE another writer held the lock, as opposed to
    // a nested begin or a real error. The one failure a user can act on ("try
    // again"), so surfaces that show a message want to distinguish it.
    bool lockTimedOut() const { return m_lockTimedOut; }

    // Commits and releases. False if the COMMIT failed (the transaction is rolled
    // back in that case) or if we never held one. After this the guard is inert,
    // so the destructor does nothing.
    bool commit() {
        if (!m_db) {
            m_commitError = QStringLiteral("no transaction was held");
            return false;
        }
        QSqlDatabase* db = m_db;
        m_db = nullptr;
        if (db->commit())
            return true;
        // Capture now. A SUCCESSFUL rollback leaves lastError() alone — Qt's
        // driver only calls setLastError on failure — but a FAILING one
        // overwrites it, and it fails exactly when SQLite has already unwound
        // the transaction ("cannot rollback - no transaction is active"). In
        // that case a caller logging db.lastError() would name the rollback
        // instead of the commit, which is the case worth diagnosing.
        m_commitError = db->lastError().text();
        db->rollback();
        return false;
    }

    // Why the last commit() failed. Always set when commit() returns false,
    // including when the guard held no transaction. Empty after a success.
    QString commitError() const { return m_commitError; }

private:
    DbWriteTxn() = default;
    explicit DbWriteTxn(QSqlDatabase& db) : m_db(&db) {}

    QSqlDatabase* m_db = nullptr;  // non-null = we own an open transaction
    bool m_lockTimedOut = false;
    QString m_commitError;
};

// Opens a temporary QSQLITE connection, runs `work(db)`, then removes the connection.
// Sets PRAGMA busy_timeout and foreign_keys. Returns true if the DB opened successfully.
// Thread-safe: each call uses a unique connection name based on the current thread ID.
template<typename Work>
static bool withTempDb(const QString& dbPath, const QString& connPrefix, Work&& work) {
    const QString connName = connPrefix + QString("_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);
    bool opened = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            qWarning() << "withTempDb: DB open failed for" << connPrefix << ":" << db.lastError().text();
        } else {
            QSqlQuery(db).exec("PRAGMA busy_timeout = 5000");
            QSqlQuery(db).exec("PRAGMA foreign_keys = ON");
            opened = true;
            work(db);
        }
    }
    QSqlDatabase::removeDatabase(connName);
    return opened;
}

// Runs background DB work on a SINGLE long-lived worker thread, in submission
// (FIFO) order. This is the ordering guarantee a fresh-thread-per-request scheme
// does NOT give: there, two writes to the same row dispatched close together are
// run by independent threads and the OS scheduler can execute the older one last,
// silently clobbering the newer value (the equipment/bag dual-write reorder this
// helper was introduced to fix). SQLite's write lock serializes commits, but not
// in submission order — so the ordering has to be enforced before the lock.
//
// One worker per owning storage. The thread is created lazily on first use and
// torn down in the destructor. post()/run() (and thus the lazy first-use) must
// always be called from the SAME thread — the owner's — because they mutate
// m_thread/m_context without synchronization; a debug Q_ASSERT enforces this.
// Results are delivered on `receiver`'s thread, which need not be that thread.
class SerialDbWorker {
public:
    explicit SerialDbWorker(const QString& name = QStringLiteral("SerialDbWorker"))
        : m_name(name) {}
    ~SerialDbWorker() {
        if (!m_thread)
            return;
        m_thread->quit();
        m_thread->wait();
        // The thread's event loop has stopped, so nothing else touches m_context;
        // deleting it from here (a different thread than it lived on) is safe.
        delete m_context;
        delete m_thread;
    }
    SerialDbWorker(const SerialDbWorker&) = delete;
    SerialDbWorker& operator=(const SerialDbWorker&) = delete;

    // Post an arbitrary task to the worker thread (FIFO). The task runs on the
    // worker thread and is responsible for opening its own DB connection (via
    // withTempDb) and marshaling any result back to its owner's thread itself
    // (e.g. QMetaObject::invokeMethod(receiver, ..., Qt::QueuedConnection)). For
    // callers whose work receives a ready db handle, prefer run() below.
    void post(std::function<void()> task) {
        ensureStarted();
        QMetaObject::invokeMethod(m_context, std::move(task), Qt::QueuedConnection);
    }

    // Identifies which storage's worker this is in a thread dump.
    QString name() const { return m_name; }

    // Post `work(db)` to the worker thread (FIFO), then deliver `done(dbOpened)`
    // back on `receiver`'s thread via a queued call. `dbOpened` is false when the
    // connection could not be opened — in which case `work` never ran and any
    // captured result is empty/default. Note `dbOpened == true` means only that
    // the connection opened and `work` ran, NOT that `work` succeeded — a write
    // caller still reports its own success/failure from inside `work`. Read callers MUST gate their "Ready"
    // emission on `dbOpened`: delivering an empty result on an open *failure* is
    // indistinguishable from a genuine not-found, and a consumer that treats
    // empty as "row vanished" (e.g. SettingsDye clearing the active bag) would
    // wipe valid state on a transient DB hiccup. Write callers can ignore it —
    // their work already tracks success and reports a terminal status either way.
    // `destroyed` guards delivery if the owner is torn down while in flight.
    void run(const QString& dbPath, const QString& connPrefix,
             std::function<void(QSqlDatabase&)> work,
             std::function<void(bool dbOpened)> done,
             QObject* receiver,
             std::shared_ptr<std::atomic<bool>> destroyed) {
        post([dbPath, connPrefix, work = std::move(work), done = std::move(done),
              receiver, destroyed]() mutable {
            const bool dbOpened = withTempDb(dbPath, connPrefix, [&](QSqlDatabase& db) { work(db); });
            if (!dbOpened)
                qWarning() << "SerialDbWorker: failed to open DB for" << connPrefix;
            if (destroyed->load())
                return;
            QMetaObject::invokeMethod(receiver,
                [done = std::move(done), dbOpened, destroyed]() {
                    if (destroyed->load())
                        return;
                    done(dbOpened);
                }, Qt::QueuedConnection);
        });
    }

private:
    void ensureStarted() {
        // m_thread/m_context are touched here and in post() without a lock, so
        // every call must come from the one owner thread (see class comment).
        Q_ASSERT(!m_ownerThread || m_ownerThread == QThread::currentThread());
        if (m_thread)
            return;
        m_ownerThread = QThread::currentThread();
        m_thread = new QThread;
        m_thread->setObjectName(m_name);
        m_context = new QObject;          // event-loop affinity = the worker thread
        m_context->moveToThread(m_thread);
        m_thread->start();
    }

    QString m_name;
    QThread* m_ownerThread = nullptr;  // thread that first used this worker
    QThread* m_thread = nullptr;
    QObject* m_context = nullptr;
};
