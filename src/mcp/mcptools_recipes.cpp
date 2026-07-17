// MCP tools for recipes (add-recipes): full CRUD + activation over the
// whole-drink recipe objects (profile + linked bag + equipment + dose/yield/
// temp + the recipe's own grind + steam block).
//
// Conventions (docs/CLAUDE_MD/MCP_SERVER.md): unit-suffixed field names
// (doseG, yieldG, milkWeightG, steamTemperatureC), ISO 8601 timestamps with
// offset, human-readable enums. Grind always lives on the recipe
// (fix-recipe-grind-integrity): reads expose it as {"value", "rpm"} — omitted
// entirely when the recipe has no grind — with no inherited/pinned mode.
//
// Reads run on background threads over the storage statics; mutations go
// through the RecipeStorage instance (one-shot signal connections) so the
// UI's recipesChanged refreshes fire exactly as they do for in-app edits.
// recipe_activate calls MainController's single activation path — the same
// code the idle pill tap runs.

#include "mcptoolregistry.h"
#include "../controllers/maincontroller.h"
#include "../controllers/profilemanager.h"
#include "../core/dbutils.h"
#include "../core/settings.h"
#include "../core/settings_dye.h"
#include "../core/yieldspec.h"
#include "../history/shothistorystorage.h"
#include "../history/coffeebagstorage.h"
#include "../history/recipestorage.h"
#include "../history/recipepromotion.h"
#include "../network/beanbase_blob.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <optional>

namespace {

// Water preset "flowRate" is stored in tenths of mL/s; the MCP surface uses
// mL/s (flowMlPerSec), matching the water_vessel_* tools (mcptools_presets.cpp).
constexpr double kWaterFlowScale = 10.0;

QString isoFromEpoch(qint64 epochSecs)
{
    if (epochSecs <= 0)
        return QString();
    QDateTime dt = QDateTime::fromSecsSinceEpoch(epochSecs);
    return dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate);
}

// The recipe hot-water block is stored with the water vessel's NATIVE field
// names (volume, flowRate) so it round-trips straight through the vessel preset
// API. The MCP surface, however, uses the house conventions the sibling
// water_vessel_* tools expose (volumeMl, flowMlPerSec = flowRate / 10) so an LLM
// can carry values between the two surfaces without a 10x flow error.
QJsonObject hotWaterBlockToMcp(const QJsonObject& native)
{
    QJsonObject o;
    if (native.contains("hasWater"))    o["hasWater"]    = native["hasWater"];
    if (native.contains("vesselName"))  o["vesselName"]  = native["vesselName"];
    if (native.contains("volume"))      o["volumeMl"]    = native["volume"];
    if (native.contains("mode"))        o["mode"]        = native["mode"];
    if (native.contains("flowRate"))    o["flowMlPerSec"] = native["flowRate"].toDouble() / kWaterFlowScale;
    if (native.contains("temperatureC")) o["temperatureC"] = native["temperatureC"];
    if (native.contains("order"))       o["order"]       = native["order"];
    return o;
}

QJsonObject hotWaterBlockFromMcp(const QJsonObject& mcp)
{
    QJsonObject o;
    if (mcp.contains("hasWater"))     o["hasWater"]   = mcp["hasWater"];
    if (mcp.contains("vesselName"))   o["vesselName"] = mcp["vesselName"];
    if (mcp.contains("volumeMl"))     o["volume"]     = mcp["volumeMl"].toInt();
    if (mcp.contains("mode"))         o["mode"]       = mcp["mode"];
    if (mcp.contains("flowMlPerSec")) o["flowRate"]   = qRound(mcp["flowMlPerSec"].toDouble() * kWaterFlowScale);
    if (mcp.contains("temperatureC")) o["temperatureC"] = mcp["temperatureC"];
    if (mcp.contains("order"))        o["order"]      = mcp["order"];
    return o;
}

// Full recipe JSON. `db` (optional) enables the resolved fields: the linked
// bag's display identity + staleness.
QJsonObject recipeToJson(const Recipe& r, Settings* settings, QSqlDatabase* db,
                         qint64 shotCount = -1,
                         const QHash<QString, QString>& bevByTitle = {})
{
    QJsonObject o;
    o["id"] = r.id;
    o["name"] = r.name;
    o["profileTitle"] = r.profileTitle;
    // Stored drink type; legacy rows (pre-migration-28) derive from the
    // blocks (embedded profile JSON supplies beverage_type when present).
    if (!r.drinkType.isEmpty()) {
        o["drinkType"] = r.drinkType;
    } else {
        QString bev;
        if (!r.profileJson.isEmpty())
            bev = QJsonDocument::fromJson(r.profileJson.toUtf8())
                      .object().value(QStringLiteral("beverage_type")).toString();
        // Installed profiles embed no JSON — the main-thread-captured catalog
        // snapshot supplies their beverage_type (else tea derives as espresso).
        if (bev.isEmpty())
            bev = bevByTitle.value(r.profileTitle.trimmed().toLower());
        o["drinkType"] = Recipe::deriveDrinkType(r, bev);
    }
    if (!r.beanBaseId.isEmpty() || !r.roasterName.isEmpty() || !r.coffeeName.isEmpty()) {
        QJsonObject bean;
        if (!r.beanBaseId.isEmpty()) bean["beanBaseId"] = r.beanBaseId;
        if (!r.roasterName.isEmpty()) bean["roasterName"] = r.roasterName;
        if (!r.coffeeName.isEmpty()) bean["coffeeName"] = r.coffeeName;
        o["bean"] = bean;
    }
    if (r.bagId > 0)
        o["bagId"] = r.bagId;
    if (r.equipmentId > 0)
        o["equipmentId"] = r.equipmentId;
    if (r.doseG > 0) o["doseG"] = r.doseG;
    // Yield spec (add-yield-ratio-anchor): sparse, mutually exclusive keys.
    // An absolute yield emits yieldG (grams), a ratio emits yieldRatio (the
    // dose multiplier — 2.0 means 1:2); mode "none" emits neither.
    if (r.yieldMode == QLatin1String("absolute") && r.yieldValue > 0)
        o["yieldG"] = r.yieldValue;
    else if (r.yieldMode == QLatin1String("ratio") && r.yieldValue > 0)
        o["yieldRatio"] = r.yieldValue;
    // The temperature is a SIGNED OFFSET against the recipe's profile
    // (recipe-relative-temp-offset) — present only when the recipe pins one.
    if (qAbs(r.tempOffsetC) > 0.05) o["tempOffsetC"] = r.tempOffsetC;

    // Grind: the recipe's own value (grind always lives on the recipe —
    // fix-recipe-grind-integrity; the inherited/pinned mode is retired).
    QJsonObject grind;
    if (!r.grindPinned.isEmpty()) {
        grind["value"] = r.grindPinned;
        if (r.rpmPinned > 0)
            grind["rpm"] = r.rpmPinned;
    }
    if (db) {
        // The linked bag (hard link — no MRU resolution): display identity
        // and staleness as a human-readable indication.
        bool bagLoaded = false;
        if (r.bagId > 0) {
            const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(*db, r.bagId);
            if (bag.isValid()) {
                bagLoaded = true;
                QJsonObject bagO;
                if (!bag.roasterName.isEmpty()) bagO["roasterName"] = bag.roasterName;
                if (!bag.coffeeName.isEmpty()) bagO["coffeeName"] = bag.coffeeName;
                if (!bag.roastDate.isEmpty()) bagO["roastDate"] = bag.roastDate;
                bagO["status"] = bag.inInventory ? QStringLiteral("open")
                                                 : QStringLiteral("finished");
                o["bag"] = bagO;
                if (!bag.inInventory)
                    o["bagStale"] = QStringLiteral(
                        "The linked bag is finished (no longer in inventory). The recipe "
                        "still activates fully with its own grind.");
            }
        }
        if (!bagLoaded
            && (!r.beanBaseId.isEmpty() || !r.roasterName.isEmpty() || !r.coffeeName.isEmpty())) {
            o["bagStale"] = QStringLiteral(
                "No bag is linked for this bean (the linked bag was deleted, or no open bag "
                "existed at migration). Adding a NEW bag of this bean relinks the recipe "
                "automatically; the recipe still activates.");
        }
    }
    if (!grind.isEmpty())
        o["grind"] = grind;

    if (!r.steamJson.isEmpty()) {
        const QJsonObject steam = QJsonDocument::fromJson(r.steamJson.toUtf8()).object();
        if (!steam.isEmpty())
            o["steam"] = steam;
    }
    if (!r.hotWaterJson.isEmpty()) {
        const QJsonObject water = QJsonDocument::fromJson(r.hotWaterJson.toUtf8()).object();
        if (!water.isEmpty())
            o["hotWater"] = hotWaterBlockToMcp(water);
    }
    if (r.archived)
        o["archived"] = true;
    if (r.createdFromShotId > 0)
        o["createdFromShotId"] = r.createdFromShotId;
    if (r.clonedFromRecipeId > 0)
        o["clonedFromRecipeId"] = r.clonedFromRecipeId;
    if (shotCount >= 0)
        o["shotCount"] = shotCount;
    const QString lastUsed = isoFromEpoch(r.lastUsedEpoch);
    if (!lastUsed.isEmpty())
        o["lastUsed"] = lastUsed;
    const QString created = isoFromEpoch(r.createdEpoch);
    if (!created.isEmpty())
        o["created"] = created;
    o["isActive"] = settings
        && settings->dye()->activeRecipeId() == static_cast<int>(r.id);
    return o;
}

// Recipe field map from tool arguments (shared by create and update).
// Only keys present in `args` land in the map.
QVariantMap recipeFieldsFromArgs(const QJsonObject& args)
{
    QVariantMap fields;
    static const QStringList kStringKeys = {
        "name", "profileTitle", "beanBaseId", "roasterName", "coffeeName", "grindPinned",
        "drinkType"};
    for (const QString& key : kStringKeys) {
        if (args.contains(key))
            fields.insert(key, args[key].toString());
    }
    if (args.contains("bagId"))
        fields.insert("bagId", args["bagId"].toInteger());
    if (args.contains("equipmentId"))
        fields.insert("equipmentId", args["equipmentId"].toInteger());
    if (args.contains("rpmPinned"))
        fields.insert("rpmPinned", args["rpmPinned"].toInteger());
    if (args.contains("doseG"))
        fields.insert("doseG", args["doseG"].toDouble());
    // Yield spec (add-yield-ratio-anchor): the wire keys are sparse and
    // mutually exclusive — writing one IS setting the mode, which implicitly
    // clears the other (one value column + a discriminator; no stale sibling
    // can survive a partial update). Both-present is rejected loudly in the
    // create/update handlers before this runs. 0/negative clears the anchor.
    if (args.contains("yieldG")) {
        const double g = args["yieldG"].toDouble();
        fields.insert("yieldValue", g > 0 ? g : 0.0);
        fields.insert("yieldMode", g > 0 ? QStringLiteral("absolute") : QStringLiteral("none"));
    } else if (args.contains("yieldRatio")) {
        const double ratio = args["yieldRatio"].toDouble();
        fields.insert("yieldValue", ratio > 0 ? YieldSpec::clampRatio(ratio) : 0.0);
        fields.insert("yieldMode", ratio > 0 ? QStringLiteral("ratio") : QStringLiteral("none"));
    }
    if (args.contains("tempOffsetC"))
        fields.insert("tempOffsetC", args["tempOffsetC"].toDouble());
    if (args.contains("steam"))
        fields.insert("steamJson", QString::fromUtf8(
            QJsonDocument(args["steam"].toObject()).toJson(QJsonDocument::Compact)));
    if (args.contains("hotWater"))
        fields.insert("hotWaterJson", QString::fromUtf8(QJsonDocument(
            hotWaterBlockFromMcp(args["hotWater"].toObject())).toJson(QJsonDocument::Compact)));
    return fields;
}

// The steam block's schema, shared by recipe_create and recipe_update.
QJsonObject steamBlockSchema()
{
    return QJsonObject{
        {"type", "object"},
        {"description", "The drink's steam block. hasMilk drives steam-heater intent on "
                        "activation. The pitcher is snapshotted by value (name + parameters), "
                        "never referenced by preset index."},
        {"properties", QJsonObject{
            {"hasMilk", QJsonObject{{"type", "boolean"}}},
            {"milkWeightG", QJsonObject{{"type", "number"}}},
            {"pitcherName", QJsonObject{{"type", "string"}}},
            {"durationSec", QJsonObject{{"type", "integer"}}},
            {"flow", QJsonObject{{"type", "integer"},
                {"description", "Steam flow in the machine's stored unit (hundredths of mL/s), "
                                "matching the pitcher preset's flow field"}}},
            {"temperatureC", QJsonObject{{"type", "number"}}}
        }}
    };
}

// The hot-water block's schema, shared by recipe_create and recipe_update.
// Hot water is opt-in and the selected water vessel carries the values (there is
// no separate amount) — the block is a by-value vessel snapshot plus the on/off
// flag and a pour order. Stored and returned verbatim, mirroring the steam block.
QJsonObject hotWaterBlockSchema()
{
    return QJsonObject{
        {"type", "object"},
        {"description", "The drink's added-hot-water block (for an Americano or long black). "
                        "hasWater turns it on; the water vessel is snapshotted by value (name + "
                        "parameters), never referenced by preset index. order is the pour intent: "
                        "'before' the espresso (a long black) or 'after' it (an Americano)."},
        {"properties", QJsonObject{
            {"hasWater", QJsonObject{{"type", "boolean"}}},
            {"vesselName", QJsonObject{{"type", "string"}}},
            {"volumeMl", QJsonObject{{"type", "integer"},
                {"description", "Water amount — mL when mode='volume', grams when mode='weight' "
                                "(matches the water_vessel_* tools' volumeMl)"}}},
            {"mode", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"weight", "volume"}}}},
            {"flowMlPerSec", QJsonObject{{"type", "number"},
                {"description", "Hot-water flow in mL/s, matching the water_vessel_* tools' "
                                "flowMlPerSec field"}}},
            {"temperatureC", QJsonObject{{"type", "number"}}},
            {"order", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"before", "after"}},
                {"description", "'before' = water first (long black), 'after' = water last "
                                "(Americano). Defaults to 'after'."}}}
        }}
    };
}

} // namespace

void registerRecipeTools(McpToolRegistry* registry, ShotHistoryStorage* shotHistory,
                         RecipeStorage* recipeStorage, MainController* mainController,
                         Settings* settings)
{
    // recipe_list — the recipe inventory (MRU order, like the idle pills).
    registry->registerAsyncTool(
        "recipe_list",
        "List the user's recipes. A recipe is the whole drink: profile, linked bag, equipment, "
        "dose/yield/temperature, the recipe's own grind, and "
        "steam block. MRU-ordered; the recipe with isActive=true is what the machine is set up "
        "for right now. A bagStale field flags recipes whose linked bag is finished (they still "
        "activate). Pass includeArchived=true to also list archived recipes.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"includeArchived", QJsonObject{{"type", "boolean"}}}
            }}
        },
        [shotHistory, settings, mainController](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            const bool includeArchived = args["includeArchived"].toBool();
            const QString dbPath = shotHistory->databasePath();
            const QHash<QString, QString> bevByTitle =
                (mainController && mainController->profileManager())
                    ? mainController->profileManager()->beverageTypeByTitleSnapshot()
                    : QHash<QString, QString>();
            QThread* thread = QThread::create([dbPath, includeArchived, settings, respond, bevByTitle]() {
                QJsonArray recipes;
                const bool opened = withTempDb(dbPath, "mcp_recipes", [&](QSqlDatabase& db) {
                    const auto addAll = [&](bool archived) {
                        const QVector<InventoryRecipe> inventory =
                            RecipeStorage::loadInventoryStatic(db, archived);
                        for (const InventoryRecipe& entry : inventory)
                            recipes.append(recipeToJson(entry.recipe, settings, &db,
                                                        entry.shotCount, bevByTitle));
                    };
                    addAll(false);
                    if (includeArchived)
                        addAll(true);
                });
                QMetaObject::invokeMethod(qApp, [opened, recipes, respond]() {
                    if (!opened) {
                        respond(QJsonObject{{"error", "Could not open shot database"}});
                        return;
                    }
                    respond(QJsonObject{{"recipes", recipes}, {"count", recipes.size()}});
                }, Qt::QueuedConnection);
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read");

    // recipe_get — one recipe, fully resolved.
    registry->registerAsyncTool(
        "recipe_get",
        "Get one recipe by id, including the recipe's own grind, the linked bag's identity and "
        "status (open / finished — a finished link is flagged via bagStale but never blocks "
        "activation), and the steam block.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"recipeId", QJsonObject{{"type", "integer"}, {"description", "Recipe ID (from recipe_list)"}}}
            }},
            {"required", QJsonArray{"recipeId"}}
        },
        [shotHistory, settings, mainController](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            const qint64 recipeId = args["recipeId"].toInteger();
            const QString dbPath = shotHistory->databasePath();
            const QHash<QString, QString> bevByTitle =
                (mainController && mainController->profileManager())
                    ? mainController->profileManager()->beverageTypeByTitleSnapshot()
                    : QHash<QString, QString>();
            QThread* thread = QThread::create([dbPath, recipeId, settings, respond, bevByTitle]() {
                QJsonObject result;
                bool found = false;
                const bool opened = withTempDb(dbPath, "mcp_recipe_get", [&](QSqlDatabase& db) {
                    const Recipe r = RecipeStorage::loadRecipeStatic(db, recipeId);
                    if (r.isValid()) {
                        result = recipeToJson(r, settings, &db, -1, bevByTitle);
                        found = true;
                    }
                });
                QMetaObject::invokeMethod(qApp, [opened, found, result, recipeId, respond]() {
                    if (!opened)
                        respond(QJsonObject{{"error", "Could not open shot database"}});
                    else if (!found)
                        respond(QJsonObject{{"error", QString("Recipe %1 not found").arg(recipeId)}});
                    else
                        respond(result);
                }, Qt::QueuedConnection);
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read");

    // recipe_create — new recipe from explicit fields.
    registry->registerAsyncTool(
        "recipe_create",
        "Create a recipe. Only name is always required; profileTitle is required unless the "
        "recipe carries a hot-water block with hasWater true (a profile-less hot-water tea). "
        "Bag link, equipment, and every parameter are optional (a recipe works with whatever "
        "the user tracks). Link the specific bag with bagId (from bag_list); passing only bean "
        "identity fields resolves them to that bean's open bag once, at save time. Grind lives "
        "on the recipe: OMIT grindPinned to adopt the linked bag's current dial as this "
        "recipe's own starting grind (recommended), or set it explicitly; an explicitly empty "
        "string stores no grind.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"name", QJsonObject{{"type", "string"}}},
                {"profileTitle", QJsonObject{{"type", "string"},
                    {"description", "Installed profile title (see profiles_list). Omit only "
                                    "for a hot-water-only recipe (hotWater.hasWater true)"}}},
                {"drinkType", QJsonObject{{"type", "string"},
                    {"enum", QJsonArray{"espresso", "filter", "americano", "long_black",
                                        "latte", "tea", "tea_hotwater"}},
                    {"description", "The drink this recipe makes (user intent; presentation "
                                    "only — machine behavior follows the blocks). Derived "
                                    "from the blocks when omitted"}}},
                {"bagId", QJsonObject{{"type", "integer"},
                    {"description", "The specific coffee bag this recipe is made with (from "
                                    "bag_list). The bag's bean identity is adopted "
                                    "automatically."}}},
                {"beanBaseId", QJsonObject{{"type", "string"},
                    {"description", "Canonical bean UUID (display fallback + relink matching "
                                    "key; resolved to the bean's open bag when bagId is omitted)"}}},
                {"roasterName", QJsonObject{{"type", "string"}}},
                {"coffeeName", QJsonObject{{"type", "string"}}},
                {"equipmentId", QJsonObject{{"type", "integer"}, {"description", "Equipment package id (equipment_list)"}}},
                {"doseG", QJsonObject{{"type", "number"}}},
                {"yieldG", QJsonObject{{"type", "number"},
                    {"description", "Absolute yield target in grams. Mutually exclusive with "
                                    "yieldRatio: a recipe holds ONE yield anchor, and writing "
                                    "yieldG replaces any stored ratio (no separate clear "
                                    "needed). Sending both keys in one call is rejected. "
                                    "0 clears the yield entirely."}}},
                {"yieldRatio", QJsonObject{{"type", "number"},
                    {"description", "Yield as a multiplier of the dose (2.0 = a 1:2 ratio; "
                                    "clamped to 0.5-6.0). The gram target then follows the "
                                    "dose actually weighed. Mutually exclusive with yieldG: "
                                    "writing yieldRatio replaces any stored absolute yield "
                                    "(no separate clear needed). Sending both keys in one "
                                    "call is rejected. 0 clears the yield entirely."}}},
                {"tempOffsetC", QJsonObject{{"type", "number"},
                    {"description", "Signed temperature delta in Celsius against the recipe's "
                                    "profile (0/omitted = brew at the profile's own temperature)"}}},
                {"grindPinned", QJsonObject{{"type", "string"},
                    {"description", "The recipe's own grind. Omitted = adopt the linked bag's "
                                    "current dial at save time; explicitly empty = no grind"}}},
                {"rpmPinned", QJsonObject{{"type", "integer"},
                    {"description", "The recipe's own grinder rpm (0 = unset)"}}},
                {"steam", steamBlockSchema()},
                {"hotWater", hotWaterBlockSchema()}
            }},
            {"required", QJsonArray{"name"}}
        },
        [recipeStorage, settings, mainController](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!recipeStorage) {
                respond(QJsonObject{{"error", "Recipe storage not available"}});
                return;
            }
            // The retired absolute field fails LOUD: a pre-rename client
            // writing ~90 into the delta column would be a 90° offset.
            if (args.contains("temperatureOverrideC")) {
                respond(QJsonObject{{"error", "temperatureOverrideC was replaced by tempOffsetC — a SIGNED DELTA in Celsius against the recipe's profile (recipe-relative-temp-offset). Rejected rather than silently dropped: an absolute written into the delta field would corrupt the recipe's temperature."}});
                return;
            }
            // A recipe holds ONE yield anchor — both keys at once is a
            // contradiction, rejected loudly rather than one silently winning
            // (mirrors the temperatureOverrideC rejection above).
            if (args.contains("yieldG") && args.contains("yieldRatio")) {
                respond(QJsonObject{{"error", "yieldG and yieldRatio are mutually exclusive — a recipe holds ONE yield anchor (an absolute gram target OR a ratio of the dose). Send exactly one; writing it replaces the other automatically."}});
                return;
            }
            QVariantMap fields = recipeFieldsFromArgs(args);
            // Schema enums are advisory to the model — enforce the vocabulary
            // here (a typo'd type silently breaks every exact-match consumer).
            const QString requestedType = fields.value("drinkType").toString();
            if (!requestedType.isEmpty() && !Recipe::isKnownDrinkType(requestedType)) {
                respond(QJsonObject{{"error", QString("Unknown drinkType '%1' — one of espresso, "
                    "filter, americano, long_black, latte, tea, tea_hotwater").arg(requestedType)}});
                return;
            }
            if (!Recipe::saveValidationPasses(fields.value("name").toString(),
                                              fields.value("profileTitle").toString(),
                                              fields.value("hotWaterJson").toString())) {
                respond(QJsonObject{{"error", "name is required, and profileTitle is required "
                                              "unless the recipe has a hot-water block with "
                                              "hasWater true"}});
                return;
            }
            // Derive the drink type when the caller didn't state one (embedded
            // profile JSON supplies beverage_type when present).
            if (fields.value("drinkType").toString().isEmpty()) {
                const Recipe shaped = Recipe::fromVariantMap(fields);
                QString bev;
                if (!shaped.profileJson.isEmpty())
                    bev = QJsonDocument::fromJson(shaped.profileJson.toUtf8())
                              .object().value(QStringLiteral("beverage_type")).toString();
                if (bev.isEmpty() && mainController && mainController->profileManager())
                    bev = mainController->profileManager()->beverageTypeForTitle(shaped.profileTitle);
                fields.insert("drinkType", Recipe::deriveDrinkType(shaped, bev));
            }
            // Correlate on a request token: recipeCreated is a broadcast and
            // a concurrent create from another surface must not satisfy (or
            // steal) this listener.
            const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
            fields.insert(QStringLiteral("requestToken"), token);
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = QObject::connect(recipeStorage, &RecipeStorage::recipeCreated, qApp,
                [conn, settings, respond, token](qint64 recipeId, const QVariantMap& recipe) {
                    if (recipe.value(QStringLiteral("requestToken")).toString() != token)
                        return;  // another surface's create
                    QObject::disconnect(*conn);
                    if (recipeId <= 0) {
                        respond(QJsonObject{{"error", "Could not create the recipe"}});
                        return;
                    }
                    respond(recipeToJson(Recipe::fromVariantMap(recipe), settings, nullptr));
                });
            recipeStorage->requestCreateRecipe(fields);
        },
        "settings");

    // recipe_update — edit fields; only provided keys change.
    registry->registerAsyncTool(
        "recipe_update",
        "Update fields on a recipe. Only provided fields change. grindPinned is the recipe's "
        "own grind ('' clears it). Pass bagId to re-point the recipe "
        "at a different bag (its bean identity follows automatically; the recipe's grind is "
        "untouched). Pass a steam object to "
        "replace the steam block. Changing the steam/hot-water block or the profile re-derives "
        "drinkType unless you set it explicitly in the same call.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"recipeId", QJsonObject{{"type", "integer"}}},
                {"name", QJsonObject{{"type", "string"}}},
                {"profileTitle", QJsonObject{{"type", "string"}}},
                {"drinkType", QJsonObject{{"type", "string"},
                    {"enum", QJsonArray{"espresso", "filter", "americano", "long_black",
                                        "latte", "tea", "tea_hotwater"}}}},
                {"bagId", QJsonObject{{"type", "integer"},
                    {"description", "Re-point the recipe at this coffee bag (from bag_list)"}}},
                {"beanBaseId", QJsonObject{{"type", "string"}}},
                {"roasterName", QJsonObject{{"type", "string"}}},
                {"coffeeName", QJsonObject{{"type", "string"}}},
                {"equipmentId", QJsonObject{{"type", "integer"}}},
                {"doseG", QJsonObject{{"type", "number"}}},
                {"yieldG", QJsonObject{{"type", "number"},
                    {"description", "Absolute yield target in grams. Mutually exclusive with "
                                    "yieldRatio; sending yieldG alone REPLACES a stored ratio "
                                    "(updates are present-keys-only, but the yield anchor is "
                                    "one field — no explicit clear of yieldRatio is needed or "
                                    "possible). 0 clears the yield entirely."}}},
                {"yieldRatio", QJsonObject{{"type", "number"},
                    {"description", "Yield as a multiplier of the dose (2.0 = 1:2; clamped to "
                                    "0.5-6.0); the gram target then follows the dose actually "
                                    "weighed. Mutually exclusive with yieldG; sending "
                                    "yieldRatio alone REPLACES a stored absolute yield. "
                                    "0 clears the yield entirely."}}},
                {"tempOffsetC", QJsonObject{{"type", "number"},
                    {"description", "Signed temperature delta in Celsius against the recipe's "
                                    "profile (0 clears it)"}}},
                {"grindPinned", QJsonObject{{"type", "string"},
                    {"description", "The recipe's own grind ('' clears it)"}}},
                {"rpmPinned", QJsonObject{{"type", "integer"}}},
                {"steam", steamBlockSchema()},
                {"hotWater", hotWaterBlockSchema()}
            }},
            {"required", QJsonArray{"recipeId"}}
        },
        [recipeStorage, mainController](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!recipeStorage) {
                respond(QJsonObject{{"error", "Recipe storage not available"}});
                return;
            }
            const qint64 recipeId = args["recipeId"].toInteger();
            // The retired absolute field fails LOUD (see recipe_create).
            if (args.contains("temperatureOverrideC")) {
                respond(QJsonObject{{"error", "temperatureOverrideC was replaced by tempOffsetC — a SIGNED DELTA in Celsius against the recipe's profile (recipe-relative-temp-offset). Rejected rather than silently dropped: an absolute written into the delta field would corrupt the recipe's temperature."}});
                return;
            }
            // One yield anchor per recipe — both keys at once is rejected
            // loudly (see recipe_create).
            if (args.contains("yieldG") && args.contains("yieldRatio")) {
                respond(QJsonObject{{"error", "yieldG and yieldRatio are mutually exclusive — a recipe holds ONE yield anchor (an absolute gram target OR a ratio of the dose). Send exactly one; writing it replaces the other automatically."}});
                return;
            }
            QVariantMap fields = recipeFieldsFromArgs(args);
            const QString requestedType = fields.value("drinkType").toString();
            if (!requestedType.isEmpty() && !Recipe::isKnownDrinkType(requestedType)) {
                respond(QJsonObject{{"error", QString("Unknown drinkType '%1' — one of espresso, "
                    "filter, americano, long_black, latte, tea, tea_hotwater").arg(requestedType)}});
                return;
            }
            // Installed profiles embed no JSON: resolve the new title's
            // beverage_type here (main thread) so the storage-side drink-type
            // re-derivation doesn't mis-derive a tea/filter profile as
            // espresso. Transient hint — RecipeStorage strips it.
            if (!fields.value("profileTitle").toString().trimmed().isEmpty()
                && fields.value("drinkType").toString().isEmpty()
                && mainController && mainController->profileManager()) {
                const QString bev = mainController->profileManager()->beverageTypeForTitle(
                    fields.value("profileTitle").toString());
                if (!bev.isEmpty())
                    fields.insert("profileBeverageType", bev);
            }
            if (recipeId <= 0 || fields.isEmpty()) {
                respond(QJsonObject{{"error", "recipeId plus at least one field is required"}});
                return;
            }
            // Clearing the profile is only valid when this same call makes the
            // recipe hot-water-only — otherwise it would strand an unactivatable
            // recipe (profile required unless hot-water block present).
            if (fields.contains("profileTitle")
                && fields.value("profileTitle").toString().trimmed().isEmpty()
                && !Recipe::hotWaterActive(fields.value("hotWaterJson").toString())) {
                respond(QJsonObject{{"error", "profileTitle can only be cleared when the same "
                                              "call sets a hot-water block with hasWater true"}});
                return;
            }
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = QObject::connect(recipeStorage, &RecipeStorage::recipeUpdated, qApp,
                [conn, recipeId, respond](qint64 updatedId, bool success) {
                    if (updatedId != recipeId)
                        return;  // someone else's update
                    QObject::disconnect(*conn);
                    if (success)
                        respond(QJsonObject{{"updated", true}, {"recipeId", recipeId}});
                    else
                        respond(QJsonObject{{"error", QString("Recipe %1 not found or update failed").arg(recipeId)}});
                });
            recipeStorage->requestUpdateRecipe(recipeId, fields);
        },
        "settings");

    // recipe_create_from_shot — the promotion path.
    registry->registerAsyncTool(
        "recipe_create_from_shot",
        "Create a recipe from a shot in history (the promotion path: 'that shot was great — save "
        "it as a drink'). Prefills profile, the shot's bag as the recipe's linked bag, "
        "equipment, dose, yield, and temperature from the shot record, and the steam block from "
        "the shot's steam snapshot (falling back to the current steam settings for older "
        "shots). The shot's own recorded grind/rpm become the recipe's grind — the exact "
        "dial that produced the shot.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotId", QJsonObject{{"type", "integer"}, {"description", "Shot ID (from shots_list)"}}},
                {"name", QJsonObject{{"type", "string"}, {"description", "The new recipe's name"}}},
                {"hasMilk", QJsonObject{{"type", "boolean"},
                    {"description", "Is this a milk drink? Drives steam-heater intent on activation."}}}
            }},
            {"required", QJsonArray{"shotId", "name"}}
        },
        [shotHistory, recipeStorage, mainController, settings](const QJsonObject& args,
                                                               std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady() || !recipeStorage) {
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            const qint64 shotId = args["shotId"].toInteger();
            const QString name = args["name"].toString().trimmed();
            if (shotId <= 0 || name.isEmpty()) {
                respond(QJsonObject{{"error", "shotId and a non-empty name are required"}});
                return;
            }
            const bool hasMilkProvided = args.contains("hasMilk");
            const bool hasMilk = args["hasMilk"].toBool();
            const QString fallbackSteam = mainController ? mainController->currentSteamSpecJson()
                                                         : QString();
            const QString dbPath = shotHistory->databasePath();
            QThread* thread = QThread::create([dbPath, shotId, name, hasMilkProvided, hasMilk,
                                               fallbackSteam, recipeStorage, settings, respond]() {
                ShotRecord record;
                const bool opened = withTempDb(dbPath, "mcp_recipe_promote", [&](QSqlDatabase& db) {
                    record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
                });
                QMetaObject::invokeMethod(qApp, [opened, record, shotId, name, hasMilkProvided,
                                                 hasMilk, fallbackSteam, recipeStorage, settings,
                                                 respond]() {
                    if (!opened) {
                        respond(QJsonObject{{"error", "Could not open shot database"}});
                        return;
                    }
                    if (record.summary.id <= 0) {
                        respond(QJsonObject{{"error", QString("Shot %1 not found").arg(shotId)}});
                        return;
                    }
                    const std::optional<bool> hasMilkOverride =
                        hasMilkProvided ? std::optional<bool>(hasMilk) : std::nullopt;
                    QVariantMap fields = RecipePromotion::fieldsFromShotRecord(
                        record, name, hasMilkOverride, fallbackSteam);
                    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    fields.insert(QStringLiteral("requestToken"), token);

                    auto conn = std::make_shared<QMetaObject::Connection>();
                    *conn = QObject::connect(recipeStorage, &RecipeStorage::recipeCreated, qApp,
                        [conn, settings, respond, token](qint64 recipeId, const QVariantMap& recipe) {
                            if (recipe.value(QStringLiteral("requestToken")).toString() != token)
                                return;  // another surface's create
                            QObject::disconnect(*conn);
                            if (recipeId <= 0) {
                                respond(QJsonObject{{"error", "Could not create the recipe"}});
                                return;
                            }
                            respond(recipeToJson(Recipe::fromVariantMap(recipe), settings, nullptr));
                        });
                    recipeStorage->requestCreateRecipe(fields);
                }, Qt::QueuedConnection);
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "settings");

    // recipe_clone — copy + rename (the family-variant workflow).
    registry->registerAsyncTool(
        "recipe_clone",
        "Clone a recipe under a new name (e.g. a family member's variant of the same drink). "
        "Copies every field including the steam block and grind. Provenance points at "
        "the source recipe; the source's golden-shot link is not copied. Follow with "
        "recipe_update to change what differs.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"recipeId", QJsonObject{{"type", "integer"}, {"description", "Source recipe id"}}},
                {"name", QJsonObject{{"type", "string"}, {"description", "The clone's name"}}}
            }},
            {"required", QJsonArray{"recipeId", "name"}}
        },
        [recipeStorage, settings](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!recipeStorage) {
                respond(QJsonObject{{"error", "Recipe storage not available"}});
                return;
            }
            const qint64 sourceId = args["recipeId"].toInteger();
            const QString name = args["name"].toString().trimmed();
            if (sourceId <= 0 || name.isEmpty()) {
                respond(QJsonObject{{"error", "recipeId and a non-empty name are required"}});
                return;
            }
            const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = QObject::connect(recipeStorage, &RecipeStorage::recipeCreated, qApp,
                [conn, settings, respond, token](qint64 recipeId, const QVariantMap& recipe) {
                    if (recipe.value(QStringLiteral("requestToken")).toString() != token)
                        return;  // another surface's create
                    QObject::disconnect(*conn);
                    if (recipeId <= 0) {
                        respond(QJsonObject{{"error", "Clone failed (source recipe not found?)"}});
                        return;
                    }
                    respond(recipeToJson(Recipe::fromVariantMap(recipe), settings, nullptr));
                });
            recipeStorage->requestCloneRecipe(sourceId, name, token);
        },
        "settings");

    // recipe_archive — retire (or restore); delete only for unused recipes.
    registry->registerAsyncTool(
        "recipe_archive",
        "Archive a recipe (it leaves the pickers and idle pills but stays readable — shots keep "
        "their provenance; a used recipe can never be hard-deleted, same rule as coffee bags). "
        "Pass restore=true to unarchive. Pass delete=true to permanently delete a recipe that "
        "has no shots (fails if any shot references it).",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"recipeId", QJsonObject{{"type", "integer"}}},
                {"restore", QJsonObject{{"type", "boolean"}, {"description", "Unarchive instead"}}},
                {"delete", QJsonObject{{"type", "boolean"},
                    {"description", "Hard-delete (only recipes with zero shots)"}}}
            }},
            {"required", QJsonArray{"recipeId"}}
        },
        [recipeStorage, mainController, settings](const QJsonObject& args,
                                                  std::function<void(QJsonObject)> respond) {
            if (!recipeStorage) {
                respond(QJsonObject{{"error", "Recipe storage not available"}});
                return;
            }
            const qint64 recipeId = args["recipeId"].toInteger();
            if (recipeId <= 0) {
                respond(QJsonObject{{"error", "Valid recipeId is required"}});
                return;
            }
            const bool restore = args["restore"].toBool();
            const bool hardDelete = args["delete"].toBool();

            // Leaving the recipe you're deleting/archiving: deactivate first
            // so the selection can't point at a hidden/removed row.
            if (!restore && settings && settings->dye()->activeRecipeId() == recipeId
                && mainController)
                mainController->deactivateRecipe();

            if (hardDelete) {
                auto conn = std::make_shared<QMetaObject::Connection>();
                *conn = QObject::connect(recipeStorage, &RecipeStorage::recipeDeleted, qApp,
                    [conn, recipeId, respond](qint64 deletedId, bool success) {
                        if (deletedId != recipeId)
                            return;
                        QObject::disconnect(*conn);
                        if (success)
                            respond(QJsonObject{{"deleted", true}, {"recipeId", recipeId}});
                        else
                            respond(QJsonObject{{"error",
                                "Delete refused — the recipe has shots (archive it instead) or was not found"}});
                    });
                recipeStorage->requestDeleteRecipe(recipeId);
                return;
            }
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = QObject::connect(recipeStorage, &RecipeStorage::recipeUpdated, qApp,
                [conn, recipeId, restore, respond](qint64 updatedId, bool success) {
                    if (updatedId != recipeId)
                        return;
                    QObject::disconnect(*conn);
                    if (success)
                        respond(QJsonObject{{restore ? "restored" : "archived", true},
                                            {"recipeId", recipeId}});
                    else
                        respond(QJsonObject{{"error", QString("Recipe %1 not found").arg(recipeId)}});
                });
            if (restore)
                recipeStorage->requestUnarchiveRecipe(recipeId);
            else
                recipeStorage->requestArchiveRecipe(recipeId);
        },
        "settings");

    // recipe_activate — the pill-tap equivalent (single shared path).
    registry->registerAsyncTool(
        "recipe_activate",
        "Activate a recipe: loads its profile, selects the linked bag (even when that bag is "
        "finished — a stale recipe activates fully with its own grind; a bean-less recipe "
        "clears the active bag) and the "
        "equipment package, applies dose/yield/temperature and the recipe's own grind, "
        "applies the steam block, and warms the steam heater when the drink has milk. Exactly "
        "what tapping the recipe's pill on the idle screen does.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"recipeId", QJsonObject{{"type", "integer"}, {"description", "Recipe ID (from recipe_list)"}}}
            }},
            {"required", QJsonArray{"recipeId"}}
        },
        [mainController](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!mainController) {
                respond(QJsonObject{{"error", "Controller not available"}});
                return;
            }
            const qint64 recipeId = args["recipeId"].toInteger();
            if (recipeId <= 0) {
                respond(QJsonObject{{"error", "Valid recipeId is required"}});
                return;
            }
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = QObject::connect(mainController, &MainController::recipeActivated, qApp,
                [conn, recipeId, respond](qint64 activatedId, bool success) {
                    if (activatedId != recipeId)
                        return;
                    QObject::disconnect(*conn);
                    if (success)
                        respond(QJsonObject{{"activated", true}, {"recipeId", recipeId}});
                    else
                        respond(QJsonObject{{"error", QString("Recipe %1 not found or activation failed").arg(recipeId)}});
                });
            mainController->activateRecipe(recipeId);
        },
        "control");
}
