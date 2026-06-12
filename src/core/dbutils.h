#pragma once

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QMutex>
#include <QDateTime>
#include <QDebug>

// --- DB-lock diagnostics (temporary, investigate/shot-save-db-lock) ---------
// Every background DB op goes through withTempDb, so a registry of currently
// open connections lets us name whoever holds the write lock when another
// connection (e.g. shs_save) gets SQLITE_BUSY. Logs nothing on its own — a
// caller dumps dbDiagActiveConnections() only when it actually hits a lock.
namespace dbdiag {
inline QMutex& mutex() { static QMutex m; return m; }
inline QHash<QString, qint64>& active() { static QHash<QString, qint64> a; return a; }
inline void noteOpen(const QString& name) {
    QMutexLocker lock(&mutex());
    active().insert(name, QDateTime::currentMSecsSinceEpoch());
}
inline void noteClose(const QString& name) {
    QMutexLocker lock(&mutex());
    active().remove(name);
}
}  // namespace dbdiag

// Snapshot of withTempDb connections open right now, excluding any whose name
// starts with `excludePrefix` (the caller's own). Each entry shows how long it
// has been open, so a long-lived holder stands out. Call this from a lock-error
// path to identify the concurrent writer.
inline QString dbDiagActiveConnections(const QString& excludePrefix = QString()) {
    QMutexLocker lock(&dbdiag::mutex());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList parts;
    for (auto it = dbdiag::active().constBegin(); it != dbdiag::active().constEnd(); ++it) {
        if (!excludePrefix.isEmpty() && it.key().startsWith(excludePrefix))
            continue;
        parts << QStringLiteral("%1 (open %2ms)").arg(it.key()).arg(now - it.value());
    }
    return parts.isEmpty() ? QStringLiteral("<none>") : parts.join(QStringLiteral(", "));
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
            dbdiag::noteOpen(connName);
            work(db);
            dbdiag::noteClose(connName);
        }
    }
    QSqlDatabase::removeDatabase(connName);
    return opened;
}
