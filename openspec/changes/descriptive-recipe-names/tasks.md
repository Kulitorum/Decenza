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

## 4. Tests

- [x] 4.1 QML is tested manually in this project (no QML test harness; logic lives entirely in `RecipeWizardPage.qml`). No automated test added — coverage is the manual verification in task 6. The naming logic was deliberately NOT extracted into a C++ helper just to unit-test it (over-building for QML-only presentation logic).

## 5. Documentation

- [x] 5.1 Update the `suggestName()` note in `docs/CLAUDE_MD/RECIPES.md` (Surfaces / `DrinkType.suggestName()` description) to reflect profile inclusion, cleanup, and the collision qualifier.
- [x] 5.2 Update the wiki Manual recipe-naming wording to match — done and pushed (Manual.md: Summary step name-field note + search tie-in; commit 931de68 on `Decenza.wiki@master`).

## 6. Verify

- [ ] 6.1 Build and drive the wizard: confirm a first recipe shows `bean + type + profile`, a `D-Flow/` profile is cleaned, a hot-water tea shows no profile token, a same-profile duplicate gains a yield/dose qualifier, and a user-typed name is never overwritten.
