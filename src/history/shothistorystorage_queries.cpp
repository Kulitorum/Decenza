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
//   - grinder context: queryGrinderContext + requestUpdateGrinderFields.

#include "shothistorystorage.h"
#include "shothistorystorage_internal.h"

#include "core/dbutils.h"
#include "core/grinderaliases.h"

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
                "grinder_brand", "grinder_model", "grinder_setting", "barista", "roast_level"
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
    filter.filterTemperatureUnstable = filterMap.value("filterTemperatureUnstable", false).toBool();
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
    if (!filter.grinderBrand.isEmpty()) {
        conditions << "grinder_brand = ?";
        bindValues << filter.grinderBrand;
    }
    if (!filter.grinderModel.isEmpty()) {
        conditions << "grinder_model = ?";
        bindValues << filter.grinderModel;
    }
    if (!filter.grinderBurrs.isEmpty()) {
        conditions << "grinder_burrs = ?";
        bindValues << filter.grinderBurrs;
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
    if (filter.filterTemperatureUnstable) {
        conditions << "temperature_unstable = 1";
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

    QString sql;
    if (!ftsQuery.isEmpty()) {
        QString extraConditions;
        if (!whereClause.isEmpty()) {
            extraConditions = whereClause;
            extraConditions.replace(extraConditions.indexOf("WHERE"), 5, "AND");
        }
        sql = QString(R"(
            SELECT id, uuid, timestamp, profile_name, duration_seconds,
                   final_weight, dose_weight, bean_brand, bean_type,
                   enjoyment, visualizer_id, grinder_setting,
                   temperature_override, yield_override, beverage_type,
                   drink_tds, drink_ey,
                   channeling_detected, temperature_unstable, grind_issue_detected,
                   skip_first_frame_detected, pour_truncated_detected
            FROM shots
            WHERE id IN (SELECT rowid FROM shots_fts WHERE shots_fts MATCH '%1')
            %2
            %3
            LIMIT ? OFFSET ?
        )").arg(ftsQuery).arg(extraConditions).arg(orderByClause);
    } else {
        sql = QString(R"(
            SELECT id, uuid, timestamp, profile_name, duration_seconds,
                   final_weight, dose_weight, bean_brand, bean_type,
                   enjoyment, visualizer_id, grinder_setting,
                   temperature_override, yield_override, beverage_type,
                   drink_tds, drink_ey,
                   channeling_detected, temperature_unstable, grind_issue_detected,
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
        countSql = QString("SELECT COUNT(*) FROM shots WHERE id IN "
                           "(SELECT rowid FROM shots_fts WHERE shots_fts MATCH '%1') %2")
                       .arg(ftsQuery).arg(extraConditions);
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
                            shot["enjoyment"] = query.value(9).toInt();
                            shot["hasVisualizerUpload"] = !query.value(10).isNull();
                            shot["grinderSetting"] = query.value(11).toString();
                            shot["temperatureOverrideC"] = query.value(12).toDouble();
                            shot["targetWeightG"] = query.value(13).toDouble();
                            shot["beverageType"] = query.value(14).toString();
                            shot["drinkTds"] = query.value(15).toDouble();
                            shot["drinkEy"] = query.value(16).toDouble();
                            shot["channelingDetected"] = query.value(17).toInt() != 0;
                            shot["temperatureUnstable"] = query.value(18).toInt() != 0;
                            shot["grindIssueDetected"] = query.value(19).toInt() != 0;
                            shot["skipFirstFrameDetected"] = query.value(20).toInt() != 0;
                            shot["pourTruncatedDetected"] = query.value(21).toInt() != 0;

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
    QString sql = QStringLiteral(R"(
        SELECT id, timestamp, profile_name, duration_seconds, final_weight, dose_weight,
               bean_brand, bean_type, roast_level, grinder_brand, grinder_model,
               grinder_burrs, grinder_setting, drink_tds, drink_ey, enjoyment,
               espresso_notes, roast_date, temperature_override, yield_override, profile_json, beverage_type
        FROM shots
        WHERE profile_kb_id = ?
    )");
    if (excludeShotId >= 0)
        sql += QStringLiteral(" AND id != ?");
    sql += QStringLiteral(" ORDER BY timestamp DESC LIMIT ?");

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
            shot["enjoyment"] = query.value("enjoyment").toInt();
            shot["grinderSetting"] = query.value("grinder_setting").toString();
            shot["grinderModel"] = query.value("grinder_model").toString();
            shot["grinderBrand"] = query.value("grinder_brand").toString();
            shot["grinderBurrs"] = query.value("grinder_burrs").toString();
            shot["espressoNotes"] = query.value("espresso_notes").toString();
            shot["beanBrand"] = query.value("bean_brand").toString();
            shot["beanType"] = query.value("bean_type").toString();
            shot["roastLevel"] = query.value("roast_level").toString();
            shot["roastDate"] = query.value("roast_date").toString();
            shot["drinkTds"] = query.value("drink_tds").toDouble();
            shot["drinkEy"] = query.value("drink_ey").toDouble();
            shot["temperatureOverrideC"] = query.value("temperature_override").toDouble();
            shot["targetWeightG"] = query.value("yield_override").toDouble();
            shot["profileJson"] = query.value("profile_json").toString();
            shot["beverageType"] = query.value("beverage_type").toString();

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

// convertShotRecord (the QVariantMap projection consumed by requestShot,
// ShotServer, and the AI advisor) lives in shothistorystorage_serialize.cpp.


GrinderContext ShotHistoryStorage::queryGrinderContext(QSqlDatabase& db,
    const QString& grinderModel, const QString& beverageType)
{
    GrinderContext ctx;
    if (grinderModel.isEmpty()) return ctx;

    ctx.model = grinderModel;
    ctx.beverageType = beverageType.isEmpty() ? QStringLiteral("espresso") : beverageType;

    QSqlQuery q(db);
    q.prepare("SELECT DISTINCT grinder_setting FROM shots "
              "WHERE grinder_model = :model AND beverage_type = :bev "
              "AND grinder_setting != ''");
    q.bindValue(":model", grinderModel);
    q.bindValue(":bev", ctx.beverageType);
    if (!q.exec()) return ctx;

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
    "grinder_brand", "grinder_model", "grinder_setting", "barista", "roast_level"
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
    return getDistinctValues("grinder_model");
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
        selectColumns = "COALESCE(bean_brand, '') AS gb_bean_brand, "
                        "COALESCE(bean_type, '') AS gb_bean_type, "
                        "COALESCE(profile_name, '') AS gb_profile_name, "
                        "COALESCE(grinder_brand, '') AS gb_grinder_brand, "
                        "COALESCE(grinder_model, '') AS gb_grinder_model, "
                        "COALESCE(grinder_setting, '') AS gb_grinder_setting";
        groupColumns = "COALESCE(bean_brand, ''), COALESCE(bean_type, ''), "
                       "COALESCE(profile_name, ''), COALESCE(grinder_brand, ''), "
                       "COALESCE(grinder_model, ''), COALESCE(grinder_setting, '')";
        joinConditions = "COALESCE(s.bean_brand, '') = g.gb_bean_brand "
                         "AND COALESCE(s.bean_type, '') = g.gb_bean_type "
                         "AND COALESCE(s.profile_name, '') = g.gb_profile_name "
                         "AND COALESCE(s.grinder_brand, '') = g.gb_grinder_brand "
                         "AND COALESCE(s.grinder_model, '') = g.gb_grinder_model "
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
        "s.grinder_brand, s.grinder_model, s.grinder_burrs, s.grinder_setting, "
        "s.dose_weight, s.final_weight, %5, %6, "
        "s.timestamp, g.shot_count, g.avg_enjoyment "
        "FROM shots s "
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
        addCondition("grinder_brand", grinderBrand);
        addCondition("grinder_model", grinderModel);
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
    // grinder_brand is pre-warmed by requestDistinctCache(), but use cache-only pattern
    return getDistinctValues("grinder_brand");
}

QStringList ShotHistoryStorage::getDistinctGrinderModelsForBrand(const QString& grinderBrand)
{
    if (grinderBrand.isEmpty())
        return getDistinctGrinders();

    const QString cacheKey = "grinder_model:" + grinderBrand;
    if (m_distinctCache.contains(cacheKey))
        return m_distinctCache.value(cacheKey);

    if (!m_ready) return {};

    requestDistinctValueAsync(cacheKey,
        "SELECT DISTINCT grinder_model FROM shots "
        "WHERE grinder_brand = ? AND grinder_model IS NOT NULL AND grinder_model != '' "
        "ORDER BY grinder_model",
        {grinderBrand});
    return {};
}

QStringList ShotHistoryStorage::getDistinctGrinderBurrsForModel(const QString& grinderBrand, const QString& grinderModel)
{
    const QString cacheKey = "grinder_burrs:" + grinderBrand + ":" + grinderModel;
    if (m_distinctCache.contains(cacheKey))
        return m_distinctCache.value(cacheKey);

    if (!m_ready) return {};

    requestDistinctValueAsync(cacheKey,
        "SELECT DISTINCT grinder_burrs FROM shots "
        "WHERE grinder_brand = ? AND grinder_model = ? "
        "AND grinder_burrs IS NOT NULL AND grinder_burrs != '' "
        "ORDER BY grinder_burrs",
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

    requestDistinctValueAsync(cacheKey,
        "SELECT DISTINCT grinder_setting FROM shots "
        "WHERE grinder_model = ? AND grinder_setting IS NOT NULL AND grinder_setting != '' "
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


void ShotHistoryStorage::requestUpdateGrinderFields(const QString& oldBrand, const QString& oldModel,
                                                     const QString& newBrand, const QString& newModel,
                                                     const QString& newBurrs)
{
    if (!m_ready) {
        emit grinderFieldsUpdated(0);
        return;
    }

    const QString dbPath = m_dbPath;
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([this, dbPath, oldBrand, oldModel, newBrand, newModel, newBurrs, destroyed]() {
        int count = 0;
        withTempDb(dbPath, "shs_grinder_update", [&](QSqlDatabase& db) {
            QSqlQuery query(db);
            query.prepare("UPDATE shots SET grinder_brand = ?, grinder_model = ?, grinder_burrs = ?, "
                          "updated_at = strftime('%s', 'now') "
                          "WHERE grinder_brand = ? AND grinder_model = ?");
            query.bindValue(0, newBrand);
            query.bindValue(1, newModel);
            query.bindValue(2, newBurrs);
            query.bindValue(3, oldBrand);
            query.bindValue(4, oldModel);
            if (query.exec())
                count = query.numRowsAffected();
            else
                qWarning() << "ShotHistoryStorage: Failed to bulk update grinder fields:" << query.lastError().text();
        });

        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, count, destroyed]() {
            if (*destroyed) return;
            invalidateDistinctCache();
            emit grinderFieldsUpdated(count);
            qDebug() << "ShotHistoryStorage: Updated grinder fields for" << count << "shots";
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
