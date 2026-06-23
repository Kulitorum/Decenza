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

    // Post `work(db)` to the worker thread (FIFO), then deliver `done()` back on
    // `receiver`'s thread via a queued call. `destroyed` guards delivery if the
    // owner is torn down while a task is in flight.
    void run(const QString& dbPath, const QString& connPrefix,
             std::function<void(QSqlDatabase&)> work,
             std::function<void()> done,
             QObject* receiver,
             std::shared_ptr<std::atomic<bool>> destroyed) {
        post([dbPath, connPrefix, work = std::move(work), done = std::move(done),
              receiver, destroyed]() mutable {
            if (!withTempDb(dbPath, connPrefix, [&](QSqlDatabase& db) { work(db); }))
                qWarning() << "SerialDbWorker: failed to open DB for" << connPrefix;
            if (destroyed->load())
                return;
            QMetaObject::invokeMethod(receiver,
                [done = std::move(done), destroyed]() {
                    if (destroyed->load())
                        return;
                    done();
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
