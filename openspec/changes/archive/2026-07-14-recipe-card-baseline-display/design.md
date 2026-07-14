## Context

`ShotPlanText.qml` is the single shared renderer behind every "Shot Plan" line in the app (idle widget, Brew Settings' echoes, shot-detail/post-shot-review snapshot lines, and the recipe card). Two override-presentation modes already coexist in it:

- **Baseline mode** (`recipeBaselineYield`/`recipeBaselineTemp`, used by the idle `ShotPlanItem` widget): a recipe's own yield/temp render as the plain resulting value; only a genuine per-brew tweak *beyond* the recipe highlights. Internally, the temperature side calls `ProfileManager.temperatureDisplay(anchor, hasOverride, overrideTemp, baselineShiftC)`, which reads `m_currentProfile` in C++ — i.e. **the machine's currently-loaded profile**.
- **Source + delta mode** (`profileStepTemps`/`recipeTempOffsetC`, used only by `RecipeDrinkCard.qml`): the card's own profile's frame temps render plain, followed by the recipe's stored offset as a separately-highlighted signed tag (e.g. "84 · 94°C −3°"). Internally, the temperature side calls `ProfileManager.temperatureDisplayForSteps(stepTempsC, anchor, hasOverride, overrideTemp, baselineShiftC)`, which takes an **explicit frame array** instead of reading the loaded profile.

Recipe cards use the second mode specifically because a recipe card must render correctly even when the machine currently holds a *different* profile than the one the recipe is built on (`openspec/specs/recipe-quick-switch/spec.md` — "Cards are immune to the loaded profile"). Swapping cards over to plain baseline mode (`recipeBaselineTemp`) would silently reintroduce that bug, because `ProfileManager.temperatureDisplay` always reads the loaded profile.

User feedback ([#1482](https://github.com/Kulitorum/Decenza/issues/1482)) says the resulting "84 · 94°C −3°" / "36.0 → 40.0g" presentation is confusing. The fix must drop the tag/arrow while preserving the loaded-profile immunity.

## Goals / Non-Goals

**Goals:**
- Recipe cards (management page grid + wizard summary hero) render a recipe's temperature/yield as plain resulting values, with no delta tag and no arrow — visually matching what the idle Shot Plan widget shows once that recipe is active.
- Cards keep resolving and rendering their **own** recipe's profile frames regardless of what profile is currently loaded on the machine (no regression on `recipe-quick-switch`'s "Cards are immune to the loaded profile" scenario).
- `docs/CLAUDE_MD/RECIPES.md` and the GitHub wiki manual describe the new, single behavior — no stale "source + delta" documentation left behind.

**Non-Goals:**
- No change to the post-shot Shot Review dialog (`ShotAnalysisDialog.qml`/`QualityBadges.qml`) or Shot Detail page — already unified and already baseline/shot-relative, not part of this feedback.
- No change to Brew Settings' own override-highlight behavior (`recipe-baseline-not-override`, #1485) — already correct.
- No change to how the recipe's `tempOffsetC`/`yieldG` are stored, edited, or written through — this is a display-only fix.

## Decisions

### Decision: Bake the offset into the frame shift instead of switching card temperature to `recipeBaselineTemp`

`ShotPlanText.qml`'s `_tempStr` step-based branch currently calls:
```qml
ProfileManager.temperatureDisplayForSteps(profileStepTemps, profileTemp, false, 0, 0)
```
passing `0` for `baselineShiftC` (so the frames render unshifted — the "source" half), with the recipe's offset rendered separately by `_tempTagStr` and concatenated in `_build()`. This changes to:
```qml
ProfileManager.temperatureDisplayForSteps(profileStepTemps, profileTemp, false, 0, recipeTempOffsetC)
```
`TemperatureDisplay::format` (the shared C++ formatter) already shifts every frame by `baselineShiftC` when it's non-zero — this is the exact mechanism the baseline path already relies on for the loaded-profile case. Passing the recipe's offset through `profileStepTemps` (the card's own, possibly-not-loaded profile's frames) gets the correct resulting numbers ("81 · 91°C") without borrowing the loaded profile's shape.

`_tempTagStr` (the separately-highlighted "−3°" fragment) and its concatenation onto `temp` in `_build()` are deleted outright — no consumer needs a rendered tag once this lands, since recipe cards are the tag's only consumer.

**Alternative considered:** Add a third boolean prop (e.g. `cardBaselineMode`) to switch behavior explicitly. Rejected — once the tag is gone, the step-based branch has exactly one behavior (shift the given frames and show the result), so no toggle is needed; `recipeTempOffsetC` simply changes role from "value to render as a tag" to "value to fold into the shift," with no new surface area.

### Decision: Yield arrow removal is a call-site change, not a `ShotPlanText.qml` change

`RecipeDrinkCard.qml` currently sets:
```qml
yieldOverridden: card.recipe.yieldG > 0 && card.profileYieldG > 0
                 && Math.abs(card.recipe.yieldG - card.profileYieldG) > 0.1
```
This becomes `yieldOverridden: false`. `ShotPlanText.qml`'s existing `_yieldStr` logic already renders the plain target with no arrow whenever `yieldOverridden` is false — no core logic change needed there. `profileYield`/`targetWeight` bindings are unchanged (still needed as the displayed value and as `_yieldBaseline`'s fallback elsewhere).

**Alternative considered:** Set `yieldTargetOnly: true` instead (the flag `ShotPlanItem.qml` doesn't use for this purpose but exists on `ShotPlanText`). Rejected in favor of `yieldOverridden: false` — it's the more direct statement of "this card has no override to report" and avoids reusing a flag whose name/semantics belong to a different call site (`ShotPlanItem`'s per-instance "hide the profileDefault→target arrow" option).

### Decision: Comments and spec text move from "source + delta" framing to "baseline, resolved against the card's own profile"

`ShotPlanText.qml` (lines ~62-88), `RecipeDrinkCard.qml` (lines ~41-48, ~224-236), `docs/CLAUDE_MD/RECIPES.md`'s "Recipe cards deliberately show the opposite decomposition" paragraph, and `openspec/specs/recipe-quick-switch/spec.md` / `openspec/specs/plan-widgets/spec.md` all currently document the source+delta behavior as deliberate (PR #1498). All of these get updated in the same change so no doc contradicts the code afterward.

## Risks / Trade-offs

- **[Risk]** A reader of `ShotPlanText.qml` could mistake the remaining `profileStepTemps`/`recipeTempOffsetC` props as vestigial once the "source + delta" framing is gone, and try to collapse them into `recipeBaselineTemp`. → **Mitigation**: the updated code comment on `profileStepTemps` explicitly states why it must stay separate (loaded-profile immunity), referencing the `recipe-quick-switch` scenario by name.
- **[Risk]** `docs/CLAUDE_MD/RECIPES.md`'s single paragraph documenting this whole subsystem is dense and easy to under-edit (leaving a stale sentence describing the old tag behavior next to new sentences describing the fix). → **Mitigation**: task explicitly calls for rereading the full paragraph after editing, not just patching the one sentence naming "source + delta."
- **[Trade-off]** Recipe cards no longer visually communicate "this recipe differs from its stock profile" at a glance — a user comparing two recipes on the same profile with different offsets can no longer tell them apart by looking at the temperature alone (both show only resulting values). This is the crux of the user request and is accepted per the proposal; no replacement affordance is in scope here (fredphoesh's follow-up ideas — an asterisk/reset affordance — are a separate potential future change, not part of this one).

## Migration Plan

Pure QML/C++ display-layer change, no data migration. Land as one PR: `ShotPlanText.qml` + `RecipeDrinkCard.qml` + doc/spec updates together, verified by building and visually checking the Recipes page and Recipe Wizard summary step against a recipe with a non-zero `tempOffsetC` and a yield that differs from its profile's target. Rollback is a straight revert (no persisted state changes).

## Open Questions

- Does the GitHub wiki manual (`Kulitorum/Decenza.wiki.git`) currently document the recipe card's temperature/yield display at all? Needs a check during implementation; if it does, update it as part of this change per `CLAUDE.md`'s user-manual policy.
