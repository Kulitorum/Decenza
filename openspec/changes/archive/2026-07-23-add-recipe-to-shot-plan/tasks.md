## 1. Shared item catalog

- [x] 1.1 Add `"recipe"` to `allKeys` in `qml/components/layout/ShotPlanConfig.js` (canonical order — place before `doseYield` or after `roastDate` consistently with the editor's label ordering); confirm no legacy-derivation line is needed since it defaults OFF.

## 2. Renderer (ShotPlanText.qml)

- [x] 2.1 Add `property string recipeName` defaulting to the live active recipe name (`Settings.dye.activeRecipeId >= 0 ? (MainController.activeRecipe.name || "") : ""`).
- [x] 2.2 Add a `readonly property string _recipeStr` gated by `_has("recipe") && recipeName.length > 0`.
- [x] 2.3 Introduce a single anchor string: `_anchorStr` = `_profileStr` when non-empty, else `_recipeStr`. In `_build`, drive the profile-anchored sentence scaffold (the four "using {anchor}" degradation templates) off `_anchorStr` in place of the current `_profileStr`, and gate entry to that branch on `_anchorStr !== ""` (not just `_profileStr !== ""`). Preserve the existing per-item override highlighting for temperature/yield unchanged.
- [x] 2.4 In the profile-anchored sentence tail `switch`, add a `case "recipe":` that pushes the recipe name **only when it is not the current anchor** (i.e. only when a profile filled the anchor and recipe is also shown) — so recipe never both anchors and trails.
- [x] 2.5 In the fragment `switch`, add a plain `case "recipe":` pushing `fmt(_recipeStr, true)` in item-list order. Do **not** add recipe to the profile-less recipe sentence tail (if recipe were active it would have anchored; an empty recipe contributes nothing).
- [x] 2.6 Verify the value flows through the existing `fmt` → escape → `replaceEmojiWithImg` path (emoji-safe) and that the a11y `text` and rich `_rich` builders share the same code (no separate handling).

## 3. In-app chip editor (ScreensaverEditorPopup.qml)

- [x] 3.1 Add a `case "recipe": return TranslationManager.translate("shotPlanEditor.itemRecipe", "Recipe")` to the item-label function.
- [x] 3.2 Confirm the live `ShotPlanText` preview shows the active recipe when the Recipe chip is added (its `recipeName` default already resolves the live value — no extra wiring expected; verify).

## 4. Web layout editor (shotserver_layout.cpp)

- [x] 4.1 Add `"recipe"` to `SP_ALL_KEYS` and `recipe: "Recipe"` to `SP_ITEM_LABELS` (matching canonical order in `ShotPlanConfig.js`).
- [x] 4.2 Confirm `spItemsFromProps` needs no new derivation line (default OFF; no legacy boolean) and the web editor offers Recipe in Available items.

## 5. Per-shot snapshot surfaces

- [x] 5.1 In `qml/pages/ShotDetailPage.qml`, pass the shot's frozen recipe name into the snapshot `ShotPlanText` via `recipeName:` (reuse the page's existing `recipeResolver.recipe.name`).
- [x] 5.2 In `qml/pages/PostShotReviewPage.qml`, do the same for its snapshot `ShotPlanText`.
- [ ] 5.3 Verify a frozen-shot line shows the shot's own recipe, not the live active recipe, when a different recipe is active.

## 6. Translations & docs

- [x] 6.1 Add the `shotPlanEditor.itemRecipe` → "Recipe" string to the base translation source.
- [x] 6.2 Update the GitHub wiki Manual (Shot Plan section) to list Recipe as a selectable item.

## 7. Verification

- [x] 7.1 Build via Qt Creator MCP (`mcp__qtcreator__build`) and run qmllint with `-I` on the build dir for the edited QML files; clear any new warnings/TypeErrors.
- [x] 7.2 Run the full test suite via `mcp__qtcreator__run_tests` (scope `all`); if any plan-widgets/shot-plan test enumerates the item set, update it to include `recipe`.
- [ ] 7.3 Manually verify in the running app: default layout unchanged (Recipe off); adding Recipe in Shot Plan Settings shows/reorders/removes correctly in fragment and both sentence modes; empty when no recipe active.
