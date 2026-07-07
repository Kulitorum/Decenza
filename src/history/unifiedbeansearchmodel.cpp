#include "unifiedbeansearchmodel.h"
#include "coffeebagstorage.h"
#include "network/beanbaseclient.h"
#include "core/dbutils.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QThread>
#include <QDebug>

namespace {

QString identityKey(const QString& roaster, const QString& coffee)
{
    return roaster.trimmed().toLower() + QLatin1Char('|') + coffee.trimmed().toLower();
}

bool matchesQuery(const QVariantMap& row, const QString& query)
{
    if (query.isEmpty())
        return true;
    return row.value("roasterName").toString().contains(query, Qt::CaseInsensitive)
        || row.value("coffeeName").toString().contains(query, Qt::CaseInsensitive);
}

// A blob whose only key is "link" is BagCard's backfill artifact for a bag
// that was created without descriptive data (see the entryJson comment in
// mergeLanes) — it carries nothing the fresh canonical entry doesn't (entries
// include "link"), so it must not shadow the entry the way a real legacy blob
// (CDN image URL, tasting tags) legitimately does.
bool blobIsEffectivelyEmpty(const QString& blob)
{
    if (blob.trimmed().isEmpty())
        return true;
    const QJsonObject obj = QJsonDocument::fromJson(blob.toUtf8()).object();
    if (obj.isEmpty())
        return true;  // unparseable or {}
    const QStringList keys = obj.keys();
    return keys.size() == 1 && keys.first() == QLatin1String("link");
}

// One-line differentiator for canonical rows. Bean Base holds near-duplicate
// submissions of the same roaster+name under distinct canonical ids, so
// roaster+name alone renders identical rows; roast level, origin and tasting
// notes are the fields that actually vary between them.
QString canonicalDetail(const QVariantMap& entry)
{
    QStringList parts;
    for (const char* key : {"degree", "origin", "tastingNotes"}) {
        const QString v = entry.value(QLatin1String(key)).toString().trimmed();
        if (!v.isEmpty())
            parts << v;
    }
    return parts.join(QStringLiteral(" · "));
}

} // namespace

UnifiedBeanSearchModel::UnifiedBeanSearchModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

UnifiedBeanSearchModel::~UnifiedBeanSearchModel()
{
    *m_destroyed = true;
}

void UnifiedBeanSearchModel::setSources(CoffeeBagStorage* bagStorage, BeanBaseClient* beanBase,
                                        const QString& dbPath)
{
    m_bagStorage = bagStorage;
    m_beanBase = beanBase;
    m_dbPath = dbPath;

    if (m_bagStorage) {
        connect(m_bagStorage, &CoffeeBagStorage::inventoryReady, this,
                [this](const QVariantList& bags) {
                    m_inventory = bags;
                    rebuild();
                });
        connect(m_bagStorage, &CoffeeBagStorage::bagsChanged, this,
                [this]() { m_bagStorage->requestInventory(); });
    }
    if (m_beanBase) {
        connect(m_beanBase, &BeanBaseClient::searchResults, this,
                [this](const QString& query, const QVariantList& entries) {
                    if (query.compare(m_query, Qt::CaseInsensitive) != 0)
                        return;  // stale echo
                    m_canonical = entries;
                    setSearching(m_historyInFlight);
                    rebuild();
                });
        connect(m_beanBase, &BeanBaseClient::searchFailed, this,
                [this](const QString& query, const QString&) {
                    if (query.compare(m_query, Qt::CaseInsensitive) != 0)
                        return;
                    m_canonical.clear();
                    setSearching(m_historyInFlight);
                    rebuild();  // history-only results, no error banner here
                });
    }
}

void UnifiedBeanSearchModel::setQuery(const QString& query)
{
    const QString trimmed = query.trimmed();
    if (m_query == trimmed)
        return;
    m_query = trimmed;
    emit queryChanged();

    if (m_beanBase && !m_query.isEmpty()) {
        setSearching(true);
        m_beanBase->search(m_query);  // canonical lane (debounced internally)
    } else {
        m_canonical.clear();
    }
    requestHistory();
    rebuild();  // inventory filters instantly while async lanes catch up
}

void UnifiedBeanSearchModel::refresh()
{
    if (m_bagStorage)
        m_bagStorage->requestInventory();
    requestHistory();
}

void UnifiedBeanSearchModel::requestHistory()
{
    if (m_dbPath.isEmpty())
        return;
    // Event-based coalescing: one query in flight at a time; a newer query
    // parks a pending flag and re-runs on completion (latest wins).
    if (m_historyInFlight) {
        m_historyPending = true;
        return;
    }
    m_historyInFlight = true;

    const QString dbPath = m_dbPath;
    const QString filter = m_query;
    auto destroyed = m_destroyed;
    auto rows = std::make_shared<QVariantList>();
    QThread* thread = QThread::create([this, dbPath, filter, rows, destroyed]() {
        withTempDb(dbPath, "bean_hist", [&](QSqlDatabase& db) {
            *rows = queryHistoryStatic(db, filter);
        });
        if (*destroyed) return;
        QMetaObject::invokeMethod(this, [this, filter, rows, destroyed]() {
            if (*destroyed) return;
            m_historyInFlight = false;
            if (filter == m_query) {
                m_history = *rows;
                setSearching(false);
                rebuild();
            }
            if (m_historyPending) {
                m_historyPending = false;
                requestHistory();
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QVariantList UnifiedBeanSearchModel::queryHistoryStatic(QSqlDatabase& db, const QString& filter, int limit)
{
    QVariantList rows;
    QSqlQuery query(db);
    // SQLite guarantees bare columns accompany the MAX() row, so the grinder/
    // dose values come from each coffee's most recent shot.
    // Grinder identity resolves through each shot's equipment_id pointer (the
    // per-shot grinder_brand/model/burrs columns are dropped in migration 23,
    // add-equipment-packages task 4.1); the JOINed grinder item rides along the
    // MAX(timestamp) row the same way the bare shot columns do. burrs is in the
    // item's attrs JSON blob; grinder_setting stays on the shot.
    query.prepare(QStringLiteral(
        "SELECT bean_brand, bean_type, beanbase_id, beanbase_json, roast_level, "
        "       eg.brand AS grinder_brand, eg.model AS grinder_model, "
        "       json_extract(eg.attrs, '$.burrs') AS grinder_burrs, grinder_setting, "
        "       dose_weight, yield_override, MAX(timestamp) AS last_ts "
        "FROM shots s "
        "LEFT JOIN equipment_items eg ON eg.package_id = s.equipment_id AND eg.kind = 'grinder' "
        "WHERE (COALESCE(bean_brand,'') <> '' OR COALESCE(bean_type,'') <> '') "
        "  AND (:filter = '' OR bean_brand LIKE :like OR bean_type LIKE :like) "
        "GROUP BY COALESCE(beanbase_id, LOWER(COALESCE(bean_brand,'')) || '|' || LOWER(COALESCE(bean_type,''))) "
        "ORDER BY last_ts DESC LIMIT :limit"));
    query.bindValue(":filter", filter);
    query.bindValue(":like", QStringLiteral("%%%1%%").arg(filter));
    query.bindValue(":limit", limit);
    if (!query.exec()) {
        qWarning() << "UnifiedBeanSearchModel: history query failed:" << query.lastError().text();
        return rows;
    }

    // Post-merge linked + unlinked groups of the same coffee (shots before/
    // after canonical linking): keep the linked row, newest timestamp wins
    // for ordering.
    QHash<QString, qsizetype> indexByName;
    while (query.next()) {
        QVariantMap row;
        row["roasterName"] = query.value(0).toString();
        row["coffeeName"] = query.value(1).toString();
        row["beanBaseId"] = query.value(2).toString();
        row["beanBaseData"] = query.value(3).toString();
        row["roastLevel"] = query.value(4).toString();
        row["grinderBrand"] = query.value(5).toString();
        row["grinderModel"] = query.value(6).toString();
        row["grinderBurrs"] = query.value(7).toString();
        row["grinderSetting"] = query.value(8).toString();
        row["doseWeightG"] = query.value(9).toDouble();
        row["yieldOverrideG"] = query.value(10).toDouble();
        row["lastUsedEpoch"] = query.value(11).toLongLong();

        const QString key = identityKey(row["roasterName"].toString(), row["coffeeName"].toString());
        const auto existing = indexByName.constFind(key);
        if (existing != indexByName.constEnd()) {
            QVariantMap kept = rows[existing.value()].toMap();
            // Prefer the linked variant's identity; keep the newest epoch.
            if (kept.value("beanBaseId").toString().isEmpty()
                && !row.value("beanBaseId").toString().isEmpty()) {
                row["lastUsedEpoch"] = qMax(row["lastUsedEpoch"].toLongLong(),
                                            kept["lastUsedEpoch"].toLongLong());
                rows[existing.value()] = row;
            }
            continue;
        }
        indexByName.insert(key, rows.size());
        rows.append(row);
    }
    return rows;
}

QVariantList UnifiedBeanSearchModel::mergeLanes(const QVariantList& inventoryBags,
                                                const QVariantList& canonicalResults,
                                                const QVariantList& historyEntries,
                                                const QString& query)
{
    QVariantList tier0, tier1, tier2, tier34;

    // Identity indexes of inventory bags for absorption.
    QHash<QString, int> invByCanonicalId;  // beanBaseId -> tier0 index
    QHash<QString, int> invByName;         // roaster|coffee -> tier0 index
    for (const QVariant& v : inventoryBags) {
        QVariantMap bag = v.toMap();
        if (!matchesQuery(bag, query))
            continue;
        bag["tier"] = static_cast<int>(Tier::Inventory);
        bag["sources"] = QStringLiteral("inventory");
        // Mirror the bag's "id" into "bagId" so BagIdRole carries the real bag
        // id for inventory rows (every other lane sets bagId == -1). Single
        // source of truth for the cross-role invariant and QML's bagId checks.
        bag["bagId"] = bag.value("id", -1);
        const int idx = static_cast<int>(tier0.size());
        const QString canonicalId = bag.value("beanBaseId").toString();
        if (!canonicalId.isEmpty())
            invByCanonicalId.insert(canonicalId, idx);
        invByName.insert(identityKey(bag.value("roasterName").toString(),
                                     bag.value("coffeeName").toString()), idx);
        tier0.append(bag);
    }

    // History indexes for canonical merging (Tier 1).
    QHash<QString, qsizetype> histByCanonicalId;
    QHash<QString, qsizetype> histByName;
    QVector<bool> histConsumed(historyEntries.size(), false);
    for (qsizetype i = 0; i < historyEntries.size(); i++) {
        const QVariantMap h = historyEntries[i].toMap();
        const QString canonicalId = h.value("beanBaseId").toString();
        if (!canonicalId.isEmpty())
            histByCanonicalId.insert(canonicalId, i);
        histByName.insert(identityKey(h.value("roasterName").toString(),
                                      h.value("coffeeName").toString()), i);
    }

    for (const QVariant& v : canonicalResults) {
        const QVariantMap entry = v.toMap();
        const QString canonicalId = entry.value("id").toString();
        const QString roaster = entry.value("roasterName").toString();
        const QString coffee = entry.value("roastName").toString();
        const QString nameKey = identityKey(roaster, coffee);

        // Absorbed into an inventory bag — never offer to re-create it.
        if (invByCanonicalId.contains(canonicalId) || invByName.contains(nameKey)) {
            const int idx = invByCanonicalId.value(canonicalId, invByName.value(nameKey, -1));
            if (idx >= 0) {
                QVariantMap bag = tier0[idx].toMap();
                bag["sources"] = QStringLiteral("inventory");  // label stays "In inventory"
                tier0[idx] = bag;
            }
            continue;
        }

        QVariantMap row;
        row["roasterName"] = roaster;
        row["coffeeName"] = coffee;
        row["beanBaseId"] = canonicalId;
        row["bagId"] = -1;
        row["detail"] = canonicalDetail(entry);
        // The full entry, serialized the same way a BeanBaseSearchBar pick
        // stores it (JSON.stringify(entry)): a bag created from this row must
        // carry the descriptive blob, not just the canonical id — an id with
        // an empty blob renders an empty details popup (BagCard's link
        // backfill then persists `{"link":…}` as the whole blob).
        const QString entryJson = QString::fromUtf8(
            QJsonDocument(QJsonObject::fromVariantMap(entry)).toJson(QJsonDocument::Compact));

        // Same coffee in history -> single Tier 1 entry, both source labels:
        // grinder/dose from history, canonical identity from Bean Base.
        qsizetype histIdx = histByCanonicalId.value(canonicalId, -1);
        if (histIdx < 0)
            histIdx = histByName.value(nameKey, -1);
        if (histIdx >= 0 && !histConsumed[histIdx]) {
            const QVariantMap h = historyEntries[histIdx].toMap();
            histConsumed[histIdx] = true;
            row["grinderBrand"] = h.value("grinderBrand");
            row["grinderModel"] = h.value("grinderModel");
            row["grinderBurrs"] = h.value("grinderBurrs");
            row["grinderSetting"] = h.value("grinderSetting");
            row["doseWeightG"] = h.value("doseWeightG");
            row["yieldOverrideG"] = h.value("yieldOverrideG");
            row["roastLevel"] = h.value("roastLevel");
            // History's blob may carry legacy-only fields (CDN image URL,
            // tasting tags), so keep it when it has real content; the fresh
            // entry covers snapshots that predate the blob AND snapshots
            // holding only BagCard's `{"link":…}` backfill artifact.
            const QString histBlob = h.value("beanBaseData").toString();
            row["beanBaseData"] = blobIsEffectivelyEmpty(histBlob) ? entryJson : histBlob;
            row["lastUsedEpoch"] = h.value("lastUsedEpoch");
            row["tier"] = static_cast<int>(Tier::HistoryCanonical);
            row["sources"] = QStringLiteral("beanbase+history");
            tier1.append(row);
        } else {
            row["beanBaseData"] = entryJson;
            row["tier"] = static_cast<int>(Tier::CanonicalOnly);
            row["sources"] = QStringLiteral("beanbase");
            tier2.append(row);
        }
    }

    // Remaining history entries: absorbed by inventory, else tier 3/4.
    for (qsizetype i = 0; i < historyEntries.size(); i++) {
        if (histConsumed[i])
            continue;
        QVariantMap h = historyEntries[i].toMap();
        if (!matchesQuery(h, query))
            continue;
        const QString canonicalId = h.value("beanBaseId").toString();
        const QString nameKey = identityKey(h.value("roasterName").toString(),
                                            h.value("coffeeName").toString());
        if ((!canonicalId.isEmpty() && invByCanonicalId.contains(canonicalId))
            || invByName.contains(nameKey))
            continue;  // absorbed into the bag's Tier 0 row
        h["bagId"] = -1;
        h["tier"] = static_cast<int>(canonicalId.isEmpty() ? Tier::HistoryFreeText
                                                            : Tier::HistoryLinked);
        h["sources"] = QStringLiteral("history");
        tier34.append(h);
    }

    // Within-tier ordering: MRU for everything history-backed; canonical-only
    // keeps the API's relevance order. Tier 3 before tier 4.
    auto byEpochDesc = [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("lastUsedEpoch").toLongLong()
             > b.toMap().value("lastUsedEpoch").toLongLong();
    };
    std::stable_sort(tier0.begin(), tier0.end(), byEpochDesc);
    std::stable_sort(tier1.begin(), tier1.end(), byEpochDesc);
    std::stable_sort(tier34.begin(), tier34.end(), [](const QVariant& a, const QVariant& b) {
        const QVariantMap ma = a.toMap(), mb = b.toMap();
        const auto ta = static_cast<Tier>(ma.value("tier").toInt());
        const auto tb = static_cast<Tier>(mb.value("tier").toInt());
        if (ta != tb)
            return ta < tb;  // HistoryLinked (3) before HistoryFreeText (4)
        return ma.value("lastUsedEpoch").toLongLong() > mb.value("lastUsedEpoch").toLongLong();
    });

    const QVariantList merged = tier0 + tier1 + tier2 + tier34;
#ifndef QT_NO_DEBUG
    // A positive bagId must only ever ride on a Tier::Inventory row: inventory
    // rows mirror the bag's id into "bagId" above, every other lane sets it to
    // -1 by construction.
    for (const QVariant& v : merged) {
        const QVariantMap m = v.toMap();
        const auto tier = static_cast<Tier>(m.value("tier").toInt());
        const qint64 bagId = m.value("bagId", -1).toLongLong();
        Q_ASSERT(bagIdInvariantHolds(tier, bagId));
    }
#endif
    return merged;
}

void UnifiedBeanSearchModel::rebuild()
{
    beginResetModel();
    m_results = mergeLanes(m_inventory, m_canonical, m_history, m_query);
    endResetModel();
    emit countChanged();
}

void UnifiedBeanSearchModel::setSearching(bool searching)
{
    if (m_searching == searching)
        return;
    m_searching = searching;
    emit searchingChanged();
}

QVariantMap UnifiedBeanSearchModel::get(int row) const
{
    if (row < 0 || row >= m_results.size())
        return QVariantMap();
    return m_results[row].toMap();
}

int UnifiedBeanSearchModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_results.size());
}

QVariant UnifiedBeanSearchModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_results.size())
        return QVariant();
    const QVariantMap row = m_results[index.row()].toMap();
    switch (role) {
    case TierRole: return row.value("tier");
    case SourcesRole: return row.value("sources");
    case RoasterNameRole: return row.value("roasterName");
    case CoffeeNameRole: return row.value("coffeeName");
    case BagIdRole: return row.value("bagId", -1);
    case BeanBaseIdRole: return row.value("beanBaseId");
    case RoastDateRole: return row.value("roastDate");
    case FrozenDateRole: return row.value("frozenDate");
    case DefrostDateRole: return row.value("defrostDate");
    case LastUsedEpochRole: return row.value("lastUsedEpoch");
    case DetailRole: return row.value("detail");
    }
    return QVariant();
}

QHash<int, QByteArray> UnifiedBeanSearchModel::roleNames() const
{
    return {
        {TierRole, "tier"},
        {SourcesRole, "sources"},
        {RoasterNameRole, "roasterName"},
        {CoffeeNameRole, "coffeeName"},
        {BagIdRole, "bagId"},
        {BeanBaseIdRole, "beanBaseId"},
        {RoastDateRole, "roastDate"},
        {FrozenDateRole, "frozenDate"},
        {DefrostDateRole, "defrostDate"},
        {LastUsedEpochRole, "lastUsedEpoch"},
        {DetailRole, "detail"},
    };
}
