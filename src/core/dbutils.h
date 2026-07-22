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

// Outcome of beginImmediateTransaction().
//
// Only `Started` means "you own a transaction and must commit or roll it back
// exactly once". Every caller here refuses everything else, and new ones should
// too: test `!= TxnBegin::Started`, not `== TxnBegin::Locked`. Handling one
// failure state and falling through on the others runs the body with no
// transaction at all, which turns its rollbacks into silent no-ops.
//
// `Nested` is unreachable today — every caller runs on a fresh withTempDb
// connection in autocommit. It is distinguished anyway because an outer
// transaction is the one case where proceeding could be legitimate, and a caller
// that adopts one must NOT commit or roll back. If you find yourself wanting
// that, prefer restructuring so the transaction has a single owner.
enum class TxnBegin {
    Started,  // we own a fresh write transaction — commit or roll back exactly once
    Nested,   // already inside one, owned by an outer frame — do not terminate it
    Locked,   // another writer held the lock through every attempt
    Failed,   // anything else — see the logged error
};

// Starts a write transaction, retrying while another writer holds the lock.
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
// BEGIN IMMEDIATE takes the write lock up front, which is what lets busy_timeout
// absorb the contention, and concentrates it HERE: no other writer can exist once
// the lock is held, so the body never has to retry the read→write upgrade. COMMIT
// is the one statement that can still come back busy (see the commit retry in
// ShotHistoryStorage::saveShotStatic) — a caller that cannot afford to lose
// staged work should check it.
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
// a blocked worker thread, not the ~50 ms the backoff below suggests. Callers on
// the GUI thread should pass attempts = 1.
[[nodiscard]] inline TxnBegin beginImmediateTransaction(QSqlDatabase& db, const char* what, int attempts = 2) {
    QSqlQuery q(db);
    for (int attempt = 1; ; ++attempt) {
        if (q.exec(QStringLiteral("BEGIN IMMEDIATE")))
            return TxnBegin::Started;
        const QSqlError err = q.lastError();
        if (!isSqliteLockError(err)) {
            // A caller that already opened a transaction gets "cannot start a
            // transaction within a transaction" — a SQLITE_ERROR with no distinct
            // result code, so the message is the only signal available.
            if (err.text().contains(QLatin1String("within a transaction"), Qt::CaseInsensitive)) {
                // Logged, not silent: this is the one outcome whose caller-side
                // warning would otherwise carry no cause at all.
                qWarning() << "beginImmediateTransaction:" << what
                           << "is nested inside a transaction owned by an outer frame";
                return TxnBegin::Nested;
            }
            qWarning() << "beginImmediateTransaction:" << what << "failed:" << err.text();
            return TxnBegin::Failed;
        }
        if (attempt >= attempts) {
            qWarning() << "beginImmediateTransaction:" << what << "could not take the write lock after"
                       << attempts << "attempts:" << err.text();
            return TxnBegin::Locked;
        }
        // Backoff on top of the busy_timeout each attempt already waited out: a
        // backstop for a writer held past it.
        QThread::msleep(static_cast<unsigned long>(50 * attempt));
    }
}

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
