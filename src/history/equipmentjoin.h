#pragma once

#include "shotprojection.h"

#include <QList>
#include <QSqlQuery>
#include <QString>
#include <QVariantMap>

// Attaching a shot's equipment package to a query, in one place.
//
// The package is four rows across two tables, so a read that reports the WHOLE
// package needs the same four LEFT JOINs and the same columns. They were written
// out per query instead, and the copies did not agree: the advisor's history
// read joined the grinder alone, so a session that switched baskets reached the
// model looking like one setup. Tracking a new component is one row in
// `columns()` below — the SELECT list and the reader follow from it.
//
// Scope, so this is not read as a claim it does not make: the consumer is
// `loadRecentShotsByKbIdStatic`. A query that needs only the package NAME
// (`requestAutoFavorites`) joins `equipment_packages` alone and is not a copy of
// this — pulling in four joins to read one column would cost more than it saves.
namespace EquipmentJoin {

struct Column {
    const char* expr;   // SQL expression, relative to the joins below
    const char* alias;  // stable name the readers look it up by
    const char* field;  // ShotProjection field name, "" when it has none
};

inline const QList<Column>& columns()
{
    static const QList<Column> cols = {
        { "eg.brand",                             "grinder_brand", "grinderBrand" },
        { "eg.model",                             "grinder_model", "grinderModel" },
        { "json_extract(eg.attrs, '$.burrs')",    "grinder_burrs", "grinderBurrs" },
        { "eb.brand",                             "basket_brand",  "basketBrand" },
        { "eb.model",                             "basket_model",  "basketModel" },
        // Puck prep has no brand — its flags ride in the item's model column as
        // a comma list ("puckScreen,rdt,shaker").
        { "epp.model",                            "puck_prep",     "puckPrep" },
        // The user's own name for the package ("Graph"). No projection field, and
        // the current reader does not use it — kept because it is part of the
        // package a caller may report, and the joins are already paid for.
        { "ep.name",                              "equipment_name", "" },
    };
    return cols;
}

// The SELECT list, comma-separated, each column under its alias.
inline QString selectList()
{
    QStringList parts;
    for (const Column& c : columns())
        parts << QStringLiteral("%1 AS %2").arg(QLatin1String(c.expr), QLatin1String(c.alias));
    return parts.join(QStringLiteral(", "));
}

// The joins. `shotAlias` is the shots table's alias in the caller's FROM.
inline QString joins(const QString& shotAlias = QStringLiteral("s"))
{
    return QStringLiteral(
        " LEFT JOIN equipment_items eg ON eg.package_id = %1.equipment_id AND eg.kind = 'grinder'"
        " LEFT JOIN equipment_items eb ON eb.package_id = %1.equipment_id AND eb.kind = 'basket'"
        " LEFT JOIN equipment_items epp ON epp.package_id = %1.equipment_id AND epp.kind = 'puckprep'"
        " LEFT JOIN equipment_packages ep ON ep.id = %1.equipment_id"
    ).arg(shotAlias);
}

// Copy the joined columns off an executed query into a shot map, keyed by the
// ShotProjection field names so ShotProjection::fromVariantMap picks them up.
inline void readInto(const QSqlQuery& query, QVariantMap& shot)
{
    for (const Column& c : columns()) {
        if (*c.field)
            shot[QLatin1String(c.field)] = query.value(QLatin1String(c.alias)).toString();
    }
}

} // namespace EquipmentJoin
