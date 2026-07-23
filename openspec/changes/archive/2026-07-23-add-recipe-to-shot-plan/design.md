## Context

The Shot Plan widget is rendered by `qml/components/ShotPlanText.qml` and configured through an ordered item-key list (`shotPlanItems`). The item list is resolved by the shared library `qml/components/layout/ShotPlanConfig.js` (`allKeys` + `itemsFor`), which is the single source of truth used by three surfaces:

1. The widget itself (`ShotPlanItem.qml` → `ShotPlanText.qml`).
2. The in-app chip editor (`ScreensaverEditorPopup.qml`, the "Shown"/"Available" rows in the screenshot).
3. The ShotServer web layout editor, which re-implements the same rule in embedded JS (`src/network/shotserver_layout.cpp`: `SP_ALL_KEYS`, `SP_ITEM_LABELS`, `spItemsFromProps`).

Today there are seven items (`doseYield`, `profile`, `temperature`, `roaster`, `coffee`, `grind`, `roastDate`). The active recipe (drink) name — the label many users now brew by — is not among them, even though `MainController.activeRecipe` (a `QVariantMap` with a `name` key, gated on `Settings.dye.activeRecipeId >= 0`) already exposes it, and the shot-detail / post-shot-review pages already resolve a per-shot recipe name via `recipeResolver.recipe.name`.

`ShotPlanText` renders in three formats, all built by the one `_build(fmt, sep, blockSep)` function so the plain (a11y) and rich (display) strings can't drift: fragment list, profile-anchored sentence (+ trailing tail), and profile-less "recipe" sentence (+ trailing tail). Every property that varies per surface (dose, grind, temperature, beverage, override flags, baselines) is already an overridable `property` with a live-singleton default, so per-shot surfaces can inject frozen snapshot values.

## Goals / Non-Goals

**Goals:**
- Add `recipe` as an eighth, default-OFF Shot Plan item showing the active recipe name.
- Place it correctly in all three formats (fragment order; trailing tail in both sentences).
- Offer/label it in both the in-app and web editors, kept in sync.
- Let the shot-detail and post-shot-review snapshot lines show the shot's frozen recipe name via an overridable property.

**Non-Goals:**
- No change to how recipes are selected, stored, or activated.
- No new setting on the `Settings` façade or any domain object (the item list already lives in `shotPlanItems` on the widget instance).
- Recipe reuses the **existing** "using {…}" scaffold slot — it never invents a new translated template. It anchors only when Profile is absent; otherwise it trails.
- No legacy boolean (`shotPlanShowRecipe`) — default-OFF is achieved purely by absence from `allKeys`-driven defaults, exactly like a brand-new key.

## Decisions

### Recipe is a stand-in anchor for Profile in the "using {…}" slot
The intended use is to **swap** Profile (and Roaster/Coffee) out for Recipe, so the recipe name must land where the profile name normally reads — the "using {…}" clause — not get buried in a trailing tail. Rather than invent a new sentence template, `recipe` fills the *existing* profile scaffold's anchor slot:

- The "using {anchor}" anchor is filled by the profile name when the Profile item is present with a name; else by the active recipe name when the Recipe item is present and a recipe is active; else there is no anchor and the plan falls to the profile-less recipe sentence (roaster/coffee/dose based) as today.
- When **both** Profile and Recipe are present with values, Profile keeps the anchor and Recipe trails as an ordinary tail item — Recipe never displaces a shown, available Profile. This gives a user who deliberately shows both the profile in its natural slot plus the recipe name after it, and keeps the anchor rule deterministic.

Concretely, `ShotPlanText` computes an anchor string once (`_anchorStr` = `_profileStr` if non-empty, else `_recipeStr` when the Recipe item is present) and the four existing degradation templates ("Brew {yield} of {beverage}, using {anchor} at {temperature}", etc.) consume it in place of the current `_profileStr`. The profile-anchored branch's tail loop gains a `case "recipe":` that pushes the recipe name **only when it is not the current anchor** (i.e. only in the both-present case). Fragment mode gains a plain `case "recipe":` like any other segment. No new translated string is added for the sentence.

- Alternative considered: a new "recipe" head template ("Brew {recipe}: …"). Rejected — heavier, needs new translated templates for all degradations, and collides with the existing profile-less "recipe" sentence that already owns that word for the beans-based fallback.
- Alternative considered: keep recipe as a pure trailing tail item. Rejected by the user — it buries the recipe name when it is meant to headline the "using" clause in the swap case.

### Default OFF via absence, no legacy boolean
`roastDate` is the precedent: it is in `allKeys` but defaults off (the legacy derivation only pushes it when `shotPlanShowRoastDate === true`). For a brand-new key there is no legacy config to honor, so `recipe` simply is not pushed by any legacy-derivation branch and is absent from any pre-change `shotPlanItems` array. Adding it to `allKeys` makes it appear in the editors' "Available" row without altering any saved widget.

- This means `ShotPlanConfig.itemsFor`, `spItemsFromProps`, and the legacy branches need **no** new derivation line — only `allKeys` / `SP_ALL_KEYS` / `SP_ITEM_LABELS` grow.

### Recipe name is an overridable property, live by default
Add `property string recipeName` to `ShotPlanText`, defaulting to the live active recipe name — e.g. `Settings.dye.activeRecipeId >= 0 ? (MainController.activeRecipe.name || "") : ""`. `_recipeStr` gates on `_has("recipe") && recipeName.length > 0`. The shot-detail and post-shot-review `ShotPlanText` instances set `recipeName:` to the shot's frozen recipe name (they already have `recipeResolver.recipe.name` in scope). This mirrors the existing override pattern (`grindSize`, `beverageType`, `isCleaning`, …) exactly.

- Recipe cards (`RecipeDrinkCard.qml`) pin an explicit `itemOrder` without `recipe`, so they are unaffected; if a card ever wanted it, it would inject its own `recipeName`.

### Emoji safety
Recipe names are user-authored and the picker encourages emoji. `_recipeStr` flows through the same `fmt()` → `escapeHtml` → `replaceEmojiWithImg` path as the bean/recipe names already rendered in `_rich`, so a color emoji in a recipe name is rewritten to a bundled `<img>` and never reaches the macOS render thread. No new handling is required — just route the value through the existing tail formatting, not a raw `Text`.

### Editor preview
`ScreensaverEditorPopup.qml` renders a live `ShotPlanText` preview. Its `recipeName` defaults to the live value, so the preview shows the real active recipe when the user adds the Recipe chip — no extra wiring beyond the chip label case.

## Risks / Trade-offs

- [The two editors' item lists drift] → Add `recipe` to `allKeys` (JS) and `SP_ALL_KEYS` + `SP_ITEM_LABELS` (C++ embedded JS) in the same change; the existing `ShotPlanConfig.js` header comment already mandates keeping them in sync. A test/asserted parity check is out of scope, but the change is a single additive key in each.
- [Recipe empty when no recipe active looks like a bug] → Spec'd behavior: the item contributes nothing, identical to Roaster/Coffee with no bean. Users who want it always-on will simply have a recipe active; this matches every other data-driven item.
- [Frozen-shot line shows the wrong recipe if a page forgets to override] → Default is the live active recipe (a reasonable fallback), and both shot pages already resolve the shot's recipe name for their recipe card, so wiring `recipeName` there is a one-line addition next to existing code.
- [Translations] → One new key (`shotPlanEditor.itemRecipe` "Recipe"); the web editor label is an English string in `SP_ITEM_LABELS` like its peers. Low risk.

## Migration Plan

Purely additive; no data migration. Existing `shotPlanItems` arrays remain valid and unchanged (they simply lack `recipe`). Rollback is a straight revert — no persisted state depends on the new key, and a saved layout that somehow contained `recipe` would just render an unknown key as nothing under the old code (the `switch` default), so forward/back compatibility is safe.

## Open Questions

- None blocking. (Confirmed during design: `settings_network.cpp` treats `shotPlan` as a single catalog widget and does not enumerate item keys or expose a per-item readout option schema, so no change is needed there.)
