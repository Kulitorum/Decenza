#pragma once

#include <QString>
#include <QtGlobal>

// One equipment package: the shots the advisor may learn from.
//
// A value rather than a SQL fragment, so a scoped query cannot be written
// without one. Bucket 0 is the unpackaged pool, not "unknown".
//
// Distinct from two neighbouring questions: EquipmentStorage
// ::findPackageByGrinderIdentityStatic() folds accidental forks of one real
// package, and grind step size stays grinder-wide to match the Grind widget's
// grindStepForGrinder().
class AdviceScope
{
public:
    explicit AdviceScope(qint64 bucket) : m_bucket(bucket) {}

    qint64 bucket() const { return m_bucket; }

    // The bucket is embedded rather than bound: it is a qint64 from our own
    // schema, so there is no placeholder style to match and no bind to forget.
    // COALESCE is required — `equipment_id = 0` drops the NULL rows.
    QString sql(const QString& tableAlias = QString()) const
    {
        const QString column = tableAlias.isEmpty()
            ? QStringLiteral("equipment_id")
            : tableAlias + QStringLiteral(".equipment_id");
        return QStringLiteral("COALESCE(%1, 0) = %2")
            .arg(column, QString::number(m_bucket));
    }

    QString andSql(const QString& tableAlias = QString()) const
    {
        return QStringLiteral(" AND ") + sql(tableAlias);
    }

private:
    qint64 m_bucket = 0;
};
