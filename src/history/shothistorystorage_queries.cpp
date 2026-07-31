// ShotHistoryStorage queries — filtered list, recents-by-kbId, auto-favorites,
// distinct-value getters, grinder context. Split out of the main TU so the
// query / read-projection code lives separately from DB lifecycle, save,
// load+recompute, and the badge cascade. All concerns share the same
// `ShotHistoryStorage` class — these are member function definitions in a
// separate translation unit, no behaviour or API change.
//
// Owning concerns (per openspec/changes/split-shothistorystorage-by-concern/):
//   - filtered queries: requestShotsFiltered + buildFilterQuery + parseFilterMap +
//     formatFtsQuery (FTS5 query construction) + s_sortColumnMap (sort-column whitelist).
//   - recents-by-kbId: requestRecentShotsByKbId + loadRecentShotsByKbIdStatic.
//   - distinct-value getters: queryDistinctList + getDistinctValues +
//     getDistinct* getters + s_allowedColumns whitelist + sortGrinderSettings.
//   - auto-favorites: requestAutoFavorites + requestAutoFavoriteGroupDetails.
//   - grinder context: queryGrinderContext.

#include "shothistorystorage.h"
#include "shothistorystorage_internal.h"

#include "core/dbutils.h"
#include "core/grinderaliases.h"
#include "equipmentlogging.h"
#include "core/yieldspec.h"

#include <QSet>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QThread>
#include <QMap>
#include <algorithm>
#include <cmath>

using decenza::storage::detail::use12h;

// THE single place the getDistinct* family touches the database. Runs the query
// synchronously on m_db and returns the non-empty values in the order the SQL
// asked for.
//
// These used to go through an async cache: a getter returned {} on a miss, kicked
// off a background fetch, and relied on a distinctCacheReady() signal to make QML
// re-evaluate. That is the bug this change removes — requestDistinctCache()
// cleared every key
// but refilled only six bare columns, so composite keys were dropped by every
// shot save, and a re-fetch overtaken by a refresh was discarded in silence with
// nobody left to re-ask. Worse, the cache was invalidated far more often than it
// was read: every shot save, delete, and metadata edit (a rating, a note, one
// taste slider) wiped it and kicked a six-query refresh, to serve dialogs that
// might not open at all.
//
// Measured live on a real 18.5 MB / 1,124-shot database, fresh connection per
// run: 0.36-1.9 ms per column, 4.66 ms for all six. On a 16x copy (157 MB):
// 1.6-17 ms per column. Each getter is called when a dialog or picker opens.
QStringList ShotHistoryStorage::queryDistinctList(const QString& sql, const QVariantList& binds)
{
    if (!m_ready)
        return {};

    QSqlQuery q(m_db);
    if (!q.prepare(sql)) {
        qWarning() << "ShotHistoryStorage::queryDistinctList: prepare failed:"
                   << q.lastError().text() << "sql=" << sql;
        return {};
    }
    for (const QVariant& b : binds)
        q.addBindValue(b);
    if (!q.exec()) {
        qWarning() << "ShotHistoryStorage::queryDistinctList: query failed:"
                   << q.lastError().text() << "sql=" << sql;
        return {};
    }

    QStringList values;
    while (q.next()) {
        const QString v = q.value(0).toString();
        if (!v.isEmpty())
            values << v;
    }
    return values;
}

ShotFilter ShotHistoryStorage::parseFilterMap(const QVariantMap& filterMap)
{
    ShotFilter filter;
    filter.profileName = filterMap.value("profileName").toString();
    filter.beanBrand = filterMap.value("beanBrand").toString();
    filter.beanType = filterMap.value("beanType").toString();
    filter.grinderBrand = filterMap.value("grinderBrand").toString();
    filter.grinderModel = filterMap.value("grinderModel").toString();
    filter.grinderBurrs = filterMap.value("grinderBurrs").toString();
    filter.grinderSetting = filterMap.value("grinderSetting").toString();
    filter.roastLevel = filterMap.value("roastLevel").toString();
    filter.minEnjoyment = filterMap.value("minEnjoyment", -1).toInt();
    filter.maxEnjoyment = filterMap.value("maxEnjoyment", -1).toInt();
    filter.minDose = filterMap.value("minDose", -1).toDouble();
    filter.maxDose = filterMap.value("maxDose", -1).toDouble();
    filter.minYield = filterMap.value("minYield", -1).toDouble();
    filter.maxYield = filterMap.value("maxYield", -1).toDouble();
    filter.targetWeight = filterMap.value("targetWeight", -1).toDouble();
    filter.minDuration = filterMap.value("minDuration", -1).toDouble();
    filter.maxDuration = filterMap.value("maxDuration", -1).toDouble();
    filter.minTds = filterMap.value("minTds", -1).toDouble();
    filter.maxTds = filterMap.value("maxTds", -1).toDouble();
    filter.minEy = filterMap.value("minEy", -1).toDouble();
    filter.maxEy = filterMap.value("maxEy", -1).toDouble();
    filter.dateFrom = filterMap.value("dateFrom", 0).toLongLong();
    filter.dateTo = filterMap.value("dateTo", 0).toLongLong();
    filter.searchText = filterMap.value("searchText").toString();
    filter.onlyWithVisualizer = filterMap.value("onlyWithVisualizer", false).toBool();
    filter.filterChanneling = filterMap.value("filterChanneling", false).toBool();
    filter.filterGrindIssue = filterMap.value("filterGrindIssue", false).toBool();
    filter.filterSkipFirstFrame = filterMap.value("filterSkipFirstFrame", false).toBool();
    filter.filterPourTruncated = filterMap.value("filterPourTruncated", false).toBool();
    filter.sortColumn = filterMap.value("sortField", "timestamp").toString();
    filter.sortDirection = filterMap.value("sortDirection", "DESC").toString();
    return filter;
}

QString ShotHistoryStorage::buildFilterQuery(const ShotFilter& filter, QVariantList& bindValues)
{
    QStringList conditions;

    if (!filter.profileName.isEmpty()) {
        conditions << "profile_name = ?";
        bindValues << filter.profileName;
    }
    if (!filter.beanBrand.isEmpty()) {
        conditions << "bean_brand = ?";
        bindValues << filter.beanBrand;
    }
    if (!filter.beanType.isEmpty()) {
        conditions << "bean_type = ?";
        bindValues << filter.beanType;
    }
    // Grinder identity filters resolve through the equipment_id pointer rather
    // than the dropped grinder_brand/model/burrs columns (add-equipment-packages
    // task 4.1): match the equipment_items grinder row, then keep shots pointing
    // at one of those packages. The three fields combine into one subquery so
    // any subset (brand only, brand+model, …) works.
    {
        QStringList grinderItemConds;
        QVariantList grinderItemBinds;
        if (!filter.grinderBrand.isEmpty()) {
            grinderItemConds << "brand = ?";
            grinderItemBinds << filter.grinderBrand;
        }
        if (!filter.grinderModel.isEmpty()) {
            grinderItemConds << "model = ?";
            grinderItemBinds << filter.grinderModel;
        }
        if (!filter.grinderBurrs.isEmpty()) {
            grinderItemConds << "json_extract(attrs, '$.burrs') = ?";
            grinderItemBinds << filter.grinderBurrs;
        }
        if (!grinderItemConds.isEmpty()) {
            conditions << QString("equipment_id IN (SELECT package_id FROM equipment_items "
                                  "WHERE kind = 'grinder' AND %1)").arg(grinderItemConds.join(" AND "));
            bindValues << grinderItemBinds;
        }
    }
    if (!filter.grinderSetting.isEmpty()) {
        conditions << "grinder_setting = ?";
        bindValues << filter.grinderSetting;
    }
    if (!filter.roastLevel.isEmpty()) {
        conditions << "roast_level = ?";
        bindValues << filter.roastLevel;
    }
    if (filter.minEnjoyment >= 0) {
        conditions << "enjoyment >= ?";
        bindValues << filter.minEnjoyment;
    }
    if (filter.maxEnjoyment >= 0) {
        conditions << "enjoyment <= ?";
        bindValues << filter.maxEnjoyment;
    }
    if (filter.minDose >= 0) { conditions << "dose_weight >= ?"; bindValues << filter.minDose; }
    if (filter.maxDose >= 0) { conditions << "dose_weight <= ?"; bindValues << filter.maxDose; }
    if (filter.minYield >= 0) { conditions << "final_weight >= ?"; bindValues << filter.minYield; }
    if (filter.maxYield >= 0) { conditions << "final_weight <= ?"; bindValues << filter.maxYield; }
    if (filter.targetWeight >= 0) { conditions << "COALESCE(yield_override, 0) = ?"; bindValues << filter.targetWeight; }
    if (filter.minDuration >= 0) { conditions << "duration_seconds >= ?"; bindValues << filter.minDuration; }
    if (filter.maxDuration >= 0) { conditions << "duration_seconds <= ?"; bindValues << filter.maxDuration; }
    if (filter.minTds >= 0) { conditions << "drink_tds >= ?"; bindValues << filter.minTds; }
    if (filter.maxTds >= 0) { conditions << "drink_tds <= ?"; bindValues << filter.maxTds; }
    if (filter.minEy >= 0) { conditions << "drink_ey >= ?"; bindValues << filter.minEy; }
    if (filter.maxEy >= 0) { conditions << "drink_ey <= ?"; bindValues << filter.maxEy; }
    if (filter.dateFrom > 0) {
        conditions << "timestamp >= ?";
        bindValues << filter.dateFrom;
    }
    if (filter.dateTo > 0) {
        conditions << "timestamp <= ?";
        bindValues << filter.dateTo;
    }
    if (filter.onlyWithVisualizer) {
        conditions << "visualizer_id IS NOT NULL";
    }
    if (filter.filterChanneling) {
        conditions << "channeling_detected = 1";
    }
    if (filter.filterGrindIssue) {
        conditions << "grind_issue_detected = 1";
    }
    if (filter.filterSkipFirstFrame) {
        conditions << "skip_first_frame_detected = 1";
    }
    if (filter.filterPourTruncated) {
        conditions << "pour_truncated_detected = 1";
    }

    if (conditions.isEmpty()) {
        return QString();
    }
    return " WHERE " + conditions.join(" AND ");
}

QString ShotHistoryStorage::formatFtsQuery(const QString& userInput)
{
    // FTS5 tokenizes on punctuation (hyphens, slashes, etc)
    // So "D-Flow / Q" becomes tokens: "D", "Flow", "Q"
    // We need to split user input the same way to match

    QString cleaned = userInput.simplified();
    if (cleaned.isEmpty()) {
        return QString();
    }

    // Replace common punctuation with spaces so "d-flo" becomes "d flo"
    // This matches how FTS5 tokenizes the indexed data
    QString normalized = cleaned;
    normalized.replace(QRegularExpression("[\\-/\\.]"), " ");

    QStringList words = normalized.split(' ', Qt::SkipEmptyParts);
    QStringList terms;

    for (const QString& word : words) {
        // Escape double quotes by doubling them
        QString escaped = word;
        escaped.replace('"', "\"\"");
        // Escape single quotes (for SQL string literal embedding)
        escaped.replace('\'', "''");
        // Use prefix matching with * for partial word matches
        // Wrap in quotes to handle special characters
        terms << QString("\"%1\"*").arg(escaped);
    }

    // Join with AND (implicit in FTS5 when space-separated)
    return terms.join(" ");
}

// Whitelist for sort columns — maps user-facing keys to SQL ORDER BY expressions

static const QHash<QString, QString> s_sortColumnMap = {
    {"timestamp",        "timestamp"},
    {"profile_name",     "LOWER(profile_name)"},
    {"bean_brand",       "LOWER(bean_brand)"},
    {"bean_type",        "LOWER(bean_type)"},
    {"enjoyment",        "enjoyment"},
    {"ratio",            "CASE WHEN dose_weight > 0 THEN CAST(final_weight AS REAL) / dose_weight ELSE 0 END"},
    {"duration_seconds", "duration_seconds"},
    {"dose_weight",      "dose_weight"},
    {"final_weight",     "final_weight"},
};

void ShotHistoryStorage::requestShotsFiltered(const QVariantMap& filterMap, int offset, int limit)
{
    bool isAppend = (offset > 0);

    if (!m_ready) {
        emit shotsFilteredReady(QVariantList(), isAppend, 0);
        return;
    }

    ++m_filterSerial;
    int serial = m_filterSerial;
    const QString dbPath = m_dbPath;

    // Build SQL on main thread (pure computation, fast)
    ShotFilter filter = parseFilterMap(filterMap);
    QVariantList bindValues;
    QString whereClause = buildFilterQuery(filter, bindValues);

    QString orderByExpr = s_sortColumnMap.value(filter.sortColumn, "timestamp");
    QString sortDir = (filter.sortDirection == "ASC") ? "ASC" : "DESC";
    QString orderByClause = QString("ORDER BY %1 %2").arg(orderByExpr, sortDir);

    QString ftsQuery;
    if (!filter.searchText.isEmpty())
        ftsQuery = formatFtsQuery(filter.searchText);

    // Grinder identity is no longer in shots_fts (migration 23), so a free-text
    // search resolves the term against equipment_items and matches shots via the
    // equipment_id pointer (add-equipment-packages 4b.6) — without this, typing a
    // grinder name like "niche" finds nothing. Substring LIKE on the combined
    // brand/model/burrs identity, OR'd with the FTS hit below. Built by
    // concatenation (NOT QString::arg): the escaped LIKE value carries '%'
    // wildcards that would collide with arg's %N placeholders. The literal is
    // single-quote- and wildcard-escaped (ESCAPE '\') so user input is inert.
    QString grinderMatchClause;
    if (!ftsQuery.isEmpty()) {
        QString likeVal = filter.searchText.trimmed().toLower();
        likeVal.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_").replace('\'', "''");
        grinderMatchClause = QStringLiteral(
            " OR equipment_id IN (SELECT package_id FROM equipment_items WHERE kind = 'grinder' "
            "AND LOWER(IFNULL(brand,'') || ' ' || IFNULL(model,'') || ' ' || "
            "IFNULL(json_extract(attrs,'$.burrs'),'')) LIKE '%") + likeVal
            + QStringLiteral("%' ESCAPE '\\')");
    }

    QString sql;
    if (!ftsQuery.isEmpty()) {
        QString extraConditions;
        if (!whereClause.isEmpty()) {
            extraConditions = whereClause;
            extraConditions.replace(extraConditions.indexOf("WHERE"), 5, "AND");
        }
        const QString ftsMatch = QString("id IN (SELECT rowid FROM shots_fts WHERE shots_fts MATCH '%1')")
                                     .arg(ftsQuery);
        sql = QStringLiteral(
            "SELECT id, uuid, timestamp, profile_name, duration_seconds, "
            "final_weight, dose_weight, bean_brand, bean_type, "
            "enjoyment, visualizer_id, grinder_setting, "
            "temperature_override, yield_override, beverage_type, "
            "drink_tds, drink_ey, "
            "channeling_detected, grind_issue_detected, "
            "skip_first_frame_detected, pour_truncated_detected, rpm "
            "FROM shots WHERE (") + ftsMatch + grinderMatchClause + ") "
            + extraConditions + " " + orderByClause + " LIMIT ? OFFSET ?";
    } else {
        sql = QString(R"(
            SELECT id, uuid, timestamp, profile_name, duration_seconds,
                   final_weight, dose_weight, bean_brand, bean_type,
                   enjoyment, visualizer_id, grinder_setting,
                   temperature_override, yield_override, beverage_type,
                   drink_tds, drink_ey,
                   channeling_detected, grind_issue_detected,
                   skip_first_frame_detected, pour_truncated_detected, rpm
            FROM shots
            %1
            %2
            LIMIT ? OFFSET ?
        )").arg(whereClause).arg(orderByClause);
    }

    // Count SQL
    QString countSql;
    if (!ftsQuery.isEmpty()) {
        QString extraConditions;
        if (!whereClause.isEmpty()) {
            extraConditions = whereClause;
            extraConditions.replace(extraConditions.indexOf("WHERE"), 5, "AND");
        }
        const QString ftsMatch = QString("id IN (SELECT rowid FROM shots_fts WHERE shots_fts MATCH '%1')")
                                     .arg(ftsQuery);
        countSql = QStringLiteral("SELECT COUNT(*) FROM shots WHERE (") + ftsMatch
                   + grinderMatchClause + ") " + extraConditions;
    } else {
        countSql = "SELECT COUNT(*) FROM shots" + whereClause;
    }

    // Separate bind values: data query gets limit+offset appended
    QVariantList countBindValues = bindValues;
    bindValues << limit << offset;

    if (!m_loadingFiltered) {
        m_loadingFiltered = true;
        emit loadingFilteredChanged();
    }

    auto destroyed = m_destroyed;
    runDetachedDbThread(
        [this, dbPath, sql, countSql, bindValues, countBindValues, serial, isAppend, destroyed]() {
            QVariantList results;
            int totalCount = 0;

            withTempDb(dbPath, "shs_filter", [&](QSqlDatabase& db) {
                // Data query
                QSqlQuery query(db);
                if (query.prepare(sql)) {
                    for (int i = 0; i < bindValues.size(); ++i)
                        query.bindValue(i, bindValues[i]);

                    if (query.exec()) {
                        while (query.next()) {
                            QVariantMap shot;
                            shot["id"] = query.value(0).toLongLong();
                            shot["uuid"] = query.value(1).toString();
                            shot["timestamp"] = query.value(2).toLongLong();
                            shot["profileName"] = query.value(3).toString();
                            shot["durationSec"] = query.value(4).toDouble();
                            shot["finalWeightG"] = query.value(5).toDouble();
                            shot["doseWeightG"] = query.value(6).toDouble();
                            shot["beanBrand"] = query.value(7).toString();
                            shot["beanType"] = query.value(8).toString();
                            shot["enjoyment0to100"] = query.value(9).toInt();
                            shot["hasVisualizerUpload"] = !query.value(10).isNull();
                            shot["grinderSetting"] = query.value(11).toString();
                            shot["temperatureOverrideC"] = query.value(12).toDouble();
                            shot["targetWeightG"] = query.value(13).toDouble();
                            shot["beverageType"] = query.value(14).toString();
                            shot["drinkTdsPct"] = query.value(15).toDouble();
                            shot["drinkEyPct"] = query.value(16).toDouble();
                            shot["channelingDetected"] = query.value(17).toInt() != 0;
                            shot["grindIssueDetected"] = query.value(18).toInt() != 0;
                            shot["skipFirstFrameDetected"] = query.value(19).toInt() != 0;
                            shot["pourTruncatedDetected"] = query.value(20).toInt() != 0;
                            shot["rpm"] = query.value(21).toLongLong();  // RPM half of the dial-in

                            QDateTime dt = QDateTime::fromSecsSinceEpoch(
                                query.value(2).toLongLong());
                            shot["dateTime"] = dt.toString(use12h() ? "yyyy-MM-dd h:mm AP" : "yyyy-MM-dd HH:mm");

                            results.append(shot);
                        }
                    }
                }

                // Count query
                QSqlQuery countQuery(db);
                if (countQuery.prepare(countSql)) {
                    for (int i = 0; i < countBindValues.size(); ++i)
                        countQuery.bindValue(i, countBindValues[i]);
                    if (countQuery.exec() && countQuery.next())
                        totalCount = countQuery.value(0).toInt();
                }
            });

            if (*destroyed) return;
            QMetaObject::invokeMethod(
                this,
                [this, results = std::move(results), serial, isAppend, totalCount, destroyed]() mutable {
                    if (*destroyed) {
                        qDebug() << "ShotHistoryStorage: shotsFiltered callback dropped (object destroyed)";
                        return;
                    }
                    if (serial != m_filterSerial) return;
                    m_loadingFiltered = false;
                    emit loadingFilteredChanged();
                    emit shotsFilteredReady(results, isAppend, totalCount);
                },
                Qt::QueuedConnection);
        });

}


void ShotHistoryStorage::requestRecentShotsByKbId(const QString& kbId, int limit)
{
    if (!m_ready || kbId.isEmpty()) {
        emit recentShotsByKbIdReady(kbId, QVariantList());
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runDetachedDbThread([this, dbPath, kbId, limit, destroyed]() {
        QVariantList results;
        withTempDb(dbPath, "shs_kbid", [&](QSqlDatabase& db) {
            results = loadRecentShotsByKbIdStatic(db, kbId, limit);
        });

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, kbId, results = std::move(results), destroyed]() {
            if (*destroyed) {
                qDebug() << "ShotHistoryStorage: recentShotsByKbId callback dropped (object destroyed)";
                return;
            }
            emit recentShotsByKbIdReady(kbId, results);
        }, Qt::QueuedConnection);
    });

}

QVariantList ShotHistoryStorage::loadRecentShotsByKbIdStatic(QSqlDatabase& db, const QString& kbId, int limit, qint64 excludeShotId)
{
    QVariantList results;
    // Grinder identity resolved via the equipment_id pointer (add-equipment-
    // packages task 4.1); the per-shot grinder_brand/model/burrs columns are
    // dropped in migration 23. Aliases keep the value("grinder_*") reads below
    // unchanged. burrs is json_extract'd from the grinder item's attrs blob.
    QString sql = QStringLiteral(R"(
        SELECT s.id, s.timestamp, s.profile_name, s.duration_seconds, s.final_weight, s.dose_weight,
               s.bean_brand, s.bean_type, s.roast_level,
               eg.brand AS grinder_brand, eg.model AS grinder_model,
               json_extract(eg.attrs, '$.burrs') AS grinder_burrs,
               s.grinder_setting, s.drink_tds, s.drink_ey, s.enjoyment,
               s.espresso_notes, s.roast_date, s.temperature_override, s.yield_override, s.profile_json, s.beverage_type,
               s.stopped_by,
               s.frozen_date, s.defrost_date, s.storage_hint, s.opened_date
        FROM shots s
        LEFT JOIN equipment_items eg ON eg.package_id = s.equipment_id AND eg.kind = 'grinder'
        WHERE s.profile_kb_id = ?
    )");
    if (excludeShotId >= 0)
        sql += QStringLiteral(" AND s.id != ?");
    sql += QStringLiteral(" ORDER BY s.timestamp DESC LIMIT ?");

    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        qWarning() << "ShotHistoryStorage::loadRecentShotsByKbIdStatic: prepare failed:" << query.lastError().text();
        return results;
    }

    int idx = 0;
    query.bindValue(idx++, kbId);
    if (excludeShotId >= 0)
        query.bindValue(idx++, excludeShotId);
    query.bindValue(idx, limit);

    if (query.exec()) {
        while (query.next()) {
            QVariantMap shot;
            shot["id"] = query.value("id").toLongLong();
            qint64 ts = query.value("timestamp").toLongLong();
            shot["timestamp"] = ts;
            shot["profileName"] = query.value("profile_name").toString();
            shot["doseWeightG"] = query.value("dose_weight").toDouble();
            shot["finalWeightG"] = query.value("final_weight").toDouble();
            shot["durationSec"] = query.value("duration_seconds").toDouble();
            shot["enjoyment0to100"] = query.value("enjoyment").toInt();
            shot["grinderSetting"] = query.value("grinder_setting").toString();
            shot["grinderModel"] = query.value("grinder_model").toString();
            shot["grinderBrand"] = query.value("grinder_brand").toString();
            shot["grinderBurrs"] = query.value("grinder_burrs").toString();
            shot["espressoNotes"] = query.value("espresso_notes").toString();
            shot["beanBrand"] = query.value("bean_brand").toString();
            shot["beanType"] = query.value("bean_type").toString();
            shot["roastLevel"] = query.value("roast_level").toString();
            shot["roastDate"] = query.value("roast_date").toString();
            shot["drinkTdsPct"] = query.value("drink_tds").toDouble();
            shot["drinkEyPct"] = query.value("drink_ey").toDouble();
            shot["temperatureOverrideC"] = query.value("temperature_override").toDouble();
            shot["targetWeightG"] = query.value("yield_override").toDouble();
            shot["profileJson"] = query.value("profile_json").toString();
            shot["beverageType"] = query.value("beverage_type").toString();
            // Bean storage lifecycle (bean-freshness-followup): needed for the
            // dialInSessions per-shot hoist/override. Sparse-emit so an unset
            // field stays absent from the map (ShotProjection::fromVariantMap
            // defaults it empty), matching the toVariantMap sparse convention.
            {
                const QString fd = query.value("frozen_date").toString();
                if (!fd.isEmpty()) shot["frozenDate"] = fd;
                const QString dd = query.value("defrost_date").toString();
                if (!dd.isEmpty()) shot["defrostDate"] = dd;
                const QString sh = query.value("storage_hint").toString();
                if (!sh.isEmpty()) shot["storageHint"] = sh;
                const QString od = query.value("opened_date").toString();
                if (!od.isEmpty()) shot["openedDate"] = od;
            }
            // #1161: sparse-emit (see ShotProjection::toVariantMap) so a
            // future consumer of this map can't surface "profileEnd"/"".
            {
                const QString sb = query.value("stopped_by").toString();
                if (sb == QStringLiteral("manual")
                    || sb == QStringLiteral("weight")
                    || sb == QStringLiteral("volume"))
                    shot["stoppedBy"] = sb;
            }

            // ISO 8601 with timezone for API/AI consumption (CLAUDE.md convention).
            // Written into ShotProjection::timestampIso so it does not collide with
            // dateTime, which convertShotRecord populates with a locale-formatted
            // display string for QML.
            QDateTime dt = QDateTime::fromSecsSinceEpoch(ts);
            shot["timestampIso"] = dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate);

            results.append(shot);
        }
    } else {
        qWarning() << "ShotHistoryStorage::loadRecentShotsByKbIdStatic: query failed:" << query.lastError().text();
    }
    return results;
}

void ShotHistoryStorage::requestRankedProfilesForBean(const QString& beanBrand,
                                                      const QString& beanType,
                                                      const QString& roastLevel,
                                                      const QString& teaType)
{
    if (!m_ready) {
        emit rankedProfilesForBeanReady(QVariantMap());
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runDetachedDbThread([this, dbPath, beanBrand, beanType, roastLevel, teaType, destroyed]() {
        QVariantMap result;
        withTempDb(dbPath, "shs_rankedprof", [&](QSqlDatabase& db) {
            result = loadRankedProfilesForBeanStatic(db, beanBrand, beanType, roastLevel, teaType);
        });
        // Echo the queried bean so a caller can drop a stale reply that lands
        // after the user switched beans (the QML wizard's stale-reply guard).
        result.insert(QStringLiteral("queryBrand"), beanBrand);
        result.insert(QStringLiteral("queryType"), beanType);

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, result = std::move(result), destroyed]() {
            if (*destroyed) return;
            emit rankedProfilesForBeanReady(result);
        }, Qt::QueuedConnection);
    });

}

QVariantMap ShotHistoryStorage::loadRankedProfilesForBeanStatic(QSqlDatabase& db,
                                                                const QString& beanBrand,
                                                                const QString& beanType,
                                                                const QString& roastLevel,
                                                                const QString& teaType)
{
    // Recency, not frequency: a re-dial makes the old pairing stale, and
    // frequency is sticky in the wrong direction. Rows are
    // {profileName, lastUsed} (epoch seconds), newest pairing first.
    const auto runTier = [&db](const QString& sql,
                               const QVariantList& binds) -> QVariantList {
        QVariantList tier;
        QSqlQuery query(db);
        if (!query.prepare(sql)) {
            qWarning() << "ShotHistoryStorage::loadRankedProfilesForBeanStatic: prepare failed:"
                       << query.lastError().text();
            return tier;
        }
        for (qsizetype i = 0; i < binds.size(); ++i)
            query.bindValue(static_cast<int>(i), binds.at(i));
        if (!query.exec()) {
            qWarning() << "ShotHistoryStorage::loadRankedProfilesForBeanStatic: query failed:"
                       << query.lastError().text();
            return tier;
        }
        while (query.next()) {
            QVariantMap row;
            row["profileName"] = query.value(0).toString();
            row["lastUsed"] = query.value(1).toLongLong();
            tier.append(row);
        }
        return tier;
    };

    QVariantMap result;

    // Tier ①: profiles used with THIS bean (exact identity match).
    QVariantList withBean;
    if (!beanBrand.isEmpty() || !beanType.isEmpty()) {
        withBean = runTier(QStringLiteral(
            "SELECT profile_name, MAX(timestamp) AS last_used FROM shots "
            "WHERE COALESCE(bean_brand,'') = ? AND COALESCE(bean_type,'') = ? "
            "AND COALESCE(profile_name,'') != '' "
            "GROUP BY profile_name ORDER BY last_used DESC"),
            {beanBrand, beanType});
    }
    result["withBean"] = withBean;

    // Tier ②: profiles used with SIMILAR beans — same teaType (bag-blob JOIN)
    // for tea, same roast level for coffee — excluding this bean's own shots.
    QVariantList similar;
    if (!teaType.isEmpty()) {
        similar = runTier(QStringLiteral(
            "SELECT s.profile_name, MAX(s.timestamp) AS last_used FROM shots s "
            "JOIN coffee_bags b ON b.id = s.bag_id "
            "WHERE LOWER(COALESCE(json_extract(b.beanbase_json,'$.teaType'),'')) = LOWER(?) "
            "AND NOT (COALESCE(s.bean_brand,'') = ? AND COALESCE(s.bean_type,'') = ?) "
            "AND COALESCE(s.profile_name,'') != '' "
            "GROUP BY s.profile_name ORDER BY last_used DESC"),
            {teaType, beanBrand, beanType});
    } else if (!roastLevel.isEmpty()) {
        similar = runTier(QStringLiteral(
            "SELECT profile_name, MAX(timestamp) AS last_used FROM shots "
            "WHERE COALESCE(roast_level,'') = ? "
            "AND NOT (COALESCE(bean_brand,'') = ? AND COALESCE(bean_type,'') = ?) "
            "AND COALESCE(profile_name,'') != '' "
            "GROUP BY profile_name ORDER BY last_used DESC"),
            {roastLevel, beanBrand, beanType});
    }

    // Dedupe: a profile already in tier ① never repeats in tier ②.
    QSet<QString> seen;
    for (const QVariant& v : std::as_const(withBean))
        seen.insert(v.toMap().value(QStringLiteral("profileName")).toString());
    QVariantList similarDeduped;
    for (const QVariant& v : std::as_const(similar)) {
        if (!seen.contains(v.toMap().value(QStringLiteral("profileName")).toString()))
            similarDeduped.append(v);
    }
    result["similar"] = similarDeduped;

    return result;
}

void ShotHistoryStorage::requestLatestShotForBeanProfile(const QString& beanBrand,
                                                         const QString& beanType,
                                                         const QString& profileName)
{
    if (!m_ready || profileName.isEmpty() || (beanBrand.isEmpty() && beanType.isEmpty())) {
        emit latestShotForBeanProfileReady(QVariantMap());
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runDetachedDbThread([this, dbPath, beanBrand, beanType, profileName, destroyed]() {
        QVariantMap shot;
        withTempDb(dbPath, "shs_beanprof", [&](QSqlDatabase& db) {
            shot = loadLatestShotForBeanProfileStatic(db, beanBrand, beanType, profileName);
        });

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, shot = std::move(shot), destroyed]() {
            if (*destroyed) return;
            emit latestShotForBeanProfileReady(shot);
        }, Qt::QueuedConnection);
    });

}

QVariantMap ShotHistoryStorage::loadLatestShotForBeanProfileStatic(QSqlDatabase& db,
                                                                   const QString& beanBrand,
                                                                   const QString& beanType,
                                                                   const QString& profileName)
{
    QVariantMap shot;
    QSqlQuery query(db);
    // targetWeightG prefers the shot's SAW target (the user's intent) over
    // the landed final weight (target + stop error + drips).
    if (!query.prepare(QStringLiteral(
            "SELECT id, timestamp, dose_weight, yield_override, final_weight, "
            "temperature_override, grinder_setting, rpm, "
            "yield_mode, yield_anchor_value FROM shots "
            "WHERE COALESCE(bean_brand,'') = ? AND COALESCE(bean_type,'') = ? "
            "AND profile_name = ? "
            "ORDER BY timestamp DESC LIMIT 1"))) {
        qWarning() << "ShotHistoryStorage::loadLatestShotForBeanProfileStatic: prepare failed:"
                   << query.lastError().text();
        return shot;
    }
    query.bindValue(0, beanBrand);
    query.bindValue(1, beanType);
    query.bindValue(2, profileName);
    if (!query.exec()) {
        qWarning() << "ShotHistoryStorage::loadLatestShotForBeanProfileStatic: query failed:"
                   << query.lastError().text();
        return shot;
    }
    if (!query.next())
        return shot;

    shot["shotId"] = query.value("id").toLongLong();
    shot["timestamp"] = query.value("timestamp").toLongLong();
    shot["doseWeightG"] = query.value("dose_weight").toDouble();
    const double yieldOverride = query.value("yield_override").toDouble();
    shot["targetWeightG"] = yieldOverride > 0 ? yieldOverride
                                              : query.value("final_weight").toDouble();
    shot["temperatureOverrideC"] = query.value("temperature_override").toDouble();
    shot["grinderSetting"] = query.value("grinder_setting").toString();
    shot["rpm"] = query.value("rpm").toLongLong();
    // The shot's yield anchor (add-yield-ratio-anchor). The recipe wizard
    // prefills from this: without the mode it can only ever see grams, so a
    // bean+profile last pulled at 1:2 would seed a recipe with that ratio
    // FLATTENED to a frozen weight — the exact loss this change exists to
    // stop. Same NULL relabel as loadShotRecordStatic (a pre-34 row reads
    // absolute when a target was recorded, else none).
    const QVariant modeVal = query.value("yield_mode");
    shot["yieldMode"] = YieldSpec::normalizedMode(modeVal.toString());
    shot["yieldAnchorValue"] = query.value("yield_anchor_value").toDouble();
    if (modeVal.isNull() && shot["targetWeightG"].toDouble() > 0) {
        shot["yieldMode"] = YieldSpec::modeAbsolute();
        shot["yieldAnchorValue"] = shot["targetWeightG"].toDouble();
    }
    return shot;
}

void ShotHistoryStorage::requestLatestGrindForBean(const QString& beanBrand,
                                                   const QString& beanType,
                                                   const QString& roastLevel)
{
    if (!m_ready || (beanBrand.isEmpty() && beanType.isEmpty() && roastLevel.isEmpty())) {
        emit latestGrindForBeanReady(QVariantMap());
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    runDetachedDbThread([this, dbPath, beanBrand, beanType, roastLevel, destroyed]() {
        QVariantMap grind;
        withTempDb(dbPath, "shs_beangrind", [&](QSqlDatabase& db) {
            grind = loadLatestGrindForBeanStatic(db, beanBrand, beanType, roastLevel);
        });
        // Echo the query so QML can drop a stale reply (a "no grind" result is
        // still tagged so its own stale copy can be filtered — the QML side
        // keys the empty case on the absence of grinderSetting, not the map).
        grind.insert(QStringLiteral("queryBrand"), beanBrand);
        grind.insert(QStringLiteral("queryType"), beanType);
        grind.insert(QStringLiteral("queryRoast"), roastLevel);

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, grind = std::move(grind), destroyed]() {
            if (*destroyed) return;
            emit latestGrindForBeanReady(grind);
        }, Qt::QueuedConnection);
    });

}

QVariantMap ShotHistoryStorage::loadLatestGrindForBeanStatic(QSqlDatabase& db,
                                                             const QString& beanBrand,
                                                             const QString& beanType,
                                                             const QString& roastLevel)
{
    // Exact bean identity first; same roast level as the similar-bean
    // fallback. Only shots that actually recorded a grind qualify.
    const auto tryQuery = [&db](const QString& where, const QVariantList& binds) -> QVariantMap {
        QVariantMap grind;
        QSqlQuery query(db);
        if (!query.prepare(QStringLiteral(
                "SELECT grinder_setting, rpm, profile_name FROM shots WHERE %1 "
                "AND COALESCE(grinder_setting,'') != '' "
                "ORDER BY timestamp DESC LIMIT 1").arg(where))) {
            qWarning() << "ShotHistoryStorage::loadLatestGrindForBeanStatic: prepare failed:"
                       << query.lastError().text();
            return grind;
        }
        for (qsizetype i = 0; i < binds.size(); ++i)
            query.bindValue(static_cast<int>(i), binds.at(i));
        if (!query.exec()) {
            qWarning() << "ShotHistoryStorage::loadLatestGrindForBeanStatic: query failed:"
                       << query.lastError().text();
            return grind;
        }
        if (!query.next())
            return grind;
        grind["grinderSetting"] = query.value(0).toString();
        grind["rpm"] = query.value(1).toLongLong();
        grind["profileName"] = query.value(2).toString();
        return grind;
    };

    if (!beanBrand.isEmpty() || !beanType.isEmpty()) {
        QVariantMap exact = tryQuery(QStringLiteral(
            "COALESCE(bean_brand,'') = ? AND COALESCE(bean_type,'') = ?"),
            {beanBrand, beanType});
        if (!exact.isEmpty()) {
            exact["matchLevel"] = QStringLiteral("bean");
            return exact;
        }
    }
    if (!roastLevel.isEmpty()) {
        QVariantMap similar = tryQuery(QStringLiteral(
            "COALESCE(roast_level,'') = ?"), {roastLevel});
        if (!similar.isEmpty()) {
            similar["matchLevel"] = QStringLiteral("similarRoast");
            return similar;
        }
    }
    return {};
}

// convertShotRecord (the QVariantMap projection consumed by requestShot,
// ShotServer, and the AI advisor) lives in shothistorystorage_serialize.cpp.


// Estimator: the "common smallest adjustment" — the finest gap between the
// user's observed settings that they make REPEATEDLY. This is the single source
// of truth for a grinder's effective step, shared by the widget and the AI
// grinderContext. `sortedDistinct` MUST be sorted ascending and de-duplicated.
// Returns 0 when it cannot derive (fewer than 2 distinct values) so callers
// apply their own default.
//
// Why smallest-repeated rather than the raw minimum or the modal gap:
//   - raw minimum: a single mistyped setting (an 8.1 among 7.5/8/8.5/9) creates
//     a one-off 0.1 gap that would force the step to 0.1 forever.
//   - modal (most common) gap: a user who mostly makes coarse moves (0.5) with
//     occasional fine ones gets 0.5, hiding the 0.25 their grinder actually does.
// The step is the finest increment the user genuinely dials — the grinder's
// effective resolution. So take the SMALLEST gap that occurs at least twice: a
// real, repeated fine move survives; a lone typo or a one-off jump when
// switching beans (occurring once) is skipped. Fall back to the smallest gap
// only when nothing repeats (very sparse history). Clamped to a 0.05 floor.
static double deriveGrindStep(const QList<double>& sortedDistinct)
{
    if (sortedDistinct.size() < 2)
        return 0.0;

    // Histogram of consecutive gaps, rounded to 2 decimals to absorb float dirt
    // (e.g. 8.75 - 8.5 == 0.24999999). QMap keeps keys sorted ascending.
    QMap<double, int> freq;
    for (qsizetype i = 1; i < sortedDistinct.size(); ++i) {
        double gap = std::round((sortedDistinct[i] - sortedDistinct[i - 1]) * 100.0) / 100.0;
        if (gap > 0.0)
            freq[gap] += 1;
    }
    if (freq.isEmpty())
        return 0.0;

    // Smallest gap the user makes repeatedly (>= 2). Ascending iteration returns
    // the first such gap = the smallest common adjustment.
    for (auto it = freq.constBegin(); it != freq.constEnd(); ++it) {
        if (it.value() >= 2)
            return it.key() < 0.05 ? 0.05 : it.key();
    }

    // Nothing repeats (sparse/scattered history): fall back to the smallest gap.
    const double smallest = freq.constBegin().key();
    return smallest < 0.05 ? 0.05 : smallest;
}

// The grinder's distinct NUMERIC settings, sorted. The step is a property of the
// grinder (its effective dial resolution), not of the bean or the drink, so this
// is deliberately not scoped to either.
//
// Returning the values rather than the step is what lets both callers derive AND
// report from one query: the widget needs the sample count for its log line, and
// the AI's queryGrinderContext needs the values themselves.
//
// An empty model means "no grinder selected" and pools every grinder's history —
// including shots with no equipment row at all. That is a LAST RESORT, not a
// neutral default: pooling mixes every grinder's dial resolution into one
// estimate, and the caller gets no signal that it did. It reads as correct to a
// one-grinder user because the pool IS their grinder, so nothing has ever
// flagged it; it was found by reading the log attached to #1726, where the
// post-shot review reached this branch with an empty model.
//
// So callers must exhaust their own identity first, and both surfaces now do:
// GrindRowSource.grindStep() and ShotServer::handleGrindCandidatesApi each
// resolve to the ACTIVE grinder before querying. queryGrinderContext returns
// early on an empty model and never reaches here at all.
//
// What still legitimately pools is a user with no grinder anywhere — no
// injected identity and none selected — for whom every grinder IS the only
// pool available, and a grinder whose own history is too thin to derive from,
// where grindStep() prefers the pool to a blind 1.0 default. Both are second
// attempts after a scoped one failed, never a first choice.
//
// Cost, measured with a fresh connection per run: 3.3 ms median / 87 ms worst on
// a real 1,124-shot / 18.5 MB database; 37 ms median / 41 ms worst on a 16x copy
// (17,984 shots / 157 MB). The more expensive of the two read shapes in this file
// — a correlated equipment_id IN (SELECT ...) subquery over `shots`, whose cost
// tracks table BYTES because a page read drags the profile_json and debug_log
// blobs along. Two call paths reach it, and the frequency argument has to cover
// both: GrindRowSource.grindStep() and the ShotServer's grind-candidates
// endpoint build a picker's rows, while queryGrinderContext (below) builds the
// AI dialing/advisor context. Neither is per frame, per keystroke, or a binding.
//
// (Until #1725 the QML side WAS a binding, and this sentence described it as
// one; the cost is unchanged but the frequency is not, so do not read the old
// shape back into it. It then listed only the two picker callers, which read as
// exhaustive and was not — queryGrinderContext returns early on an EMPTY model,
// which is a narrower thing than never calling this.)
//
// `queryOk` reports whether the QUERY ran, not whether it found anything. A failed
// query and a grinder with no numeric history both yield an empty list, and the
// caller must not describe them the same way — see reportGrindStep.
static QList<double> grinderWideNumericSettings(QSqlDatabase& db, const QString& grinderModel,
                                                bool* queryOk = nullptr)
{
    if (queryOk) *queryOk = false;
    static const QString kAll = QStringLiteral(
        "SELECT DISTINCT grinder_setting FROM shots "
        "WHERE grinder_setting IS NOT NULL AND grinder_setting != ''");
    const QString kScoped = kAll + QStringLiteral(" AND ")
                          + ShotHistoryStorage::grinderModelMatchSql(":model");

    QSqlQuery q(db);
    if (!q.prepare(grinderModel.isEmpty() ? kAll : kScoped)) {
        qWarning() << "ShotHistoryStorage::grinderWideNumericSettings: prepare failed:"
                   << q.lastError().text() << "grinderModel=" << grinderModel;
        return {};
    }
    if (!grinderModel.isEmpty())
        q.bindValue(":model", grinderModel);
    if (!q.exec()) {
        qWarning() << "ShotHistoryStorage::grinderWideNumericSettings: query failed:"
                   << q.lastError().text() << "grinderModel=" << grinderModel;
        return {};
    }
    if (queryOk) *queryOk = true;
    QSet<double> numericSet;
    while (q.next()) {
        bool ok = false;
        const double v = q.value(0).toString().trimmed().toDouble(&ok);
        if (ok)
            numericSet.insert(v);
    }
    QList<double> numeric(numericSet.begin(), numericSet.end());
    std::sort(numeric.begin(), numeric.end());
    return numeric;
}

// RPM counterpart of grinderWideNumericSettings, over the integer shots.rpm
// column (rpm > 0 = a real dial-in), so the widget and the AI grinderContext
// never disagree.
//
// Returns the VALUES, not the step, for the same reason as its settings sibling:
// the caller needs the sample count for its log line. Returning only the step
// forced grindRpmStepForGrinder to report a hard-coded 0, so a successful
// derivation logged "= 100, derived from 0 distinct numeric setting(s)".
//
// REQUIRES a non-empty model — unlike its settings sibling there is no
// all-grinders branch, because pooling RPMs across different grinders describes
// no real dial. Both callers guard for that.
//
// Same shape and same cost as grinderWideNumericSettings above (3.3 ms median /
// 87 ms worst on a real 18.5 MB database), over the integer rpm column instead of
// the text one. `queryOk` reports whether the query RAN, so the caller can tell a
// failure from a grinder that simply has no recorded RPMs.
static QList<double> grinderWideRpms(QSqlDatabase& db, const QString& grinderModel,
                                     bool* queryOk = nullptr)
{
    if (queryOk) *queryOk = false;
    QSqlQuery q(db);
    const QString sql = QStringLiteral("SELECT DISTINCT rpm FROM shots WHERE %1 AND rpm > 0")
                            .arg(ShotHistoryStorage::grinderModelMatchSql(":model"));
    if (!q.prepare(sql)) {
        qWarning() << "ShotHistoryStorage::grinderWideRpms: prepare failed:"
                   << q.lastError().text() << "grinderModel=" << grinderModel;
        return {};
    }
    q.bindValue(":model", grinderModel);
    if (!q.exec()) {
        qWarning() << "ShotHistoryStorage::grinderWideRpms: query failed:"
                   << q.lastError().text() << "grinderModel=" << grinderModel;
        return {};
    }
    if (queryOk) *queryOk = true;
    QList<double> rpms;
    while (q.next()) {
        const int r = q.value(0).toInt();
        if (r > 0)
            rpms.append(r);
    }
    std::sort(rpms.begin(), rpms.end());
    return rpms;
}

GrinderContext ShotHistoryStorage::queryGrinderContext(QSqlDatabase& db,
    const QString& grinderModel, const QString& beverageType,
    const QString& beanBrand)
{
    GrinderContext ctx;
    if (grinderModel.isEmpty()) return ctx;

    ctx.model = grinderModel;
    ctx.beverageType = beverageType.isEmpty() ? QStringLiteral("espresso") : beverageType;

    // Build SQL with an optional bean_brand filter — same conditional-
    // append pattern used by loadRecentShotsByKbIdStatic and
    // buildFilterQuery in this file.
    // Grinder model resolves through the equipment_id pointer (the per-shot
    // grinder_model column is dropped in migration 23, add-equipment-packages
    // task 4.1). grinder_setting (per-shot dial-in) stays on the shot row.
    QString sql = QStringLiteral(
        "SELECT DISTINCT grinder_setting FROM shots WHERE %1 "
        "AND beverage_type = :bev "
        "AND grinder_setting != ''").arg(grinderModelMatchSql(":model"));
    if (!beanBrand.isEmpty()) {
        sql += QStringLiteral(" AND bean_brand = :brand");
    }

    QSqlQuery q(db);
    q.prepare(sql);
    q.bindValue(":model", grinderModel);
    q.bindValue(":bev", ctx.beverageType);
    if (!beanBrand.isEmpty()) {
        q.bindValue(":brand", beanBrand);
    }
    if (!q.exec()) {
        qWarning() << "ShotHistoryStorage::queryGrinderContext: query failed:"
                   << q.lastError().text()
                   << "grinderModel=" << grinderModel
                   << "beverageType=" << ctx.beverageType
                   << "beanBrand=" << beanBrand;
        return ctx;
    }

    QSet<double> numericSet;
    ctx.allNumeric = true;
    bool hasAny = false;

    while (q.next()) {
        QString s = q.value(0).toString().trimmed();
        if (s.isEmpty()) continue;
        hasAny = true;
        ctx.settingsObserved.append(s);
        bool ok;
        double v = s.toDouble(&ok);
        if (ok) {
            numericSet.insert(v);
        } else {
            ctx.allNumeric = false;
        }
    }

    if (!hasAny) {
        ctx.allNumeric = false;
        return ctx;
    }

    QList<double> numeric(numericSet.begin(), numericSet.end());
    std::sort(numeric.begin(), numeric.end());
    // stepSize is a GRINDER property, not bean- or beverage-scoped: derive it
    // grinder-model-wide (all beans/beverages) so it reflects the grinder's true
    // resolution and matches the widget exactly — a user who mostly makes coarse
    // moves on the current bean still gets the fine step their grinder can do.
    // (settingsObserved / min / max stay bean-scoped below — those are per-bean
    // context, unlike the step.)
    ctx.stepSize = deriveGrindStep(grinderWideNumericSettings(db, grinderModel));
    // min/max stay gated on an all-numeric history — a mixed list has no
    // meaningful numeric range to report.
    if (ctx.allNumeric && numeric.size() >= 2) {
        ctx.minSetting = numeric.first();
        ctx.maxSetting = numeric.last();
    }

    // RPM axis: the observed RPMs (and their range) are per-bean context, so
    // they stay bean/beverage-scoped like settingsObserved. rpm > 0 = a real
    // dial-in. ORDER BY rpm makes first()/last() the min/max.
    {
        QString rpmSql = QStringLiteral(
            "SELECT DISTINCT rpm FROM shots WHERE %1 "
            "AND beverage_type = :bev "
            "AND rpm > 0").arg(grinderModelMatchSql(":model"));
        if (!beanBrand.isEmpty())
            rpmSql += QStringLiteral(" AND bean_brand = :brand");
        rpmSql += QStringLiteral(" ORDER BY rpm");

        QSqlQuery rq(db);
        rq.prepare(rpmSql);
        rq.bindValue(":model", grinderModel);
        rq.bindValue(":bev", ctx.beverageType);
        if (!beanBrand.isEmpty())
            rq.bindValue(":brand", beanBrand);
        if (!rq.exec()) {
            qWarning() << "ShotHistoryStorage::queryGrinderContext: rpm query failed:"
                       << rq.lastError().text()
                       << "grinderModel=" << grinderModel;
        } else {
            while (rq.next()) {
                const int r = rq.value(0).toInt();
                if (r > 0)
                    ctx.rpmsObserved.append(r);
            }
            if (ctx.rpmsObserved.size() >= 2) {
                ctx.rpmMin = ctx.rpmsObserved.first();
                ctx.rpmMax = ctx.rpmsObserved.last();
            }
        }
    }
    // rpmStepSize is a GRINDER property like stepSize — grinder-model-wide, not
    // bean/beverage-scoped — so it matches the widget's grindRpmStepForGrinder.
    ctx.rpmStepSize = deriveGrindStep(grinderWideRpms(db, grinderModel));

    return ctx;
}


// The model compare is case- and whitespace-FOLDED so it agrees with the identity
// lookup that decides two packages are the same gear
// (findPackageByGrinderIdentityStatic, which has always used LOWER). An exact
// compare meant a model string differing only in case or padding — two packages
// the write path considers the same grinder — read back as a grinder with NO
// history. That is invisible: an empty result is indistinguishable from a new
// grinder, so the step silently falls back to 1.0 and the wheel loses the
// resolution the user actually dials.
//
// One definition for every model-scoped lookup in THIS file — there were six
// hand-written copies, which is exactly the drift CLAUDE.md's centralization rule
// describes. %1 is the placeholder style the call site uses (":model" or "?").
//
// One copy is deliberately NOT collapsed: src/ai/dialing_blocks.cpp:1194 inserts
// an extra burrs clause INSIDE the same subquery, and this helper emits that
// subquery's closing paren, so it cannot be reused without a second parameter
// nothing else would want. It must still fold identically — if you change the
// folding here, change it there.
QString ShotHistoryStorage::grinderModelMatchSql(const QString& placeholder)
{
    return QStringLiteral(
        "equipment_id IN (SELECT package_id FROM equipment_items "
        "WHERE kind = 'grinder' AND LOWER(TRIM(IFNULL(model,''))) = LOWER(TRIM(%1)))")
        .arg(placeholder);
}

static const QStringList s_allowedColumns = {
    "profile_name", "bean_brand", "bean_type",
    "grinder_setting", "barista", "roast_level"
};

QStringList ShotHistoryStorage::getDistinctValues(const QString& column)
{
    // The column name is interpolated, not bound — SQLite cannot bind an
    // identifier — so it must come from the allow-list or not at all.
    if (!s_allowedColumns.contains(column)) {
        qWarning() << "ShotHistoryStorage::getDistinctValues: rejected column" << column;
        return {};
    }
    return queryDistinctList(
        QStringLiteral("SELECT DISTINCT %1 FROM shots "
                       "WHERE %1 IS NOT NULL AND %1 != '' ORDER BY %1").arg(column));
}

QStringList ShotHistoryStorage::getDistinctBeanBrands()
{
    return getDistinctValues("bean_brand");
}

QStringList ShotHistoryStorage::getDistinctBeanTypes()
{
    return getDistinctValues("bean_type");
}

QStringList ShotHistoryStorage::getDistinctGrinders()
{
    // Grinder models come from the equipment inventory, not the dropped
    // shots.grinder_model column (add-equipment-packages task 4.2). All grinder
    // items across every package (inventory or superseded) so history retains
    // sold/retired grinders.
    return queryDistinctList(
        QStringLiteral("SELECT DISTINCT model FROM equipment_items "
                       "WHERE kind = 'grinder' AND model IS NOT NULL AND model != '' "
                       "ORDER BY model"));
}

QStringList ShotHistoryStorage::getDistinctGrinderSettings()
{
    QStringList settings = getDistinctValues("grinder_setting");
    sortGrinderSettings(settings);
    return settings;
}

QStringList ShotHistoryStorage::getDistinctBaristas()
{
    return getDistinctValues("barista");
}


void ShotHistoryStorage::requestAutoFavorites(const QString& groupBy, int maxItems)
{
    if (!m_ready) {
        emit autoFavoritesReady(QVariantList());
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    // Build SQL on main thread (pure string manipulation, fast)
    QString selectColumns;
    QString groupColumns;
    QString joinConditions;

    // "bean_profile_grinder_weight" shares grinder-level grouping and also splits
    // by target yield (exact) and dose rounded to the nearest 0.5 g, so shots with
    // different dose/yield targets on the same bean + profile + grinder get their
    // own cards.
    const bool weightAware = (groupBy == "bean_profile_grinder_weight");

    if (groupBy == "bean") {
        selectColumns = "COALESCE(bean_brand, '') AS gb_bean_brand, "
                        "COALESCE(bean_type, '') AS gb_bean_type";
        groupColumns = "COALESCE(bean_brand, ''), COALESCE(bean_type, '')";
        joinConditions = "COALESCE(s.bean_brand, '') = g.gb_bean_brand "
                         "AND COALESCE(s.bean_type, '') = g.gb_bean_type";
    } else if (groupBy == "profile") {
        selectColumns = "COALESCE(profile_name, '') AS gb_profile_name";
        groupColumns = "COALESCE(profile_name, '')";
        joinConditions = "COALESCE(s.profile_name, '') = g.gb_profile_name";
    } else if (groupBy == "bean_profile_grinder" || weightAware) {
        // Grinder identity is the equipment_id pointer, not the dropped
        // grinder_brand/model columns (add-equipment-packages task 4.1).
        // Grouping on equipment_id is equivalent to the old brand+model key
        // (shots with the same identity share a package) and additionally
        // honours burrs, which the old key ignored. grinder_setting (per-shot
        // dial-in) stays in the key so different settings still split cards.
        selectColumns = "COALESCE(bean_brand, '') AS gb_bean_brand, "
                        "COALESCE(bean_type, '') AS gb_bean_type, "
                        "COALESCE(profile_name, '') AS gb_profile_name, "
                        "COALESCE(equipment_id, 0) AS gb_equipment_id, "
                        "COALESCE(grinder_setting, '') AS gb_grinder_setting";
        groupColumns = "COALESCE(bean_brand, ''), COALESCE(bean_type, ''), "
                       "COALESCE(profile_name, ''), COALESCE(equipment_id, 0), "
                       "COALESCE(grinder_setting, '')";
        joinConditions = "COALESCE(s.bean_brand, '') = g.gb_bean_brand "
                         "AND COALESCE(s.bean_type, '') = g.gb_bean_type "
                         "AND COALESCE(s.profile_name, '') = g.gb_profile_name "
                         "AND COALESCE(s.equipment_id, 0) = g.gb_equipment_id "
                         "AND COALESCE(s.grinder_setting, '') = g.gb_grinder_setting";
    } else {
        // Default: bean_profile
        selectColumns = "COALESCE(bean_brand, '') AS gb_bean_brand, "
                        "COALESCE(bean_type, '') AS gb_bean_type, "
                        "COALESCE(profile_name, '') AS gb_profile_name";
        groupColumns = "COALESCE(bean_brand, ''), COALESCE(bean_type, ''), COALESCE(profile_name, '')";
        joinConditions = "COALESCE(s.bean_brand, '') = g.gb_bean_brand "
                         "AND COALESCE(s.bean_type, '') = g.gb_bean_type "
                         "AND COALESCE(s.profile_name, '') = g.gb_profile_name";
    }

    if (weightAware) {
        selectColumns += ", ROUND(COALESCE(dose_weight, 0) * 2) / 2.0 AS gb_dose_bucket, "
                         "COALESCE(yield_override, 0) AS gb_yield_override";
        groupColumns += ", ROUND(COALESCE(dose_weight, 0) * 2) / 2.0, "
                        "COALESCE(yield_override, 0)";
        joinConditions += " AND ROUND(COALESCE(s.dose_weight, 0) * 2) / 2.0 = g.gb_dose_bucket "
                          "AND COALESCE(s.yield_override, 0) = g.gb_yield_override";
    }

    // dose_weight is always the raw latest shot's dose so dialing-in users see
    // (and load) their most recent setting, even while the 0.5 g bucket keeps
    // 18.1 / 18.2 shots collapsed into one card in weight mode.
    //
    // yield_override is the latest shot's saved target yield (for the chip's
    // "dose → yield" display). Weight mode substitutes the group's exact bucket
    // value, which is the same number by grouping. When the latest shot has no
    // saved override (legacy rows), QML's recipeYield() helper falls back to
    // finalWeight.
    //
    // dose_bucket exposes the group's rounded dose separately so Info / Show
    // can filter by the bucket range even though the card displays raw dose.
    const QString yieldCol = weightAware ? "g.gb_yield_override AS yield_override" : "s.yield_override";
    const QString bucketCol = weightAware ? "g.gb_dose_bucket AS dose_bucket" : "0 AS dose_bucket";

    QString sql = QString(
        "SELECT s.id, s.profile_name, s.bean_brand, s.bean_type, "
        "eg.brand AS grinder_brand, eg.model AS grinder_model, "
        "json_extract(eg.attrs, '$.burrs') AS grinder_burrs, s.grinder_setting, "
        "s.dose_weight, s.final_weight, %5, %6, "
        "s.timestamp, g.shot_count, g.avg_enjoyment "
        "FROM shots s "
        "LEFT JOIN equipment_items eg ON eg.package_id = s.equipment_id AND eg.kind = 'grinder' "
        "INNER JOIN ("
        "  SELECT %1, MAX(timestamp) as max_ts, "
        "  COUNT(*) as shot_count, "
        "  AVG(CASE WHEN enjoyment > 0 THEN enjoyment ELSE NULL END) as avg_enjoyment "
        "  FROM shots "
        "  WHERE (bean_brand IS NOT NULL AND bean_brand != '') "
        "     OR (profile_name IS NOT NULL AND profile_name != '') "
        "  GROUP BY %2"
        ") g ON s.timestamp = g.max_ts AND %3 "
        "ORDER BY s.timestamp DESC "
        "LIMIT %4"
    ).arg(selectColumns, groupColumns, joinConditions).arg(maxItems).arg(yieldCol, bucketCol);

    runDetachedDbThread([this, dbPath, sql, destroyed]() {
        QVariantList results;
        if (!withTempDb(dbPath, "shs_raf", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            if (query.exec(sql)) {
                while (query.next()) {
                    QVariantMap entry;
                    entry["shotId"] = query.value("id").toLongLong();
                    entry["profileName"] = query.value("profile_name").toString();
                    entry["beanBrand"] = query.value("bean_brand").toString();
                    entry["beanType"] = query.value("bean_type").toString();
                    entry["grinderBrand"] = query.value("grinder_brand").toString();
                    entry["grinderModel"] = query.value("grinder_model").toString();
                    entry["grinderBurrs"] = query.value("grinder_burrs").toString();
                    entry["grinderSetting"] = query.value("grinder_setting").toString();
                    entry["doseWeightG"] = query.value("dose_weight").toDouble();
                    entry["finalWeightG"] = query.value("final_weight").toDouble();
                    entry["targetWeightG"] = query.value("yield_override").toDouble();
                    entry["doseBucket"] = query.value("dose_bucket").toDouble();
                    entry["lastUsedTimestamp"] = query.value("timestamp").toLongLong();
                    entry["shotCount"] = query.value("shot_count").toInt();
                    entry["avgEnjoyment"] = query.value("avg_enjoyment").toInt();
                    results.append(entry);
                }
            } else {
                qWarning() << "ShotHistoryStorage: Async getAutoFavorites query failed:" << query.lastError().text();
            }
        })) {
            if (*destroyed) return;
            QMetaObject::invokeMethod(this, [this, destroyed]() {
                if (*destroyed) return;
                emit errorOccurred("Failed to open database for auto-favorites");
            }, Qt::QueuedConnection);
        }

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, results, destroyed]() {
            if (*destroyed) {
                qDebug() << "ShotHistoryStorage: autoFavorites callback dropped (object destroyed)";
                return;
            }
            emit autoFavoritesReady(results);
        }, Qt::QueuedConnection);
    });

}

void ShotHistoryStorage::requestAutoFavoriteGroupDetails(const QString& groupBy,
                                                          const QString& beanBrand,
                                                          const QString& beanType,
                                                          const QString& profileName,
                                                          const QString& grinderBrand,
                                                          const QString& grinderModel,
                                                          const QString& grinderSetting,
                                                          double doseBucket,
                                                          double targetWeight)
{
    if (!m_ready) {
        emit autoFavoriteGroupDetailsReady(QVariantMap());
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    // Build WHERE clause on main thread (pure computation, fast)
    QStringList conditions;
    QVariantList bindValues;

    auto addCondition = [&](const QString& column, const QString& value) {
        conditions << QString("COALESCE(%1, '') = ?").arg(column);
        bindValues << value;
    };

    if (groupBy == "bean") {
        addCondition("bean_brand", beanBrand);
        addCondition("bean_type", beanType);
    } else if (groupBy == "profile") {
        addCondition("profile_name", profileName);
    } else if (groupBy == "bean_profile_grinder" || groupBy == "bean_profile_grinder_weight") {
        addCondition("bean_brand", beanBrand);
        addCondition("bean_type", beanType);
        addCondition("profile_name", profileName);
        // Grinder identity resolves through the equipment_id pointer (the
        // dropped grinder_brand/model columns no longer exist — migration 23).
        // Correlated subqueries against the package's grinder item reproduce the
        // old COALESCE(col,'')=? semantics, so a card with no grinder (NULL
        // equipment_id → NULL → '') still matches empty brand/model. This
        // APPROXIMATES requestAutoFavorites' grouping key (which groups on the
        // raw equipment_id): when two packages share a brand+model — e.g. a
        // superseded fork and its in-inventory successor — these brand+model
        // conditions match shots across both, which is acceptable for the card's
        // aggregate stats scope. (burrs is intentionally not matched here.)
        addCondition("(SELECT brand FROM equipment_items "
                     "WHERE package_id = shots.equipment_id AND kind = 'grinder')", grinderBrand);
        addCondition("(SELECT model FROM equipment_items "
                     "WHERE package_id = shots.equipment_id AND kind = 'grinder')", grinderModel);
        addCondition("grinder_setting", grinderSetting);
        if (groupBy == "bean_profile_grinder_weight") {
            // Match requestAutoFavorites's weight-mode bucketing exactly so stats scope
            // to the same (dose bucket, target yield) group the card belongs to. The
            // card itself displays the latest shot's raw dose, but the group boundary
            // is the rounded bucket.
            conditions << "ROUND(COALESCE(dose_weight, 0) * 2) / 2.0 = ?";
            bindValues << doseBucket;
            conditions << "COALESCE(yield_override, 0) = ?";
            bindValues << targetWeight;
        }
    } else {
        // bean_profile (default)
        addCondition("bean_brand", beanBrand);
        addCondition("bean_type", beanType);
        addCondition("profile_name", profileName);
    }

    QString whereClause = " WHERE " + conditions.join(" AND ");

    QString statsSql = "SELECT "
        "AVG(CASE WHEN drink_tds > 0 THEN drink_tds ELSE NULL END) as avg_tds, "
        "AVG(CASE WHEN drink_ey > 0 THEN drink_ey ELSE NULL END) as avg_ey, "
        "AVG(CASE WHEN duration_seconds > 0 THEN duration_seconds ELSE NULL END) as avg_duration, "
        "AVG(CASE WHEN dose_weight > 0 THEN dose_weight ELSE NULL END) as avg_dose, "
        "AVG(CASE WHEN final_weight > 0 THEN final_weight ELSE NULL END) as avg_yield, "
        "AVG(CASE WHEN temperature_override > 0 THEN temperature_override ELSE NULL END) as avg_temperature "
        "FROM shots" + whereClause;

    QString notesSql = "SELECT espresso_notes, timestamp FROM shots" + whereClause +
        " AND espresso_notes IS NOT NULL AND espresso_notes != '' "
        "ORDER BY timestamp DESC";

    runDetachedDbThread([this, dbPath, statsSql, notesSql, bindValues, destroyed]() {
        QVariantMap result;
        if (!withTempDb(dbPath, "shs_ragd", [&](QSqlDatabase& db) {
            // Stats query
            QSqlQuery statsQuery(db);
            statsQuery.prepare(statsSql);
            for (int i = 0; i < bindValues.size(); ++i)
                statsQuery.bindValue(i, bindValues[i]);

            if (statsQuery.exec() && statsQuery.next()) {
                result["avgTds"] = statsQuery.value("avg_tds").toDouble();
                result["avgEy"] = statsQuery.value("avg_ey").toDouble();
                result["avgDuration"] = statsQuery.value("avg_duration").toDouble();
                result["avgDose"] = statsQuery.value("avg_dose").toDouble();
                result["avgYield"] = statsQuery.value("avg_yield").toDouble();
                result["avgTemperature"] = statsQuery.value("avg_temperature").toDouble();
            }

            // Notes query
            QSqlQuery notesQuery(db);
            notesQuery.prepare(notesSql);
            for (int i = 0; i < bindValues.size(); ++i)
                notesQuery.bindValue(i, bindValues[i]);

            QVariantList notes;
            if (notesQuery.exec()) {
                while (notesQuery.next()) {
                    QVariantMap note;
                    note["text"] = notesQuery.value("espresso_notes").toString();
                    qint64 ts = notesQuery.value("timestamp").toLongLong();
                    note["timestamp"] = ts;
                    note["dateTime"] = QDateTime::fromSecsSinceEpoch(ts).toString(use12h() ? "yyyy-MM-dd h:mm AP" : "yyyy-MM-dd HH:mm");
                    notes.append(note);
                }
            }
            result["notes"] = notes;
        })) {
            if (*destroyed) return;
            QMetaObject::invokeMethod(this, [this, destroyed]() {
                if (*destroyed) return;
                emit errorOccurred("Failed to open database for auto-favorite details");
            }, Qt::QueuedConnection);
        }

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, result, destroyed]() {
            if (*destroyed) {
                qDebug() << "ShotHistoryStorage: autoFavoriteGroupDetails callback dropped (object destroyed)";
                return;
            }
            emit autoFavoriteGroupDetailsReady(result);
        }, Qt::QueuedConnection);
    });

}


QStringList ShotHistoryStorage::getDistinctBeanTypesForBrand(const QString& beanBrand)
{
    if (beanBrand.isEmpty())
        return getDistinctBeanTypes();

    return queryDistinctList(
        QStringLiteral("SELECT DISTINCT bean_type FROM shots "
                       "WHERE bean_brand = ? AND bean_type IS NOT NULL AND bean_type != '' "
                       "ORDER BY bean_type"),
        {beanBrand});
}

QStringList ShotHistoryStorage::getDistinctGrinderBrands()
{
    // Grinder brands come from the equipment inventory (every grinder item,
    // inventory or superseded), not the dropped shots.grinder_brand column
    // (add-equipment-packages task 4.2).
    return queryDistinctList(
        QStringLiteral("SELECT DISTINCT brand FROM equipment_items "
                       "WHERE kind = 'grinder' AND brand IS NOT NULL AND brand != '' "
                       "ORDER BY brand"));
}

QStringList ShotHistoryStorage::getDistinctGrinderModelsForBrand(const QString& grinderBrand)
{
    if (grinderBrand.isEmpty())
        return getDistinctGrinders();

    return queryDistinctList(
        QStringLiteral("SELECT DISTINCT model FROM equipment_items "
                       "WHERE kind = 'grinder' AND brand = ? AND model IS NOT NULL AND model != '' "
                       "ORDER BY model"),
        {grinderBrand});
}

QStringList ShotHistoryStorage::getDistinctGrinderBurrsForModel(const QString& grinderBrand, const QString& grinderModel)
{
    // burrs lives in the grinder item's attrs JSON blob.
    return queryDistinctList(
        QStringLiteral("SELECT DISTINCT json_extract(attrs, '$.burrs') AS burrs FROM equipment_items "
                       "WHERE kind = 'grinder' AND brand = ? AND model = ? "
                       "AND burrs IS NOT NULL AND burrs != '' "
                       "ORDER BY burrs"),
        {grinderBrand, grinderModel});
}

QStringList ShotHistoryStorage::getDistinctGrinderSettingsForGrinder(const QString& grinderModel)
{
    if (grinderModel.isEmpty())
        return getDistinctGrinderSettings();

    // Settings are per-shot dial-in (grinder_setting stays on shots); the grinder
    // model resolves through the equipment_id pointer (task 4.2), folded by
    // grinderModelMatchSql().
    QStringList settings = queryDistinctList(
        QStringLiteral("SELECT DISTINCT grinder_setting FROM shots WHERE %1 "
                       "AND grinder_setting IS NOT NULL AND grinder_setting != '' "
                       "ORDER BY grinder_setting").arg(grinderModelMatchSql("?")),
        {grinderModel});
    sortGrinderSettings(settings);
    return settings;
}

// Say what was derived and from how much, ONCE per (grinder, sample size, answer).
//
// This line exists because #1713 could not be diagnosed from the 25,720-line log
// attached to it: "grind" appeared three times, none of them reporting the step,
// and the reported symptom (whole numbers only, no 1.2) is entirely a consequence
// of what grindStepForGrinder() returns. That value is derived from the user's OWN
// history, so it differs per install and cannot be inferred from the version number
// — exactly the kind of fact a log has to carry, because nobody can reconstruct it
// afterwards.
//
// One function rather than one call site per return, because grindStepForGrinder()
// has THREE ways to answer 0 — not-ready, query-failed and too-thin — and they
// must not describe it in three wordings, or worse, in one.
//
// Naming the outcome is the point. All three return 0.0 with sampleCount 0, so a
// line that says only "derived from 0 distinct numeric setting(s) — too thin"
// tells a reader the user has no numeric history when the truth may be that the
// SELECT errored. That reader closes the investigation, which is exactly how
// #1713 survived a 25,720-line log.
//
// Deduped PER MODEL, and the key includes the outcome. The caller is a QML
// binding (and the ShotServer /grind-candidates handler), and more than one
// GrindRowSource is live at once — startup builds two, one usually with an empty
// model — so a single scalar alternated between them and deduped nothing. The
// outcome belongs in the key because otherwise a model that has already reported
// "0 / too thin" stays silent when its query later starts FAILING, the one
// transition most worth seeing.
void ShotHistoryStorage::reportGrindStep(const QString& grinderModel, qsizetype sampleCount,
                                         double step, GrindStepOutcome outcome)
{
    const QString observed = QStringLiteral("%1:%2:%3")
                                 .arg(sampleCount).arg(step).arg(int(outcome));
    if (m_lastGrindStepReport.value(grinderModel) == observed)
        return;
    m_lastGrindStepReport.insert(grinderModel, observed);

    QString why;
    switch (outcome) {
    case GrindStepOutcome::Derived:
        break;
    case GrindStepOutcome::NotReady:
        why = QStringLiteral(" — the database is not ready yet; the caller's "
                             "fallback step is used instead");
        break;
    case GrindStepOutcome::QueryFailed:
        why = QStringLiteral(" — the history QUERY FAILED (see the preceding warning); "
                             "this is NOT a grinder without history, and the caller's "
                             "fallback step is used instead");
        break;
    case GrindStepOutcome::TooThin:
        why = QStringLiteral(" — too thin to derive; the caller's fallback step is "
                             "used instead");
        break;
    case GrindStepOutcome::NoGrinder:
        why = QStringLiteral(" — no grinder was supplied, so there is no history to "
                             "pool; the caller's fallback step is used instead");
        break;
    }

    qDebug().noquote()
        << QStringLiteral("ShotHistoryStorage: grind step for %1 = %2, derived from %3 "
                          "distinct numeric setting(s)%4")
               .arg(grinderModel.isEmpty() ? QStringLiteral("(no grinder)") : grinderModel)
               .arg(step)
               .arg(sampleCount)
               .arg(why);
}

double ShotHistoryStorage::grindStepForGrinder(const QString& grinderModel)
{
    // Every return is narrated, including the ones that answer 0 — they are the
    // returns a reader most needs to tell apart. See reportGrindStep.
    if (!m_ready) {
        reportGrindStep(grinderModel, 0, 0.0, GrindStepOutcome::NotReady);
        return 0.0;
    }

    bool queryOk = false;
    const QList<double> numeric = grinderWideNumericSettings(m_db, grinderModel, &queryOk);
    const double step = deriveGrindStep(numeric);
    reportGrindStep(grinderModel, numeric.size(), step,
                    !queryOk     ? GrindStepOutcome::QueryFailed
                  : step > 0.0   ? GrindStepOutcome::Derived
                                 : GrindStepOutcome::TooThin);
    return step;
}

double ShotHistoryStorage::grindRpmStepForGrinder(const QString& grinderModel)
{
    // RPM mode always has an identified grinder; an empty model has no meaningful
    // RPM history to pool, which is also grinderWideRpms' stated precondition.
    //
    // Narrated for the same reason as its settings twin: this also returns 0.0 for
    // "no grinder", "not ready" and "query failed" alike, and the caller silently
    // substitutes a 50 RPM default. The bug behind this whole change was
    // undiagnosable precisely because the value behind the symptom was never
    // written down.
    //
    // NoGrinder is its own outcome, not TooThin. "Too thin to derive" would tell a
    // reader the user has no recorded RPMs, when in fact no grinder was supplied.
    if (grinderModel.isEmpty() || !m_ready) {
        reportGrindStep(QStringLiteral("%1 (rpm)").arg(
                            grinderModel.isEmpty() ? QStringLiteral("(no grinder)") : grinderModel),
                        0, 0.0,
                        grinderModel.isEmpty() ? GrindStepOutcome::NoGrinder
                                               : GrindStepOutcome::NotReady);
        return 0.0;
    }

    bool queryOk = false;
    const QList<double> rpms = grinderWideRpms(m_db, grinderModel, &queryOk);
    const double step = deriveGrindStep(rpms);
    reportGrindStep(QStringLiteral("%1 (rpm)").arg(grinderModel), rpms.size(), step,
                    !queryOk     ? GrindStepOutcome::QueryFailed
                  : step > 0.0   ? GrindStepOutcome::Derived
                                 : GrindStepOutcome::TooThin);
    return step;
}

// See the declaration for why this exists. Three small queries rather than one
// clever one: SQLite cannot tell a numeric setting from a text one, and that
// distinction is the whole point — "1102 shots, 0 numeric" and "0 shots" look
// identical in the grind-step line and mean opposite things.
void ShotHistoryStorage::logGrinderCensus()
{
    const QString dbPath = m_dbPath;
    runOnDbThread([dbPath]() {
        withTempDb(dbPath, "shs_census", [](QSqlDatabase& db) {
            // Keyed on the folded model, because that is what the step query
            // matches on (grinderModelMatchSql folds case and whitespace). Two
            // packages spelled "Niche Zero" and "niche zero" are ONE grinder to
            // every query this census is meant to explain, and reporting them
            // as two rows would invent a discrepancy that does not exist. The
            // first spelling seen is kept for display.
            struct Row {
                QString display;
                qint64 shots = 0;
                int numeric = 0;
                int nonNumeric = 0;
            };
            QMap<QString, Row> byModel;
            const auto fold = [](const QString& m) { return m.trimmed().toLower(); };

            // Shots per attributed grinder. The LEFT JOIN is what makes an
            // unlinked shot visible: it lands under the empty model rather than
            // dropping out of the result, which is the number a reader needs
            // when a scoped query "found nothing".
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral(
                    "SELECT COALESCE(eg.model, ''), COUNT(*) FROM shots s "
                    "LEFT JOIN equipment_items eg ON eg.package_id = s.equipment_id "
                    "AND eg.kind = 'grinder' "
                    "GROUP BY LOWER(TRIM(COALESCE(eg.model, '')))"))) {
                EQUIP_WARN_STDERR("Census", QStringLiteral("shot count query failed: %1")
                                                .arg(q.lastError().text()));
                return;
            }
            qint64 total = 0;
            qint64 unlinked = 0;
            while (q.next()) {
                const QString model = q.value(0).toString().trimmed();
                const qint64 n = q.value(1).toLongLong();
                total += n;
                if (model.isEmpty()) {
                    unlinked += n;
                    continue;  // reported as the headline "not attributed" count
                }
                Row& row = byModel[fold(model)];
                if (row.display.isEmpty())
                    row.display = model;
                row.shots += n;
            }

            // Distinct (model, setting) pairs, not per-shot rows — this is the
            // same population deriveGrindStep() sees, so a mismatch between the
            // count here and the count in the grind-step line is itself a
            // finding rather than noise.
            QSqlQuery s(db);
            if (!s.exec(QStringLiteral(
                    "SELECT DISTINCT COALESCE(eg.model, ''), TRIM(s.grinder_setting) FROM shots s "
                    "LEFT JOIN equipment_items eg ON eg.package_id = s.equipment_id "
                    "AND eg.kind = 'grinder' "
                    "WHERE s.grinder_setting IS NOT NULL AND TRIM(s.grinder_setting) != ''"))) {
                EQUIP_WARN_STDERR("Census", QStringLiteral("distinct settings query failed: %1")
                                                .arg(s.lastError().text()));
                return;
            }
            while (s.next()) {
                const QString model = s.value(0).toString().trimmed();
                if (model.isEmpty())
                    continue;
                bool numeric = false;
                s.value(1).toString().toDouble(&numeric);
                Row& row = byModel[fold(model)];
                if (row.display.isEmpty())
                    row.display = model;
                (numeric ? row.numeric : row.nonNumeric)++;
            }

            // Grinders that exist as equipment but own no shots. Without this a
            // package nothing points at is simply absent, which reads as "no
            // such grinder" when the truth is "nothing was ever attributed to
            // it" — the fork-detaches-history failure in one line.
            QSqlQuery g(db);
            if (g.exec(QStringLiteral(
                    "SELECT DISTINCT COALESCE(model, '') FROM equipment_items "
                    "WHERE kind = 'grinder' AND TRIM(COALESCE(model, '')) != ''"))) {
                while (g.next()) {
                    const QString model = g.value(0).toString().trimmed();
                    Row& row = byModel[fold(model)];
                    if (row.display.isEmpty())
                        row.display = model;
                }
            }

            EQUIP_LOG_STDERR("Census", QStringLiteral(
                                           "%1 shot(s), %2 attributed to a grinder package, %3 not")
                                           .arg(total).arg(total - unlinked).arg(unlinked));

            for (const Row& row : std::as_const(byModel)) {
                EQUIP_LOG_STDERR("Census", QStringLiteral(
                                               "\"%1\" - %2 shot(s), %3 distinct numeric "
                                               "setting(s), %4 non-numeric")
                                               .arg(row.display)
                                               .arg(row.shots)
                                               .arg(row.numeric)
                                               .arg(row.nonNumeric));
            }
        });
    });
}

void ShotHistoryStorage::sortGrinderSettings(QStringList& settings)
{
    if (settings.isEmpty()) {
        return;
    }

    // Check if all values parse as numbers
    bool allNumeric = true;
    for (const QString& setting : settings) {
        bool ok = false;
        setting.toDouble(&ok);
        if (!ok) {
            allNumeric = false;
            break;
        }
    }

    if (allNumeric) {
        // Sort numerically
        std::sort(settings.begin(), settings.end(), [](const QString& a, const QString& b) {
            return a.toDouble() < b.toDouble();
        });
    } else {
        // Sort alphabetically with natural ordering
        std::sort(settings.begin(), settings.end(), [](const QString& a, const QString& b) {
            return QString::localeAwareCompare(a, b) < 0;
        });
    }
}
