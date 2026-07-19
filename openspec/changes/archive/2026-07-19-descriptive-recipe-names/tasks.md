## 1. Profile cleanup helper

- [x] 1.1 Add a pure `cleanProfileForName(title, typeWord)` helper (local to `RecipeWizardPage.qml`, beside `suggestName()`): strip a leading `D-Flow/` / `A-Flow/` editor-prefix segment, trim, and de-stutter a trailing type word (return "" when nothing survives).
- [x] 1.2 Leave non-editor titles untouched (no `/` prefix → returned as-is, trimmed).

## 2. Descriptive suggestName()

- [x] 2.1 Extend `suggestName()` to append `" · " + cleanProfileForName(fProfileTitle, typeWord)` when a profile is selected and the cleaned token is non-empty; keep the existing bean + short-type prefix, the bean-stutter rule, and the `_autoName`/empty guard unchanged.
- [x] 2.2 Skip the profile token entirely for profile-less (hot-water tea) recipes.
- [x] 2.3 Ensure the profile-selection handler calls `suggestName()` so a profile change updates an untouched name (already called in `selectProfile()`).

## 3. Collision qualifier

- [x] 3.1 Obtain the set of existing non-archived recipe names via `MainController.recipeStorage` (cached in `_existingRecipeNames` on `onInventoryReady`, requested in `Component.onCompleted`); degrade to no-qualifier if the set isn't loaded yet.
- [x] 3.2 When the composed name collides, append the first differing dial-in axis — yield (ratio "1:2.5" or absolute "40g"), else dose ("20g"); never a bare numeric counter.
- [x] 3.3 Confirm the qualifier is only auto-applied under the existing untouched-name guard.

## 4. Idle pill rows fit two rows (live) — Recipes, Beans, Profiles; BOTH rendering paths

- [x] 4a.1 Add a shared greedy packing helper `packPageSizes(widths, spacing, availWidth, maxRows)` (pure JS, `qml/components/layout/PillFit.js`) that returns per-page pill counts (variable per page), capping each page at `maxRows` rows. Registered in `CMakeLists.txt`.
- [x] 4a.2 Compact-bar popups: `RecipesItem`, `BeansItem`, `EspressoItem`, `EquipmentItem`, `FlushItem`, `HotWaterItem` — replace fixed-5 slicing / fixed cap / unbounded wrap with a live two-row fit (measure with a `TextMetrics` mirroring `PresetPillRow`'s constants; pack against the child's real `effectiveMaxWidth`, two passes for the arrow gutter; maxRows = 2). Keep the existing wiring; map absolute-index rows (profiles/flush/hot water) page-relative ↔ absolute; equipment keeps its full inventory (drop the `slice(0,5)` cap).
- [x] 4a.3 Center-zone expansion: `IdlePage.qml` — same live two-row fit for recipes, beans, profiles, equipment, flush, and hot-water rows, driving each off its Loader width (the pill-row id is Component-scoped). Reset each page index on activation; map absolute-index rows' select/long-press to absolute indices; announce the visible page; equipment keeps its full inventory.
- [x] 4a.4 Cross-reference comments in every caller + the helper noting the pill-metric constants mirror `PresetPillRow` and must stay in sync.
- [x] 4a.5 Leave `PresetPillRow.qml` unchanged (its width-flow + pagination arrows are reused as-is).

## 4. Tests

- [x] 4.1 QML is tested manually in this project (no QML test harness; logic lives entirely in `RecipeWizardPage.qml`). No automated test added — coverage is the manual verification in task 6. The naming logic was deliberately NOT extracted into a C++ helper just to unit-test it (over-building for QML-only presentation logic).

## 5. Documentation

- [x] 5.1 Update the `suggestName()` note in `docs/CLAUDE_MD/RECIPES.md` (Surfaces / `DrinkType.suggestName()` description) to reflect profile inclusion, cleanup, and the collision qualifier.
- [x] 5.2 Update the wiki Manual recipe-naming wording to match — done and pushed (Manual.md: Summary step name-field note + search tie-in; commit 931de68 on `Decenza.wiki@master`).

## 6. Code-review follow-ups

- [x] 6a.1 Gate `requestInventory()` in `Component.onCompleted` to the blank-create branch only — it must not queue ahead of edit mode's `requestRecipe()` on the FIFO DB worker (edit/promote/clone never use the collision list). (Historical-context review, Issue 1.)
- [x] 6a.2 Correct the `suggestName()` collision comment + `docs/CLAUDE_MD/RECIPES.md` + spec to describe a display-name match appending the draft's own yield/dose (not "first differing axis" / "same bean+type+profile identity"). (Comment-analyzer review.)
- [ ] 6a.3 (Optional) Decide whether to add an `onRecipesChanged → requestInventory()` refresh for `_existingRecipeNames`, or accept the documented stale-cache trade-off. (Historical-context review, Issue 2.)

## 6. Verify

- [x] 6.1 Build and drive the wizard: confirmed a first recipe shows `bean + type + profile`, a `D-Flow/` profile is cleaned, a hot-water tea shows no profile token, a same-profile duplicate gains a yield/dose qualifier, and a user-typed name is never overwritten (user tested the build 2026-07-19).
- [x] 6.2 Build and drive every paged idle pill row (Recipes, Beans, Profiles, Equipment, Flush, Hot water) — in BOTH the compact-bar popup and the center-zone expansion — with long names: confirm pills never exceed two rows, the per-page count adapts to name length, arrows appear only when more than one page exists, paging doesn't activate/select/reorder, and a tap on a later page acts on the correct (absolute) item.
