#pragma once

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>
#include <QString>
#include <QDebug>

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
