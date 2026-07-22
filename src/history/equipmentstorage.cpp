#include "equipmentstorage.h"
#include "core/dbutils.h"
#include "core/grinderaliases.h"
#include "core/basketaliases.h"
#include "core/puckprep.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QSqlRecord>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QRegularExpression>
#include <QThread>
#include <QDebug>

#include <tuple>

#include <iterator>
#include <type_traits>
#include <utility>

namespace {

QVariant nullIfEmpty(const QString& s) {
    return s.isEmpty() ? QVariant() : QVariant(s);
}

// -------------------------------------------------------------------------
// Single source of truth for the equipment_packages column set, mirroring
// CoffeeBagStorage::kCols: the SELECT list, positional read, INSERT list/binds,
// the camelCase->column update map, and the QVariantMap round-trip are all
// DERIVED from this one ordered table so they can't drift. The physical schema
// (CREATE TABLE in ensureTablesStatic + the created_at/updated_at bookkeeping
// columns) is the one thing NOT generated here — adding a column is a kCols row
// PLUS a matching schema/migration edit.
// -------------------------------------------------------------------------
template<auto M> using PkgMemberT = std::remove_reference_t<decltype(std::declval<EquipmentPackage&>().*M)>;

template<auto M> void readStr (EquipmentPackage& p, const QVariant& v) { static_assert(std::is_same_v<PkgMemberT<M>, QString>); p.*M = v.toString(); }
template<auto M> void readI64 (EquipmentPackage& p, const QVariant& v) { static_assert(std::is_same_v<PkgMemberT<M>, qint64>);  p.*M = v.toLongLong(); }
template<auto M> void readBool(EquipmentPackage& p, const QVariant& v) { static_assert(std::is_same_v<PkgMemberT<M>, bool>);    p.*M = v.toInt() != 0; }

template<auto M> QVariant bindStr  (const EquipmentPackage& p) { static_assert(std::is_same_v<PkgMemberT<M>, QString>); return nullIfEmpty(p.*M); }
template<auto M> QVariant bindBool (const EquipmentPackage& p) { static_assert(std::is_same_v<PkgMemberT<M>, bool>);    return (p.*M) ? 1 : 0; }
template<auto M> QVariant bindPos  (const EquipmentPackage& p) { static_assert(std::is_same_v<PkgMemberT<M>, qint64>);  return (p.*M) > 0 ? QVariant(p.*M) : QVariant(); }

template<auto M> QVariant getMember(const EquipmentPackage& p) { return QVariant::fromValue(p.*M); }
template<auto M> void setStr (EquipmentPackage& p, const QVariant& v) { static_assert(std::is_same_v<PkgMemberT<M>, QString>); p.*M = v.toString(); }
template<auto M> void setI64 (EquipmentPackage& p, const QVariant& v) { static_assert(std::is_same_v<PkgMemberT<M>, qint64>);  p.*M = v.toLongLong(); }
template<auto M> void setBool(EquipmentPackage& p, const QVariant& v) { static_assert(std::is_same_v<PkgMemberT<M>, bool>);    p.*M = v.toBool(); }

struct PkgCol {
    const char* sql;
    const char* key;
    bool writable;
    void (*read)(EquipmentPackage&, const QVariant&);
    QVariant (*bind)(const EquipmentPackage&);
    QVariant (*get)(const EquipmentPackage&);
    void (*set)(EquipmentPackage&, const QVariant&);
};

#define COL_STR(sqlName, member) \
    PkgCol{ sqlName, #member, true, \
            &readStr<&EquipmentPackage::member>, &bindStr<&EquipmentPackage::member>, \
            &getMember<&EquipmentPackage::member>, &setStr<&EquipmentPackage::member> }
#define COL_BOOL(sqlName, member) \
    PkgCol{ sqlName, #member, true, \
            &readBool<&EquipmentPackage::member>, &bindBool<&EquipmentPackage::member>, \
            &getMember<&EquipmentPackage::member>, &setBool<&EquipmentPackage::member> }
#define COL_POS(sqlName, member) \
    PkgCol{ sqlName, #member, true, \
            &readI64<&EquipmentPackage::member>, &bindPos<&EquipmentPackage::member>, \
            &getMember<&EquipmentPackage::member>, &setI64<&EquipmentPackage::member> }
#define COL_ID(sqlName, member) \
    PkgCol{ sqlName, #member, false, \
            &readI64<&EquipmentPackage::member>, nullptr, \
            &getMember<&EquipmentPackage::member>, &setI64<&EquipmentPackage::member> }

const PkgCol kCols[] = {
    COL_ID  ("id",                 id),
    COL_STR ("name",               name),
    COL_BOOL("in_inventory",       inInventory),
    COL_STR ("last_grind_setting", lastGrindSetting),
    COL_POS ("last_rpm",           lastRpm),
    COL_POS ("last_used",          lastUsedEpoch),
    // Non-writable: set only by the copy-on-write path's raw UPDATE (paired with
    // in_inventory=0), never via INSERT or the generic field-update map, so the
    // superseded_by / in_inventory coupling can't be half-set.
    COL_ID  ("superseded_by",      supersededBy),
};

#undef COL_STR
#undef COL_BOOL
#undef COL_POS
#undef COL_ID

constexpr int kColCount = static_cast<int>(std::size(kCols));

const QString& packageColumnList() {
    static const QString cols = []() {
        QStringList names;
        for (const PkgCol& c : kCols)
            names << QLatin1String(c.sql);
        return names.join(QStringLiteral(", "));
    }();
    return cols;
}

const QHash<QString, const PkgCol*>& packageColumnForKey() {
    static const QHash<QString, const PkgCol*> map = []() {
        QHash<QString, const PkgCol*> m;
        for (const PkgCol& c : kCols)
            if (c.writable)
                m.insert(QString::fromLatin1(c.key), &c);
        return m;
    }();
    return map;
}

EquipmentPackage packageFromQueryRow(const QSqlQuery& query) {
    EquipmentPackage pkg;
    for (int i = 0; i < kColCount; ++i)
        kCols[i].read(pkg, query.value(i));
    return pkg;
}

EquipmentItem grinderItemFromQueryRow(const QSqlQuery& query) {
    EquipmentItem item;
    item.id = query.value(0).toLongLong();
    item.packageId = query.value(1).toLongLong();
    item.kind = query.value(2).toString();
    item.brand = query.value(3).toString();
    item.model = query.value(4).toString();
    item.setAttrsFromJson(query.value(5).toString());
    return item;
}

const char* kItemColumns = "id, package_id, kind, brand, model, attrs";

} // namespace

// ---------------------------------------------------------------------------
// EquipmentItem attrs (de)serialization
// ---------------------------------------------------------------------------
QString EquipmentItem::attrsJson() const
{
    // Only the grinder kind carries attrs (burrs + rpmCapable). A basket's specs
    // are derived from BasketAliases at read time, so it persists an empty blob.
    if (kind != QStringLiteral("grinder"))
        return QStringLiteral("{}");
    QJsonObject obj;
    if (!burrs.isEmpty())
        obj.insert(QStringLiteral("burrs"), burrs);
    obj.insert(QStringLiteral("rpmCapable"), rpmCapable);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void EquipmentItem::setAttrsFromJson(const QString& json)
{
    const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
    burrs = obj.value(QStringLiteral("burrs")).toString();
    rpmCapable = obj.value(QStringLiteral("rpmCapable")).toBool();
}

// ---------------------------------------------------------------------------
// EquipmentPackage / EquipmentPackageView round-trip
// ---------------------------------------------------------------------------
QVariantMap EquipmentPackage::toVariantMap() const
{
    QVariantMap map;
    for (const PkgCol& c : kCols)
        map.insert(QString::fromLatin1(c.key), c.get(*this));
    return map;
}

EquipmentPackage EquipmentPackage::fromVariantMap(const QVariantMap& map)
{
    EquipmentPackage pkg;
    for (const PkgCol& c : kCols) {
        const QString key = QString::fromLatin1(c.key);
        if (map.contains(key))
            c.set(pkg, map.value(key));
    }
    return pkg;
}

QVariantMap EquipmentPackageView::toVariantMap() const
{
    QVariantMap map = package.toVariantMap();
    // Resolved grinder identity, flattened for QML cards and MCP.
    map.insert(QStringLiteral("grinderBrand"), grinder.brand);
    map.insert(QStringLiteral("grinderModel"), grinder.model);
    map.insert(QStringLiteral("grinderBurrs"), grinder.burrs);
    map.insert(QStringLiteral("rpmCapable"), grinder.rpmCapable);
    // Resolved basket identity (empty when the package has no basket) plus its
    // registry-derived specs. Specs are NOT stored — they are looked up here from
    // BasketAliases::findEntry, so a curated-DB refinement flows to every package.
    // A custom (off-registry) basket resolves to identity only (specs omitted).
    map.insert(QStringLiteral("basketBrand"), basket.brand);
    map.insert(QStringLiteral("basketModel"), basket.model);
    if (!basket.brand.isEmpty() || !basket.model.isEmpty()) {
        if (const BasketAliases::BasketEntry* e =
                BasketAliases::findEntry(basket.brand, basket.model)) {
            map.insert(QStringLiteral("basketWallProfile"), BasketAliases::wallProfileName(e->wall));
            map.insert(QStringLiteral("basketRelativeFlow"), BasketAliases::flowRateName(e->flow));
            map.insert(QStringLiteral("basketPrecision"), e->precision);
            map.insert(QStringLiteral("basketDoseMinG"), e->doseMinG);
            map.insert(QStringLiteral("basketDoseMaxG"), e->doseMaxG);
            map.insert(QStringLiteral("basketSummary"), BasketAliases::summary(*e));
        }
    }
    // Resolved puck prep: the canonical flag string lives in the item's `model`
    // column; reconstruct the individual booleans + the derived distribution from
    // it (PuckPrep). Emitted only when the package has a puckprep item.
    // Flags (for the card/info display + the dialog edit prefill) and the canonical
    // string (for the dialog dedup check + SettingsDye prefill). The derived
    // `distribution` rollup is NOT emitted here — it's an AI-only signal, computed
    // for the dialing context (buildCurrentBeanBlock) and the MCP equipment tools,
    // and deliberately kept out of anything user-visible.
    const QString puckCanon = puckPrep.model;
    if (!puckCanon.isEmpty()) {
        for (const QString& k : PuckPrep::flagKeys())
            map.insert(QStringLiteral("puckPrep_") + k, PuckPrep::has(puckCanon, k));
        map.insert(QStringLiteral("puckPrepCanonical"), puckCanon);
    }
    map.insert(QStringLiteral("shotCount"), shotCount);
    return map;
}

// ---------------------------------------------------------------------------
// EquipmentStorage
// ---------------------------------------------------------------------------
EquipmentStorage::EquipmentStorage(QObject* parent)
    : QObject(parent)
{
}

EquipmentStorage::~EquipmentStorage()
{
    *m_destroyed = true;
    // Stop the worker before members vanish. reset() runs ~SerialDbWorker, which
    // quit()s (discarding queued-but-unstarted tasks) and wait()s for the single
    // in-flight task to finish its DB work — all while `this` is still alive. The
    // m_destroyed flag, set just above, suppresses that task's result callback so
    // it can't touch a half-destroyed object.
    m_dbWorker.reset();
}

void EquipmentStorage::initialize(const QString& dbPath)
{
    m_dbPath = dbPath;
}

void EquipmentStorage::runAsync(const QString& connPrefix,
                                std::function<void(QSqlDatabase&)> work,
                                std::function<void(bool dbOpened)> done)
{
    if (m_dbPath.isEmpty()) {
        qWarning() << "EquipmentStorage: not initialized, dropping" << connPrefix;
        return;
    }
    if (!m_dbWorker)
        m_dbWorker = std::make_unique<SerialDbWorker>(QStringLiteral("EquipmentStorageWorker"));
    m_dbWorker->run(m_dbPath, connPrefix, std::move(work), std::move(done), this, m_destroyed);
}

void EquipmentStorage::requestInventory()
{
    auto packages = std::make_shared<QVariantList>();
    runAsync("equip_inv",
        [packages](QSqlDatabase& db) {
            const QVector<EquipmentPackageView> inventory = loadInventoryStatic(db);
            for (const EquipmentPackageView& entry : inventory)
                packages->append(entry.toVariantMap());
        },
        // Read: skip the emit on open failure so the UI keeps its current list
        // instead of being told the inventory is empty.
        [this, packages](bool dbOpened) { if (dbOpened) emit inventoryReady(*packages); });
}

void EquipmentStorage::requestPackage(qint64 packageId)
{
    auto result = std::make_shared<QVariantMap>();
    runAsync("equip_get",
        [packageId, result](QSqlDatabase& db) {
            const EquipmentPackage pkg = loadPackageStatic(db, packageId);
            if (pkg.isValid()) {
                EquipmentPackageView view;
                view.package = pkg;
                view.grinder = loadGrinderItemStatic(db, packageId);
                view.basket = loadBasketItemStatic(db, packageId);
                view.puckPrep = loadPuckPrepItemStatic(db, packageId);
                *result = view.toVariantMap();
            }
        },
        // Read: skip the emit on open failure. An empty result here would be read
        // by SettingsDye as "package gone" and clear the grinder identity; only a
        // genuine not-found (db opened, row absent) should do that.
        [this, packageId, result](bool dbOpened) { if (dbOpened) emit packageReady(packageId, *result); });
}

void EquipmentStorage::requestCreatePackage(const QVariantMap& packageMap)
{
    auto newId = std::make_shared<qint64>(-1);
    auto created = std::make_shared<QVariantMap>();
    runAsync("equip_create",
        [packageMap, newId, created](QSqlDatabase& db) {
            EquipmentPackage pkg = EquipmentPackage::fromVariantMap(packageMap);
            pkg.lastUsedEpoch = QDateTime::currentSecsSinceEpoch();
            const QString brand = packageMap.value(QStringLiteral("grinderBrand")).toString();
            const QString model = packageMap.value(QStringLiteral("grinderModel")).toString();
            const QString burrs = packageMap.value(QStringLiteral("grinderBurrs")).toString();
            const QString basketBrand = packageMap.value(QStringLiteral("basketBrand")).toString();
            const QString basketModel = packageMap.value(QStringLiteral("basketModel")).toString();
            const QString puckPrep = PuckPrep::canonicalMerged(QString(), packageMap);
            // Dedup backstop: if an in-inventory package already has this exact full
            // identity (grinder + basket + puck prep), return it instead of inserting
            // a duplicate. The dialog blocks this in the UI too, but the storage is
            // the authoritative guard against duplicate gear (we don't want dups).
            const qint64 existing = findPackageByGrinderIdentityStatic(db, brand, model, burrs, 0,
                                                                       basketBrand, basketModel, puckPrep);
            if (existing > 0) {
                // Same gear already in inventory — idempotent, return it.
                *newId = existing;
            } else if (findPackageByNameStatic(db, pkg.name, 0) > 0) {
                // New gear, but its name duplicates another active package — reject
                // (block-duplicate-active-names). Both callers read the "error" key
                // to name the cause: the dialog keeps itself open and shows it, and
                // ShotServer's POST /api/equipment turns it into a 409.
                *newId = -1;
                (*created)[QStringLiteral("error")] = QStringLiteral("nameInUse");
            } else {
                *newId = createPackageWithGrinderStatic(db, pkg, brand, model, burrs,
                                                        basketBrand, basketModel, puckPrep);
            }
            if (*newId > 0) {
                EquipmentPackageView view;
                view.package = loadPackageStatic(db, *newId);
                view.grinder = loadGrinderItemStatic(db, *newId);
                view.basket = loadBasketItemStatic(db, *newId);
                view.puckPrep = loadPuckPrepItemStatic(db, *newId);
                *created = view.toVariantMap();
            }
        },
        // Write: emit regardless — *newId is -1 on failure, a terminal status.
        [this, newId, created](bool) {
            emit packageCreated(*newId, *created);
            if (*newId > 0)
                emit packagesChanged();
        });
}

void EquipmentStorage::requestUpdatePackage(qint64 packageId, const QVariantMap& fields)
{
    if (m_dbPath.isEmpty()) {
        qWarning() << "EquipmentStorage: requestUpdatePackage on uninitialized storage, package" << packageId;
        emit packageUpdated(packageId, false);
        return;
    }
    auto success = std::make_shared<bool>(false);
    // Set when the failure has a caller-actionable cause (see packageUpdateFailed).
    auto failReason = std::make_shared<QString>();
    // Grinder identity edits honor copy-on-write/merge and may return a DIFFERENT
    // package id (a fork or a merge target); the result is emitted so the caller
    // can repoint the active selection. Non-identity fields (e.g. name) apply to
    // the resulting package.
    auto resultId = std::make_shared<qint64>(packageId);
    runAsync("equip_update",
        [packageId, fields, success, resultId, failReason](QSqlDatabase& db) {
            bool any = false;
            // Active-name uniqueness (block-duplicate-active-names): refuse a RENAME
            // whose new name duplicates another in-inventory package. Checked before
            // anything is applied so a rejected rename is a clean no-op.
            //
            // Gated on the name actually CHANGING, not merely being present in the
            // patch. Both save paths send `name` on every save, and a blank name is
            // derived-and-persisted at creation ("{brand} {model}", see
            // createPackageWithGrinderStatic) — so a plain "contains(name)" test
            // fired on every edit of any package whose derived name happened to
            // match another's, permanently disabling Save on BOTH and locking the
            // user out of editing gear they never named. Pre-existing duplicates are
            // data the app itself created; this rule is now-and-future only.
            if (fields.contains(QStringLiteral("name"))) {
                const QString newName = fields.value(QStringLiteral("name")).toString().trimmed();
                const QString oldName = loadPackageStatic(db, packageId).name.trimmed();
                const bool renamed = QString::compare(newName, oldName, Qt::CaseInsensitive) != 0;
                if (renamed && findPackageByNameStatic(db, newName, packageId) > 0) {
                    qWarning() << "EquipmentStorage: rejecting rename of package" << packageId
                               << "to" << newName << "- already used by another in-inventory package";
                    *success = false;
                    *resultId = packageId;
                    *failReason = QStringLiteral("nameInUse");
                    return;
                }
            }
            // Identity = grinder (brand/model/burrs) + basket (brand/model) + puck
            // prep (flag-set). An edit to ANY side runs through the copy-on-write
            // engine; untouched sides default from the current items so they're
            // preserved.
            const bool touchesGrinder = fields.contains(QStringLiteral("grinderBrand"))
                || fields.contains(QStringLiteral("grinderModel"))
                || fields.contains(QStringLiteral("grinderBurrs"));
            const bool touchesBasket = fields.contains(QStringLiteral("basketBrand"))
                || fields.contains(QStringLiteral("basketModel"));
            const bool touchesPuckPrep = PuckPrep::mapTouches(fields);
            if (touchesGrinder || touchesBasket || touchesPuckPrep) {
                const EquipmentItem cur = loadGrinderItemStatic(db, packageId);
                const EquipmentItem curBasket = loadBasketItemStatic(db, packageId);
                const EquipmentItem curPuck = loadPuckPrepItemStatic(db, packageId);
                const QString brand = fields.value(QStringLiteral("grinderBrand"), cur.brand).toString();
                const QString model = fields.value(QStringLiteral("grinderModel"), cur.model).toString();
                const QString burrs = fields.value(QStringLiteral("grinderBurrs"), cur.burrs).toString();
                const QString bBrand = fields.value(QStringLiteral("basketBrand"), curBasket.brand).toString();
                const QString bModel = fields.value(QStringLiteral("basketModel"), curBasket.model).toString();
                const QString puck = PuckPrep::canonicalMerged(curPuck.model, fields);
                *resultId = supersedeOrEditStatic(db, packageId, brand, model, burrs, bBrand, bModel, puck);
                if (*resultId <= 0) {
                    // The identity edit rolled back (lock, or a genuine SQL error).
                    // Stop here rather than applying the remaining fields: a save
                    // that persists the rename but drops the burr change, and
                    // reports success, is the worst of the three outcomes.
                    qWarning() << "EquipmentStorage: identity edit failed for package" << packageId
                               << "- reporting the whole update as failed";
                    *success = false;
                    *resultId = packageId;  // signals carry a real id, not the sentinel
                    return;
                }
                any = true;
            }
            // Strip identity keys before the package-column update; apply the rest
            // (e.g. name) to the RESULT package.
            QVariantMap pkgFields = fields;
            pkgFields.remove(QStringLiteral("grinderBrand"));
            pkgFields.remove(QStringLiteral("grinderModel"));
            pkgFields.remove(QStringLiteral("grinderBurrs"));
            pkgFields.remove(QStringLiteral("basketBrand"));
            pkgFields.remove(QStringLiteral("basketModel"));
            for (const QString& k : PuckPrep::flagKeys())
                pkgFields.remove(QStringLiteral("puckPrep_") + k);
            if (!pkgFields.isEmpty())
                any = updatePackageFieldsStatic(db, *resultId, pkgFields) || any;
            *success = any;
        },
        // Write: emit regardless — *success is false on open failure, the
        // terminal status callers wait on.
        [this, resultId, success, failReason](bool) {
            // Reason first, so a listener can record it before the terminal status.
            if (!*success && !failReason->isEmpty())
                emit packageUpdateFailed(*resultId, *failReason);
            emit packageUpdated(*resultId, *success);
            if (*success)
                emit packagesChanged();
        });
}

void EquipmentStorage::requestMarkRemoved(qint64 packageId)
{
    requestUpdatePackage(packageId, {{QStringLiteral("inInventory"), false}});
}

void EquipmentStorage::requestTouchLastUsed(qint64 packageId)
{
    runAsync("equip_touch",
        [packageId](QSqlDatabase& db) {
            QSqlQuery query(db);
            query.prepare("UPDATE equipment_packages SET last_used = :now, "
                          "updated_at = strftime('%s', 'now') WHERE id = :id");
            query.bindValue(":now", QDateTime::currentSecsSinceEpoch());
            query.bindValue(":id", packageId);
            if (!query.exec())
                qWarning() << "EquipmentStorage: touch last_used failed:" << query.lastError().text();
        },
        [](bool) {});
}

void EquipmentStorage::requestDeletePackage(qint64 packageId)
{
    auto success = std::make_shared<bool>(false);
    runAsync("equip_delete",
        [packageId, success](QSqlDatabase& db) {
            // Packages referenced by any bag or shot are history — only Remove
            // (soft-delete) is allowed. Hard delete is for mistaken creations.
            // Also block when another package's superseded_by points here: hard-
            // deleting a fork target would leave that older package with a
            // dangling lineage pointer (it would render as "older" with no
            // successor to resolve).
            QSqlQuery countQuery(db);
            countQuery.prepare("SELECT "
                               "(SELECT COUNT(*) FROM coffee_bags WHERE equipment_id = :id) + "
                               "(SELECT COUNT(*) FROM shots WHERE equipment_id = :id) + "
                               "(SELECT COUNT(*) FROM equipment_packages WHERE superseded_by = :id)");
            countQuery.bindValue(":id", packageId);
            if (!countQuery.exec() || !countQuery.next()) {
                qWarning() << "EquipmentStorage: delete pre-check failed:" << countQuery.lastError().text();
                return;
            }
            if (countQuery.value(0).toInt() > 0) {
                qWarning() << "EquipmentStorage: refusing to delete package" << packageId << "with references";
                return;
            }
            // Release the pre-check statement: an active read holds a read
            // transaction open, which would make the BEGIN below fail outright
            // (see DbWriteTxn's precondition).
            countQuery.finish();
            // Delete items + package atomically so a failure can't orphan items.
            // Both statements are writes, so a plain BEGIN would be safe here on
            // the lock-upgrade axis — but IMMEDIATE costs nothing and keeps every
            // transaction in this file on one rule.
            //
            // A failed begin aborts. This used to fall through and delete
            // unwrapped, which is precisely the orphaned-items state the
            // transaction is here to prevent.
            DbWriteTxn txn = DbWriteTxn::begin(db, "equipment package delete");
            if (!txn.ok()) {
                qWarning() << "EquipmentStorage: delete of package" << packageId
                           << "could not start a transaction - leaving it in place";
                return;
            }
            QSqlQuery delItems(db);
            delItems.prepare("DELETE FROM equipment_items WHERE package_id = :id");
            delItems.bindValue(":id", packageId);
            QSqlQuery delPkg(db);
            delPkg.prepare("DELETE FROM equipment_packages WHERE id = :id");
            delPkg.bindValue(":id", packageId);
            if (delItems.exec() && delPkg.exec() && txn.commit())
                *success = true;
            else
                qWarning() << "EquipmentStorage: delete failed:" << delPkg.lastError().text();
        },
        // Write: emit regardless — *success is false on open failure, terminal.
        [this, packageId, success](bool) {
            emit packageDeleted(packageId, *success);
            if (*success)
                emit packagesChanged();
        });
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------
bool EquipmentStorage::ensureTablesStatic(QSqlDatabase& db)
{
    QSqlQuery query(db);
    const bool okPkg = query.exec(R"(
        CREATE TABLE IF NOT EXISTS equipment_packages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT,
            in_inventory INTEGER NOT NULL DEFAULT 1,
            last_grind_setting TEXT,
            last_rpm INTEGER,
            last_used INTEGER,
            superseded_by INTEGER,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )");
    if (!okPkg) {
        qWarning() << "EquipmentStorage: failed to create equipment_packages table:" << query.lastError().text();
        return false;
    }
    const bool okItem = query.exec(R"(
        CREATE TABLE IF NOT EXISTS equipment_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            package_id INTEGER NOT NULL,
            kind TEXT NOT NULL,
            brand TEXT,
            model TEXT,
            attrs TEXT,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )");
    if (!okItem) {
        qWarning() << "EquipmentStorage: failed to create equipment_items table:" << query.lastError().text();
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_equipment_inventory ON equipment_packages(in_inventory, last_used DESC)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_equipment_items_package ON equipment_items(package_id, kind)");
    return true;
}

qint64 EquipmentStorage::insertPackageStatic(QSqlDatabase& db, const EquipmentPackage& pkg)
{
    QStringList columns, placeholders;
    QVariantList binds;
    for (const PkgCol& c : kCols) {
        if (!c.writable)
            continue;
        columns << QString::fromLatin1(c.sql);
        placeholders << QStringLiteral("?");
        binds << c.bind(pkg);
    }
    QSqlQuery query(db);
    query.prepare(QString("INSERT INTO equipment_packages (%1) VALUES (%2)")
                      .arg(columns.join(QStringLiteral(", ")), placeholders.join(QStringLiteral(", "))));
    for (qsizetype i = 0; i < binds.size(); ++i)
        query.bindValue(static_cast<int>(i), binds.at(i));
    if (!query.exec()) {
        qWarning() << "EquipmentStorage: package insert failed:" << query.lastError().text();
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

qint64 EquipmentStorage::insertItemStatic(QSqlDatabase& db, const EquipmentItem& item)
{
    QSqlQuery query(db);
    query.prepare("INSERT INTO equipment_items (package_id, kind, brand, model, attrs) "
                  "VALUES (:package_id, :kind, :brand, :model, :attrs)");
    query.bindValue(":package_id", item.packageId);
    query.bindValue(":kind", item.kind);
    query.bindValue(":brand", nullIfEmpty(item.brand));
    query.bindValue(":model", nullIfEmpty(item.model));
    query.bindValue(":attrs", item.attrsJson());
    if (!query.exec()) {
        qWarning() << "EquipmentStorage: item insert failed:" << query.lastError().text();
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

qint64 EquipmentStorage::createPackageWithGrinderStatic(QSqlDatabase& db, EquipmentPackage pkg,
                                                        const QString& brand, const QString& model,
                                                        const QString& burrs,
                                                        const QString& basketBrand,
                                                        const QString& basketModel,
                                                        const QString& puckPrep)
{
    // A fully-empty grinder identity makes this a grinder-less package (e.g. a
    // basket-only tea setup, add-recipe-wizard-tea): no grinder item row is
    // inserted, and the display name falls back to the basket identity.
    const bool grinderLess = brand.trimmed().isEmpty() && model.trimmed().isEmpty()
        && burrs.trimmed().isEmpty();
    // Persist a name at creation so it survives identity edits / copy-on-write
    // (the id, never the name, is the permanent handle).
    //
    // Note this DERIVED name is deliberately not unique, and this function
    // deliberately carries no uniqueness guard: same-grinder packages that differ
    // only by basket or puck prep are distinct gear and both derive
    // "{brand} {model}". Active-name uniqueness (block-duplicate-active-names) is
    // enforced one level up, in requestCreatePackage / requestUpdatePackage, and
    // applies to a name the USER entered or changed. Direct callers here —
    // migration and device import — reproduce gear the user already owns rather
    // than accept new input, so they are correctly exempt.
    if (pkg.name.trimmed().isEmpty())
        pkg.name = grinderLess
            ? (basketBrand.trimmed() + QLatin1Char(' ') + basketModel.trimmed()).trimmed()
            : (brand.trimmed() + QLatin1Char(' ') + model.trimmed()).trimmed();
    const qint64 packageId = insertPackageStatic(db, pkg);
    if (packageId <= 0)
        return -1;
    auto dropPackage = [&]() {
        QSqlQuery dp(db);
        dp.prepare("DELETE FROM equipment_packages WHERE id = :id");
        dp.bindValue(":id", packageId);
        // On the direct (non-transactional) create path a failed cleanup here
        // would orphan the package row; log it so an incomplete rollback is
        // visible rather than silent. (In the fork path the outer transaction
        // rolls it back regardless.)
        if (!dp.exec())
            qWarning() << "EquipmentStorage: rollback drop of package" << packageId
                       << "failed (possible orphan):" << dp.lastError().text();
    };
    if (!grinderLess) {
        EquipmentItem grinder;
        grinder.packageId = packageId;
        grinder.kind = QStringLiteral("grinder");
        grinder.brand = brand;
        grinder.model = model;
        grinder.burrs = burrs;
        grinder.rpmCapable = deriveRpmCapable(brand, model);
        if (insertItemStatic(db, grinder) <= 0) {
            // A package that ASKED for a grinder but lost the item resolves to a
            // blank grinder everywhere; don't leave that orphan behind — drop the
            // just-inserted package row.
            dropPackage();
            return -1;
        }
    }
    // Optional basket item. A failure here is fatal too: a half-built package
    // (grinder but a dropped basket the caller asked for) would silently lose the
    // basket identity, so roll the whole package back.
    // Cleanup used by the optional-item failures below: drop the items then the
    // package so a partial build never persists.
    auto dropAll = [&]() {
        QSqlQuery di(db);
        di.prepare("DELETE FROM equipment_items WHERE package_id = :id");
        di.bindValue(":id", packageId);
        if (!di.exec())
            qWarning() << "EquipmentStorage: rollback delete of items for package" << packageId
                       << "failed (possible orphan):" << di.lastError().text();
        dropPackage();
    };
    if (!basketBrand.trimmed().isEmpty() || !basketModel.trimmed().isEmpty()) {
        EquipmentItem basket;
        basket.packageId = packageId;
        basket.kind = QStringLiteral("basket");
        basket.brand = basketBrand.trimmed();
        basket.model = basketModel.trimmed();
        if (insertItemStatic(db, basket) <= 0) {
            dropAll();
            return -1;
        }
    }
    // Optional puck-prep item: the canonical flag string lives in the `model`
    // column (brand empty); empty string = no puck prep. Re-canonicalized so the
    // stored value is always canonical (the dedup compares it with a plain '=').
    // Fatal on failure, like the basket, so a requested config is never silently dropped.
    const QString puckCanon = PuckPrep::recanonical(puckPrep);
    if (!puckCanon.isEmpty()) {
        EquipmentItem puck;
        puck.packageId = packageId;
        puck.kind = QStringLiteral("puckprep");
        puck.model = puckCanon;
        if (insertItemStatic(db, puck) <= 0) {
            dropAll();
            return -1;
        }
    }
    return packageId;
}

EquipmentPackage EquipmentStorage::loadPackageStatic(QSqlDatabase& db, qint64 packageId)
{
    QSqlQuery query(db);
    query.prepare(QString("SELECT %1 FROM equipment_packages WHERE id = :id").arg(packageColumnList()));
    query.bindValue(":id", packageId);
    if (!query.exec() || !query.next())
        return EquipmentPackage();
    return packageFromQueryRow(query);
}

EquipmentItem EquipmentStorage::loadGrinderItemStatic(QSqlDatabase& db, qint64 packageId)
{
    QSqlQuery query(db);
    query.prepare(QString("SELECT %1 FROM equipment_items "
                          "WHERE package_id = :id AND kind = 'grinder' ORDER BY id LIMIT 1")
                      .arg(QLatin1String(kItemColumns)));
    query.bindValue(":id", packageId);
    if (!query.exec() || !query.next())
        return EquipmentItem();
    return grinderItemFromQueryRow(query);
}

EquipmentItem EquipmentStorage::loadBasketItemStatic(QSqlDatabase& db, qint64 packageId)
{
    QSqlQuery query(db);
    query.prepare(QString("SELECT %1 FROM equipment_items "
                          "WHERE package_id = :id AND kind = 'basket' ORDER BY id LIMIT 1")
                      .arg(QLatin1String(kItemColumns)));
    query.bindValue(":id", packageId);
    if (!query.exec() || !query.next())
        return EquipmentItem();  // invalid (id == 0): no basket
    return grinderItemFromQueryRow(query);  // generic item read (kind/brand/model)
}

bool EquipmentStorage::setBasketItemStatic(QSqlDatabase& db, qint64 packageId,
                                           const QString& brand, const QString& model)
{
    const QString b = brand.trimmed();
    const QString m = model.trimmed();
    const EquipmentItem cur = loadBasketItemStatic(db, packageId);
    const bool wantNone = b.isEmpty() && m.isEmpty();

    // Return contract: true on success OR when no change is needed (the basket is
    // already in the desired state); false ONLY on a genuine SQL failure. This
    // distinction matters — a caller inside a transaction (supersedeOrEditStatic's
    // edit-in-place branch) must roll back on a real write error but must NOT
    // treat a benign no-op as failure. A successful statement that affects 0 rows
    // is success, not failure, so we don't gate on numRowsAffected().
    if (wantNone) {
        if (!cur.isValid())
            return true;  // already has no basket — desired state reached, nothing to do
        QSqlQuery del(db);
        del.prepare("DELETE FROM equipment_items WHERE package_id = :id AND kind = 'basket'");
        del.bindValue(":id", packageId);
        if (!del.exec()) {
            qWarning() << "EquipmentStorage: basket clear failed for package" << packageId << ":"
                       << del.lastError().text();
            return false;
        }
        return true;
    }

    if (cur.isValid()) {
        QSqlQuery upd(db);
        upd.prepare("UPDATE equipment_items SET brand = :brand, model = :model, attrs = '{}', "
                    "updated_at = strftime('%s', 'now') WHERE package_id = :id AND kind = 'basket'");
        upd.bindValue(":brand", nullIfEmpty(b));
        upd.bindValue(":model", nullIfEmpty(m));
        upd.bindValue(":id", packageId);
        if (!upd.exec()) {
            qWarning() << "EquipmentStorage: basket update failed for package" << packageId << ":"
                       << upd.lastError().text();
            return false;
        }
        return true;
    }

    EquipmentItem basket;
    basket.packageId = packageId;
    basket.kind = QStringLiteral("basket");
    basket.brand = b;
    basket.model = m;
    return insertItemStatic(db, basket) > 0;
}

EquipmentItem EquipmentStorage::loadPuckPrepItemStatic(QSqlDatabase& db, qint64 packageId)
{
    QSqlQuery query(db);
    query.prepare(QString("SELECT %1 FROM equipment_items "
                          "WHERE package_id = :id AND kind = 'puckprep' ORDER BY id LIMIT 1")
                      .arg(QLatin1String(kItemColumns)));
    query.bindValue(":id", packageId);
    if (!query.exec() || !query.next())
        return EquipmentItem();  // invalid (id == 0): no puck prep
    return grinderItemFromQueryRow(query);  // generic read; canonical flags in `model`
}

bool EquipmentStorage::setPuckPrepItemStatic(QSqlDatabase& db, qint64 packageId,
                                             const QString& puckPrep)
{
    // Re-canonicalize so the stored `model` is always canonical regardless of the
    // caller (the identity dedup compares it with a plain '='), and unknown tokens
    // are dropped.
    const QString canon = PuckPrep::recanonical(puckPrep);
    const EquipmentItem cur = loadPuckPrepItemStatic(db, packageId);

    // Same return contract as setBasketItemStatic: true on success/no-op, false
    // ONLY on a genuine SQL failure (so the edit-in-place caller can roll back).
    if (canon.isEmpty()) {
        if (!cur.isValid())
            return true;  // already no puck prep — desired state, nothing to do
        QSqlQuery del(db);
        del.prepare("DELETE FROM equipment_items WHERE package_id = :id AND kind = 'puckprep'");
        del.bindValue(":id", packageId);
        if (!del.exec()) {
            qWarning() << "EquipmentStorage: puckprep clear failed for package" << packageId << ":"
                       << del.lastError().text();
            return false;
        }
        return true;
    }

    if (cur.isValid()) {
        QSqlQuery upd(db);
        upd.prepare("UPDATE equipment_items SET brand = NULL, model = :model, attrs = '{}', "
                    "updated_at = strftime('%s', 'now') WHERE package_id = :id AND kind = 'puckprep'");
        upd.bindValue(":model", canon);
        upd.bindValue(":id", packageId);
        if (!upd.exec()) {
            qWarning() << "EquipmentStorage: puckprep update failed for package" << packageId << ":"
                       << upd.lastError().text();
            return false;
        }
        return true;
    }

    EquipmentItem puck;
    puck.packageId = packageId;
    puck.kind = QStringLiteral("puckprep");
    puck.model = canon;
    return insertItemStatic(db, puck) > 0;
}

QVector<EquipmentPackageView> EquipmentStorage::loadInventoryStatic(QSqlDatabase& db)
{
    QVector<EquipmentPackageView> views;
    QSqlQuery query(db);
    if (!query.exec(QString("SELECT %1, "
                            "(SELECT COUNT(*) FROM shots WHERE equipment_id = equipment_packages.id) AS shot_count "
                            "FROM equipment_packages WHERE in_inventory = 1 "
                            "ORDER BY last_used DESC, id DESC").arg(packageColumnList()))) {
        qWarning() << "EquipmentStorage: inventory query failed:" << query.lastError().text();
        return views;
    }
    const int shotCountCol = query.record().indexOf("shot_count");
    QVector<qint64> ids;
    while (query.next()) {
        EquipmentPackageView view;
        view.package = packageFromQueryRow(query);
        if (shotCountCol >= 0)
            view.shotCount = query.value(shotCountCol).toLongLong();
        views.append(view);
        ids.append(view.package.id);
    }
    // Resolve each package's grinder + (optional) basket item (inventory is small
    // — a handful of rows).
    for (qsizetype i = 0; i < views.size(); ++i) {
        views[i].grinder = loadGrinderItemStatic(db, ids.at(i));
        views[i].basket = loadBasketItemStatic(db, ids.at(i));
        views[i].puckPrep = loadPuckPrepItemStatic(db, ids.at(i));
    }
    return views;
}

bool EquipmentStorage::updatePackageFieldsStatic(QSqlDatabase& db, qint64 packageId, const QVariantMap& fields)
{
    const QHash<QString, const PkgCol*>& kColumnFor = packageColumnForKey();
    QStringList assignments;
    QVariantList values;
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        const PkgCol* col = kColumnFor.value(it.key(), nullptr);
        if (!col) {
            qWarning() << "EquipmentStorage: ignoring unknown package field" << it.key();
            continue;
        }
        assignments << QString("%1 = ?").arg(QString::fromLatin1(col->sql));
        EquipmentPackage scratch;
        col->set(scratch, it.value());
        values << col->bind(scratch);
    }
    if (assignments.isEmpty())
        return false;
    QSqlQuery query(db);
    query.prepare(QString("UPDATE equipment_packages SET %1, updated_at = strftime('%s', 'now') WHERE id = ?")
                      .arg(assignments.join(QStringLiteral(", "))));
    int pos = 0;
    for (const QVariant& value : values)
        query.bindValue(pos++, value);
    query.bindValue(pos, packageId);
    if (!query.exec()) {
        qWarning() << "EquipmentStorage: package update failed for" << packageId << ":" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool EquipmentStorage::updateGrinderItemStatic(QSqlDatabase& db, qint64 packageId,
                                               const QString& brand, const QString& model,
                                               const QString& burrs)
{
    // Same optional set-semantics + return contract as setBasketItemStatic
    // (grinder-less packages exist since add-recipe-wizard-tea): empty
    // identity deletes any existing grinder item, an existing item updates,
    // a missing one inserts; false ONLY on a genuine SQL failure.
    const QString b = brand.trimmed();
    const QString m = model.trimmed();
    const QString bu = burrs.trimmed();
    const EquipmentItem cur = loadGrinderItemStatic(db, packageId);
    const bool wantNone = b.isEmpty() && m.isEmpty() && bu.isEmpty();

    if (wantNone) {
        if (!cur.isValid())
            return true;  // already grinder-less — desired state reached
        QSqlQuery del(db);
        del.prepare("DELETE FROM equipment_items WHERE package_id = :id AND kind = 'grinder'");
        del.bindValue(":id", packageId);
        if (!del.exec()) {
            qWarning() << "EquipmentStorage: grinder clear failed for package" << packageId << ":"
                       << del.lastError().text();
            return false;
        }
        return true;
    }

    EquipmentItem item;
    item.brand = b;
    item.model = m;
    item.burrs = bu;
    item.rpmCapable = deriveRpmCapable(b, m);

    if (cur.isValid()) {
        QSqlQuery query(db);
        query.prepare("UPDATE equipment_items SET brand = :brand, model = :model, attrs = :attrs, "
                      "updated_at = strftime('%s', 'now') WHERE package_id = :id AND kind = 'grinder'");
        query.bindValue(":brand", nullIfEmpty(b));
        query.bindValue(":model", nullIfEmpty(m));
        query.bindValue(":attrs", item.attrsJson());
        query.bindValue(":id", packageId);
        if (!query.exec()) {
            qWarning() << "EquipmentStorage: grinder item update failed for package" << packageId << ":"
                       << query.lastError().text();
            return false;
        }
        return true;
    }

    item.packageId = packageId;
    item.kind = QStringLiteral("grinder");
    if (insertItemStatic(db, item) <= 0) {
        qWarning() << "EquipmentStorage: grinder item insert failed for package" << packageId;
        return false;
    }
    return true;
}

qint64 EquipmentStorage::findPackageByGrinderIdentityStatic(QSqlDatabase& db, const QString& brand,
                                                            const QString& model, const QString& burrs,
                                                            qint64 excludeId,
                                                            const QString& basketBrand,
                                                            const QString& basketModel,
                                                            const QString& puckPrep)
{
    // Full-identity match: grinder brand/model/burrs AND the package's basket
    // brand/model AND its puckprep canonical flag string. EVERY component —
    // including the grinder, which is optional since grinder-less basket-only
    // packages (add-recipe-wizard-tea) — is matched via correlated subqueries
    // anchored on the package row, so a package LACKING a component resolves
    // to '' and matches an empty param ("no grinder" / "no basket" / "no puck
    // prep" are distinct, matchable identity values). IFNULL on the bind side
    // too: a caller omitting a component passes a null QString → SQL NULL, and
    // without IFNULL the '' = NULL comparison is NULL (never true) and
    // component-less packages stop matching.
    QSqlQuery query(db);
    query.prepare("SELECT p.id FROM equipment_packages p "
                  "WHERE p.in_inventory = 1 "
                  "AND p.id != :exclude "
                  "AND LOWER(IFNULL((SELECT g.brand FROM equipment_items g "
                  "  WHERE g.package_id = p.id AND g.kind = 'grinder' ORDER BY g.id LIMIT 1),'')) = LOWER(IFNULL(:brand,'')) "
                  "AND LOWER(IFNULL((SELECT g.model FROM equipment_items g "
                  "  WHERE g.package_id = p.id AND g.kind = 'grinder' ORDER BY g.id LIMIT 1),'')) = LOWER(IFNULL(:model,'')) "
                  "AND LOWER(IFNULL((SELECT json_extract(g.attrs,'$.burrs') FROM equipment_items g "
                  "  WHERE g.package_id = p.id AND g.kind = 'grinder' ORDER BY g.id LIMIT 1),'')) = LOWER(IFNULL(:burrs,'')) "
                  "AND LOWER(IFNULL((SELECT b.brand FROM equipment_items b "
                  "  WHERE b.package_id = p.id AND b.kind = 'basket' ORDER BY b.id LIMIT 1),'')) = LOWER(IFNULL(:bbrand,'')) "
                  "AND LOWER(IFNULL((SELECT b.model FROM equipment_items b "
                  "  WHERE b.package_id = p.id AND b.kind = 'basket' ORDER BY b.id LIMIT 1),'')) = LOWER(IFNULL(:bmodel,'')) "
                  // Puck-prep identity is the canonical flag string in the puckprep
                  // item's `model` column. Stored values are always canonical (the
                  // write path re-canonicalizes), and the bind below is too, so a
                  // plain '=' is a correct full-identity match.
                  "AND IFNULL((SELECT pp.model FROM equipment_items pp "
                  "  WHERE pp.package_id = p.id AND pp.kind = 'puckprep' ORDER BY pp.id LIMIT 1),'') = IFNULL(:puck,'') "
                  "ORDER BY p.id LIMIT 1");
    query.bindValue(":exclude", excludeId);
    query.bindValue(":brand", brand.trimmed());
    query.bindValue(":model", model.trimmed());
    query.bindValue(":burrs", burrs.trimmed());
    query.bindValue(":bbrand", basketBrand.trimmed());
    query.bindValue(":bmodel", basketModel.trimmed());
    // Re-canonicalize the query arg so an unsorted/non-canonical caller still matches
    // the canonical stored value (the compare is a plain '=', not order-aware).
    query.bindValue(":puck", PuckPrep::recanonical(puckPrep));
    if (!query.exec() || !query.next())
        return 0;
    return query.value(0).toLongLong();
}

qint64 EquipmentStorage::findPackageByNameStatic(QSqlDatabase& db, const QString& name, qint64 excludeId)
{
    const QString target = name.trimmed();
    if (target.isEmpty())
        return 0;  // A blank name is derived from brand+model and never collides.
    // The comparison is done in C++, NOT in SQL: SQLite's LOWER() folds ASCII
    // only, so "Café"/"CAFÉ" would compare unequal here while the QML gate's
    // JS toLowerCase() (full Unicode) called them equal — the two layers would
    // disagree for every non-ASCII name. Qt::CaseInsensitive folds the whole
    // range, so client and storage now agree. An inventory is tens of rows, so
    // scanning it is cheaper than the bug.
    QSqlQuery query(db);
    query.prepare("SELECT id, IFNULL(name,'') FROM equipment_packages "
                  "WHERE in_inventory = 1 AND id != :exclude ORDER BY id");
    query.bindValue(":exclude", excludeId);
    if (!query.exec())
        return 0;
    while (query.next()) {
        if (QString::compare(query.value(1).toString().trimmed(), target, Qt::CaseInsensitive) == 0)
            return query.value(0).toLongLong();
    }
    return 0;
}

qint64 EquipmentStorage::supersedeOrEditGrinderStatic(QSqlDatabase& db, qint64 packageId,
                                                      const QString& brand, const QString& model,
                                                      const QString& burrs)
{
    // Grinder-only edit: preserve the package's current basket + puck prep.
    const EquipmentItem curBasket = loadBasketItemStatic(db, packageId);
    const EquipmentItem curPuck = loadPuckPrepItemStatic(db, packageId);
    return supersedeOrEditStatic(db, packageId, brand, model, burrs,
                                 curBasket.brand, curBasket.model, curPuck.model);
}

qint64 EquipmentStorage::supersedeOrEditStatic(QSqlDatabase& db, qint64 packageId,
                                               const QString& brand, const QString& model,
                                               const QString& burrs,
                                               const QString& basketBrand, const QString& basketModel,
                                               const QString& puckPrep)
{
    const EquipmentItem cur = loadGrinderItemStatic(db, packageId);
    const EquipmentItem curBasket = loadBasketItemStatic(db, packageId);
    const EquipmentItem curPuck = loadPuckPrepItemStatic(db, packageId);

    // Canonicalize the target puck prep once so the unchanged-check and the merge
    // lookup compare like-for-like against the (always-canonical) stored value — an
    // unsorted/non-canonical arg would otherwise read as a change and spuriously
    // fork. The setter/create below re-canonicalize again (idempotent), harmlessly.
    const QString puck = PuckPrep::recanonical(puckPrep);

    auto norm = [](const QString& s) { return s.trimmed().toLower(); };
    const bool unchanged = norm(cur.brand) == norm(brand)
        && norm(cur.model) == norm(model)
        && norm(cur.burrs) == norm(burrs)
        && norm(curBasket.brand) == norm(basketBrand)
        && norm(curBasket.model) == norm(basketModel)
        && curPuck.model == puck;  // both canonical
    if (unchanged)
        return packageId;  // no identity change

    auto shotCount = [&]() -> qint64 {
        QSqlQuery q(db);
        q.prepare("SELECT COUNT(*) FROM shots WHERE equipment_id = :id");
        q.bindValue(":id", packageId);
        return (q.exec() && q.next()) ? q.value(0).toLongLong() : 0;
    };
    auto repointBags = [&](qint64 from, qint64 to) -> bool {
        QSqlQuery q(db);
        q.prepare("UPDATE coffee_bags SET equipment_id = :to WHERE equipment_id = :from");
        q.bindValue(":to", to);
        q.bindValue(":from", from);
        if (!q.exec()) {
            qWarning() << "EquipmentStorage: repoint bags failed:" << q.lastError().text();
            return false;
        }
        return true;
    };
    auto softDelete = [&](qint64 id, qint64 supersededBy) -> bool {
        QSqlQuery q(db);
        q.prepare("UPDATE equipment_packages SET in_inventory = 0, superseded_by = :by, "
                  "updated_at = strftime('%s','now') WHERE id = :id");
        q.bindValue(":by", supersededBy > 0 ? QVariant(supersededBy) : QVariant());
        q.bindValue(":id", id);
        if (!q.exec()) {
            qWarning() << "EquipmentStorage: soft-delete failed:" << q.lastError().text();
            return false;
        }
        return true;
    };

    // The whole identity edit must be atomic — a partial commit could repoint
    // bags without retiring the old package (a duplicate live package). Wrap in a
    // transaction and roll back on any failure.
    //
    // Failure returns -1, NOT packageId. Both are "the package still has its old
    // identity", but packageId is also what a successful edit-in-place returns,
    // and callers only have `> 0` to test — so returning it reported every failure
    // as a save, closed the editor, and dropped the user's change silently.
    //
    // Reads (findPackageByGrinderIdentityStatic) before it writes, so it takes
    // the write lock up front — see DbWriteTxn.
    DbWriteTxn txn = DbWriteTxn::begin(db, "equipment identity edit");
    if (!txn.ok()) {
        qWarning() << "EquipmentStorage: identity edit for package" << packageId
                   << "could not start a transaction - leaving the package unchanged";
        return -1;
    }
    // Every `return -1` below rolls back through the guard's destructor; only a
    // successful commit lets a result out.
    auto done = [&](qint64 result) -> qint64 {
        if (!txn.commit()) {
            qWarning() << "EquipmentStorage: identity edit commit failed for package" << packageId
                       << "-" << db.lastError().text();
            return -1;
        }
        return result;
    };

    // Merge: another current package already has this exact full identity.
    const qint64 mergeTarget = findPackageByGrinderIdentityStatic(db, brand, model, burrs, packageId,
                                                                  basketBrand, basketModel, puck);
    if (mergeTarget > 0) {
        if (!repointBags(packageId, mergeTarget))
            return -1;
        if (shotCount() == 0) {
            // Inherit the lineage before the row goes: an older package may have
            // been superseded BY this one, and hard-deleting it would leave that
            // package pointing at an id that no longer exists — it would render
            // as "older" with no successor to resolve. requestDeletePackage
            // refuses the delete outright for this reason; here the merge target
            // is the natural successor, so re-point rather than refuse.
            //
            // Found by merging a package that was itself a fork target, on a live
            // database: the check `superseded_by NOT IN (SELECT id FROM
            // equipment_packages)` came back non-empty afterwards.
            QSqlQuery lineage(db);
            lineage.prepare("UPDATE equipment_packages SET superseded_by = :target, "
                            "updated_at = strftime('%s','now') WHERE superseded_by = :source");
            lineage.bindValue(":target", mergeTarget);
            lineage.bindValue(":source", packageId);
            if (!lineage.exec()) {
                qWarning() << "EquipmentStorage: lineage re-point failed for package" << packageId
                           << "-" << lineage.lastError().text();
                return -1;
            }
            QSqlQuery di(db);
            di.prepare("DELETE FROM equipment_items WHERE package_id = :id");
            di.bindValue(":id", packageId);
            if (!di.exec()) return -1;
            QSqlQuery dp(db);
            dp.prepare("DELETE FROM equipment_packages WHERE id = :id");
            dp.bindValue(":id", packageId);
            if (!dp.exec()) return -1;
        } else if (!softDelete(packageId, mergeTarget)) {
            return -1;
        }
        return done(mergeTarget);
    }

    // Unused package: safe to edit in place (grinder + basket + puckprep items).
    if (shotCount() == 0) {
        if (!updateGrinderItemStatic(db, packageId, brand, model, burrs))
            return -1;
        // A false return is a genuine SQL failure (a no-op returns true), so roll
        // back rather than commit a half-applied identity — these edits run inside
        // the transaction opened above.
        if (!setBasketItemStatic(db, packageId, basketBrand, basketModel))
            return -1;
        if (!setPuckPrepItemStatic(db, packageId, puck))
            return -1;
        return done(packageId);
    }

    // Used package: fork (copy name + last dial), repoint bags, retire the old.
    const EquipmentPackage old = loadPackageStatic(db, packageId);
    EquipmentPackage np;
    np.name = old.name;
    np.lastGrindSetting = old.lastGrindSetting;
    np.lastRpm = old.lastRpm;
    np.lastUsedEpoch = QDateTime::currentSecsSinceEpoch();
    const qint64 newId = createPackageWithGrinderStatic(db, np, brand, model, burrs,
                                                        basketBrand, basketModel, puck);
    if (newId <= 0)
        return -1;  // fork failed; leave as-is
    if (!repointBags(packageId, newId))
        return -1;
    if (!softDelete(packageId, newId))
        return -1;
    return done(newId);
}

bool EquipmentStorage::deriveRpmCapable(const QString& brand, const QString& model)
{
    // Registry match → its variableRpm flag; custom grinder (no match) → true,
    // so the rpm field shows. (Honors "grinder not in the table → show rpm".)
    const GrinderAliases::GrinderEntry* entry = GrinderAliases::findEntry(brand, model);
    return entry ? entry->variableRpm : true;
}

void EquipmentStorage::splitGrindAndRpm(const QString& combined, QString& outGrind, qint64& outRpm)
{
    outGrind = combined.trimmed();
    outRpm = 0;
    // Only split on an explicit trailing rpm marker, so compound ("1+4") and
    // other annotated ("24 clicks") settings are left verbatim.
    static const QRegularExpression rx(QStringLiteral(R"(\s*(\d+)\s*rpm\s*$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = rx.match(outGrind);
    if (m.hasMatch()) {
        outRpm = m.captured(1).toLongLong();
        outGrind = outGrind.left(m.capturedStart(0)).trimmed();
    }
}

bool EquipmentStorage::migrateFromGrinderColumnsStatic(QSqlDatabase& db,
                                                       const QString& currentBrand,
                                                       const QString& currentModel,
                                                       const QString& currentBurrs,
                                                       const QString& currentSetting)
{
    auto trimmed = [](const QString& s) { return s.trimmed(); };
    auto isEmptyIdentity = [&](const QString& b, const QString& m, const QString& bu) {
        return trimmed(b).isEmpty() && trimmed(m).isEmpty() && trimmed(bu).isEmpty();
    };
    auto identityKey = [&](const QString& b, const QString& m, const QString& bu) {
        return trimmed(b).toLower() + QChar(0x1f) + trimmed(m).toLower()
             + QChar(0x1f) + trimmed(bu).toLower();
    };

    QHash<QString, qint64> idToPackage;                                  // identityKey -> package id
    QVector<std::tuple<QString, QString, QString, qint64>> allPackages;  // brand, model, burrs, id

    auto registerPackage = [&](const QString& b, const QString& m, const QString& bu, qint64 id) {
        idToPackage.insert(identityKey(b, m, bu), id);
        allPackages.append(std::make_tuple(trimmed(b), trimmed(m), trimmed(bu), id));
    };

    // 1. Default package from the user's current grinder settings.
    if (!isEmptyIdentity(currentBrand, currentModel, currentBurrs)) {
        QString grind; qint64 rpm = 0;
        splitGrindAndRpm(currentSetting, grind, rpm);
        EquipmentPackage pkg;
        pkg.lastGrindSetting = grind;
        pkg.lastRpm = rpm;
        pkg.lastUsedEpoch = QDateTime::currentSecsSinceEpoch();
        const qint64 id = createPackageWithGrinderStatic(db, pkg, trimmed(currentBrand),
                                                         trimmed(currentModel), trimmed(currentBurrs));
        if (id > 0)
            registerPackage(currentBrand, currentModel, currentBurrs, id);
    }

    // 2. One package per remaining distinct historical identity (bags ∪ shots).
    QSqlQuery distinctQ(db);
    if (!distinctQ.exec(
            "SELECT DISTINCT grinder_brand, grinder_model, grinder_burrs FROM ("
            "  SELECT grinder_brand, grinder_model, grinder_burrs FROM coffee_bags "
            "  UNION ALL "
            "  SELECT grinder_brand, grinder_model, grinder_burrs FROM shots)")) {
        qWarning() << "EquipmentStorage: migration distinct-identity query failed:"
                   << distinctQ.lastError().text();
        return false;
    }
    struct Identity { QString brand, model, burrs; };
    QVector<Identity> newIdentities;
    while (distinctQ.next()) {
        const Identity id{ distinctQ.value(0).toString(), distinctQ.value(1).toString(),
                           distinctQ.value(2).toString() };
        if (isEmptyIdentity(id.brand, id.model, id.burrs))
            continue;
        if (idToPackage.contains(identityKey(id.brand, id.model, id.burrs)))
            continue;
        newIdentities.append(id);
    }
    for (const Identity& id : newIdentities) {
        // Seed the package's last dial from that grinder's most-recent shot
        // (fall back to a bag), so a switch pre-fills a real value.
        QString grind; qint64 rpm = 0;
        QSqlQuery seedQ(db);
        seedQ.prepare("SELECT grinder_setting FROM shots "
                      "WHERE LOWER(IFNULL(grinder_brand,''))=:b AND LOWER(IFNULL(grinder_model,''))=:m "
                      "AND LOWER(IFNULL(grinder_burrs,''))=:bu AND grinder_setting IS NOT NULL "
                      "ORDER BY id DESC LIMIT 1");
        seedQ.bindValue(":b", id.brand.trimmed().toLower());
        seedQ.bindValue(":m", id.model.trimmed().toLower());
        seedQ.bindValue(":bu", id.burrs.trimmed().toLower());
        if (seedQ.exec() && seedQ.next())
            splitGrindAndRpm(seedQ.value(0).toString(), grind, rpm);

        EquipmentPackage pkg;
        pkg.lastGrindSetting = grind;
        pkg.lastRpm = rpm;
        pkg.lastUsedEpoch = QDateTime::currentSecsSinceEpoch();
        const qint64 pid = createPackageWithGrinderStatic(db, pkg, id.brand.trimmed(),
                                                          id.model.trimmed(), id.burrs.trimmed());
        if (pid > 0)
            registerPackage(id.brand, id.model, id.burrs, pid);
    }

    // 3. Link every bag/shot to its package by identity (bulk per identity).
    qint64 linkedRows = 0;
    for (const auto& [b, m, bu, pid] : allPackages) {
        for (const char* table : { "coffee_bags", "shots" }) {
            QSqlQuery linkQ(db);
            linkQ.prepare(QString("UPDATE %1 SET equipment_id = :pid "
                                  "WHERE LOWER(IFNULL(grinder_brand,'')) = :b "
                                  "AND LOWER(IFNULL(grinder_model,'')) = :m "
                                  "AND LOWER(IFNULL(grinder_burrs,'')) = :bu").arg(QLatin1String(table)));
            linkQ.bindValue(":pid", pid);
            linkQ.bindValue(":b", b.toLower());
            linkQ.bindValue(":m", m.toLower());
            linkQ.bindValue(":bu", bu.toLower());
            if (!linkQ.exec())
                qWarning() << "EquipmentStorage: migration link failed on" << table << ":"
                           << linkQ.lastError().text();
            else
                linkedRows += linkQ.numRowsAffected();
        }
    }

    // 4. Split combined grind+rpm settings into grinder_setting + rpm. Only rows
    //    carrying an explicit rpm marker need touching.
    qint64 splitRows = 0;
    for (const char* table : { "coffee_bags", "shots" }) {
        QSqlQuery scanQ(db);
        if (!scanQ.exec(QString("SELECT id, grinder_setting FROM %1 "
                                "WHERE grinder_setting LIKE '%rpm%'").arg(QLatin1String(table)))) {
            qWarning() << "EquipmentStorage: migration rpm-scan failed on" << table << ":"
                       << scanQ.lastError().text();
            continue;
        }
        QVector<std::tuple<qint64, QString, qint64>> updates;  // id, grind, rpm
        while (scanQ.next()) {
            const qint64 rowId = scanQ.value(0).toLongLong();
            QString grind; qint64 rpm = 0;
            splitGrindAndRpm(scanQ.value(1).toString(), grind, rpm);
            if (rpm > 0)
                updates.append(std::make_tuple(rowId, grind, rpm));
        }
        for (const auto& [rowId, grind, rpm] : updates) {
            QSqlQuery upd(db);
            upd.prepare(QString("UPDATE %1 SET grinder_setting = :g, rpm = :r WHERE id = :id")
                            .arg(QLatin1String(table)));
            upd.bindValue(":g", nullIfEmpty(grind));
            upd.bindValue(":r", rpm);
            upd.bindValue(":id", rowId);
            if (!upd.exec())
                qWarning() << "EquipmentStorage: migration rpm-split update failed on" << table << ":"
                           << upd.lastError().text();
            else
                ++splitRows;
        }
    }

    // Success summary for the one-shot upgrade — the log is the only window into
    // what migration 22 actually did on a user's device (created N packages,
    // linked B+S bag/shot rows, split R combined settings). qInfo so it survives
    // release log levels.
    qInfo() << "EquipmentStorage: migration 22 data step complete -"
            << allPackages.size() << "packages created,"
            << linkedRows << "bag/shot rows linked,"
            << splitRows << "grind+rpm settings split";
    return true;
}

bool EquipmentStorage::importEquipmentStatic(QSqlDatabase& srcDb, QSqlDatabase& destDb, bool merge,
                                             QHash<qint64, qint64>& outIdMap)
{
    if (!merge) {
        // Replace mode: clear dest equipment first (items before packages — no
        // FK cascade on these tables). Done BEFORE the source probe below so a
        // replace from an older source that predates equipment tables still wipes
        // the dest's inventory (otherwise the early return would leave it orphaned
        // after importDatabaseStatic has already cleared the dest shots/bags).
        QSqlQuery clr(destDb);
        if (!clr.exec("DELETE FROM equipment_items") || !clr.exec("DELETE FROM equipment_packages")) {
            qWarning() << "EquipmentStorage: failed to clear equipment for replace import:"
                       << clr.lastError().text();
            return false;
        }
    }

    // Older source DB without equipment tables: nothing to import (in replace mode
    // the dest was already cleared above).
    {
        QSqlQuery srcCheck(srcDb);
        if (!srcCheck.exec("SELECT COUNT(*) FROM equipment_packages"))
            return true;
    }

    QSqlQuery srcPkgs(srcDb);
    if (!srcPkgs.exec(QString("SELECT %1 FROM equipment_packages").arg(packageColumnList()))) {
        qWarning() << "EquipmentStorage: failed to query source packages:" << srcPkgs.lastError().text();
        return false;
    }
    QVector<EquipmentPackage> pkgs;
    while (srcPkgs.next())
        pkgs << packageFromQueryRow(srcPkgs);

    // superseded_by is a package->package pointer; remap it only after every
    // package has a dest id. Only packages we actually insert can carry a
    // lineage pointer (a merged-into existing package keeps its own).
    QHash<qint64, qint64> srcSupersededBy;       // srcId -> src superseded target
    QSet<qint64> newlyInsertedSrcIds;

    for (const EquipmentPackage& srcPkg : pkgs) {
        if (srcPkg.supersededBy > 0)
            srcSupersededBy.insert(srcPkg.id, srcPkg.supersededBy);

        // Load the source grinder + basket items (for full-identity merge dedup).
        EquipmentItem srcGrinder;
        {
            QSqlQuery gi(srcDb);
            gi.prepare(QString("SELECT %1 FROM equipment_items "
                               "WHERE package_id = :id AND kind = 'grinder' LIMIT 1").arg(kItemColumns));
            gi.bindValue(":id", srcPkg.id);
            if (gi.exec() && gi.next())
                srcGrinder = grinderItemFromQueryRow(gi);
        }
        EquipmentItem srcBasket = loadBasketItemStatic(srcDb, srcPkg.id);
        EquipmentItem srcPuck = loadPuckPrepItemStatic(srcDb, srcPkg.id);

        // Merge dedup: an IN-INVENTORY source package whose FULL identity (grinder
        // + basket + puck prep) already exists in dest maps to that package — no
        // duplicate. Superseded (historical) packages always import as new rows so
        // their shots keep a distinct lineage.
        qint64 destId = -1;
        if (merge && srcPkg.inInventory
            && !(srcGrinder.brand.isEmpty() && srcGrinder.model.isEmpty())) {
            const qint64 existing = findPackageByGrinderIdentityStatic(
                destDb, srcGrinder.brand, srcGrinder.model, srcGrinder.burrs, 0,
                srcBasket.brand, srcBasket.model, srcPuck.model);
            if (existing > 0)
                destId = existing;
        }

        if (destId < 0) {
            EquipmentPackage destPkg = srcPkg;
            destPkg.id = 0;            // new autoincrement id (id is non-writable)
            destPkg.supersededBy = 0;  // remapped in the second pass (non-writable)
            destId = insertPackageStatic(destDb, destPkg);
            if (destId < 0)
                return false;
            // Copy the package's items (grinder today; future kinds ride along
            // unchanged because the loop is kind-agnostic). A failed items SELECT
            // is fatal, not best-effort: the package row already exists, so
            // skipping the items would leave an orphan package with no grinder
            // item — the exact blank-grinder corruption createPackageWithGrinderStatic
            // deletes a package to avoid. Fail so the caller rolls back the import.
            QSqlQuery srcItems(srcDb);
            srcItems.prepare(QString("SELECT %1 FROM equipment_items WHERE package_id = :id").arg(kItemColumns));
            srcItems.bindValue(":id", srcPkg.id);
            if (!srcItems.exec()) {
                qWarning() << "EquipmentStorage: import items query failed for package" << srcPkg.id
                           << ":" << srcItems.lastError().text();
                return false;
            }
            while (srcItems.next()) {
                EquipmentItem item = grinderItemFromQueryRow(srcItems);
                item.packageId = destId;
                if (insertItemStatic(destDb, item) <= 0)  // consistent with the create path
                    return false;
            }
            newlyInsertedSrcIds.insert(srcPkg.id);
        }
        outIdMap.insert(srcPkg.id, destId);
    }

    // Second pass: remap superseded_by through the id map. A src target absent
    // from the map (its package merged into an existing one) clears the pointer.
    for (auto it = srcSupersededBy.constBegin(); it != srcSupersededBy.constEnd(); ++it) {
        if (!newlyInsertedSrcIds.contains(it.key()))
            continue;
        const qint64 destId = outIdMap.value(it.key(), 0);
        if (destId <= 0)
            continue;
        const qint64 destTarget = outIdMap.value(it.value(), 0);
        QSqlQuery upd(destDb);
        upd.prepare("UPDATE equipment_packages SET superseded_by = :by WHERE id = :id");
        upd.bindValue(":by", destTarget > 0 ? QVariant(destTarget) : QVariant());
        upd.bindValue(":id", destId);
        if (!upd.exec()) {
            // Fail rather than silently drop the lineage pointer: a swallowed
            // failure leaves superseded_by NULL, so a forked-from package renders
            // as "retired" instead of "older". Propagate so the caller rolls back.
            qWarning() << "EquipmentStorage: import superseded_by remap failed for package" << destId
                       << "-> target" << destTarget << ":" << upd.lastError().text();
            return false;
        }
    }

    // Summary mirrors CoffeeBagStorage's bag-import log so device-transfer issues
    // are traceable: how many source packages imported as new rows vs merged into
    // an existing dest package by grinder identity.
    const qsizetype merged = outIdMap.size() - newlyInsertedSrcIds.size();
    qInfo() << "EquipmentStorage: equipment import -" << newlyInsertedSrcIds.size()
            << "packages imported," << merged << "merged into existing,"
            << srcSupersededBy.size() << "superseded_by pointers remapped";
    return true;
}
