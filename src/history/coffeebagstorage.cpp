#include "coffeebagstorage.h"
#include "core/dbutils.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QSqlRecord>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDate>
#include <QDateTime>
#include <QSettings>
#include <QSet>
#include <QThread>
#include <QDebug>

#include <iterator>
#include <type_traits>
#include <utility>

namespace {

QVariant nullIfEmpty(const QString& s) {
    return s.isEmpty() ? QVariant() : QVariant(s);
}

QVariant nullIfZero(double v) {
    return v > 0 ? QVariant(v) : QVariant();
}

// -------------------------------------------------------------------------
// Single source of truth for the coffee_bags column set.
//
// Every per-column code representation — the SELECT list, the positional read
// in bagFromQueryRow, the INSERT list + binds, the camelCase->column update
// map, the Visualizer-stored subset, and the QVariantMap round-trip — is
// DERIVED from this one ordered table, so those can't drift out of sync, and
// bagFromQueryRow can no longer read the wrong column positionally because it
// indexes the SAME array the SELECT is built from.
//
// The one thing NOT generated from this table is the physical schema: the
// CREATE TABLE in ensureTableStatic (plus its created_at/updated_at bookkeeping
// columns, which kCols does not model) and the migrations. Adding a column is a
// kCols row here PLUS a matching schema/migration edit there.
//
// `shotCount` is intentionally NOT in this table, and not a CoffeeBag field at
// all: it is a per-query aggregate fed by loadInventoryStatic's subquery alias.
// It rides on InventoryBag (the inventory loader's return type) and is injected
// into the variant map by requestInventory, so toVariantMap/fromVariantMap stay
// pure column round-trips.
//
// Per-column behaviour is parameterised on the CoffeeBag member pointer so the
// member is named exactly once per row (via the COL_* macros below). The
// static_asserts pin each hook to the member type its conversion expects, so a
// future row built with the wrong COL_* macro (e.g. COL_DBL on a qint64 member,
// which would silently truncate) is a compile error rather than silent
// data corruption.
template<auto M> using BagMemberT = std::remove_reference_t<decltype(std::declval<CoffeeBag&>().*M)>;

template<auto M> void readStr (CoffeeBag& b, const QVariant& v) { static_assert(std::is_same_v<BagMemberT<M>, QString>); b.*M = v.toString(); }
template<auto M> void readDbl (CoffeeBag& b, const QVariant& v) { static_assert(std::is_same_v<BagMemberT<M>, double>);  b.*M = v.toDouble(); }
template<auto M> void readI64 (CoffeeBag& b, const QVariant& v) { static_assert(std::is_same_v<BagMemberT<M>, qint64>);  b.*M = v.toLongLong(); }
template<auto M> void readBool(CoffeeBag& b, const QVariant& v) { static_assert(std::is_same_v<BagMemberT<M>, bool>);    b.*M = v.toInt() != 0; }

template<auto M> QVariant bindStr  (const CoffeeBag& b) { static_assert(std::is_same_v<BagMemberT<M>, QString>); return nullIfEmpty(b.*M); }
template<auto M> QVariant bindDbl  (const CoffeeBag& b) { static_assert(std::is_same_v<BagMemberT<M>, double>);  return nullIfZero(b.*M); }
template<auto M> QVariant bindBool (const CoffeeBag& b) { static_assert(std::is_same_v<BagMemberT<M>, bool>);    return (b.*M) ? 1 : 0; }
template<auto M> QVariant bindEpoch(const CoffeeBag& b) { static_assert(std::is_same_v<BagMemberT<M>, qint64>);  return (b.*M) > 0 ? QVariant(b.*M) : QVariant(); }

template<auto M> QVariant getMember(const CoffeeBag& b) { return QVariant::fromValue(b.*M); }
template<auto M> void setStr (CoffeeBag& b, const QVariant& v) { static_assert(std::is_same_v<BagMemberT<M>, QString>); b.*M = v.toString(); }
template<auto M> void setDbl (CoffeeBag& b, const QVariant& v) { static_assert(std::is_same_v<BagMemberT<M>, double>);  b.*M = v.toDouble(); }
template<auto M> void setI64 (CoffeeBag& b, const QVariant& v) { static_assert(std::is_same_v<BagMemberT<M>, qint64>);  b.*M = v.toLongLong(); }
template<auto M> void setBool(CoffeeBag& b, const QVariant& v) { static_assert(std::is_same_v<BagMemberT<M>, bool>);    b.*M = v.toBool(); }

struct BagCol {
    const char* sql;                              // SQLite column name
    const char* key;                              // camelCase CoffeeBag / QVariantMap key
    bool visualizer;                              // Visualizer stores this on its bean record
    bool writable;                                // in INSERT + the update map (false only for autoincrement id)
    void (*read)(CoffeeBag&, const QVariant&);    // SELECT row value -> field (read positionally)
    QVariant (*bind)(const CoffeeBag&);           // field -> INSERT bind (NULL-collapsing); null when !writable
    QVariant (*get)(const CoffeeBag&);            // field -> QVariantMap value
    void (*set)(CoffeeBag&, const QVariant&);     // QVariantMap value -> field
};

// String columns carry an explicit Visualizer flag (the identity/lifecycle
// strings sync; the grinder/visualizer-id strings do not). The numeric/bool
// columns are all local-only.
#define COL_STR(sqlName, member, viz) \
    BagCol{ sqlName, #member, viz, true, \
            &readStr<&CoffeeBag::member>, &bindStr<&CoffeeBag::member>, \
            &getMember<&CoffeeBag::member>, &setStr<&CoffeeBag::member> }
#define COL_DBL(sqlName, member) \
    BagCol{ sqlName, #member, false, true, \
            &readDbl<&CoffeeBag::member>, &bindDbl<&CoffeeBag::member>, \
            &getMember<&CoffeeBag::member>, &setDbl<&CoffeeBag::member> }
#define COL_BOOL(sqlName, member) \
    BagCol{ sqlName, #member, false, true, \
            &readBool<&CoffeeBag::member>, &bindBool<&CoffeeBag::member>, \
            &getMember<&CoffeeBag::member>, &setBool<&CoffeeBag::member> }
#define COL_EPOCH(sqlName, member) \
    BagCol{ sqlName, #member, false, true, \
            &readI64<&CoffeeBag::member>, &bindEpoch<&CoffeeBag::member>, \
            &getMember<&CoffeeBag::member>, &setI64<&CoffeeBag::member> }
#define COL_ID(sqlName, member) \
    BagCol{ sqlName, #member, false, false, \
            &readI64<&CoffeeBag::member>, nullptr, \
            &getMember<&CoffeeBag::member>, &setI64<&CoffeeBag::member> }

const BagCol kCols[] = {
    COL_ID   ("id",                    id),
    COL_STR  ("roaster_name",          roasterName,         true),
    COL_STR  ("coffee_name",           coffeeName,          true),
    COL_STR  ("roast_date",            roastDate,           true),
    COL_STR  ("roast_level",           roastLevel,          true),
    COL_STR  ("beanbase_id",           beanBaseId,          true),
    COL_STR  ("beanbase_json",         beanBaseData,        true),
    COL_STR  ("frozen_date",           frozenDate,          true),
    COL_STR  ("defrost_date",          defrostDate,         true),
    COL_STR  ("notes",                 notes,               true),
    COL_DBL  ("start_weight_g",        startWeightG),
    COL_BOOL ("in_inventory",          inInventory),
    // Grinder identity (brand/model/burrs) is no longer stored on the bag —
    // it resolves through equipment_id to the package's grinder item
    // (migration 23 dropped the columns). The grind setting + rpm stay as the
    // bag's bean-scoped dial memory.
    COL_STR  ("grinder_setting",       grinderSetting,      false),
    COL_EPOCH("equipment_id",          equipmentId),
    COL_EPOCH("rpm",                   rpm),
    COL_DBL  ("dose_weight_g",         doseWeightG),
    COL_DBL  ("yield_override_g",      yieldOverrideG),
    COL_STR  ("visualizer_bag_id",     visualizerBagId,     false),
    COL_STR  ("visualizer_roaster_id", visualizerRoasterId, false),
    COL_EPOCH("last_used",             lastUsedEpoch),
};

#undef COL_STR
#undef COL_DBL
#undef COL_BOOL
#undef COL_EPOCH
#undef COL_ID

constexpr int kColCount = static_cast<int>(std::size(kCols));

// Comma-joined SELECT column list (also the INSERT column order), derived
// once from kCols.
const QString& bagColumnList() {
    static const QString cols = []() {
        QStringList names;
        for (const BagCol& c : kCols)
            names << QLatin1String(c.sql);
        return names.join(QStringLiteral(", "));
    }();
    return cols;
}

// camelCase CoffeeBag key -> column descriptor, for the writable columns only
// (everything but the autoincrement id). Drives updateBagFieldsStatic, which
// reaches through the descriptor to the column's sql name and its set/bind
// hooks so update-time value coercion is the SAME logic as insert.
const QHash<QString, const BagCol*>& bagColumnForKey() {
    static const QHash<QString, const BagCol*> map = []() {
        QHash<QString, const BagCol*> m;
        for (const BagCol& c : kCols)
            if (c.writable)
                m.insert(QString::fromLatin1(c.key), &c);
        return m;
    }();
    return map;
}

// The camelCase keys whose values Visualizer stores on its bean record.
// Drives touchesVisualizerFields.
const QSet<QString>& bagVisualizerKeys() {
    static const QSet<QString> keys = []() {
        QSet<QString> s;
        for (const BagCol& c : kCols)
            if (c.visualizer)
                s.insert(QString::fromLatin1(c.key));
        return s;
    }();
    return keys;
}

} // namespace

QVariantMap CoffeeBag::toVariantMap() const
{
    QVariantMap map;
    for (const BagCol& c : kCols)
        map.insert(QString::fromLatin1(c.key), c.get(*this));
    return map;
}

CoffeeBag CoffeeBag::fromVariantMap(const QVariantMap& map)
{
    // Absent keys keep the struct's member defaults, which match the previous
    // map.value(key, <default>) defaults exactly (inInventory = true, the rest
    // 0 / empty string) — so the round-trip behaviour is unchanged.
    CoffeeBag bag;
    for (const BagCol& c : kCols) {
        const QString key = QString::fromLatin1(c.key);
        if (map.contains(key))
            c.set(bag, map.value(key));
    }
    return bag;
}

CoffeeBagStorage::CoffeeBagStorage(QObject* parent)
    : QObject(parent)
{
}

CoffeeBagStorage::~CoffeeBagStorage()
{
    *m_destroyed = true;
    // Stop the worker before members vanish: its destructor quit/wait()s the
    // thread, so any in-flight task finishes (or is skipped via m_destroyed)
    // while `this` is still alive.
    m_dbWorker.reset();
}

void CoffeeBagStorage::initialize(const QString& dbPath)
{
    m_dbPath = dbPath;
}

void CoffeeBagStorage::runAsync(const QString& connPrefix,
                                std::function<void(QSqlDatabase&)> work,
                                std::function<void()> done)
{
    if (m_dbPath.isEmpty()) {
        qWarning() << "CoffeeBagStorage: not initialized, dropping" << connPrefix;
        return;
    }
    if (!m_dbWorker)
        m_dbWorker = std::make_unique<SerialDbWorker>();
    m_dbWorker->run(m_dbPath, connPrefix, std::move(work), std::move(done), this, m_destroyed);
}

void CoffeeBagStorage::requestInventory()
{
    auto bags = std::make_shared<QVariantList>();
    runAsync("bags_inv",
        [bags](QSqlDatabase& db) {
            const QVector<InventoryBag> inventory = loadInventoryStatic(db);
            for (const InventoryBag& entry : inventory) {
                // shotCount is an inventory-only aggregate, not a CoffeeBag
                // field — inject it into the map the QML card reads.
                QVariantMap map = entry.bag.toVariantMap();
                map.insert(QStringLiteral("shotCount"), entry.shotCount);
                bags->append(map);
            }
        },
        [this, bags]() { emit inventoryReady(*bags); });
}

void CoffeeBagStorage::requestBag(qint64 bagId)
{
    auto result = std::make_shared<QVariantMap>();
    runAsync("bags_get",
        [bagId, result](QSqlDatabase& db) {
            const CoffeeBag bag = loadBagStatic(db, bagId);
            if (bag.isValid())
                *result = bag.toVariantMap();
        },
        [this, bagId, result]() { emit bagReady(bagId, *result); });
}

void CoffeeBagStorage::requestCreateBag(const QVariantMap& bagMap)
{
    auto newId = std::make_shared<qint64>(-1);
    auto created = std::make_shared<QVariantMap>();
    runAsync("bags_create",
        [bagMap, newId, created](QSqlDatabase& db) {
            CoffeeBag bag = CoffeeBag::fromVariantMap(bagMap);
            bag.lastUsedEpoch = QDateTime::currentSecsSinceEpoch();
            *newId = insertBagStatic(db, bag);
            if (*newId > 0)
                *created = loadBagStatic(db, *newId).toVariantMap();
        },
        [this, newId, created]() {
            emit bagCreated(*newId, *created);
            if (*newId > 0)
                emit bagsChanged();
        });
}

void CoffeeBagStorage::requestUpdateBag(qint64 bagId, const QVariantMap& fields,
                                        bool propagateBeanBase)
{
    // Guarantee a terminal bagUpdated even when uninitialized: runAsync drops
    // the job (no done callback) if m_dbPath is empty, and callers like the MCP
    // bag_update tool arm a one-shot bagUpdated to send their response — without
    // this they would hang forever. (A *destroyed shutdown race still drops the
    // callback, but the app is exiting, so an abandoned response is acceptable.)
    if (m_dbPath.isEmpty()) {
        qWarning() << "CoffeeBagStorage: requestUpdateBag on uninitialized storage, bag" << bagId;
        emit bagUpdated(bagId, false);
        return;
    }
    auto success = std::make_shared<bool>(false);
    runAsync("bags_update",
        [bagId, fields, success, propagateBeanBase](QSqlDatabase& db) {
            *success = updateBagFieldsStatic(db, bagId, fields);
            if (*success && propagateBeanBase)
                propagateBeanBaseStatic(db, bagId);
        },
        [this, bagId, fields, success]() {
            emit bagUpdated(bagId, *success);
            if (*success) {
                emit bagsChanged();
                if (touchesVisualizerFields(fields))
                    emit bagVisualizerFieldsChanged(bagId);
            }
        });
}

void CoffeeBagStorage::requestMarkEmpty(qint64 bagId)
{
    requestUpdateBag(bagId, {{"inInventory", false}});
}

void CoffeeBagStorage::requestTouchLastUsed(qint64 bagId)
{
    runAsync("bags_touch",
        [bagId](QSqlDatabase& db) {
            QSqlQuery query(db);
            query.prepare("UPDATE coffee_bags SET last_used = :now, "
                          "updated_at = strftime('%s', 'now') WHERE id = :id");
            query.bindValue(":now", QDateTime::currentSecsSinceEpoch());
            query.bindValue(":id", bagId);
            if (!query.exec())
                qWarning() << "CoffeeBagStorage: touch last_used failed:" << query.lastError().text();
        },
        []() {});
}

void CoffeeBagStorage::requestDeleteBag(qint64 bagId)
{
    auto success = std::make_shared<bool>(false);
    runAsync("bags_delete",
        [bagId, success](QSqlDatabase& db) {
            // Bags with linked shots are history — only Mark as Empty is
            // allowed for them. Delete is for mistaken creations.
            QSqlQuery countQuery(db);
            countQuery.prepare("SELECT COUNT(*) FROM shots WHERE bag_id = :id");
            countQuery.bindValue(":id", bagId);
            if (!countQuery.exec() || !countQuery.next()) {
                qWarning() << "CoffeeBagStorage: delete pre-check failed:" << countQuery.lastError().text();
                return;
            }
            if (countQuery.value(0).toInt() > 0) {
                qWarning() << "CoffeeBagStorage: refusing to delete bag" << bagId << "with linked shots";
                return;
            }
            QSqlQuery deleteQuery(db);
            deleteQuery.prepare("DELETE FROM coffee_bags WHERE id = :id");
            deleteQuery.bindValue(":id", bagId);
            *success = deleteQuery.exec();
            if (!*success)
                qWarning() << "CoffeeBagStorage: delete failed:" << deleteQuery.lastError().text();
        },
        [this, bagId, success]() {
            emit bagDeleted(bagId, *success);
            if (*success)
                emit bagsChanged();
        });
}

bool CoffeeBagStorage::ensureTableStatic(QSqlDatabase& db)
{
    QSqlQuery query(db);
    const bool ok = query.exec(R"(
        CREATE TABLE IF NOT EXISTS coffee_bags (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            roaster_name TEXT,
            coffee_name TEXT,
            roast_date TEXT,
            roast_level TEXT,
            beanbase_id TEXT,
            beanbase_json TEXT,
            frozen_date TEXT,
            defrost_date TEXT,
            notes TEXT,
            start_weight_g REAL,
            in_inventory INTEGER NOT NULL DEFAULT 1,
            -- Grinder identity columns are created here for the migration chain
            -- (migration 22 reads them to seed equipment packages) and dropped by
            -- migration 23 once identity resolves via equipment_id. Runtime
            -- reads/writes go through kCols, which no longer lists them.
            grinder_brand TEXT,
            grinder_model TEXT,
            grinder_burrs TEXT,
            grinder_setting TEXT,
            equipment_id INTEGER,
            rpm INTEGER,
            dose_weight_g REAL,
            yield_override_g REAL,
            visualizer_bag_id TEXT,
            visualizer_roaster_id TEXT,
            last_used INTEGER,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )");
    if (!ok) {
        qWarning() << "CoffeeBagStorage: failed to create coffee_bags table:" << query.lastError().text();
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_coffee_bags_inventory ON coffee_bags(in_inventory, last_used DESC)");
    return true;
}

CoffeeBag CoffeeBagStorage::bagFromQueryRow(const QSqlQuery& query)
{
    // Read positionally from the SAME table the SELECT list is built from, so
    // a column's read can never drift from its SELECT position. The first
    // kColCount columns are the bag columns (a trailing shot_count alias, when
    // present, is read separately by loadInventoryStatic).
    CoffeeBag bag;
    for (int i = 0; i < kColCount; ++i)
        kCols[i].read(bag, query.value(i));
    return bag;
}

qint64 CoffeeBagStorage::insertBagStatic(QSqlDatabase& db, const CoffeeBag& bag)
{
    // Column list, placeholders, and binds all derived from the writable
    // columns of kCols (every column but the autoincrement id), in table order
    // — so adding a column needs no edit in this function, just a kCols row.
    QStringList columns, placeholders;
    QVariantList binds;
    for (const BagCol& c : kCols) {
        if (!c.writable)
            continue;
        columns << QString::fromLatin1(c.sql);
        placeholders << QStringLiteral("?");
        binds << c.bind(bag);
    }

    QSqlQuery query(db);
    query.prepare(QString("INSERT INTO coffee_bags (%1) VALUES (%2)")
                      .arg(columns.join(QStringLiteral(", ")), placeholders.join(QStringLiteral(", "))));
    for (int i = 0; i < binds.size(); ++i)
        query.bindValue(i, binds.at(i));

    if (!query.exec()) {
        qWarning() << "CoffeeBagStorage: insert failed:" << query.lastError().text();
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

CoffeeBag CoffeeBagStorage::loadBagStatic(QSqlDatabase& db, qint64 bagId)
{
    QSqlQuery query(db);
    query.prepare(QString("SELECT %1 FROM coffee_bags WHERE id = :id").arg(bagColumnList()));
    query.bindValue(":id", bagId);
    if (!query.exec() || !query.next())
        return CoffeeBag();
    CoffeeBag bag = bagFromQueryRow(query);
    // Materialize the read-only grinder identity from the bag's equipment package
    // (the bag no longer stores brand/model/burrs — migration 23). Keeps the
    // CoffeeBag display-cache fields useful for consumers like MCP bag_list;
    // burrs lives in the grinder item's attrs JSON. Self-contained so there's no
    // dependency on EquipmentStorage. NULL equipment_id leaves the fields empty.
    if (bag.equipmentId > 0) {
        QSqlQuery gi(db);
        gi.prepare("SELECT brand, model, json_extract(attrs, '$.burrs') FROM equipment_items "
                   "WHERE package_id = :id AND kind = 'grinder' LIMIT 1");
        gi.bindValue(":id", bag.equipmentId);
        if (gi.exec() && gi.next()) {
            bag.grinderBrand = gi.value(0).toString();
            bag.grinderModel = gi.value(1).toString();
            bag.grinderBurrs = gi.value(2).toString();
        }
    }
    return bag;
}

QVector<InventoryBag> CoffeeBagStorage::loadInventoryStatic(QSqlDatabase& db)
{
    QVector<InventoryBag> bags;
    QSqlQuery query(db);
    // shot_count subquery feeds the card's single delete-vs-finished action:
    // a bag nothing references is a mistaken creation (trash); one with
    // shots is history ("Bag finished").
    if (!query.exec(QString("SELECT %1, "
                            "(SELECT COUNT(*) FROM shots WHERE bag_id = coffee_bags.id) AS shot_count "
                            "FROM coffee_bags WHERE in_inventory = 1 "
                            "ORDER BY last_used DESC, id DESC").arg(bagColumnList()))) {
        qWarning() << "CoffeeBagStorage: inventory query failed:" << query.lastError().text();
        return bags;
    }
    // Read shot_count by its alias, not a hardcoded position, so it no longer
    // silently depends on the bag column count.
    const int shotCountCol = query.record().indexOf("shot_count");
    while (query.next()) {
        InventoryBag entry;
        entry.bag = bagFromQueryRow(query);
        if (shotCountCol >= 0)
            entry.shotCount = query.value(shotCountCol).toLongLong();
        bags.append(entry);
    }
    return bags;
}

bool CoffeeBagStorage::updateBagFieldsStatic(QSqlDatabase& db, qint64 bagId, const QVariantMap& fields)
{
    // camelCase CoffeeBag key -> column descriptor (writable columns only),
    // derived from kCols. Only listed keys are updatable.
    const QHash<QString, const BagCol*>& kColumnFor = bagColumnForKey();

    QStringList assignments;
    QVariantList values;
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        const BagCol* col = kColumnFor.value(it.key(), nullptr);
        if (!col) {
            qWarning() << "CoffeeBagStorage: ignoring unknown field" << it.key();
            continue;
        }
        assignments << QString("%1 = ?").arg(QString::fromLatin1(col->sql));
        // Coerce through the column's own set+bind hooks so update-time value
        // handling is identical to insert: empty string -> NULL, inInventory ->
        // 0/1, an unset (0) numeric -> NULL. The round-trip into a throwaway bag
        // keeps the "is this NULL?" rule in exactly one place (the bind hook).
        CoffeeBag scratch;
        col->set(scratch, it.value());
        values << col->bind(scratch);
    }
    if (assignments.isEmpty())
        return false;

    QSqlQuery query(db);
    query.prepare(QString("UPDATE coffee_bags SET %1, updated_at = strftime('%s', 'now') WHERE id = ?")
                      .arg(assignments.join(", ")));
    int pos = 0;
    for (const QVariant& value : values)
        query.bindValue(pos++, value);
    query.bindValue(pos, bagId);

    if (!query.exec()) {
        qWarning() << "CoffeeBagStorage: update failed for bag" << bagId << ":" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool CoffeeBagStorage::touchesVisualizerFields(const QVariantMap& fields)
{
    // The camelCase keys whose values Visualizer stores on its coffee bag
    // (the columns flagged visualizer==true in kCols): beanBaseId ->
    // canonical_coffee_bag_id; beanBaseData -> the descriptive blob
    // (country/variety/process/tasting/...); roasterName maps to the bag's
    // roaster_id (re-resolved on rename). Deliberately EXCLUDES the local-only
    // columns (grinder*/dose/yield/startWeight/lastUsed/inInventory and the
    // visualizer_* sync ids themselves) so a grinder write-through or dose/yield
    // stamp never triggers a network PATCH.
    const QSet<QString>& kVisualizerKeys = bagVisualizerKeys();
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        if (kVisualizerKeys.contains(it.key()))
            return true;
    return false;
}

double CoffeeBagStorage::yieldOverrideForTarget(double shotTargetWeightG, double profileTargetWeightG)
{
    // 0.1 g epsilon absorbs float noise / display rounding so a target that
    // equals the profile default isn't recorded as an override.
    return (shotTargetWeightG > 0.0 && qAbs(shotTargetWeightG - profileTargetWeightG) > 0.1)
               ? shotTargetWeightG : 0.0;
}

CoffeeBag CoffeeBagStorage::bagFromLegacyPreset(const QJsonObject& preset)
{
    CoffeeBag bag;
    bag.roasterName = preset.value("brand").toString();
    bag.coffeeName = preset.value("type").toString();
    bag.roastDate = preset.value("roastDate").toString();
    bag.roastLevel = preset.value("roastLevel").toString();
    bag.beanBaseId = preset.value("beanBaseId").toString();
    bag.beanBaseData = preset.value("beanBaseData").toString();
    // Grinder identity is no longer a bag column (migration 23) — it resolves
    // through equipment_id. Legacy presets predate the equipment model and carry
    // no package linkage, so only the grind setting is carried across.
    bag.grinderSetting = preset.value("grinderSetting").toString();
    bag.inInventory = true;

    // The user-chosen preset label survives in notes when it carried more
    // information than "{brand} {type}". barista (per-shot, snapshot on every
    // shot already) and showOnIdle (superseded by inInventory) are dropped.
    const QString name = preset.value("name").toString().trimmed();
    const QString brandType = QString("%1 %2").arg(bag.roasterName, bag.coffeeName).trimmed();
    if (!name.isEmpty() && name.compare(brandType, Qt::CaseInsensitive) != 0
        && name.compare(bag.coffeeName, Qt::CaseInsensitive) != 0)
        bag.notes = name;

    return bag;
}

int CoffeeBagStorage::propagateBeanBaseStatic(QSqlDatabase& db, qint64 bagId)
{
    const CoffeeBag bag = loadBagStatic(db, bagId);
    if (!bag.isValid())
        return -1;

    QSqlQuery query(db);
    query.prepare("UPDATE shots SET beanbase_id = :id, beanbase_json = :blob, "
                  "updated_at = strftime('%s', 'now') WHERE bag_id = :bag");
    query.bindValue(":id", bag.beanBaseId.isEmpty() ? QVariant() : bag.beanBaseId);
    query.bindValue(":blob", bag.beanBaseData.isEmpty() ? QVariant() : bag.beanBaseData);
    query.bindValue(":bag", bagId);
    if (!query.exec()) {
        qWarning() << "CoffeeBagStorage: beanbase propagation failed for bag" << bagId
                   << ":" << query.lastError().text();
        return -1;
    }
    const int updated = query.numRowsAffected();
    qDebug() << "CoffeeBagStorage: propagated bean base link of bag" << bagId
             << "to" << updated << "shots";
    return updated;
}

int CoffeeBagStorage::linkOrphanShotsStatic(QSqlDatabase& db)
{
    int linked = 0;

    // Pass 1: exact identity — roaster + coffee + roast date. The correlated
    // subquery returns NULL when nothing matches, which re-writes NULL over
    // NULL (a no-op), so no WHERE-EXISTS guard is needed.
    QSqlQuery exactPass(db);
    if (!exactPass.exec(
            "UPDATE shots SET bag_id = ("
            "  SELECT b.id FROM coffee_bags b"
            "  WHERE LOWER(COALESCE(b.roaster_name,'')) = LOWER(COALESCE(shots.bean_brand,''))"
            "    AND LOWER(COALESCE(b.coffee_name,''))  = LOWER(COALESCE(shots.bean_type,''))"
            "    AND COALESCE(b.roast_date,'') = COALESCE(shots.roast_date,'')"
            "  ORDER BY b.last_used DESC, b.id DESC LIMIT 1) "
            "WHERE bag_id IS NULL"
            "  AND (COALESCE(bean_brand,'') <> '' OR COALESCE(bean_type,'') <> '')")) {
        qWarning() << "CoffeeBagStorage: orphan-shot link pass 1 failed:" << exactPass.lastError().text();
        return -1;
    }

    // Pass 2: identity only (roaster + coffee) for leftovers — pre-bag shots
    // whose snapshot roast date doesn't match any bag (stale preset dates,
    // format drift). Most recently used bag of that coffee wins.
    QSqlQuery identityPass(db);
    if (!identityPass.exec(
            "UPDATE shots SET bag_id = ("
            "  SELECT b.id FROM coffee_bags b"
            "  WHERE LOWER(COALESCE(b.roaster_name,'')) = LOWER(COALESCE(shots.bean_brand,''))"
            "    AND LOWER(COALESCE(b.coffee_name,''))  = LOWER(COALESCE(shots.bean_type,''))"
            "  ORDER BY b.last_used DESC, b.id DESC LIMIT 1) "
            "WHERE bag_id IS NULL"
            "  AND (COALESCE(bean_brand,'') <> '' OR COALESCE(bean_type,'') <> '')")) {
        qWarning() << "CoffeeBagStorage: orphan-shot link pass 2 failed:" << identityPass.lastError().text();
        return -1;
    }

    QSqlQuery countQuery(db);
    if (countQuery.exec("SELECT COUNT(*) FROM shots WHERE bag_id IS NOT NULL") && countQuery.next())
        linked = countQuery.value(0).toInt();
    return linked;
}

qint64 CoffeeBagStorage::findBagForShotStatic(QSqlDatabase& db, qint64 shotId,
                                              const QString& roasterName, const QString& coffeeName)
{
    if (shotId > 0) {
        QSqlQuery linkQuery(db);
        linkQuery.prepare("SELECT b.id FROM shots s JOIN coffee_bags b ON b.id = s.bag_id "
                          "WHERE s.id = :sid");
        linkQuery.bindValue(":sid", shotId);
        if (linkQuery.exec() && linkQuery.next())
            return linkQuery.value(0).toLongLong();
    }

    if (roasterName.isEmpty() && coffeeName.isEmpty())
        return -1;

    QSqlQuery identityQuery(db);
    identityQuery.prepare(
        "SELECT id FROM coffee_bags WHERE "
        "LOWER(COALESCE(roaster_name,'')) = LOWER(:roaster) AND "
        "LOWER(COALESCE(coffee_name,'')) = LOWER(:coffee) "
        "ORDER BY in_inventory DESC, last_used DESC, id DESC LIMIT 1");
    identityQuery.bindValue(":roaster", roasterName);
    identityQuery.bindValue(":coffee", coffeeName);
    if (identityQuery.exec() && identityQuery.next())
        return identityQuery.value(0).toLongLong();
    return -1;
}

qint64 CoffeeBagStorage::convertLegacyPresetSettings(const QString& dbPath)
{
    // Same QSettings scope the app's Settings object owns — a bare
    // QSettings() resolves to a different, empty store.
    QSettings appSettings(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
    const QByteArray presetsJson = appSettings.value(QStringLiteral("bean/presets")).toByteArray();
    if (presetsJson.isEmpty())
        return -1;

    const QJsonArray presets = QJsonDocument::fromJson(presetsJson).array();
    if (presets.isEmpty()) {
        // SettingsDye historically seeded an empty array on construction —
        // nothing to import, just retire the legacy keys.
        appSettings.remove(QStringLiteral("bean/presets"));
        appSettings.remove(QStringLiteral("bean/selectedPreset"));
        return -1;
    }

    const int selectedIndex = appSettings.value(QStringLiteral("bean/selectedPreset"), -1).toInt();
    qint64 selectedBagId = -1;
    bool committed = false;
    int imported = -1;

    withTempDb(dbPath, "bags_legacy", [&](QSqlDatabase& db) {
        // Pre-migration-19 database (e.g. main DB init not yet run): leave
        // the keys for a later retry.
        QSqlQuery probe(db);
        if (!probe.exec("SELECT COUNT(*) FROM coffee_bags"))
            return;

        if (!db.transaction()) {
            qWarning() << "CoffeeBagStorage: legacy preset transaction begin failed:"
                       << db.lastError().text();
            return;
        }
        imported = importLegacyPresetsStatic(db, presets, selectedIndex, &selectedBagId);
        if (imported < 0) {
            qWarning() << "CoffeeBagStorage: legacy preset import failed - rolling back";
            db.rollback();
            return;
        }
        if (!db.commit()) {
            qWarning() << "CoffeeBagStorage: legacy preset import commit failed:"
                       << db.lastError().text();
            db.rollback();
            return;
        }
        committed = true;
    });

    if (!committed)
        return -1;

    appSettings.remove(QStringLiteral("bean/presets"));
    appSettings.remove(QStringLiteral("bean/selectedPreset"));
    qDebug() << "CoffeeBagStorage: imported" << imported << "of" << presets.size()
             << "legacy bean presets as bags; selected bag id" << selectedBagId;

    // Adopt the pre-bag shot history: link orphan shots to the bags the
    // presets just became, so a migrated favorite carries its shots (and the
    // card shows "Bag finished", not delete).
    withTempDb(dbPath, "bags_link", [&](QSqlDatabase& db) {
        linkOrphanShotsStatic(db);
    });
    return selectedBagId;
}

bool CoffeeBagStorage::importBagsStatic(QSqlDatabase& srcDb, QSqlDatabase& destDb, bool merge,
                                        QHash<qint64, qint64>& outIdMap,
                                        const QHash<qint64, qint64>& packageIdMap)
{
    // Pre-migration-19 source: no coffee_bags table, nothing to import.
    {
        QSqlQuery srcCheck(srcDb);
        if (!srcCheck.exec("SELECT COUNT(*) FROM coffee_bags"))
            return true;
    }

    if (!merge) {
        QSqlQuery clearQuery(destDb);
        if (!clearQuery.exec("DELETE FROM coffee_bags")) {
            qWarning() << "CoffeeBagStorage: failed to clear bags for replace import:"
                       << clearQuery.lastError().text();
            return false;
        }
    }

    QSqlQuery srcBags(srcDb);
    if (!srcBags.exec(QString("SELECT %1 FROM coffee_bags").arg(bagColumnList()))) {
        qWarning() << "CoffeeBagStorage: failed to query source bags:" << srcBags.lastError().text();
        return false;
    }

    int imported = 0, matched = 0;
    while (srcBags.next()) {
        CoffeeBag bag = bagFromQueryRow(srcBags);
        // Remap the source equipment_id to the imported package's new dest id
        // (add-equipment-packages task 2.8). A source id absent from the map
        // (older source with no equipment tables, or an unmatched package)
        // becomes 0 -> NULL. rpm (a plain dial-in number) carries fine as-is.
        bag.equipmentId = packageIdMap.value(bag.equipmentId, 0);

        qint64 destId = -1;
        if (merge) {
            QSqlQuery dupQuery(destDb);
            dupQuery.prepare(
                "SELECT id FROM coffee_bags WHERE "
                "LOWER(COALESCE(roaster_name,'')) = LOWER(:roaster) AND "
                "LOWER(COALESCE(coffee_name,'')) = LOWER(:coffee) AND "
                "COALESCE(roast_date,'') = :roast_date LIMIT 1");
            dupQuery.bindValue(":roaster", bag.roasterName);
            dupQuery.bindValue(":coffee", bag.coffeeName);
            dupQuery.bindValue(":roast_date", bag.roastDate);
            if (dupQuery.exec() && dupQuery.next()) {
                destId = dupQuery.value(0).toLongLong();
                matched++;
            }
        }

        if (destId < 0) {
            destId = insertBagStatic(destDb, bag);
            if (destId < 0)
                return false;
            imported++;
        }
        outIdMap.insert(bag.id, destId);
    }

    qDebug() << "CoffeeBagStorage: bag import -" << imported << "imported," << matched << "matched existing";
    return true;
}

int CoffeeBagStorage::importLegacyPresetsStatic(QSqlDatabase& db, const QJsonArray& presets,
                                                int selectedIndex, qint64* outSelectedBagId)
{
    int inserted = 0;
    for (qsizetype i = 0; i < presets.size(); i++) {
        const CoffeeBag bag = bagFromLegacyPreset(presets[i].toObject());
        if (bag.roasterName.isEmpty() && bag.coffeeName.isEmpty())
            continue;

        // Dedup on identity (case-insensitive roaster+name+roastDate).
        // Conservative on purpose: presets carry no lifecycle data, so a
        // skipped duplicate loses nothing.
        qint64 bagId = -1;
        {
            QSqlQuery dupQuery(db);
            dupQuery.prepare(
                "SELECT id FROM coffee_bags WHERE "
                "LOWER(COALESCE(roaster_name,'')) = LOWER(:roaster) AND "
                "LOWER(COALESCE(coffee_name,'')) = LOWER(:coffee) AND "
                "COALESCE(roast_date,'') = :roast_date LIMIT 1");
            dupQuery.bindValue(":roaster", bag.roasterName);
            dupQuery.bindValue(":coffee", bag.coffeeName);
            dupQuery.bindValue(":roast_date", bag.roastDate);
            if (dupQuery.exec() && dupQuery.next()) {
                bagId = dupQuery.value(0).toLongLong();
                qDebug() << "CoffeeBagStorage: preset import skipping duplicate"
                         << bag.roasterName << bag.coffeeName;
            }
        }

        if (bagId < 0) {
            bagId = insertBagStatic(db, bag);
            if (bagId < 0)
                return -1;  // caller rolls back
            inserted++;
        }

        if (outSelectedBagId && i == selectedIndex)
            *outSelectedBagId = bagId;
    }
    return inserted;
}
