## Why

Decenza already lets a user pin a single profile to auto-load on app start, DE1 wake-from-sleep, and after idle inactivity (`profile-auto-load`). Recipes are now the primary daily-use surface (profile + bag + equipment + grind + dose bundled together), but they have no equivalent — a user who has moved to recipe-based workflows can't get the same "walk up and it's already ready" behavior. Extending auto-load to recipes closes that gap, using the same mechanism the user already understands from profiles.

## What Changes

- Add a recipe auto-load setting (`Settings.dye.autoLoadRecipeId`, sentinel `-1`) mirroring `autoLoadProfileFilename`'s shape, with a shared `autoLoadRevertMinutes` timeout.
- **BREAKING (behavioral, not API)**: auto-load becomes mutually exclusive across profiles and recipes — setting one auto-load target silently clears the other. Setting `autoLoadProfileFilename` now also clears `autoLoadRecipeId` and vice versa.
- Add a pin-style `StyledIconButton` to the recipe card's action row (`RecipesPage.qml`), positioned as the first child — immediately to the left of Edit — toggling auto-load for that recipe immediately (no confirmation dialog), matching the profile card's no-confirmation toggle pattern. Tinted `Theme.primaryColor` when that recipe is the current auto-load target, outline otherwise. Not shown on archived cards.
- On the one card that is the current auto-load target, additionally show a compact status row (pin icon + an explicit "Auto-load" text label + the shared revert-minutes stepper) so the state is identifiable without relying on the pin icon's tint or a user recognizing the pin glyph. No recipe name repeated (already the card's own title) and no separate clear button (the toggle button above already clears it on a second tap). This lives on the card itself, not a page-level or toolbar strip.
- Add a shared entry point (`MainController::loadAutoLoadRecipeIfNeeded()`) invoked from the same three triggers as the profile version: app startup, DE1 Sleep→Idle wake, and idle-inactivity countdown on the Idle page. Both entry points are called at each trigger site; each is a no-op unless its own setting is populated, so no coordinator/dispatcher layer is needed given the two are already mutually exclusive.
- Add eligibility/cleanup rules mirroring the profile side: clearing `autoLoadRecipeId` when the recipe is archived or deleted, with the same "stale target" toast-and-clear behavior at trigger time.
- Add three MCP tools — `recipe_get_auto_load`, `recipe_set_auto_load`, `recipe_clear_auto_load` — matching the shape, access level (`settings`), and error semantics of the existing `profiles_*_auto_load` tools.
- Update `profiles_set_auto_load` / `profiles_clear_auto_load` behavior (and their MCP descriptions) to reflect that setting a profile auto-load clears any recipe auto-load, and vice versa for the new recipe tools.

## Capabilities

### New Capabilities
- `recipe-auto-load`: pin a single recipe as the auto-load target, loaded on the same three triggers as profile auto-load, surfaced via a card button, exposed over MCP, mutually exclusive with profile auto-load.

### Modified Capabilities
- `profile-auto-load`: setting or clearing the profile auto-load target now also clears any configured recipe auto-load target (new cross-capability mutual-exclusion requirement); MCP tool descriptions updated to document this side effect.

## Impact

- **Settings**: `src/core/settings_dye.h/.cpp` (new `autoLoadRecipeId` property, alongside the existing `activeRecipeId`/`activeBagId` int-id pattern); `src/core/settings_app.h/.cpp` (`setAutoLoadProfileFilename` gains a cross-clear of `autoLoadRecipeId`).
- **Controllers**: `src/controllers/maincontroller.h/.cpp` (new `loadAutoLoadRecipeIfNeeded()`, eligibility check, stale-clear + signal); `src/controllers/profilemanager.cpp` (`setAutoLoadProfileFilename` call site now also needs the cross-clear, or the clear lives in the setter itself).
- **Recipe storage**: `src/history/recipestorage.h/.cpp` (archive/delete hooks need to clear `autoLoadRecipeId` when it targets the affected recipe, mirroring `ProfileManager::deleteProfile`'s hidden/deleted cleanup).
- **QML**: `qml/pages/RecipesPage.qml` (new card button in the `footer: Flow` action row); `qml/main.qml` (wire `MainController.loadAutoLoadRecipeIfNeeded()` into the three existing trigger sites alongside the profile call).
- **MCP**: `src/mcp/mcptools_write.cpp` (three new tools, alongside `profiles_set_auto_load`/`profiles_clear_auto_load` — not `mcptools_recipes.cpp`, whose `recipe_activate`/`recipe_archive` calling real `MainController` methods would drag its full closure into any test linking that file).
- **Tests**: new coverage in `tests/tst_maincontroller.cpp` (or a new `tst_recipeautoload.cpp`) and `tests/tst_settings.cpp`, mirroring the profile auto-load coverage in `tests/tst_profilemanager.cpp` and `tests/tst_settings.cpp`, for the recipe side and for the cross-clear behavior.
- **Docs**: wiki manual page for auto-load needs a recipe-side addendum (task tracked in `tasks.md`).
