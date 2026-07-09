#include "recipestorage.h"
#include "coffeebagstorage.h"
#include "core/dbutils.h"

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
// for rpm_pinned, migration 27 for hot_water_json, and migration 28 for
// drink_type) is the only thing not generated from it — adding a column is a
// row here PLUS a schema/migration edit there.
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
template<auto M> QVariant bindBool (const Recipe& r) { static_assert(std::is_same_v<RecipeMemberT<M>, bool>);    return (r.*M) ? 1 : 0; }
template<auto M> QVariant bindEpoch(const Recipe& r) { static_assert(std::is_same_v<RecipeMemberT<M>, qint64>);  return (r.*M) > 0 ? QVariant(r.*M) : QVariant(); }

template<auto M> QVariant getMember(const Recipe& r) { return QVariant::fromValue(r.*M); }
template<auto M> void setStr (Recipe& r, const QVariant& v) { static_assert(std::is_same_v<RecipeMemberT<M>, QString>); r.*M = v.toString(); }
template<auto M> void setDbl (Recipe& r, const QVariant& v) { static_assert(std::is_same_v<RecipeMemberT<M>, double>);  r.*M = v.toDouble(); }
template<auto M> void setI64 (Recipe& r, const QVariant& v) { static_assert(std::is_same_v<RecipeMemberT<M>, qint64>);  r.*M = v.toLongLong(); }
template<auto M> void setBool(Recipe& r, const QVariant& v) { static_assert(std::is_same_v<RecipeMemberT<M>, bool>);    r.*M = v.toBool(); }

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
    COL_STR  ("beanbase_id",           beanBaseId),
    COL_STR  ("roaster_name",          roasterName),
    COL_STR  ("coffee_name",           coffeeName),
    COL_EPOCH("equipment_id",          equipmentId),
    COL_DBL  ("dose_g",                doseG),
    COL_DBL  ("yield_g",               yieldG),
    COL_DBL  ("temp_override_c",       tempOverrideC),
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
                // shotCount is an inventory-only aggregate, not a Recipe
                // field — inject it into the map the QML card reads.
                QVariantMap map = entry.recipe.toVariantMap();
                map.insert(QStringLiteral("shotCount"), entry.shotCount);
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
            *bagId = resolveOpenBagStatic(db, r);
            if (*bagId > 0) {
                const CoffeeBag resolved = CoffeeBagStorage::loadBagStatic(db, *bagId);
                if (resolved.isValid())
                    *bag = resolved.toVariantMap();
                else
                    *bagId = -1;
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
    // Guarantee a terminal recipeCreated even when uninitialized — runAsync
    // silently drops the job on an empty dbPath, which would leave MCP/web
    // one-shot listeners hanging forever (see requestUpdateRecipe).
    if (m_dbPath.isEmpty()) {
        qWarning() << "RecipeStorage: requestCreateRecipe on uninitialized storage";
        emit recipeCreated(-1, QVariantMap());
        return;
    }
    auto newId = std::make_shared<qint64>(-1);
    auto created = std::make_shared<QVariantMap>();
    runAsync("recipes_create",
        [recipeMap, newId, created](QSqlDatabase& db) {
            Recipe recipe = Recipe::fromVariantMap(recipeMap);
            recipe.lastUsedEpoch = QDateTime::currentSecsSinceEpoch();
            *newId = insertRecipeStatic(db, recipe);
            if (*newId > 0)
                *created = loadRecipeStatic(db, *newId).toVariantMap();
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
    runAsync("recipes_update",
        [recipeId, fields = std::move(patch), hintedBev, success](QSqlDatabase& db) {
            *success = updateRecipeFieldsStatic(db, recipeId, fields);
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
            if (*success && touchesBlocks && !fields.contains(QStringLiteral("drinkType"))) {
                const Recipe updated = loadRecipeStatic(db, recipeId);
                if (updated.isValid()) {
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
                    updateRecipeFieldsStatic(db, recipeId,
                        {{QStringLiteral("drinkType"), Recipe::deriveDrinkType(updated, bev)}});
                }
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

void RecipeStorage::requestCloneRecipe(qint64 sourceId, const QString& newName)
{
    if (m_dbPath.isEmpty()) {
        qWarning() << "RecipeStorage: requestCloneRecipe on uninitialized storage";
        emit recipeCreated(-1, QVariantMap());
        return;
    }
    auto newId = std::make_shared<qint64>(-1);
    auto created = std::make_shared<QVariantMap>();
    runAsync("recipes_clone",
        [sourceId, newName, newId, created](QSqlDatabase& db) {
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
            if (*newId > 0)
                *created = loadRecipeStatic(db, *newId).toVariantMap();
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
            beanbase_id TEXT,
            roaster_name TEXT,
            coffee_name TEXT,
            equipment_id INTEGER,
            dose_g REAL,
            yield_g REAL,
            temp_override_c REAL,
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
    // history (archive only).
    query.prepare(QString("SELECT %1, "
                          "(SELECT COUNT(*) FROM shots WHERE recipe_id = recipes.id) AS shot_count "
                          "FROM recipes WHERE archived = :archived "
                          "ORDER BY last_used DESC, id DESC").arg(recipeColumnList()));
    query.bindValue(":archived", archived ? 1 : 0);
    if (!query.exec()) {
        qWarning() << "RecipeStorage: inventory query failed:" << query.lastError().text();
        return recipes;
    }
    // Read shot_count by its alias, not a hardcoded position.
    const int shotCountCol = query.record().indexOf("shot_count");
    while (query.next()) {
        InventoryRecipe entry;
        entry.recipe = recipeFromQueryRow(query);
        if (shotCountCol >= 0)
            entry.shotCount = query.value(shotCountCol).toLongLong();
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

qint64 RecipeStorage::resolveOpenBagStatic(QSqlDatabase& db, const Recipe& recipe)
{
    // Canonical link first: the strongest identity, survives bag replacement.
    if (!recipe.beanBaseId.isEmpty()) {
        QSqlQuery query(db);
        query.prepare("SELECT id FROM coffee_bags WHERE beanbase_id = :bb "
                      "AND in_inventory = 1 ORDER BY last_used DESC, id DESC LIMIT 1");
        query.bindValue(":bb", recipe.beanBaseId);
        if (query.exec() && query.next())
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
        if (query.exec() && query.next())
            return query.value(0).toLongLong();
    }
    return -1;
}

bool RecipeStorage::importRecipesStatic(QSqlDatabase& srcDb, QSqlDatabase& destDb, bool merge,
                                        QHash<qint64, qint64>& outIdMap,
                                        const QHash<qint64, qint64>& packageIdMap)
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

    QSqlQuery srcRecipes(srcDb);
    if (!srcRecipes.exec(QString("SELECT %1 FROM recipes").arg(selectCols.join(QStringLiteral(", "))))) {
        qWarning() << "RecipeStorage: failed to query source recipes:" << srcRecipes.lastError().text();
        return false;
    }

    int imported = 0, matched = 0;
    while (srcRecipes.next()) {
        Recipe recipe = recipeFromQueryRow(srcRecipes);
        // Remap the source equipment_id to the imported package's new dest id
        // (EquipmentStorage::importEquipmentStatic ran first). A source id
        // absent from the map (older source with no equipment tables, or an
        // unmatched package) becomes 0 -> NULL, same as bags. The bean link is
        // bean-level, not a bag id, so it carries verbatim and resolves at
        // activation.
        recipe.equipmentId = packageIdMap.value(recipe.equipmentId, 0);

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
        }
        outIdMap.insert(recipe.id, destId);
    }

    qDebug() << "RecipeStorage: recipe import -" << imported << "imported," << matched << "matched existing";
    return true;
}
