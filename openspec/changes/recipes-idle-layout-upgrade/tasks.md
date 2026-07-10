## 1. New default layout

- [x] 1.1 Update `SettingsNetwork::defaultLayoutJson()`: centerTop = recipes, beans, steam, hotwater; bottomRight = flush, history, equipment, espresso, settings (drop spacer2 and autofavorites); all other zones unchanged; keep existing item ids
- [x] 1.2 Verify the equipment/recipes injection migrations are no-ops on the new default (reset → reload → identical zones) and that in-app + web "Reset to default" both produce the new composition

## 2. Upgrade transform (C++)

- [x] 2.1 Add `Q_INVOKABLE void SettingsNetwork::applyRecipesFirstUpgrade()`: pristine check first (zones structurally equal to the frozen old-default composition → `resetLayoutToDefault()`); otherwise remove espresso from center zones (insert recipes at its position if no recipes item exists anywhere), insert espresso after equipment in bar zones (fallback: before settings in bottomRight, then append), remove all autofavorites items; persist once via `saveLayoutObject()`
- [x] 2.2 Make the transform idempotent and defensive (espresso absent, espresso already in a bar zone, multiple autofavorites) per the spec scenarios
- [x] 2.3 Add `layout/recipesUpgradeOffered` flag accessors (settings domain per SETTINGS.md rules)

## 3. Starter recipe from last shot

- [x] 3.1 Extract the promotion field-building from `mcptools_recipes.cpp` (`recipe_create_from_shot` lambda) into a shared helper reusable outside MCP; re-point the MCP tool at it (behavior unchanged)
- [x] 3.2 Add the upgrade hook: fetch latest saved shot id + record on a background thread (`withTempDb`), pre-select milk vs espresso from the steam snapshot (`hasMilk` true or `milkWeightG` > 0 → milk) for the dialog, then on accept build fields from the user's drink-type choice with the translated default name, create via `RecipeStorage`, activate via `MainController::activateRecipe` on `recipeCreated`
- [x] 3.3 Guards: skip creation when the user already has recipes or has no saved shots; layout transform still applies

## 4. Upgrade offer dialog (QML)

- [x] 4.1 Add the one-time dialog in `main.qml` following the `firstRunDialog` pattern: shown when `firstRunComplete` is true and `recipesUpgradeOffered` is false; fresh-install first-run completion also sets `recipesUpgradeOffered`
- [x] 4.2 Dialog content: explain the three layout edits + starter-recipe line with the Espresso / Milk drink choice pre-selected by the heuristic (both only when a starter recipe will be created); buttons "Upgrade layout" / "Keep my layout"; translated strings; accessibility (roles, names, focus, dismiss = decline)
- [x] 4.3 Wire accept → `applyRecipesFirstUpgrade()` + starter-recipe hook (passing the drink-type choice), then confirmation toast ("Layout updated · Created '<name>'"); decline/dismiss → set flag only

## 5. Tests & verification

- [x] 5.1 Unit tests for the transform: pristine-old-default → full new default, customized-layout surgical cases (no-recipes, espresso-already-in-bar, customization preservation), idempotency (run via Qt Creator MCP; no WARN lines per TESTING.md)
- [x] 5.2 Unit tests for the heuristic pre-selection + starter-recipe guards (milk snapshot, espresso snapshot, empty snapshot, no shots, recipes exist)
- [ ] 5.3 Manual pass: fresh install shows new default and no dialog; existing-user upgrade accept/decline paths; reset-to-default from app and web editor
