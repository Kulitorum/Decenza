// ShotHistoryStorage queries — filtered list, recents-by-kbId, auto-favorites,
// distinct-value cache, grinder context. Split out of the main TU so the
// query / read-projection code lives separately from DB lifecycle, save,
// load+recompute, and the badge cascade. All concerns share the same
// `ShotHistoryStorage` class — these are member function definitions in a
// separate translation unit, no behaviour or API change.
//
// Owning concerns (per openspec/changes/split-shothistorystorage-by-concern/):
//   - filtered queries: requestShotsFiltered + buildFilterQuery + parseFilterMap +
//     formatFtsQuery (FTS5 query construction) + s_sortColumnMap (sort-column whitelist).
//   - recents-by-kbId: requestRecentShotsByKbId + loadRecentShotsByKbIdStatic.
//   - distinct-value cache: requestDistinctCache + requestDistinctValueAsync +
//     getDistinctValues + invalidateDistinctCache + getDistinct* getters +
//     s_allowedColumns whitelist + sortGrinderSettings helper.
//   - auto-favorites: requestAutoFavorites + requestAutoFavoriteGroupDetails.
//   - grinder context: queryGrinderContext.

#include "shothistorystorage.h"
#include "shothistorystorage_internal.h"

#include "core/dbutils.h"
#include "core/grinderaliases.h"

#include <QSet>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>

using decenza::storage::detail::use12h;

void ShotHistoryStorage::requestDistinctCache()
{
    if (!m_ready) {
        emit distinctCacheReady();
        return;
    }
    if (m_distinctCacheRefreshing) {
        m_distinctCacheDirty = true;  // Re-queue after in-flight refresh completes
        return;
    }
    m_distinctCacheRefreshing = true;

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    QThread* thread = QThread::create([this, dbPath, destroyed]() {
        QHash<QString, QStringList> results;
        bool opened = withTempDb(dbPath, "shs_distinct", [&](QSqlDatabase& db) {
            static const QStringList columns = {
                "profile_name", "bean_brand", "bean_type",
                "grinder_setting", "barista", "roast_level"
            };
            for (const QString& col : columns) {
                QStringList values;
                QSqlQuery query(db);
                if (!query.exec(QString("SELECT DISTINCT %1 FROM shots WHERE %1 IS NOT NULL AND %1 != '' ORDER BY %1").arg(col))) {
                    qWarning() << "ShotHistoryStorage: Failed to query distinct" << col << ":" << query.lastError().text();
                    continue;
                }
                while (query.next()) {
                    QString v = query.value(0).toString();
                    if (!v.isEmpty()) values << v;
                }
                results.insert(col, values);
            }
        });

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, results = std::move(results), opened, destroyed]() {
            if (*destroyed) return;
            m_distinctCacheRefreshing = false;
            if (opened) {
                // Clear entire cache (including composite keys like "bean_type:SomeRoaster")
                // so stale filtered entries are also refreshed on next access
                m_distinctCache.clear();
                // Discard any in-flight single-key fetches — they queried before invalidation
                // and would overwrite fresh cache data with stale results
                m_pendingDistinctKeys.clear();
                for (auto it = results.constBegin(); it != results.constEnd(); ++it)
                    m_distinctCache.insert(it.key(), it.value());
            } else
                qWarning() << "ShotHistoryStorage: Distinct cache refresh failed, keeping stale cache";
            emit distinctCacheReady();
            // If invalidation arrived while we were refreshing, re-trigger
            if (m_distinctCacheDirty) {
                m_distinctCacheDirty = false;
                requestDistinctCache();
            }
        }, Qt::QueuedConnection);
    });
    thread->start();
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
}

void ShotHistoryStorage::requestDistinctValueAsync(const QString& cacheKey, const QString& sql,
                                                    const QVariantList& bindValues)
{
    if (m_pendingDistinctKeys.contains(cacheKey)) return;
    m_pendingDistinctKeys.insert(cacheKey);

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    bool needsGrinderSort = cacheKey.startsWith("grinder_setting");

    QThread* thread = QThread::create([this, dbPath, cacheKey, sql, bindValues, needsGrinderSort, destroyed]() {
        QStringList values;
        bool opened = withTempDb(dbPath, "shs_dv", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            if (!query.prepare(sql)) {
                qWarning() << "ShotHistoryStorage::requestDistinctValueAsync: prepare failed for"
                           << cacheKey << ":" << query.lastError().text();
                return;
            }
            for (qsizetype i = 0; i < bindValues.size(); ++i)
                query.bindValue(static_cast<int>(i), bindValues[i]);
            if (!query.exec()) {
                qWarning() << "ShotHistoryStorage::requestDistinctValueAsync: query failed for" << cacheKey << ":" << query.lastError().text();
                return;
            }
            while (query.next()) {
                QString v = query.value(0).toString();
                if (!v.isEmpty()) values << v;
            }
        });

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, cacheKey, values = std::move(values), needsGrinderSort, opened, destroyed]() mutable {
            if (*destroyed) return;
            // If a full cache refresh cleared m_pendingDistinctKeys while we were in flight,
            // this key is gone — discard the stale result
            if (!m_pendingDistinctKeys.remove(cacheKey)) return;
            if (!opened) {
                qWarning() << "ShotHistoryStorage::requestDistinctValueAsync: DB open failed for"
                           << cacheKey << "- not caching empty result";
                return;
            }
            if (needsGrinderSort)
                sortGrinderSettings(values);
            m_distinctCache.insert(cacheKey, values);
            emit distinctCacheReady();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
            "skip_first_frame_detected, pour_truncated_detected "
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
                   skip_first_frame_detected, pour_truncated_detected
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
    QThread* thread = QThread::create(
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

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}


void ShotHistoryStorage::requestRecentShotsByKbId(const QString& kbId, int limit)
{
    if (!m_ready || kbId.isEmpty()) {
        emit recentShotsByKbIdReady(kbId, QVariantList());
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;
    QThread* thread = QThread::create([this, dbPath, kbId, limit, destroyed]() {
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

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
               s.stopped_by
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
    QThread* thread = QThread::create([this, dbPath, beanBrand, beanType, roastLevel, teaType, destroyed]() {
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

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
    QThread* thread = QThread::create([this, dbPath, beanBrand, beanType, profileName, destroyed]() {
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

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
            "temperature_override, grinder_setting, rpm FROM shots "
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
    QThread* thread = QThread::create([this, dbPath, beanBrand, beanType, roastLevel, destroyed]() {
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

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
        "SELECT DISTINCT grinder_setting FROM shots "
        "WHERE equipment_id IN (SELECT package_id FROM equipment_items "
        "WHERE kind = 'grinder' AND model = :model) "
        "AND beverage_type = :bev "
        "AND grinder_setting != ''");
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
    if (ctx.allNumeric && numeric.size() >= 2) {
        std::sort(numeric.begin(), numeric.end());
        ctx.minSetting = numeric.first();
        ctx.maxSetting = numeric.last();

        double smallest = numeric.last() - numeric.first();
        for (qsizetype i = 1; i < numeric.size(); ++i) {
            double diff = numeric[i] - numeric[i-1];
            if (diff > 0 && diff < smallest)
                smallest = diff;
        }
        ctx.smallestStep = smallest;
    }

    return ctx;
}


static const QStringList s_allowedColumns = {
    "profile_name", "bean_brand", "bean_type",
    "grinder_setting", "barista", "roast_level"
};

QStringList ShotHistoryStorage::getDistinctValues(const QString& column)
{
    // Cache-only: return cached result or trigger async fetch
    if (m_distinctCache.contains(column))
        return m_distinctCache.value(column);

    if (!m_ready) return {};
    if (!s_allowedColumns.contains(column)) {
        qWarning() << "ShotHistoryStorage::getDistinctValues: rejected column" << column;
        return {};
    }

    // Trigger async fetch — QML will re-evaluate when distinctCacheReady fires
    QString sql = QString("SELECT DISTINCT %1 FROM shots WHERE %1 IS NOT NULL AND %1 != '' ORDER BY %1")
                      .arg(column);
    requestDistinctValueAsync(column, sql);
    return {};
}

void ShotHistoryStorage::invalidateDistinctCache()
{
    // Keep stale cache until async refresh completes — avoids a window where
    // getDistinctValues() returns empty. Composite cache keys (e.g. "bean_type:SomeRoaster")
    // are cleared by requestDistinctCache() and re-populated async on next access.
    requestDistinctCache();
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
    // sold/retired grinders. Async + cached like the other grinder getters.
    const QString cacheKey = QStringLiteral("eq_grinder_model");
    if (m_distinctCache.contains(cacheKey))
        return m_distinctCache.value(cacheKey);
    if (!m_ready) return {};
    requestDistinctValueAsync(cacheKey,
        "SELECT DISTINCT model FROM equipment_items "
        "WHERE kind = 'grinder' AND model IS NOT NULL AND model != '' "
        "ORDER BY model");
    return {};
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

    QThread* thread = QThread::create([this, dbPath, sql, destroyed]() {
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

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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

    QThread* thread = QThread::create([this, dbPath, statsSql, notesSql, bindValues, destroyed]() {
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

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}


QStringList ShotHistoryStorage::getDistinctBeanTypesForBrand(const QString& beanBrand)
{
    if (beanBrand.isEmpty())
        return getDistinctBeanTypes();

    const QString cacheKey = "bean_type:" + beanBrand;
    if (m_distinctCache.contains(cacheKey))
        return m_distinctCache.value(cacheKey);

    if (!m_ready) return {};

    requestDistinctValueAsync(cacheKey,
        "SELECT DISTINCT bean_type FROM shots "
        "WHERE bean_brand = ? AND bean_type IS NOT NULL AND bean_type != '' "
        "ORDER BY bean_type",
        {beanBrand});
    return {};
}

QStringList ShotHistoryStorage::getDistinctGrinderBrands()
{
    // Grinder brands come from the equipment inventory (every grinder item,
    // inventory or superseded), not the dropped shots.grinder_brand column
    // (add-equipment-packages task 4.2). Async + cached.
    const QString cacheKey = QStringLiteral("eq_grinder_brand");
    if (m_distinctCache.contains(cacheKey))
        return m_distinctCache.value(cacheKey);
    if (!m_ready) return {};
    requestDistinctValueAsync(cacheKey,
        "SELECT DISTINCT brand FROM equipment_items "
        "WHERE kind = 'grinder' AND brand IS NOT NULL AND brand != '' "
        "ORDER BY brand");
    return {};
}

QStringList ShotHistoryStorage::getDistinctGrinderModelsForBrand(const QString& grinderBrand)
{
    if (grinderBrand.isEmpty())
        return getDistinctGrinders();

    const QString cacheKey = "eq_grinder_model:" + grinderBrand;
    if (m_distinctCache.contains(cacheKey))
        return m_distinctCache.value(cacheKey);

    if (!m_ready) return {};

    requestDistinctValueAsync(cacheKey,
        "SELECT DISTINCT model FROM equipment_items "
        "WHERE kind = 'grinder' AND brand = ? AND model IS NOT NULL AND model != '' "
        "ORDER BY model",
        {grinderBrand});
    return {};
}

QStringList ShotHistoryStorage::getDistinctGrinderBurrsForModel(const QString& grinderBrand, const QString& grinderModel)
{
    const QString cacheKey = "eq_grinder_burrs:" + grinderBrand + ":" + grinderModel;
    if (m_distinctCache.contains(cacheKey))
        return m_distinctCache.value(cacheKey);

    if (!m_ready) return {};

    // burrs lives in the grinder item's attrs JSON blob.
    requestDistinctValueAsync(cacheKey,
        "SELECT DISTINCT json_extract(attrs, '$.burrs') AS burrs FROM equipment_items "
        "WHERE kind = 'grinder' AND brand = ? AND model = ? "
        "AND burrs IS NOT NULL AND burrs != '' "
        "ORDER BY burrs",
        {grinderBrand, grinderModel});
    return {};
}

QStringList ShotHistoryStorage::getDistinctGrinderSettingsForGrinder(const QString& grinderModel)
{
    if (grinderModel.isEmpty())
        return getDistinctGrinderSettings();

    const QString cacheKey = "grinder_setting:" + grinderModel;
    if (m_distinctCache.contains(cacheKey))
        return m_distinctCache.value(cacheKey);

    if (!m_ready) return {};

    // Settings are per-shot dial-in (grinder_setting stays on shots); the grinder
    // model resolves through the equipment_id pointer (task 4.2).
    requestDistinctValueAsync(cacheKey,
        "SELECT DISTINCT grinder_setting FROM shots "
        "WHERE equipment_id IN (SELECT package_id FROM equipment_items "
        "WHERE kind = 'grinder' AND model = ?) "
        "AND grinder_setting IS NOT NULL AND grinder_setting != '' "
        "ORDER BY grinder_setting",
        {grinderModel});
    return {};
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
