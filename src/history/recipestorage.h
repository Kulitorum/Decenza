#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

class QSqlDatabase;
class SerialDbWorker;

// A recipe: a named whole-drink specification (openspec change add-recipes).
// A profile tells the machine how to push water; a recipe holds the drink —
// profile, bean link, equipment, dose/yield/temp, grind routing, and steam.
// Recipes follow the optionality ladder: only name + profile are required;
// bean and equipment links are optional and a recipe works with whatever
// rungs exist.
//
// Grind: a recipe with an empty grindPinned INHERITS the linked bean's
// current bag grind (resolution happens at activation, not here); a non-empty
// grindPinned is this recipe's own opaque grind text. The pin covers rpm as
// well (rpmPinned) — grind and rpm are pinned together. Bean-less recipes keep
// grind/rpm on the recipe by construction (there is nothing to inherit from).
//
// Steam: steamJson holds the drink's steam block, serialized as
// {hasMilk, milkWeightG, pitcherName, durationSec, flow, temperatureC}. The
// pitcher is snapshotted BY VALUE (name + duration/flow/temperature) — never a
// preset-list reference — per the Bean Base snapshot-not-reference rule.
//
// Hot water: hotWaterJson holds the drink's added-hot-water block (for an
// Americano), serialized as {hasWater, vesselName, volume, mode, flowRate,
// temperatureC, order}. order is "after" (americano, default) or "before"
// (long black). Hot water is opt-in — hasWater toggles it on and the user
// selects a water vessel; the vessel's own values ride the recipe. Like
// the steam pitcher the vessel is snapshotted BY VALUE (never a preset-list
// reference); activation re-selects it by name and recreates it from the
// snapshot if the preset was deleted. There is no separate per-recipe amount.
struct Recipe {
    qint64 id = 0;

    QString name;          // required
    QString profileTitle;  // resolved by title at activation; may be empty ONLY
                           // for a hot-water-only recipe (hasWater in the block)
    QString profileJson;   // fallback when the titled profile is not installed

    // Drink type (add-recipe-wizard-tea): user intent recorded by the wizard —
    // "espresso" | "filter" | "americano" | "long_black" | "latte" | "tea" |
    // "tea_hotwater". Presentation only: activation and machine behavior read
    // the blocks, never this. Empty (legacy rows) = derive via deriveDrinkType.
    QString drinkType;

    // Bean link (bean-level, NOT bag-level): canonical Bean Base id when
    // available, else roaster+coffee identity. Resolves to the current open
    // bag at activation; empty = no bean linked.
    QString beanBaseId;
    QString roasterName;
    QString coffeeName;

    qint64 equipmentId = 0;   // FK -> equipment_packages.id; 0 = none

    double doseG = 0;         // 0 = unset
    double yieldG = 0;        // 0 = unset
    double tempOverrideC = 0; // 0 = no override

    QString grindPinned;      // empty = inherit from bean's current bag
    qint64 rpmPinned = 0;     // grinder rpm override; only meaningful with a pin (0 = unset)
    QString steamJson;        // steam block, empty = none saved
    QString hotWaterJson;     // hot-water block (opt-in water-vessel snapshot), empty = none

    bool archived = false;

    // Provenance (0 = none): the golden shot this recipe was promoted from,
    // or the recipe it was cloned from. Clones do NOT copy the source's
    // shot link — a clone hasn't earned a golden shot yet.
    qint64 createdFromShotId = 0;
    qint64 clonedFromRecipeId = 0;

    qint64 lastUsedEpoch = 0; // bumped on activation and shot save (MRU)

    bool isValid() const { return id > 0; }
    QVariantMap toVariantMap() const;
    static Recipe fromVariantMap(const QVariantMap& map);

    // True when the hot-water block JSON is present with hasWater set — the
    // condition under which a recipe may be profile-less. Shared by the save
    // validation on every surface (wizard, MCP, web).
    static bool hotWaterActive(const QString& hotWaterJson);

    // The drink-type vocabulary. MCP schema enums are advisory (the registry
    // doesn't enforce them) and the web API accepts free text, so write
    // surfaces MUST gate on this — a stored "Tea" looks valid everywhere but
    // misses every exact-match consumer (wizard template lookup, equipment
    // default query, and the stored-tea protection in the update
    // re-derivation, which would then re-derive it to "latte" on a steam
    // stamp).
    static bool isKnownDrinkType(const QString& drinkType) {
        static const QStringList kDrinkTypes = {
            QStringLiteral("espresso"), QStringLiteral("filter"),
            QStringLiteral("americano"), QStringLiteral("long_black"),
            QStringLiteral("latte"), QStringLiteral("tea"),
            QStringLiteral("tea_hotwater")};
        return kDrinkTypes.contains(drinkType);
    }

    // The one save-validation rule every surface enforces: a name, and a
    // profile unless the recipe is hot-water-only. QML reaches it through
    // RecipeStorage::isSaveValid.
    static bool saveValidationPasses(const QString& name, const QString& profileTitle,
                                     const QString& hotWaterJson) {
        return !name.trimmed().isEmpty()
            && (!profileTitle.trimmed().isEmpty() || hotWaterActive(hotWaterJson));
    }

    // Derive the drink type from the blocks + the profile's beverage_type
    // (caller resolves it; pass empty when the profile is unknown). Used for
    // legacy rows without a stored drinkType and for promote-from-shot.
    // Precedence: profile-less + hot water → tea_hotwater; tea_portafilter →
    // tea; milk → latte (milk wins over added water: a flat-white-plus-splash
    // is still a milk drink); hot water by order → americano/long_black;
    // filter/pourover → filter; else espresso.
    static QString deriveDrinkType(const Recipe& recipe, const QString& profileBeverageType);
};

// An inventory row: a recipe plus its shot count. Mirrors InventoryBag — the
// count is a per-query aggregate (subquery on shots.recipe_id) computed only
// by the inventory listing, and drives the delete-vs-archive action
// (0 shots = mistaken creation, trashable; >0 = history, archive only).
struct InventoryRecipe {
    Recipe recipe;
    qint64 shotCount = 0;
};

// SQLite-backed recipe storage in the shot history database (recipes table,
// created by ShotHistoryStorage migration 25). All public request* methods are
// async: DB work runs on a serialized background worker and results are
// delivered back via signals, following the CoffeeBagStorage pattern. The
// *Static helpers are synchronous, take a caller-provided connection, and are
// shared with the migration and unit tests.
class RecipeStorage : public QObject {
    Q_OBJECT

public:
    explicit RecipeStorage(QObject* parent = nullptr);
    ~RecipeStorage();

    // dbPath must be the shot history database (table lives there).
    void initialize(const QString& dbPath);
    QString databasePath() const { return m_dbPath; }

    // Async queries — results via signals (QVariantList of toVariantMap()).
    // QML-visible view of Recipe::saveValidationPasses (the wizard's Save gate).
    Q_INVOKABLE bool isSaveValid(const QString& name, const QString& profileTitle,
                                 const QString& hotWaterJson) const {
        return Recipe::saveValidationPasses(name, profileTitle, hotWaterJson);
    }

    Q_INVOKABLE void requestInventory();                   // archived = false, MRU order
    Q_INVOKABLE void requestArchived();                    // archived = true, MRU order

    // The wizard's per-drink-type equipment default: the package on the most
    // recently used unarchived recipe of this drink type (0 = none; caller
    // falls back to the active package). Emits lastEquipmentForDrinkTypeReady.
    Q_INVOKABLE void requestLastEquipmentForDrinkType(const QString& drinkType);
    static qint64 lastEquipmentForDrinkTypeStatic(QSqlDatabase& db, const QString& drinkType);
    Q_INVOKABLE void requestRecipe(qint64 recipeId);       // recipeReady()
    // Activation bundle: the recipe row, its resolved open bag id, and that
    // bag's full map, loaded in ONE background pass so activation applies a
    // consistent snapshot. openBagId is -1 (and openBag empty) when the
    // recipe has no bean link or no open bag of the bean exists.
    Q_INVOKABLE void requestRecipeForActivation(qint64 recipeId);  // recipeActivationReady()

    // Async writes — all emit recipesChanged() on success.
    Q_INVOKABLE void requestCreateRecipe(const QVariantMap& recipe);        // recipeCreated()
    Q_INVOKABLE void requestUpdateRecipe(qint64 recipeId, const QVariantMap& fields); // recipeUpdated()
    // Clone: copies every field except id, provenance (clonedFromRecipeId =
    // source id, createdFromShotId cleared) and lastUsed; the copy is named
    // by the caller. Emits recipeCreated() with the new row.
    Q_INVOKABLE void requestCloneRecipe(qint64 sourceId, const QString& newName);
    Q_INVOKABLE void requestArchiveRecipe(qint64 recipeId);                 // recipeUpdated()
    Q_INVOKABLE void requestUnarchiveRecipe(qint64 recipeId);               // recipeUpdated()
    Q_INVOKABLE void requestTouchLastUsed(qint64 recipeId);                 // bump MRU (no signal)
    // Deletes only when no shot references the recipe (shots.recipe_id count
    // = 0); emits recipeDeleted(recipeId, success) — false when shots exist.
    Q_INVOKABLE void requestDeleteRecipe(qint64 recipeId);

    // --- Synchronous static helpers (caller provides the connection) ---

    // Create the recipes table if missing. Used by migration 25 and tests.
    static bool ensureTableStatic(QSqlDatabase& db);

    static qint64 insertRecipeStatic(QSqlDatabase& db, const Recipe& recipe);
    static Recipe loadRecipeStatic(QSqlDatabase& db, qint64 recipeId);
    static QVector<InventoryRecipe> loadInventoryStatic(QSqlDatabase& db, bool archived = false);
    // Update only the columns named in `fields` (camelCase Recipe keys).
    static bool updateRecipeFieldsStatic(QSqlDatabase& db, qint64 recipeId, const QVariantMap& fields);

    // Resolve the recipe's bean link to the current open bag: beanbase_id
    // match first, else case-insensitive roaster+coffee identity; in-inventory
    // bags only, most recently used first. Returns -1 when the recipe has no
    // bean link or no open bag of that bean exists ("no open bag" is a
    // display state, never an error).
    static qint64 resolveOpenBagStatic(QSqlDatabase& db, const Recipe& recipe);

    // Copy recipes rows from srcDb into destDb (device transfer / backup
    // restore), mirroring CoffeeBagStorage::importBagsStatic. Row ids change on
    // insert — outIdMap records old->new so the caller can remap shots.recipe_id.
    // Merge mode maps a source recipe matching an existing dest recipe
    // (case-insensitive name + profile_title + bean identity) to the existing
    // row instead of inserting a duplicate; replace mode clears dest recipes
    // first. Source DBs from before migration 25 have no recipes table —
    // returns true with an empty map. Runs inside the caller's destDb
    // transaction. packageIdMap remaps each source recipe's equipment_id to the
    // imported package's new dest id (EquipmentStorage::importEquipmentStatic
    // runs first); a source equipment_id absent from the map becomes NULL. The
    // bean link is bean-level (not a bag id) so it carries verbatim and resolves
    // at activation. archived state is preserved so shot provenance never dangles.
    static bool importRecipesStatic(QSqlDatabase& srcDb, QSqlDatabase& destDb, bool merge,
                                    QHash<qint64, qint64>& outIdMap,
                                    const QHash<qint64, qint64>& packageIdMap);

signals:
    void inventoryReady(const QVariantList& recipes);
    void archivedReady(const QVariantList& recipes);
    void lastEquipmentForDrinkTypeReady(const QString& drinkType, qint64 equipmentId);
    void recipeReady(qint64 recipeId, const QVariantMap& recipe); // map empty if not found
    // recipe empty when the id was not found (activation must fail cleanly).
    void recipeActivationReady(qint64 recipeId, const QVariantMap& recipe,
                               qint64 openBagId, const QVariantMap& openBag);
    void recipeCreated(qint64 recipeId, const QVariantMap& recipe); // recipeId -1 on failure
    void recipeUpdated(qint64 recipeId, bool success);
    void recipeDeleted(qint64 recipeId, bool success);
    // Coarse "something changed" signal so views can re-request the inventory.
    void recipesChanged();

private:
    // Run `work(db)` on a background thread, then `done(dbOpened)` on the main
    // thread. Read callers must skip their "Ready" emission when dbOpened is
    // false (open failure → empty result that must not be read as not-found).
    void runAsync(const QString& connPrefix,
                  std::function<void(QSqlDatabase&)> work,
                  std::function<void(bool dbOpened)> done);

    static Recipe recipeFromQueryRow(const class QSqlQuery& query);

    QString m_dbPath;
    std::shared_ptr<std::atomic<bool>> m_destroyed = std::make_shared<std::atomic<bool>>(false);
    std::unique_ptr<SerialDbWorker> m_dbWorker;
};
