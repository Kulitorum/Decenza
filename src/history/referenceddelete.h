#pragma once

#include "core/dbutils.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

#include <utility>

namespace ReferencedDelete {

enum class Result { Deleted, Referenced, Failed };

// Run a resource's own reference-count policy and its delete statements under
// one IMMEDIATE transaction. The caller supplies the table-specific SQL and
// deletion body so recipe provenance and equipment lineage retain their own
// reference policies.
template<typename DeleteFn>
Result run(QSqlDatabase& db, const QString& referenceCountSql, qint64 id,
           const char* transactionName, DeleteFn&& deleteFn,
           qint64* referenceCount = nullptr, QString* failure = nullptr)
{
    if (referenceCount)
        *referenceCount = 0;
    if (failure)
        failure->clear();
    DbWriteTxn txn = DbWriteTxn::begin(db, transactionName);
    if (!txn.ok()) {
        if (failure)
            *failure = QStringLiteral("Could not start deletion transaction");
        return Result::Failed;
    }

    QSqlQuery countQuery(db);
    countQuery.prepare(referenceCountSql);
    countQuery.bindValue(QStringLiteral(":id"), id);
    if (!countQuery.exec() || !countQuery.next()) {
        if (failure)
            *failure = countQuery.lastError().text();
        return Result::Failed;
    }
    const qint64 count = countQuery.value(0).toLongLong();
    countQuery.finish();
    if (referenceCount)
        *referenceCount = count;
    if (count > 0)
        return Result::Referenced;

    QString deleteError;
    if (!std::forward<DeleteFn>(deleteFn)(db, id, &deleteError)) {
        if (failure)
            *failure = deleteError;
        return Result::Failed;
    }
    if (!txn.commit()) {
        if (failure)
            *failure = txn.commitError();
        return Result::Failed;
    }
    return Result::Deleted;
}

} // namespace ReferencedDelete
