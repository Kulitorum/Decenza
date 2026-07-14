## Why

User feedback (fredphoesh, [Kulitorum/Decenza#1482](https://github.com/Kulitorum/Decenza/issues/1482)) says the recipe cards' "source + delta" temperature/yield display (e.g. "88°C +5°", "36.0 → 40.0g") is confusing and too complex for users to reason about — they read it as "something is wrong" rather than "this recipe modifies its profile." This was a deliberate design (PR #1498, `recipe-relative-temp-offset`) but the reception shows it doesn't hold up in practice. Every other Shot Plan surface (idle widget, Brew Settings, shot-detail/post-shot-review snapshot lines) already shows a recipe's own values as a plain baseline with no delta tag (the `recipe-baseline-not-override` fix, #1485) — recipe cards are the sole remaining exception. Removing that exception makes the recipe card consistent with every other surface the user already understands.

## What Changes

- **BREAKING** (spec-level, not data): `RecipeDrinkCard.qml`'s shot-plan line (used by the Recipes management page grid and, identically, the Recipe Wizard's summary hero) stops rendering "source + delta" — it no longer shows the card's own profile's raw frame temps plus a separately-highlighted signed offset tag, and no longer shows the yield as a highlighted "profile → recipe" arrow.
- Recipe cards instead render the recipe's own **resulting** values as a plain baseline — the same `recipeBaselineTemp`/`recipeBaselineYield` mode `ShotPlanItem.qml` (the idle Shot Plan widget) already uses — so a recipe with offset −3 on an 84·94°C profile reads "81 · 91°C" (untagged) on its card, matching what the live Shot Plan shows once that recipe is active.
- Highlighting on recipe cards is retired to the same rule as every other Shot Plan surface: only a genuine per-brew tweak beyond the recipe's own value would highlight — but cards show a static recipe definition (no live per-brew override to compare against), so in practice recipe cards render with no highlight at all.
- In `ShotPlanText.qml`, the recipe's stored `tempOffsetC` becomes a silent shift baked into the displayed frame temperatures (so "84 · 94°C" with offset −3 renders as "81 · 91°C") instead of a separately-highlighted signed tag appended after the plain frames — the separate tag rendering is removed. The card keeps resolving and passing its **own** profile's frame temperatures (`profileStepTemps`) rather than switching to the currently-loaded-profile baseline path (`recipeBaselineTemp`/`ProfileManager.temperatureDisplay`), which is necessary to keep cards showing their own recipe's profile even when a different profile is currently loaded on the machine (`RecipesPage.refreshProfileNumbers`-style per-card frame resolution is unaffected and still required).
- Update `docs/CLAUDE_MD/RECIPES.md`'s "Recipe cards deliberately show the opposite decomposition" paragraph and the GitHub wiki manual (if it documents the card's delta display) to describe the new, unified behavior.
- Out of scope: the actual post-shot Shot Review dialog (`ShotAnalysisDialog.qml`/`QualityBadges.qml`) and Shot Detail page are unaffected — they already show plain shot-relative values and are already unified with each other; this change only touches the recipe card / wizard summary shot-plan line.

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `recipe-quick-switch`: the management-page recipe card's shot-plan line requirement changes from "source + delta" (profile's own frame temps + signed offset tag; profile→recipe yield arrow) to a plain baseline render (recipe's own resulting temps/yield, untagged, matching the live Shot Plan's baseline mode).
- `plan-widgets`: the "Recipe cards are the one deliberate exception on the temperature item" clause is removed — recipe cards now follow the same override-highlight rule as every other Shot Plan surface (baseline values, highlight only on a genuine per-brew override beyond the surface's own baseline).
- `recipe-aware-brew-settings`: the cross-reference sentence stating recipe cards "deliberately show the opposite decomposition" from the live widget is corrected — cards now render the same baseline decomposition (resolved against their own profile, not the loaded one). The live-widget requirement's own substance is unchanged.

## Impact

- `qml/components/RecipeDrinkCard.qml` — shot-plan line's yield binding drops the profile-relative `yieldOverridden` comparison (no more arrow/highlight); temperature binding keeps `profileStepTemps`/`recipeTempOffsetC` (still needed to resolve the card's own, possibly-not-loaded profile) but the offset becomes a silent shift, not a rendered tag.
- `qml/pages/RecipeWizardPage.qml` — summary/WYSIWYG hero step reuses `RecipeDrinkCard`, inherits the same fix automatically.
- `qml/components/ShotPlanText.qml` — `_tempStr`'s step-based branch passes `recipeTempOffsetC` as the shift argument to `temperatureDisplayForSteps` instead of `0`; the separate `_tempTagStr` highlighted-tag computation and its concatenation in `_build()` are removed.
- `qml/pages/RecipesPage.qml` — no change: its per-card profile frame-temp resolution (feeding `profileStepTemps`/`profileTempC`/`profileYieldG`) remains necessary and unaffected.
- `docs/CLAUDE_MD/RECIPES.md` — update the paragraph documenting the card's source+delta decomposition.
- GitHub wiki manual (`Kulitorum/Decenza.wiki.git`) — update any page describing the recipe card's temperature/yield display, per CLAUDE.md's user-manual policy.
- `openspec/specs/recipe-quick-switch/spec.md`, `openspec/specs/plan-widgets/spec.md` — delta specs for this change.
