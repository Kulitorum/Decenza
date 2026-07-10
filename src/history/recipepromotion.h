#pragma once

#include <QString>
#include <QVariantMap>
#include <optional>

struct ShotRecord;

// Shared field-building for the recipe promotion path ("that shot was great —
// save it as a drink"): turns a ShotRecord into the QVariantMap
// RecipeStorage::requestCreateRecipe expects (profile, bag link, equipment,
// dose/yield/temp, grind routing, steam block). Shared by the
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

// Heuristic pre-selection for the recipes-idle-layout-upgrade dialog's
// Espresso/Milk drink-type choice: milk when the shot's steam snapshot says
// hasMilk, or ever recorded a steamed milk weight (milkWeightG > 0). The
// snapshot can only say "has steamed milk before", not "this drink had
// milk" (milkWeightG derives from a persistent last-steam-ever value), so
// this only pre-selects — the user can override it in the dialog.
bool milkPreselectedFromSteamJson(const QString& steamJson);

// The starter-recipe eligibility gate for the recipes-idle-layout-upgrade
// offer: create one only when the user has zero recipes AND a saved shot was
// actually found and loaded. recipeCountOk must be false whenever the recipe
// count couldn't be reliably determined (DB open/query failure) — an
// unreliable count must never be read as "zero recipes", which would offer
// (and on accept create) a spurious duplicate starter recipe for a user who
// already has some. Extracted as a pure predicate so it's testable without
// standing up the background-thread DB plumbing around it.
bool isEligibleForStarterRecipe(bool recipeCountOk, qint64 recipeCount,
                                 qint64 latestShotId, qint64 loadedRecordId);

} // namespace RecipePromotion
