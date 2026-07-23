## Why

The Shot Plan widget lets users pick and order which brew facts it shows — Dose & yield, Profile, Temperature, Roaster, Coffee, Grind, Roast date — but not the one thing that now sits at the top of many users' workflow: the **active recipe (drink) name**. With recipes driving the brew (profile, yield, temperature, grind, bean link), a user who has "Morning Latte" or "Guji V60" selected can't surface that name on the idle screen, even though it's the label they think in.

## What Changes

- Add an **eighth Shot Plan display item, `recipe`**, that renders the active recipe's name (from `MainController.activeRecipe`, gated on `Settings.dye.activeRecipeId >= 0`). Like Roast date, it defaults **OFF**, so no existing widget configuration changes.
- **Recipe is a stand-in anchor for Profile**, matching the intended use of swapping Profile (and Roaster/Coffee) out for Recipe: in sentence mode the "using {…}" slot is filled by the profile name when Profile is shown, or by the active recipe name when Profile is removed — "Brew 40.0g of Espresso, using Morning Latte at 92°C". When both Profile and Recipe are shown, Profile keeps the anchor and Recipe trails as a tail item. In fragment mode `recipe` joins in item-list order like any segment.
- Add `recipe` to the in-app chip editor (Shot Plan Settings — the "Shown"/"Available" rows) with a translatable "Recipe" label, and to the ShotServer web layout editor's mirror (item catalog + labels), keeping the two in sync.
- Make the recipe name overridable on `ShotPlanText` (a `recipeName` property) so the shot-detail and post-shot-review snapshot lines render the shot's **frozen** recipe name rather than the live active recipe — consistent with how those surfaces already override dose, grind, temperature, and beverage.
- When no recipe is active the `recipe` item contributes nothing (empty segment), exactly like Roaster/Coffee with no bean set.
- Update the wiki manual's Shot Plan documentation to list Recipe as a selectable item.

## Capabilities

### New Capabilities

_None._

### Modified Capabilities

- `plan-widgets`: the display-item set grows from seven items to eight — `recipe` (the active recipe/drink name) is added, with its content rule, its fragment/sentence/recipe-sentence placement, its default-OFF behavior, and its per-shot frozen-name override on the snapshot surfaces.

## Impact

- **QML**
  - `qml/components/layout/ShotPlanConfig.js` — add `recipe` to `allKeys` (no legacy boolean; default OFF is achieved by absence).
  - `qml/components/ShotPlanText.qml` — add `recipeName` property (default: live active recipe name), `_recipeStr` getter gated by `_has("recipe")`, and wire it into the fragment list, the profile-anchored sentence tail, and the profile-less recipe sentence tail.
  - `qml/components/layout/ScreensaverEditorPopup.qml` — add the `recipe` chip label case and feed the editor preview the current recipe name.
  - Per-shot surfaces (`qml/pages/ShotDetailPage.qml`, `qml/pages/PostShotReviewPage.qml`) — pass the shot's frozen recipe name into `ShotPlanText.recipeName` (these pages already resolve a recipe name via `recipeResolver.recipe.name`).
- **C++ / web editor**
  - `src/network/shotserver_layout.cpp` — add `recipe` to `SP_ALL_KEYS` and `SP_ITEM_LABELS` so the web layout editor offers and labels it.
  - `src/core/settings_network.cpp` — widen the Shot Plan widget's readout/option surface only if the catalog or option schema enumerates the item keys (verify during design).
- **Translations** — one new UI string (`shotPlanEditor` "Recipe" chip label); reuse existing keys where possible.
- **Docs** — GitHub wiki Manual (Shot Plan section) lists Recipe as an item.
- **No breaking changes**, no BLE/DB/settings-schema changes; `recipe` is additive and off by default, and the stored `shotPlanItems` array already accommodates arbitrary keys.
