#pragma once

#include <QString>
#include <QVariantMap>
#include <optional>

struct ShotRecord;

// Shared field-building for the recipe promotion path ("that shot was great —
// save it as a drink"): turns a ShotRecord into the QVariantMap
// RecipeStorage::requestCreateRecipe expects (profile, bag link, equipment,
// dose/yield/temp, the shot's own grind, steam block). Shared by the
// recipe_create_from_shot MCP tool (src/mcp/mcptools_recipes.cpp) and the
// recipes-idle-layout-upgrade starter-recipe hook so there is exactly one
// implementation of the promotion semantics.
namespace RecipePromotion {

// hasMilkOverride: unset = leave the shot's steam snapshot's hasMilk as-is;
// set = stamp it onto the (possibly-fallback) steam block. fallbackSteamJson
// is used verbatim when the shot predates steam snapshots (record.steamJson
// empty) — typically MainController::currentSteamSpecJson().
QVariantMap fieldsFromShotRecord(const ShotRecord& record, const QString& name,
                                  std::optional<bool> hasMilkOverride,
                                  const QString& fallbackSteamJson);

} // namespace RecipePromotion
