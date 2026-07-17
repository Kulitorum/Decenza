#include "recipestorage.h"
#include "coffeebagstorage.h"
#include "core/dbutils.h"
#include "core/yieldspec.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QSqlRecord>
#include <QDateTime>
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
// Single source of truth for the recipes column set, following the kCols
// pattern in coffeebagstorage.cpp: the SELECT list, positional row read,
// INSERT list + binds, camelCase->column update map, and QVariantMap
// round-trip are all derived from this one ordered table. The physical
// schema (CREATE TABLE in ensureTableStatic + migration 25, plus migration 26
// for rpm_pinned, migration 27 for hot_water_json, migration 28 for
// drink_type, and migration 29 for bag_id) is the only thing not generated
// from it — adding a column is a row here PLUS a schema/migration edit there.
//
// `shotCount` is intentionally NOT here (see InventoryRecipe): it is a
// per-query aggregate injected by requestInventory only.
template<auto M> using RecipeMemberT = std::remove_reference_t<decltype(std::declval<Recipe&>().*M)>;

template<auto M> void readStr (Recipe& r, const QVariant& v) { static_assert(std::is_same_v<RecipeMemberT<M>, QString>); r.*M = v.toString(); }
template<auto M> void readDbl (Recipe& r, const QVariant& v) { static_assert(std::is_same_v<RecipeMemberT<M>, double>);  r.*M = v.toDouble(); }
template<auto M> void readI64 (Recipe& r, const QVariant& v) { static_assert(std::is_same_v<RecipeMemberT<M>, qint64>);  r.*M = v.toLongLong(); }
template<auto M> void readBool(Recipe& r, const QVariant& v) { static_assert(std::is_same_v<RecipeMemberT<M>, bool>);    r.*M = v.toInt() != 0; }

template<auto M> QVariant bindStr  (const Recipe& r) { static_assert(std::is_same_v<RecipeMemberT<M>, QString>); return nullIfEmpty(r.*M); }
template<auto M> QVariant bindDbl  (const Recipe& r) { static_assert(std::is_same_v<RecipeMemberT<M>, double>);  return nullIfZero(r.*M); }
// Signed double bind: negative values are meaningful (temp_offset_c is a
// signed delta) so the nullIfZero collapse would silently drop them. 0 binds
// as an EXPLICIT 0, never NULL — NULL is reserved as migration 31's
// "unconverted" marker (see convertLegacyTempOffsetsStatic).
template<auto M> QVariant bindDblSigned(const Recipe& r) { static_assert(std::is_same_v<RecipeMemberT<M>, double>); return QVariant(r.*M); }
template<auto M> QVariant bindBool (const Recipe& r) { static_assert(std::is_same_v<RecipeMemberT<M>, bool>);    return (r.*M) ? 1 : 0; }
template<auto M> QVariant bindEpoch(const Recipe& r) { static_assert(std::is_same_v<RecipeMemberT<M>, qint64>);  return (r.*M) > 0 ? QVariant(r.*M) : QVariant(); }

template<auto M> QVariant getMember(const Recipe& r) { return QVariant::fromValue(r.*M); }
template<auto M> void setStr (Recipe& r, const QVariant& v) { static_assert(std::is_same_v<RecipeMemberT<M>, QString>); r.*M = v.toString(); }
template<auto M> void setDbl (Recipe& r, const QVariant& v) { static_assert(std::is_same_v<RecipeMemberT<M>, double>);  r.*M = v.toDouble(); }
template<auto M> void setI64 (Recipe& r, const QVariant& v) { static_assert(std::is_same_v<RecipeMemberT<M>, qint64>);  r.*M = v.toLongLong(); }
template<auto M> void setBool(Recipe& r, const QVariant& v) { static_assert(std::is_same_v<RecipeMemberT<M>, bool>);    r.*M = v.toBool(); }

// yield_mode hooks: NULL / junk normalizes to "none" on every path (a row
// imported from an unconverted source reads as mode-less until the deferred
// conversion runs), and "none" is stored explicitly — bindStr's empty->NULL
// collapse never applies because the normalized mode is never empty.
void readYieldMode(Recipe& r, const QVariant& v) { r.yieldMode = YieldSpec::normalizedMode(v.toString()); }
QVariant bindYieldMode(const Recipe& r) { return YieldSpec::normalizedMode(r.yieldMode); }
void setYieldMode(Recipe& r, const QVariant& v) { r.yieldMode = YieldSpec::normalizedMode(v.toString()); }

struct RecipeCol {
    const char* sql;                            // SQLite column name
    const char* key;                            // camelCase Recipe / QVariantMap key
    bool writable;                              // in INSERT + the update map (false only for autoincrement id)
    void (*read)(Recipe&, const QVariant&);     // SELECT row value -> field (read positionally)
    QVariant (*bind)(const Recipe&);            // field -> INSERT bind (NULL-collapsing); null when !writable
    QVariant (*get)(const Recipe&);             // field -> QVariantMap value
    void (*set)(Recipe&, const QVariant&);      // QVariantMap value -> field
};

#define COL_STR(sqlName, member) \
    RecipeCol{ sqlName, #member, true, \
               &readStr<&Recipe::member>, &bindStr<&Recipe::member>, \
               &getMember<&Recipe::member>, &setStr<&Recipe::member> }
#define COL_DBL(sqlName, member) \
    RecipeCol{ sqlName, #member, true, \
               &readDbl<&Recipe::member>, &bindDbl<&Recipe::member>, \
               &getMember<&Recipe::member>, &setDbl<&Recipe::member> }
#define COL_DBL_SIGNED(sqlName, member) \
    RecipeCol{ sqlName, #member, true, \
               &readDbl<&Recipe::member>, &bindDblSigned<&Recipe::member>, \
               &getMember<&Recipe::member>, &setDbl<&Recipe::member> }
#define COL_BOOL(sqlName, member) \
    RecipeCol{ sqlName, #member, true, \
               &readBool<&Recipe::member>, &bindBool<&Recipe::member>, \
               &getMember<&Recipe::member>, &setBool<&Recipe::member> }
#define COL_EPOCH(sqlName, member) \
    RecipeCol{ sqlName, #member, true, \
               &readI64<&Recipe::member>, &bindEpoch<&Recipe::member>, \
               &getMember<&Recipe::member>, &setI64<&Recipe::member> }
#define COL_ID(sqlName, member) \
    RecipeCol{ sqlName, #member, false, \
               &readI64<&Recipe::member>, nullptr, \
               &getMember<&Recipe::member>, &setI64<&Recipe::member> }

const RecipeCol kCols[] = {
    COL_ID   ("id",                    id),
    COL_STR  ("name",                  name),
    COL_STR  ("profile_title",         profileTitle),
    COL_STR  ("profile_json",          profileJson),
    COL_STR  ("drink_type",            drinkType),
    COL_EPOCH("bag_id",                bagId),
    COL_STR  ("beanbase_id",           beanBaseId),
    COL_STR  ("roaster_name",          roasterName),
    COL_STR  ("coffee_name",           coffeeName),
    COL_EPOCH("equipment_id",          equipmentId),
    COL_DBL  ("dose_g",                doseG),
    // Yield spec (add-yield-ratio-anchor). Plain COL_DBL is correct for
    // yield_value: both a gram target and a ratio are strictly positive, so
    // the nullIfZero collapse is safe (contrast temp_offset_c below, whose 0
    // is meaningful). yield_mode normalizes through YieldSpec so NULL/junk
    // reads as "none" and "none" round-trips explicitly. The legacy yield_g
    // column is dead in place (migration 34), like temp_override_c.
    COL_DBL  ("yield_value",           yieldValue),
    RecipeCol{ "yield_mode", "yieldMode", true,
               &readYieldMode, &bindYieldMode,
               &getMember<&Recipe::yieldMode>, &setYieldMode },
    COL_DBL_SIGNED("temp_offset_c",    tempOffsetC),
    COL_STR  ("grind_pinned",          grindPinned),
    COL_EPOCH("rpm_pinned",            rpmPinned),
    COL_STR  ("steam_json",            steamJson),
    COL_STR  ("hot_water_json",        hotWaterJson),
    COL_BOOL ("archived",              archived),
    COL_EPOCH("created_from_shot_id",  createdFromShotId),
    COL_EPOCH("cloned_from_recipe_id", clonedFromRecipeId),
    COL_EPOCH("last_used",             lastUsedEpoch),
};

#undef COL_STR
#undef COL_DBL
#undef COL_DBL_SIGNED
#undef COL_BOOL
#undef COL_EPOCH
#undef COL_ID

constexpr int kColCount = static_cast<int>(std::size(kCols));

// Comma-joined SELECT column list (also the INSERT column order), derived
// once from kCols.
const QString& recipeColumnList() {
    static const QString cols = []() {
        QStringList names;
        for (const RecipeCol& c : kCols)
            names << QLatin1String(c.sql);
        return names.join(QStringLiteral(", "));
    }();
    return cols;
}

// camelCase Recipe key -> column descriptor, for the writable columns only.
// Drives updateRecipeFieldsStatic, sharing insert-time value coercion.
const QHash<QString, const RecipeCol*>& recipeColumnForKey() {
    static const QHash<QString, const RecipeCol*> map = []() {
        QHash<QString, const RecipeCol*> m;
        for (const RecipeCol& c : kCols)
            if (c.writable)
                m.insert(QString::fromLatin1(c.key), &c);
        return m;
    }();
    return map;
}

} // namespace

QVariantMap Recipe::toVariantMap() const
{
    QVariantMap map;
    for (const RecipeCol& c : kCols)
        map.insert(QString::fromLatin1(c.key), c.get(*this));
    return map;
}

Recipe Recipe::fromVariantMap(const QVariantMap& map)
{
    // Absent keys keep the struct's member defaults (0 / empty / false), so
    // partial maps round-trip without inventing values.
    Recipe recipe;
    for (const RecipeCol& c : kCols) {
        const QString key = QString::fromLatin1(c.key);
        if (map.contains(key))
            c.set(recipe, map.value(key));
    }
    return recipe;
}

// static
bool Recipe::hotWaterActive(const QString& hotWaterJson)
{
    if (hotWaterJson.isEmpty())
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(hotWaterJson.toUtf8());
    return doc.isObject() && doc.object().value(QStringLiteral("hasWater")).toBool();
}

// static
QString Recipe::deriveDrinkType(const Recipe& recipe, const QString& profileBeverageType)
{
    const bool water = hotWaterActive(recipe.hotWaterJson);
    if (recipe.profileTitle.trimmed().isEmpty() && water)
        return QStringLiteral("tea_hotwater");

    const QString bev = profileBeverageType.trimmed().toLower();
    if (bev == QLatin1String("tea_portafilter"))
        return QStringLiteral("tea");

    bool milk = false;
    if (!recipe.steamJson.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(recipe.steamJson.toUtf8());
        milk = doc.isObject() && doc.object().value(QStringLiteral("hasMilk")).toBool();
    }
    if (milk)
        return QStringLiteral("latte");

    if (water) {
        const QString order = QJsonDocument::fromJson(recipe.hotWaterJson.toUtf8())
                                  .object().value(QStringLiteral("order")).toString();
        return order == QLatin1String("before") ? QStringLiteral("long_black")
                                                : QStringLiteral("americano");
    }

    if (bev == QLatin1String("filter") || bev == QLatin1String("pourover"))
        return QStringLiteral("filter");
    return QStringLiteral("espresso");
}

RecipeStorage::RecipeStorage(QObject* parent)
    : QObject(parent)
{
}

RecipeStorage::~RecipeStorage()
{
    *m_destroyed = true;
    // Stop the worker before members vanish (see ~CoffeeBagStorage: the
    // m_destroyed flag suppresses the in-flight task's result callback).
    m_dbWorker.reset();
}

void RecipeStorage::initialize(const QString& dbPath)
{
    m_dbPath = dbPath;
}

void RecipeStorage::runAsync(const QString& connPrefix,
                             std::function<void(QSqlDatabase&)> work,
                             std::function<void(bool dbOpened)> done)
{
    if (m_dbPath.isEmpty()) {
        qWarning() << "RecipeStorage: not initialized, dropping" << connPrefix;
        return;
    }
    if (!m_dbWorker)
        m_dbWorker = std::make_unique<SerialDbWorker>(QStringLiteral("RecipeStorageWorker"));
    m_dbWorker->run(m_dbPath, connPrefix, std::move(work), std::move(done), this, m_destroyed);
}

void RecipeStorage::requestInventory()
{
    auto recipes = std::make_shared<QVariantList>();
    runAsync("recipes_inv",
        [recipes](QSqlDatabase& db) {
            const QVector<InventoryRecipe> inventory = loadInventoryStatic(db, false);
            for (const InventoryRecipe& entry : inventory) {
                // shotCount and stale are inventory-only aggregates, not
                // Recipe fields — inject them into the map the QML card reads.
                QVariantMap map = entry.recipe.toVariantMap();
                map.insert(QStringLiteral("shotCount"), entry.shotCount);
                map.insert(QStringLiteral("stale"), entry.stale);
                recipes->append(map);
            }
        },
        // Read: skip the emit on open failure so the UI keeps its current
        // list instead of being told the inventory is empty.
        [this, recipes](bool dbOpened) { if (dbOpened) emit inventoryReady(*recipes); });
}

void RecipeStorage::requestArchived()
{
    auto recipes = std::make_shared<QVariantList>();
    runAsync("recipes_arch",
        [recipes](QSqlDatabase& db) {
            const QVector<InventoryRecipe> inventory = loadInventoryStatic(db, true);
            for (const InventoryRecipe& entry : inventory) {
                QVariantMap map = entry.recipe.toVariantMap();
                map.insert(QStringLiteral("shotCount"), entry.shotCount);
                map.insert(QStringLiteral("stale"), entry.stale);
                recipes->append(map);
            }
        },
        [this, recipes](bool dbOpened) { if (dbOpened) emit archivedReady(*recipes); });
}

void RecipeStorage::requestRecipe(qint64 recipeId)
{
    auto result = std::make_shared<QVariantMap>();
    runAsync("recipes_get",
        [recipeId, result](QSqlDatabase& db) {
            const Recipe recipe = loadRecipeStatic(db, recipeId);
            if (recipe.isValid())
                *result = recipe.toVariantMap();
        },
        // Read: skip the emit on open failure. An empty result would be read
        // as "active recipe vanished" by SettingsDye; only a genuine
        // not-found (db opened, row absent) should do that.
        [this, recipeId, result](bool dbOpened) { if (dbOpened) emit recipeReady(recipeId, *result); });
}

void RecipeStorage::requestLastEquipmentForDrinkType(const QString& drinkType)
{
    auto equipmentId = std::make_shared<qint64>(0);
    runAsync("recipes_last_equipment",
        [drinkType, equipmentId](QSqlDatabase& db) {
            *equipmentId = lastEquipmentForDrinkTypeStatic(db, drinkType);
        },
        [this, drinkType, equipmentId](bool) {
            emit lastEquipmentForDrinkTypeReady(drinkType, *equipmentId);
        });
}

// static
qint64 RecipeStorage::lastEquipmentForDrinkTypeStatic(QSqlDatabase& db, const QString& drinkType)
{
    if (drinkType.isEmpty())
        return 0;
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "SELECT equipment_id FROM recipes "
            "WHERE drink_type = :type AND COALESCE(equipment_id, 0) > 0 AND archived = 0 "
            "ORDER BY last_used DESC, id DESC LIMIT 1"))) {
        qWarning() << "RecipeStorage: lastEquipmentForDrinkType prepare failed:"
                   << query.lastError().text();
        return 0;
    }
    query.bindValue(":type", drinkType);
    if (!query.exec()) {
        qWarning() << "RecipeStorage: lastEquipmentForDrinkType query failed:"
                   << query.lastError().text();
        return 0;
    }
    return query.next() ? query.value(0).toLongLong() : 0;
}

void RecipeStorage::requestRecipeForActivation(qint64 recipeId)
{
    // Terminal emit even when uninitialized — runAsync drops the job on an
    // empty dbPath, which would leave MainController's activation caller (and
    // the MCP/web one-shot listeners waiting on recipeActivated) hanging. An
    // empty recipe map is the not-found contract applyActivatedRecipe already
    // handles → recipeActivated(id, false).
    if (m_dbPath.isEmpty()) {
        qWarning() << "RecipeStorage: requestRecipeForActivation on uninitialized storage, recipe" << recipeId;
        emit recipeActivationReady(recipeId, QVariantMap(), -1, QVariantMap());
        return;
    }
    auto recipe = std::make_shared<QVariantMap>();
    auto bagId = std::make_shared<qint64>(-1);
    auto bag = std::make_shared<QVariantMap>();
    runAsync("recipes_activate",
        [recipeId, recipe, bagId, bag](QSqlDatabase& db) {
            const Recipe r = loadRecipeStatic(db, recipeId);
            if (!r.isValid())
                return;
            *recipe = r.toVariantMap();
            // The hard bag link, applied whether or not the bag is still in
            // inventory — a stale recipe activates fully with the finished
            // bag's data (its last dial is a better starting point than
            // nothing). Only a deleted bag row degrades to bean-less.
            if (r.bagId > 0) {
                const CoffeeBag linked = CoffeeBagStorage::loadBagStatic(db, r.bagId);
                if (linked.isValid()) {
                    *bagId = r.bagId;
                    *bag = linked.toVariantMap();
                }
            }
        },
        // Emit even on open failure: activation callers (UI pill tap, MCP,
        // web) wait on this as their terminal status; an empty recipe map
        // tells them the activation failed.
        [this, recipeId, recipe, bagId, bag](bool) {
            emit recipeActivationReady(recipeId, *recipe, *bagId, *bag);
        });
}

void RecipeStorage::requestCreateRecipe(const QVariantMap& recipeMap)
{
    // Correlation token (transient, never a column): recipeCreated is a
    // broadcast signal and creates arrive from four surfaces (wizard, MCP,
    // web, clone) — without a token, whichever create completes first
    // satisfies EVERY armed one-shot listener, sending recipe JSON to the
    // wrong requester. Callers put "requestToken" in the map; the emitted
    // map echoes it (on failure too) so each listener filters on its own.
    // fromVariantMap ignores the key, so nothing reaches the row.
    const QString requestToken = recipeMap.value(QStringLiteral("requestToken")).toString();

    // Guarantee a terminal recipeCreated even when uninitialized — runAsync
    // silently drops the job on an empty dbPath, which would leave MCP/web
    // one-shot listeners hanging forever (see requestUpdateRecipe).
    if (m_dbPath.isEmpty()) {
        qWarning() << "RecipeStorage: requestCreateRecipe on uninitialized storage";
        QVariantMap failure;
        if (!requestToken.isEmpty())
            failure.insert(QStringLiteral("requestToken"), requestToken);
        emit recipeCreated(-1, failure);
        return;
    }
    auto newId = std::make_shared<qint64>(-1);
    auto created = std::make_shared<QVariantMap>();
    if (!requestToken.isEmpty())
        created->insert(QStringLiteral("requestToken"), requestToken);
    runAsync("recipes_create",
        [recipeMap, newId, created, requestToken](QSqlDatabase& db) {
            Recipe recipe = Recipe::fromVariantMap(recipeMap);
            recipe.lastUsedEpoch = QDateTime::currentSecsSinceEpoch();
            // Bag-link normalization: a caller that supplies bean identity
            // without a bag (older MCP/web clients, pre-bag promoted shots)
            // gets the identity resolved to its open bag ONCE, at save time;
            // a caller that supplies only a bag adopts the bag's identity so
            // the display fallback / relink matching key is always populated.
            if (recipe.bagId <= 0) {
                const qint64 resolved = resolveOpenBagStatic(db, recipe);
                recipe.bagId = resolved > 0 ? resolved : 0;
            } else {
                const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, recipe.bagId);
                if (bag.isValid()) {
                    if (recipe.beanBaseId.isEmpty()) recipe.beanBaseId = bag.beanBaseId;
                    if (recipe.roasterName.isEmpty()) recipe.roasterName = bag.roasterName;
                    if (recipe.coffeeName.isEmpty()) recipe.coffeeName = bag.coffeeName;
                } else {
                    qWarning() << "RecipeStorage: create carried unknown bag id"
                               << recipe.bagId << "- dropping the bag link";
                    recipe.bagId = 0;
                }
            }
            // Grind default: grind always lives on the recipe, and the linked
            // bag's dial is read exactly once, at creation. A bag-linked
            // create map that OMITS grind adopts the bag's current dial here —
            // the non-interactive surfaces' (MCP/web) equivalent of the
            // wizard's editable prefill. An explicitly empty grind in the map
            // is a deliberate "no grind" and stays empty.
            if (recipe.grindPinned.isEmpty() && recipe.bagId > 0
                && !recipeMap.contains(QStringLiteral("grindPinned"))) {
                const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, recipe.bagId);
                if (bag.isValid() && !bag.grinderSetting.isEmpty()) {
                    recipe.grindPinned = bag.grinderSetting;
                    if (recipe.rpmPinned <= 0 && bag.rpm > 0)
                        recipe.rpmPinned = bag.rpm;
                }
            }
            *newId = insertRecipeStatic(db, recipe);
            if (*newId > 0) {
                *created = loadRecipeStatic(db, *newId).toVariantMap();
                if (!requestToken.isEmpty())
                    created->insert(QStringLiteral("requestToken"), requestToken);
            }
        },
        // Write: emit regardless — *newId is -1 on failure, a terminal status.
        [this, newId, created](bool) {
            emit recipeCreated(*newId, *created);
            if (*newId > 0)
                emit recipesChanged();
        });
}

void RecipeStorage::requestUpdateRecipe(qint64 recipeId, const QVariantMap& fields)
{
    // Guarantee a terminal recipeUpdated even when uninitialized (see
    // CoffeeBagStorage::requestUpdateBag: MCP/web callers arm a one-shot
    // recipeUpdated to send their response and would hang without it).
    if (m_dbPath.isEmpty()) {
        qWarning() << "RecipeStorage: requestUpdateRecipe on uninitialized storage, recipe" << recipeId;
        emit recipeUpdated(recipeId, false);
        return;
    }
    auto success = std::make_shared<bool>(false);
    // Transient hint, not a column: the surface's resolved beverage_type for
    // an installed profileTitle in this patch (installed profiles embed no
    // JSON, and the profile catalog isn't reachable on the DB thread).
    // Stripped here; consumed by the drink-type re-derivation below.
    QVariantMap patch = fields;
    const QString hintedBev = patch.take(QStringLiteral("profileBeverageType")).toString();
    if (patch.isEmpty()) {
        // A hint-only patch would reach updateRecipeFieldsStatic with zero
        // assignments and fail with a mystery success=false — name the cause.
        qWarning() << "RecipeStorage: update for recipe" << recipeId
                   << "carried no persistable fields";
        emit recipeUpdated(recipeId, false);
        return;
    }
    runAsync("recipes_update",
        [recipeId, patchFields = std::move(patch), hintedBev, success](QSqlDatabase& db) {
            // The whole update is transactional so the validity check below
            // can reject the patch without leaving a half-applied row.
            if (!db.transaction()) {
                qWarning() << "RecipeStorage: update transaction begin failed for recipe"
                           << recipeId << "-" << db.lastError().text();
                return;
            }
            QVariantMap fields = patchFields;
            // A patch that re-points the bag link adopts the bag's bean
            // identity unless the caller set it explicitly — the identity
            // fields are the display fallback and relink matching key, and
            // must follow the bag (manual re-point to a different bean).
            const qint64 patchBagId = fields.value(QStringLiteral("bagId")).toLongLong();
            if (patchBagId > 0) {
                const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, patchBagId);
                if (bag.isValid()) {
                    if (!fields.contains(QStringLiteral("beanBaseId")))
                        fields.insert(QStringLiteral("beanBaseId"), bag.beanBaseId);
                    if (!fields.contains(QStringLiteral("roasterName")))
                        fields.insert(QStringLiteral("roasterName"), bag.roasterName);
                    if (!fields.contains(QStringLiteral("coffeeName")))
                        fields.insert(QStringLiteral("coffeeName"), bag.coffeeName);
                } else {
                    // Same rule as create: a dangling bag id drops the LINK
                    // KEY, not the whole patch — the web editor re-sends the
                    // stored bagId on every save, and a bag hard-deleted on
                    // another surface must not turn "rename the recipe" into
                    // a misleading whole-update failure. The recipe's
                    // existing link stays as stored.
                    qWarning() << "RecipeStorage: update for recipe" << recipeId
                               << "carried unknown bag id" << patchBagId
                               << "- dropping the bag-link field, applying the rest";
                    fields.remove(QStringLiteral("bagId"));
                    if (fields.isEmpty()) {
                        // The patch was ONLY the dangling link: nothing left
                        // to apply — succeed as a no-op rather than failing.
                        db.rollback();
                        *success = true;
                        return;
                    }
                }
            }
            *success = updateRecipeFieldsStatic(db, recipeId, fields);
            if (!*success) {
                db.rollback();
                return;
            }
            const Recipe updated = loadRecipeStatic(db, recipeId);
            if (!updated.isValid()) {
                db.rollback();
                *success = false;
                return;
            }
            // Storage-level invariant: a recipe must stay saveable — a name,
            // and a profile unless hot-water-only. Enforced against the
            // RESULTING row so every surface inherits it (the web update
            // route shipped without the MCP tool's early guard, and neither
            // surface caught removing the hot-water block from an already
            // profile-less recipe).
            if (!Recipe::saveValidationPasses(updated.name, updated.profileTitle,
                                              updated.hotWaterJson)) {
                qWarning() << "RecipeStorage: rejecting update that would strand recipe"
                           << recipeId << "(name/profile/hot-water invariant)";
                db.rollback();
                *success = false;
                return;
            }
            // drink_type follows the blocks when the caller changed them
            // without setting it explicitly (MCP/web edits must not strand a
            // stale type — e.g. adding a hot-water block to an espresso).
            // Derivation from the UPDATED row; profile beverage_type comes
            // from the embedded JSON when present (installed-profile lookup
            // isn't available on this thread — the wizard stores the exact
            // type on its own saves anyway).
            const bool touchesBlocks = fields.contains(QStringLiteral("steamJson"))
                || fields.contains(QStringLiteral("hotWaterJson"))
                || fields.contains(QStringLiteral("profileTitle"));
            if (touchesBlocks && !fields.contains(QStringLiteral("drinkType"))) {
                QString bev = hintedBev;
                if (bev.isEmpty() && !updated.profileJson.isEmpty())
                    bev = QJsonDocument::fromJson(updated.profileJson.toUtf8())
                              .object().value(QStringLiteral("beverage_type")).toString();
                // When the profile didn't change, the stored type already
                // encodes its profile-derived category — without this, a
                // steam-settings stamp on an active TEA recipe would
                // re-derive it into "latte" (beverage_type unresolvable
                // on this thread for installed profiles).
                if (bev.isEmpty() && !fields.contains(QStringLiteral("profileTitle"))) {
                    if (updated.drinkType == QLatin1String("tea"))
                        bev = QStringLiteral("tea_portafilter");
                    else if (updated.drinkType == QLatin1String("filter"))
                        bev = QStringLiteral("filter");
                }
                if (!updateRecipeFieldsStatic(db, recipeId,
                        {{QStringLiteral("drinkType"), Recipe::deriveDrinkType(updated, bev)}}))
                    qWarning() << "RecipeStorage: drink-type re-derivation stamp failed for recipe"
                               << recipeId << "- stored type may be stale";
            }
            if (!db.commit()) {
                qWarning() << "RecipeStorage: update commit failed for recipe" << recipeId
                           << "-" << db.lastError().text();
                db.rollback();
                *success = false;
            }
        },
        // Write: emit regardless — *success is false on open failure, the
        // terminal status callers wait on.
        [this, recipeId, success](bool) {
            emit recipeUpdated(recipeId, *success);
            if (*success)
                emit recipesChanged();
        });
}

void RecipeStorage::requestCloneRecipe(qint64 sourceId, const QString& newName,
                                       const QString& requestToken)
{
    if (m_dbPath.isEmpty()) {
        qWarning() << "RecipeStorage: requestCloneRecipe on uninitialized storage";
        QVariantMap failure;
        if (!requestToken.isEmpty())
            failure.insert(QStringLiteral("requestToken"), requestToken);
        emit recipeCreated(-1, failure);
        return;
    }
    auto newId = std::make_shared<qint64>(-1);
    auto created = std::make_shared<QVariantMap>();
    if (!requestToken.isEmpty())
        created->insert(QStringLiteral("requestToken"), requestToken);
    runAsync("recipes_clone",
        [sourceId, newName, newId, created, requestToken](QSqlDatabase& db) {
            Recipe source = loadRecipeStatic(db, sourceId);
            if (!source.isValid()) {
                qWarning() << "RecipeStorage: clone source" << sourceId << "not found";
                return;
            }
            Recipe copy = source;
            copy.id = 0;
            copy.name = newName;
            copy.archived = false;
            // Provenance: the clone points at its source recipe; the source's
            // golden-shot link is NOT copied — the clone hasn't earned one.
            copy.clonedFromRecipeId = sourceId;
            copy.createdFromShotId = 0;
            copy.lastUsedEpoch = QDateTime::currentSecsSinceEpoch();
            *newId = insertRecipeStatic(db, copy);
            if (*newId > 0) {
                *created = loadRecipeStatic(db, *newId).toVariantMap();
                if (!requestToken.isEmpty())
                    created->insert(QStringLiteral("requestToken"), requestToken);
            }
        },
        [this, newId, created](bool) {
            emit recipeCreated(*newId, *created);
            if (*newId > 0)
                emit recipesChanged();
        });
}

void RecipeStorage::requestArchiveRecipe(qint64 recipeId)
{
    requestUpdateRecipe(recipeId, {{"archived", true}});
}

void RecipeStorage::requestUnarchiveRecipe(qint64 recipeId)
{
    requestUpdateRecipe(recipeId, {{"archived", false}});
}

void RecipeStorage::requestTouchLastUsed(qint64 recipeId)
{
    runAsync("recipes_touch",
        [recipeId](QSqlDatabase& db) {
            QSqlQuery query(db);
            query.prepare("UPDATE recipes SET last_used = :now, "
                          "updated_at = strftime('%s', 'now') WHERE id = :id");
            query.bindValue(":now", QDateTime::currentSecsSinceEpoch());
            query.bindValue(":id", recipeId);
            if (!query.exec())
                qWarning() << "RecipeStorage: touch last_used failed:" << query.lastError().text();
        },
        [](bool) {});
}

void RecipeStorage::requestDeleteRecipe(qint64 recipeId)
{
    if (m_dbPath.isEmpty()) {
        qWarning() << "RecipeStorage: requestDeleteRecipe on uninitialized storage, recipe" << recipeId;
        emit recipeDeleted(recipeId, false);
        return;
    }
    auto success = std::make_shared<bool>(false);
    runAsync("recipes_delete",
        [recipeId, success](QSqlDatabase& db) {
            // Recipes with linked shots are history — only Archive is allowed
            // for them, so shot rows can always name their recipe. Delete is
            // for mistaken creations. Same rule as bags.
            QSqlQuery countQuery(db);
            countQuery.prepare("SELECT COUNT(*) FROM shots WHERE recipe_id = :id");
            countQuery.bindValue(":id", recipeId);
            if (!countQuery.exec() || !countQuery.next()) {
                qWarning() << "RecipeStorage: delete pre-check failed:" << countQuery.lastError().text();
                return;
            }
            if (countQuery.value(0).toInt() > 0) {
                qWarning() << "RecipeStorage: refusing to delete recipe" << recipeId << "with linked shots";
                return;
            }
            QSqlQuery deleteQuery(db);
            deleteQuery.prepare("DELETE FROM recipes WHERE id = :id");
            deleteQuery.bindValue(":id", recipeId);
            *success = deleteQuery.exec();
            if (!*success)
                qWarning() << "RecipeStorage: delete failed:" << deleteQuery.lastError().text();
        },
        // Write: emit regardless — *success is false on open failure, terminal.
        [this, recipeId, success](bool) {
            emit recipeDeleted(recipeId, *success);
            if (*success)
                emit recipesChanged();
        });
}

bool RecipeStorage::ensureTableStatic(QSqlDatabase& db)
{
    QSqlQuery query(db);
    const bool ok = query.exec(R"(
        CREATE TABLE IF NOT EXISTS recipes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            profile_title TEXT,
            profile_json TEXT,
            drink_type TEXT,
            bag_id INTEGER,
            beanbase_id TEXT,
            roaster_name TEXT,
            coffee_name TEXT,
            equipment_id INTEGER,
            dose_g REAL,
            yield_g REAL, -- dead: pre-34 absolute yields; carrier for legacy-source imports only
            yield_value REAL,
            yield_mode TEXT,
            temp_offset_c REAL,
            temp_override_c REAL, -- dead: pre-31 absolute temps; carrier for legacy-source imports only
            grind_pinned TEXT,
            rpm_pinned INTEGER,
            steam_json TEXT,
            hot_water_json TEXT,
            archived INTEGER NOT NULL DEFAULT 0,
            created_from_shot_id INTEGER,
            cloned_from_recipe_id INTEGER,
            last_used INTEGER,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )");
    if (!ok) {
        qWarning() << "RecipeStorage: failed to create recipes table:" << query.lastError().text();
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_recipes_inventory ON recipes(archived, last_used DESC)");
    return true;
}

Recipe RecipeStorage::recipeFromQueryRow(const QSqlQuery& query)
{
    // Read positionally from the SAME table the SELECT list is built from
    // (see kCols): a column's read can never drift from its SELECT position.
    Recipe recipe;
    for (int i = 0; i < kColCount; ++i)
        kCols[i].read(recipe, query.value(i));
    return recipe;
}

qint64 RecipeStorage::insertRecipeStatic(QSqlDatabase& db, const Recipe& recipe)
{
    // Column list, placeholders, and binds all derived from the writable
    // columns of kCols, in table order — adding a column needs no edit here.
    QStringList columns, placeholders;
    QVariantList binds;
    for (const RecipeCol& c : kCols) {
        if (!c.writable)
            continue;
        columns << QString::fromLatin1(c.sql);
        placeholders << QStringLiteral("?");
        binds << c.bind(recipe);
    }

    QSqlQuery query(db);
    query.prepare(QString("INSERT INTO recipes (%1) VALUES (%2)")
                      .arg(columns.join(QStringLiteral(", ")), placeholders.join(QStringLiteral(", "))));
    for (int i = 0; i < binds.size(); ++i)
        query.bindValue(i, binds.at(i));

    if (!query.exec()) {
        qWarning() << "RecipeStorage: insert failed:" << query.lastError().text();
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

Recipe RecipeStorage::loadRecipeStatic(QSqlDatabase& db, qint64 recipeId)
{
    QSqlQuery query(db);
    query.prepare(QString("SELECT %1 FROM recipes WHERE id = :id").arg(recipeColumnList()));
    query.bindValue(":id", recipeId);
    if (!query.exec() || !query.next())
        return Recipe();
    return recipeFromQueryRow(query);
}

QVector<InventoryRecipe> RecipeStorage::loadInventoryStatic(QSqlDatabase& db, bool archived)
{
    QVector<InventoryRecipe> recipes;
    QSqlQuery query(db);
    // shot_count subquery feeds the delete-vs-archive action: a recipe
    // nothing references is a mistaken creation (trash); one with shots is
    // history (archive only). bag_in_inventory feeds the stale display
    // state — NULL when there is no linked bag or its row was deleted.
    query.prepare(QString("SELECT %1, "
                          "(SELECT COUNT(*) FROM shots WHERE recipe_id = recipes.id) AS shot_count, "
                          "(SELECT b.in_inventory FROM coffee_bags b WHERE b.id = recipes.bag_id) "
                          "  AS bag_in_inventory "
                          "FROM recipes WHERE archived = :archived "
                          "ORDER BY last_used DESC, id DESC").arg(recipeColumnList()));
    query.bindValue(":archived", archived ? 1 : 0);
    if (!query.exec()) {
        qWarning() << "RecipeStorage: inventory query failed:" << query.lastError().text();
        return recipes;
    }
    // Read the aggregates by alias, not a hardcoded position.
    const int shotCountCol = query.record().indexOf("shot_count");
    const int bagInInventoryCol = query.record().indexOf("bag_in_inventory");
    while (query.next()) {
        InventoryRecipe entry;
        entry.recipe = recipeFromQueryRow(query);
        if (shotCountCol >= 0)
            entry.shotCount = query.value(shotCountCol).toLongLong();
        // Stale = the recipe has a bean but its bag is not open: the linked
        // bag left inventory (finished / deleted), or the migration never
        // found an open bag to link. Bean-less recipes are never stale.
        const bool hasBean = !entry.recipe.beanBaseId.isEmpty()
            || !entry.recipe.roasterName.isEmpty()
            || !entry.recipe.coffeeName.isEmpty();
        if (entry.recipe.bagId > 0)
            entry.stale = bagInInventoryCol < 0
                || query.value(bagInInventoryCol).toInt() != 1;
        else
            entry.stale = hasBean;
        recipes.append(entry);
    }
    return recipes;
}

bool RecipeStorage::updateRecipeFieldsStatic(QSqlDatabase& db, qint64 recipeId, const QVariantMap& fields)
{
    const QHash<QString, const RecipeCol*>& kColumnFor = recipeColumnForKey();

    QStringList assignments;
    QVariantList values;
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        const RecipeCol* col = kColumnFor.value(it.key());
        if (!col) {
            qWarning() << "RecipeStorage: ignoring unknown update key" << it.key();
            continue;
        }
        // Coerce through the same set+bind hooks as insert so update-time
        // NULL-collapsing (empty string / 0 -> NULL) matches insert exactly.
        Recipe scratch;
        col->set(scratch, it.value());
        assignments << QString("%1 = ?").arg(QLatin1String(col->sql));
        values << col->bind(scratch);
    }
    if (assignments.isEmpty())
        return false;

    QSqlQuery query(db);
    query.prepare(QString("UPDATE recipes SET %1, updated_at = strftime('%s', 'now') WHERE id = ?")
                      .arg(assignments.join(QStringLiteral(", "))));
    int bindIndex = 0;
    for (const QVariant& v : values)
        query.bindValue(bindIndex++, v);
    query.bindValue(bindIndex, recipeId);

    if (!query.exec()) {
        qWarning() << "RecipeStorage: update failed:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

namespace {

// Dup-guard (recipe-bag-lifecycle): true when the target bag already carries
// a non-archived recipe with the same profile title and drink type — the one
// rule that protects deliberate two-bag comparison pairs from a silent roll.
// Dose/yield/temp differences deliberately do NOT make recipes distinct:
// comparison pairs usually share numbers, so profile + drink type is the
// tightest reliable identity.
bool wouldDuplicateOnBag(QSqlDatabase& db, const Recipe& recipe, qint64 targetBagId)
{
    QSqlQuery query(db);
    // COALESCE the bound params too: empty QStrings can bind as SQL NULL and
    // `x = NULL` is never true (same trick as the import dedup query).
    query.prepare(
        "SELECT COUNT(*) FROM recipes WHERE bag_id = :bag AND archived = 0 "
        "AND id <> :self "
        "AND LOWER(COALESCE(profile_title,'')) = LOWER(COALESCE(:profile,'')) "
        "AND COALESCE(drink_type,'') = COALESCE(:type,'')");
    query.bindValue(":bag", targetBagId);
    query.bindValue(":self", recipe.id);
    query.bindValue(":profile", recipe.profileTitle);
    query.bindValue(":type", recipe.drinkType);
    if (!query.exec() || !query.next()) {
        qWarning() << "RecipeStorage: dup-guard query failed:" << query.lastError().text();
        // Fail safe: a wrongly-skipped relink costs one manual tap; a
        // wrongly-executed one silently collapses a comparison pair.
        return true;
    }
    return query.value(0).toLongLong() > 0;
}

// What the relink toast calls the bag: the coffee name (how users refer to
// a bean), roaster as fallback.
QString bagDisplayName(const CoffeeBag& bag)
{
    return bag.coffeeName.isEmpty() ? bag.roasterName : bag.coffeeName;
}

} // namespace

qint64 RecipeStorage::resolveOpenBagStatic(QSqlDatabase& db, const Recipe& recipe)
{
    // Canonical link first: the strongest identity, survives bag replacement.
    // exec failures are LOGGED so "no open bag" (a normal display state) is
    // never confused with a query error.
    if (!recipe.beanBaseId.isEmpty()) {
        QSqlQuery query(db);
        query.prepare("SELECT id FROM coffee_bags WHERE beanbase_id = :bb "
                      "AND in_inventory = 1 ORDER BY last_used DESC, id DESC LIMIT 1");
        query.bindValue(":bb", recipe.beanBaseId);
        if (!query.exec())
            qWarning() << "RecipeStorage: open-bag canonical query failed:"
                       << query.lastError().text();
        else if (query.next())
            return query.value(0).toLongLong();
    }
    // Identity fallback: case-insensitive roaster+coffee, matching
    // CoffeeBagStorage::findBagForShotStatic's identity pass.
    if (!recipe.roasterName.isEmpty() || !recipe.coffeeName.isEmpty()) {
        QSqlQuery query(db);
        query.prepare("SELECT id FROM coffee_bags WHERE in_inventory = 1 "
                      "AND LOWER(COALESCE(roaster_name,'')) = LOWER(:roaster) "
                      "AND LOWER(COALESCE(coffee_name,'')) = LOWER(:coffee) "
                      "ORDER BY last_used DESC, id DESC LIMIT 1");
        query.bindValue(":roaster", recipe.roasterName);
        query.bindValue(":coffee", recipe.coffeeName);
        if (!query.exec())
            qWarning() << "RecipeStorage: open-bag identity query failed:"
                       << query.lastError().text();
        else if (query.next())
            return query.value(0).toLongLong();
    }
    return -1;
}

// static
bool RecipeStorage::migrateBagLinksStatic(QSqlDatabase& db)
{
    // The one-time data pass of migration 29: resolve each still-unlinked
    // recipe's bean identity to its current open bag — the retired
    // activation resolver's logic, run once at migration instead of at
    // every activation. Unresolvable recipes stay NULL → stale.
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
            "SELECT id, beanbase_id, roaster_name, coffee_name FROM recipes "
            "WHERE bag_id IS NULL AND (COALESCE(beanbase_id,'') <> '' "
            "OR COALESCE(roaster_name,'') <> '' OR COALESCE(coffee_name,'') <> '')"))) {
        qWarning() << "RecipeStorage: bag-link migration query failed:"
                   << query.lastError().text();
        return false;
    }
    // Collect first, update after — updating while iterating the same
    // connection's result set is undefined with some SQLite driver configs.
    QVector<QPair<qint64, qint64>> links;   // recipe id -> resolved bag id
    int unresolved = 0;
    while (query.next()) {
        Recipe probe;
        probe.beanBaseId = query.value(1).toString();
        probe.roasterName = query.value(2).toString();
        probe.coffeeName = query.value(3).toString();
        const qint64 bagId = resolveOpenBagStatic(db, probe);
        if (bagId > 0)
            links.append({query.value(0).toLongLong(), bagId});
        else
            unresolved++;
    }
    int linked = 0;
    bool complete = true;
    for (const auto& link : links) {
        QSqlQuery update(db);
        update.prepare("UPDATE recipes SET bag_id = :bag WHERE id = :id");
        update.bindValue(":bag", link.second);
        update.bindValue(":id", link.first);
        if (update.exec()) {
            linked++;
        } else {
            qWarning() << "RecipeStorage: bag-link migration update failed for recipe"
                       << link.first << "-" << update.lastError().text();
            complete = false;
        }
    }
    if (linked > 0 || unresolved > 0)
        qDebug() << "RecipeStorage: bag-link migration -" << linked << "recipes linked,"
                 << unresolved << "left unlinked (no open bag of their bean)";
    return complete;
}

// static
bool RecipeStorage::migrateGrindOwnershipStatic(QSqlDatabase& db,
                                                const QVector<qint64>* onlyRecipeIds)
{
    // The one-time data pass of migration 30 (fix-recipe-grind-integrity):
    // grind always lives on the recipe now, so the retired "empty grind_pinned
    // = inherit from the bag" rows get their linked bag's current dial copied
    // in ONCE — the historical equivalent of the creation-time default. Rows
    // whose bag has no dial yet (tea bags, never-dialed bags) are skipped, not
    // stamped with empty strings; bag-less rows stay untouched ("no grind
    // recorded yet" is a valid state). Also reused by importRecipesStatic —
    // a transfer/backup source DB can predate this migration, and its rows
    // arrive after the local pass already ran. The import pass MUST scope to
    // the just-inserted rows (onlyRecipeIds): post-migration, "empty grind +
    // linked bag" is a supported, deliberate state (an explicitly cleared
    // grind), and a table-wide re-run would silently stamp the bag's dial
    // back onto such local rows on every import.
    QString sql = QStringLiteral(
        "UPDATE recipes SET "
        "  grind_pinned = (SELECT b.grinder_setting FROM coffee_bags b WHERE b.id = recipes.bag_id), "
        "  rpm_pinned = (SELECT COALESCE(b.rpm, 0) FROM coffee_bags b WHERE b.id = recipes.bag_id) "
        "WHERE COALESCE(grind_pinned,'') = '' "
        "  AND bag_id IS NOT NULL "
        "  AND EXISTS (SELECT 1 FROM coffee_bags b WHERE b.id = recipes.bag_id "
        "              AND COALESCE(b.grinder_setting,'') <> '')");
    if (onlyRecipeIds) {
        if (onlyRecipeIds->isEmpty())
            return true;  // nothing imported, nothing to backfill
        QStringList ids;
        ids.reserve(onlyRecipeIds->size());
        for (qint64 id : *onlyRecipeIds)
            ids << QString::number(id);
        sql += QStringLiteral(" AND id IN (%1)").arg(ids.join(QLatin1Char(',')));
    }
    QSqlQuery query(db);
    const bool ok = query.exec(sql);
    if (!ok) {
        qWarning() << "RecipeStorage: grind-ownership migration failed:"
                   << query.lastError().text();
        return false;
    }
    if (query.numRowsAffected() > 0)
        qDebug() << "RecipeStorage: grind-ownership migration -" << query.numRowsAffected()
                 << "inherit-mode recipes adopted their bag's grind";
    return true;
}

// static
bool RecipeStorage::convertLegacyTempOffsetsStatic(QSqlDatabase& db,
                                                   const QHash<QString, double>& profileTempsByTitle,
                                                   int* outConvertedCount)
{
    // Migration 31 data pass (recipe-relative-temp-offset): rows whose
    // temp_offset_c is NULL are unconverted — either migration 31 just added
    // the column, or an unconverted-source import marked them
    // (importRecipesStatic). Converted rows always hold an explicit value
    // (bindDblSigned never writes NULL), so the pass is idempotent and
    // re-runs are no-ops. The profile's CURRENT temperature is the anchor
    // (catalog snapshot first — the files are the live truth); the recipe's
    // embedded profile_json is the fallback for renamed/deleted profiles.
    // Neither resolvable → offset 0: a delta against an unknown baseline is
    // meaningless, and the profile-load stage of activation can't reproduce
    // the absolute anyway.
    if (outConvertedCount)
        *outConvertedCount = 0;
    QSqlQuery select(db);
    if (!select.exec("SELECT id, name, profile_title, profile_json, "
                     "COALESCE(temp_override_c, 0) FROM recipes "
                     "WHERE temp_offset_c IS NULL")) {
        qWarning() << "RecipeStorage: temp-offset conversion select failed:"
                   << select.lastError().text();
        return false;
    }

    struct Row { qint64 id; double offset; };
    QVector<Row> rows;
    while (select.next()) {
        const qint64 id = select.value(0).toLongLong();
        const QString name = select.value(1).toString();
        const QString title = select.value(2).toString();
        const QString json = select.value(3).toString();
        const double legacyAbs = select.value(4).toDouble();

        double offset = 0;
        // Profile-less rows (hot-water tea) legitimately have no anchor and
        // their legacy absolute was never applied at activation — drop the
        // pin quietly rather than warning about resolving "".
        if (legacyAbs > 0 && !title.isEmpty()) {
            double profileTemp = profileTempsByTitle.value(title, 0);
            if (profileTemp <= 0 && !json.isEmpty()) {
                QJsonParseError parseError;
                const QJsonObject o =
                    QJsonDocument::fromJson(json.toUtf8(), &parseError).object();
                if (parseError.error != QJsonParseError::NoError)
                    qWarning() << "RecipeStorage: temp-offset conversion - recipe" << name
                               << "has malformed embedded profile JSON:"
                               << parseError.errorString();
                profileTemp = o.value(QStringLiteral("espresso_temperature")).toString().toDouble();
                if (profileTemp <= 0)
                    profileTemp = o.value(QStringLiteral("espresso_temperature")).toDouble();
            }
            if (profileTemp > 0) {
                offset = legacyAbs - profileTemp;
                if (qAbs(offset) < 0.05)
                    offset = 0;
            } else {
                qWarning() << "RecipeStorage: temp-offset conversion could not resolve profile"
                           << title << "for recipe" << name << "- dropping its temperature pin";
            }
        }
        rows.append({id, offset});
    }

    for (const Row& row : rows) {
        QSqlQuery update(db);
        update.prepare("UPDATE recipes SET temp_offset_c = :offset WHERE id = :id");
        update.bindValue(":offset", row.offset);
        update.bindValue(":id", row.id);
        if (!update.exec()) {
            qWarning() << "RecipeStorage: temp-offset conversion update failed:"
                       << update.lastError().text();
            return false;
        }
        if (outConvertedCount)
            ++(*outConvertedCount);
    }
    if (!rows.isEmpty())
        qDebug() << "RecipeStorage: temp-offset conversion -" << rows.size()
                 << "recipes converted to relative offsets";
    return true;
}

void RecipeStorage::requestLegacyTempOffsetConversion(const QHash<QString, double>& profileTempsByTitle)
{
    auto convertedCount = std::make_shared<int>(0);
    runAsync("recipes_temp_offset",
        [profileTempsByTitle, convertedCount](QSqlDatabase& db) {
            // Existence probe first: a completed no-op pass would otherwise
            // report success and emit a spurious recipesChanged() (QML
            // inventory refresh) on every launch — the probe keeps the
            // steady state signal-free. Its own exec failing is NOT "nothing
            // to convert": most plausibly the column is missing (migration 31
            // failed and will retry), and staying silent here would hide that
            // on every launch.
            QSqlQuery probe(db);
            if (!probe.exec("SELECT 1 FROM recipes WHERE temp_offset_c IS NULL LIMIT 1")) {
                qWarning() << "RecipeStorage: temp-offset conversion probe failed"
                              "(temp_offset_c column missing? migration 31 will retry):"
                           << probe.lastError().text();
                return;
            }
            if (!probe.next())
                return;
            convertLegacyTempOffsetsStatic(db, profileTempsByTitle, convertedCount.get());
        },
        [this, convertedCount](bool) {
            // Emit for the rows that DID convert even when the pass then hit
            // an update failure — their cards should refresh now; the
            // remaining NULL rows retry on the next launch.
            if (*convertedCount > 0)
                emit recipesChanged();
        });
}

// static
QVector<qint64> RecipeStorage::relinkForFinishedBagStatic(QSqlDatabase& db, qint64 finishedBagId,
                                                          qint64* outTargetBagId)
{
    QVector<qint64> moved;
    if (outTargetBagId)
        *outTargetBagId = -1;
    const CoffeeBag finished = CoffeeBagStorage::loadBagStatic(db, finishedBagId);
    if (!finished.isValid())
        return moved;
    if (finished.beanBaseId.isEmpty() && finished.roasterName.isEmpty()
        && finished.coffeeName.isEmpty())
        return moved;   // no identity, nothing to match a successor against

    // Successor: the newest open bag of the same bean identity — canonical
    // id first, else case-insensitive roaster+coffee (the resolver's
    // matching order). "Newest" = most recently added (id DESC), so a roll
    // lands on the freshest bag, not the most recently touched one.
    qint64 targetBagId = -1;
    if (!finished.beanBaseId.isEmpty()) {
        QSqlQuery query(db);
        query.prepare("SELECT id FROM coffee_bags WHERE beanbase_id = :bb "
                      "AND in_inventory = 1 AND id <> :self ORDER BY id DESC LIMIT 1");
        query.bindValue(":bb", finished.beanBaseId);
        query.bindValue(":self", finishedBagId);
        if (!query.exec())
            qWarning() << "RecipeStorage: roll successor canonical query failed:"
                       << query.lastError().text();
        else if (query.next())
            targetBagId = query.value(0).toLongLong();
    }
    if (targetBagId <= 0) {
        QSqlQuery query(db);
        query.prepare("SELECT id FROM coffee_bags WHERE in_inventory = 1 AND id <> :self "
                      "AND LOWER(COALESCE(roaster_name,'')) = LOWER(COALESCE(:roaster,'')) "
                      "AND LOWER(COALESCE(coffee_name,'')) = LOWER(COALESCE(:coffee,'')) "
                      "ORDER BY id DESC LIMIT 1");
        query.bindValue(":self", finishedBagId);
        query.bindValue(":roaster", finished.roasterName);
        query.bindValue(":coffee", finished.coffeeName);
        if (!query.exec())
            qWarning() << "RecipeStorage: roll successor identity query failed:"
                       << query.lastError().text();
        else if (query.next())
            targetBagId = query.value(0).toLongLong();
    }
    if (targetBagId <= 0)
        return moved;   // no successor: recipes keep the finished link (stale)
    if (outTargetBagId)
        *outTargetBagId = targetBagId;

    // The finished bag's non-archived recipes, one at a time so an earlier
    // move counts against a later twin's dup-guard.
    QVector<Recipe> candidates;
    {
        QSqlQuery query(db);
        query.prepare(QString("SELECT %1 FROM recipes WHERE bag_id = :bag AND archived = 0 "
                              "ORDER BY last_used DESC, id DESC").arg(recipeColumnList()));
        query.bindValue(":bag", finishedBagId);
        if (!query.exec()) {
            qWarning() << "RecipeStorage: roll-on-finish candidate query failed:"
                       << query.lastError().text();
            return moved;
        }
        while (query.next())
            candidates.append(recipeFromQueryRow(query));
    }
    for (const Recipe& recipe : candidates) {
        if (wouldDuplicateOnBag(db, recipe, targetBagId))
            continue;   // deliberate comparison pair — stays on the finished bag
        if (updateRecipeFieldsStatic(db, recipe.id, {{QStringLiteral("bagId"), targetBagId}}))
            moved.append(recipe.id);
    }
    return moved;
}

// static
QVector<qint64> RecipeStorage::relinkForRestockedBagStatic(QSqlDatabase& db, qint64 newBagId)
{
    QVector<qint64> moved;
    const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, newBagId);
    if (!bag.isValid() || !bag.inInventory)
        return moved;
    if (bag.beanBaseId.isEmpty() && bag.roasterName.isEmpty() && bag.coffeeName.isEmpty())
        return moved;

    // Stale candidates: non-archived recipes with a bean identity whose
    // linked bag is gone or out of inventory (bag_id NULL covers recipes the
    // migration could not resolve). MRU-first so twin recipes wake one at a
    // time — the first claims the (profile, drink type) slot, the dup-guard
    // holds the rest back.
    QVector<Recipe> candidates;
    {
        QSqlQuery query(db);
        query.prepare(QString(
            "SELECT %1 FROM recipes WHERE archived = 0 "
            "AND (COALESCE(beanbase_id,'') <> '' OR COALESCE(roaster_name,'') <> '' "
            "     OR COALESCE(coffee_name,'') <> '') "
            "AND (bag_id IS NULL OR NOT EXISTS (SELECT 1 FROM coffee_bags b "
            "     WHERE b.id = recipes.bag_id AND b.in_inventory = 1)) "
            "ORDER BY last_used DESC, id DESC").arg(recipeColumnList()));
        if (!query.exec()) {
            qWarning() << "RecipeStorage: wake-on-restock candidate query failed:"
                       << query.lastError().text();
            return moved;
        }
        while (query.next())
            candidates.append(recipeFromQueryRow(query));
    }
    for (const Recipe& recipe : candidates) {
        const bool canonicalMatch = !recipe.beanBaseId.isEmpty()
            && recipe.beanBaseId == bag.beanBaseId;
        const bool identityMatch = (!recipe.roasterName.isEmpty() || !recipe.coffeeName.isEmpty())
            && recipe.roasterName.compare(bag.roasterName, Qt::CaseInsensitive) == 0
            && recipe.coffeeName.compare(bag.coffeeName, Qt::CaseInsensitive) == 0;
        if (!canonicalMatch && !identityMatch)
            continue;
        if (wouldDuplicateOnBag(db, recipe, newBagId))
            continue;
        if (updateRecipeFieldsStatic(db, recipe.id, {{QStringLiteral("bagId"), newBagId}}))
            moved.append(recipe.id);
    }
    return moved;
}

void RecipeStorage::requestRelinkForFinishedBag(qint64 finishedBagId)
{
    auto movedIds = std::make_shared<QVariantList>();
    auto targetBagId = std::make_shared<qint64>(-1);
    auto targetName = std::make_shared<QString>();
    runAsync("recipes_roll",
        [finishedBagId, movedIds, targetBagId, targetName](QSqlDatabase& db) {
            qint64 target = -1;
            const QVector<qint64> moved = relinkForFinishedBagStatic(db, finishedBagId, &target);
            *targetBagId = target;
            for (qint64 id : moved)
                movedIds->append(id);
            if (!moved.isEmpty())
                *targetName = bagDisplayName(CoffeeBagStorage::loadBagStatic(db, target));
        },
        [this, movedIds, targetBagId, targetName](bool) {
            if (movedIds->isEmpty())
                return;
            emit recipesRelinked(*movedIds, *targetBagId, *targetName);
            emit recipesChanged();
        });
}

void RecipeStorage::requestRelinkForRestockedBag(qint64 newBagId)
{
    auto movedIds = std::make_shared<QVariantList>();
    auto targetName = std::make_shared<QString>();
    runAsync("recipes_wake",
        [newBagId, movedIds, targetName](QSqlDatabase& db) {
            const QVector<qint64> moved = relinkForRestockedBagStatic(db, newBagId);
            for (qint64 id : moved)
                movedIds->append(id);
            if (!moved.isEmpty())
                *targetName = bagDisplayName(CoffeeBagStorage::loadBagStatic(db, newBagId));
        },
        [this, newBagId, movedIds, targetName](bool) {
            if (movedIds->isEmpty())
                return;
            emit recipesRelinked(*movedIds, newBagId, *targetName);
            emit recipesChanged();
        });
}

void RecipeStorage::requestRelinkRecipeToBag(qint64 recipeId, qint64 bagId)
{
    // The manual re-point is just a bag-link update: requestUpdateRecipe
    // adopts the chosen bag's bean identity (the user may re-point to a
    // different bean entirely) and validates the resulting row. Grind
    // is untouched by construction — it is not in the
    // patch, and the relink rule says a re-point never rewrites grind.
    requestUpdateRecipe(recipeId, {{QStringLiteral("bagId"), bagId}});
}

bool RecipeStorage::importRecipesStatic(QSqlDatabase& srcDb, QSqlDatabase& destDb, bool merge,
                                        QHash<qint64, qint64>& outIdMap,
                                        const QHash<qint64, qint64>& packageIdMap,
                                        const QHash<qint64, qint64>& bagIdMap)
{
    // Pre-migration-25 source: no recipes table, nothing to import.
    {
        QSqlQuery srcCheck(srcDb);
        if (!srcCheck.exec("SELECT COUNT(*) FROM recipes"))
            return true;
    }

    if (!merge) {
        QSqlQuery clearQuery(destDb);
        if (!clearQuery.exec("DELETE FROM recipes")) {
            qWarning() << "RecipeStorage: failed to clear recipes for replace import:"
                       << clearQuery.lastError().text();
            return false;
        }
    }

    // Build the source SELECT tolerantly: a backup from an older version lacks
    // newer columns (rpm_pinned pre-26, hot_water_json pre-27), and naming a
    // missing column fails the whole SELECT — so missing ones are substituted
    // with NULL, keeping recipeFromQueryRow's positional read aligned with
    // kCols while absent fields land on the struct defaults. Mirrors
    // CoffeeBagStorage::importBagsStatic.
    QSet<QString> srcColumns;
    {
        QSqlQuery info(srcDb);
        if (!info.exec("PRAGMA table_info(recipes)")) {
            // Fatal: an empty column set would substitute NULL for EVERY column,
            // repopulating a cleared table with nameless all-NULL recipes while
            // reporting success.
            qWarning() << "RecipeStorage: failed to read source recipe schema:"
                       << info.lastError().text();
            return false;
        }
        while (info.next())
            srcColumns.insert(info.value(1).toString());
    }
    if (!srcColumns.contains(QStringLiteral("id"))) {
        qWarning() << "RecipeStorage: source recipes schema has no id column - aborting import";
        return false;
    }
    QStringList selectCols;
    for (const QString& col : recipeColumnList().split(QStringLiteral(", ")))
        selectCols << (srcColumns.contains(col)
                           ? col
                           : QStringLiteral("NULL AS %1").arg(col));

    // Unconverted temperature rows (recipe-relative-temp-offset): a source
    // row still holding an ABSOLUTE temp_override_c with no converted offset
    // must arrive as "unconverted" — the absolute staged into the
    // destination's dead temp_override_c and temp_offset_c = NULL, so the
    // deferred conversion pass finishes the job. Two shapes qualify:
    //   • a pre-31 source (no temp_offset_c column at all) — every row;
    //   • a ≥31 source whose OWN deferred pass hadn't completed when the
    //     backup was taken — exactly the rows where temp_offset_c IS NULL
    //     (readDbl would otherwise flatten the marker to an explicit 0 and
    //     silently destroy the still-recoverable pin).
    // The per-row NULL check is load-bearing in the other direction too: a
    // CONVERTED row carries a correct temp_offset_c AND a stale absolute in
    // its dead column — reconverting from that would resurrect an offset the
    // user has since changed or cleared, so converted rows import their
    // offset verbatim and their dead column is ignored.
    const bool srcHasOverrideCol = srcColumns.contains(QStringLiteral("temp_override_c"));
    const bool srcHasOffsetCol = srcColumns.contains(QStringLiteral("temp_offset_c"));
    QString trailingCols;
    int nextTrailingIdx = kColCount;
    if (srcHasOverrideCol) {
        trailingCols += QStringLiteral(", COALESCE(temp_override_c, 0)");
        ++nextTrailingIdx;
    }
    if (srcHasOverrideCol && srcHasOffsetCol) {
        trailingCols += QStringLiteral(", temp_offset_c IS NULL");
        ++nextTrailingIdx;
    }
    // Yield-spec conversion for pre-34 sources (add-yield-ratio-anchor): a
    // source with yield_g but no yield_mode column converts on import —
    // yield_g > 0 becomes {yield_g, absolute}, else the struct's default
    // "none" — producing the same specs the local migration would have.
    // Unlike temperature the conversion needs no external anchor, so it runs
    // inline on the struct rather than staging for a deferred pass. A ≥34
    // source imports its spec verbatim through kCols and its dead yield_g is
    // ignored — reconverting would resurrect a yield the user has since
    // changed to a ratio or cleared.
    const bool srcNeedsYieldConversion = !srcColumns.contains(QStringLiteral("yield_mode"))
        && srcColumns.contains(QStringLiteral("yield_g"));
    const int yieldTrailingIdx = nextTrailingIdx;
    if (srcNeedsYieldConversion)
        trailingCols += QStringLiteral(", COALESCE(yield_g, 0)");
    QString selectSql = QString("SELECT %1%2 FROM recipes")
        .arg(selectCols.join(QStringLiteral(", ")), trailingCols);

    QSqlQuery srcRecipes(srcDb);
    if (!srcRecipes.exec(selectSql)) {
        qWarning() << "RecipeStorage: failed to query source recipes:" << srcRecipes.lastError().text();
        return false;
    }

    int imported = 0, matched = 0;
    QVector<qint64> insertedIds;  // scope for the post-import grind backfill
    while (srcRecipes.next()) {
        Recipe recipe = recipeFromQueryRow(srcRecipes);
        const double legacyAbsTemp = srcHasOverrideCol
            ? srcRecipes.value(kColCount).toDouble() : 0.0;
        const bool rowUnconverted = srcHasOverrideCol
            && (!srcHasOffsetCol || srcRecipes.value(kColCount + 1).toBool());
        // Remap the source equipment_id to the imported package's new dest id
        // (EquipmentStorage::importEquipmentStatic ran first). A source id
        // absent from the map (older source with no equipment tables, or an
        // unmatched package) becomes 0 -> NULL, same as bags. The bag link is
        // remapped through the bag id-map the same way (importBagsStatic ran
        // first); an absent source bag becomes NULL → the stale display
        // state. The bean identity fields carry verbatim — they are the
        // relink matching key, so wake-on-restock can later re-home the
        // recipe.
        recipe.equipmentId = packageIdMap.value(recipe.equipmentId, 0);
        recipe.bagId = bagIdMap.value(recipe.bagId, 0);

        if (srcNeedsYieldConversion) {
            const double legacyYieldG = srcRecipes.value(yieldTrailingIdx).toDouble();
            if (legacyYieldG > 0) {
                recipe.yieldValue = legacyYieldG;
                recipe.yieldMode = YieldSpec::modeAbsolute();
            }
        }

        qint64 destId = -1;
        if (merge) {
            // Identity: case-insensitive name + profile_title + bean link
            // (canonical id when present, else roaster+coffee). A bean-less
            // recipe matches on empty bean fields.
            // COALESCE the bound params too: an empty QString can bind as SQL
            // NULL, and `COALESCE(col,'') = NULL` is never true — that would
            // stop bean-less recipes (all-empty bean fields) from ever deduping.
            QSqlQuery dupQuery(destDb);
            dupQuery.prepare(
                "SELECT id FROM recipes WHERE "
                "LOWER(COALESCE(name,'')) = LOWER(COALESCE(:name,'')) AND "
                "LOWER(COALESCE(profile_title,'')) = LOWER(COALESCE(:profile,'')) AND "
                "COALESCE(beanbase_id,'') = COALESCE(:beanbase,'') AND "
                "LOWER(COALESCE(roaster_name,'')) = LOWER(COALESCE(:roaster,'')) AND "
                "LOWER(COALESCE(coffee_name,'')) = LOWER(COALESCE(:coffee,'')) LIMIT 1");
            dupQuery.bindValue(":name", recipe.name);
            dupQuery.bindValue(":profile", recipe.profileTitle);
            dupQuery.bindValue(":beanbase", recipe.beanBaseId);
            dupQuery.bindValue(":roaster", recipe.roasterName);
            dupQuery.bindValue(":coffee", recipe.coffeeName);
            if (dupQuery.exec() && dupQuery.next()) {
                destId = dupQuery.value(0).toLongLong();
                matched++;
            }
        }

        if (destId < 0) {
            destId = insertRecipeStatic(destDb, recipe);
            if (destId < 0)
                return false;
            imported++;
            insertedIds.append(destId);
            if (rowUnconverted && legacyAbsTemp > 0) {
                // Stage the legacy absolute + the unconverted marker; the
                // deferred pass (convertLegacyTempOffsetsStatic) turns it into
                // an offset once the caller re-triggers conversion.
                QSqlQuery stage(destDb);
                stage.prepare("UPDATE recipes SET temp_offset_c = NULL, "
                              "temp_override_c = :abs WHERE id = :id");
                stage.bindValue(":abs", legacyAbsTemp);
                stage.bindValue(":id", destId);
                if (!stage.exec()) {
                    qWarning() << "RecipeStorage: failed to stage legacy temp for import:"
                               << stage.lastError().text();
                    return false;
                }
            }
        }
        outIdMap.insert(recipe.id, destId);
    }

    // A source DB from before migration 30 carries inherit-mode rows (empty
    // grind_pinned + a bag link) that arrive AFTER this device's migration
    // already ran — run the same backfill, scoped to the rows THIS import
    // inserted, so they adopt their (remapped) bag's dial like local rows did
    // at upgrade. Scoping is load-bearing: a pre-existing local recipe with a
    // deliberately cleared grind must not have its bag's dial stamped back by
    // an unrelated import (post-migration, empty grind is a supported state).
    if (!RecipeStorage::migrateGrindOwnershipStatic(destDb, &insertedIds))
        qWarning() << "RecipeStorage: post-import grind-ownership backfill failed"
                   << "- imported inherit-mode recipes stay grind-less (a valid state; "
                      "editing the recipe sets one)";

    qDebug() << "RecipeStorage: recipe import -" << imported << "imported," << matched << "matched existing";
    return true;
}
