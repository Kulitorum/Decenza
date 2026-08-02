# A recipe must not stay active on a profile it does not own

## Why

Three shots on 2026-07-28 (08:47, 08:58, 09:17) were recorded as made with
recipe 43, whose `profile_title` is `A-Flow / ZZ Fmt Test aflow`. All three ran
`D-Flow / Q`. Recipe 43's `updated_at` is 09:17:42 — the exact timestamp of the
last of them, a live write-through stamp, so the recipe was genuinely active
across all three. The profile it names does not exist on disk; its sibling test
profiles (`zz_fmt_test_advanced/flow/pressure`) survive, so that one was deleted.

The result is history that lies: the shot claims a recipe whose profile it never
ran, and the recipe's dose/grind stamps were written from a shot pulled under a
different profile.

One watcher is supposed to prevent this — `MainController`'s
`currentProfileChanged` handler (`src/controllers/maincontroller.cpp:1285`),
which deactivates when the loaded profile's title stops matching the recipe's.
It covers exactly one route out of three, and its own ownership rule is not
implemented.

**The profile can stop matching without a selection.**
`ProfileManager::deleteProfile` (`:1099`) removes the file, clears favorites and
auto-load, refreshes the list, and returns. It never touches `m_currentProfile`,
never emits `currentProfileChanged`, and never reconciles a recipe pinned to the
deleted title. The recipe stays active pointing at nothing.

**Startup cannot see the mismatch at all.** `ProfileManager::initialize` loads
the persisted profile (`src/controllers/profilemanager.cpp:360`); with the file
gone, `loadProfile` falls through to `loadDefaultProfile()` (`:1545`) and a
different profile is now loaded. `MainController` restores `activeRecipeId` and
fires an async `requestRecipe` (`maincontroller.cpp:1382-1388`), so
`m_activeRecipe` is still empty — and the watcher returns early on exactly that
(`:1286`). The `recipeReady` handler that fills the cache (`:1182`) deactivates
only on an empty or archived row; it never compares the profile.

Shot metadata reads `m_settings->dye()->activeRecipeId()` directly (`:3675`),
independent of `m_activeRecipe`, so the stamp lands even while the cache the
watchers depend on holds nothing.

**The watcher deactivates recipes that own no profile.** Its two siblings check
ownership first — the bag watcher on `hasBeanLink` (`:1268`), the equipment
watcher on `recipeEq > 0` (`:1281`). The profile watcher compares raw. A
profile-less recipe (tea, hot water) has an empty `profileTitle`, which the code
special-cases elsewhere (`:1211`, `:1237`), so it mismatches every title and any
profile change drops it. The comment above the watcher block already claims "a
recipe without that rung doesn't own the choice"; the code does not do that.

Note what is NOT a defect: editing a profile in place keeps its title, no
watcher fires, and none should — the recipe still points at the right profile.

**Deleting a profile silently breaks every OTHER recipe that names it.** The
active recipe is only the visible instance. `applyActivatedRecipe` refuses to
activate a recipe whose profile is neither installed nor carried as frozen JSON
(`:1568`) — correctly, since "the profile IS the drink" (`:1550`). But the
frozen JSON is the exception, not the rule: 14 of 16 recipes in the development
database carry none. And the refusal reaches nobody. `recipeActivated(id,
false)` is emitted, `MainController` logs a `qWarning` and reverts the pill
(`:1364`), and **no QML handles the signal** — the two references in `qml/` are
`BrewDialog` re-seeding on success. The user taps the pill, it lights, it
un-lights, and nothing says why. The recipe list marks nothing either.

Recipe 43 is in that state today: present in the list, indistinguishable from a
working recipe, unactivatable. Deletion is not the only route — the same comment
names a mistyped `profileTitle` from MCP or the web, and a restored database or
a device migration arriving without its profiles does it wholesale.

## What Changes

- **Reconcile at profile deletion.** Deleting a profile that the active recipe
  names deactivates the recipe. The `deleteProfile` path gains the reconcile;
  today it is the one lifecycle event that changes what a title resolves to
  without telling anyone.
- **Reconcile when the restored recipe arrives.** The `recipeReady` handler
  compares the row's `profileTitle` against the loaded profile and deactivates
  on a mismatch, closing the startup window where the watcher is structurally
  blind. This is the same rule the watcher applies, moved to the one moment the
  watcher cannot reach.
- **Gate the watcher on ownership.** A recipe with an empty `profileTitle` owns
  no profile choice and is left alone by the profile watcher, matching the bag
  and equipment watchers and the comment that already describes this.
- **One shared predicate.** All three sites ask the same question — does this
  recipe own a profile, and is it the loaded one — through one helper, not three
  hand-written comparisons.
- **Deleting a profile warns when recipes name it.** The user is told how many
  and confirms. Deletion is never refused — a user may delete their own
  profiles.
- **A recipe whose profile is missing says so.** The recipe list marks it, so an
  unactivatable recipe is distinguishable from a working one at a glance rather
  than only by tapping it. This covers every route to the state, not just
  deletion.
- **A failed activation is visible.** The pill silently reverting is replaced by
  a message naming the missing profile, so the user knows what to fix.

Explicit non-goals:

- **No snapshotting the profile into affected recipes.** Freezing a copy at
  delete time would keep them working, and is deliberately rejected: a user
  deleting their own profile should have it deleted, and the recipe should be
  honest that its profile is gone rather than quietly carrying a hidden copy.
- **No repair UI in the delete flow.** Re-pointing a recipe at a working profile
  is what the existing recipe editor already does (`RecipeWizardPage` edits
  `fProfileTitle`). The delete path warns; the recipe list marks; the user fixes
  it where recipes are edited. No new editing surface.
- **No bulk pass over the recipe library.** Nothing scans, marks, migrates or
  repairs stored recipes. "Missing profile" is derived per recipe at the moment
  it is displayed, from the profile catalog already in memory — it is a display
  state, never a stored one, so it corrects itself the instant the profile is
  re-imported.
- No change to what deactivation itself does (the recipe row stays untouched),
  no repair of already-recorded shots, and no change to in-place profile edits.

## Capabilities

### Modified Capabilities

- `recipe-activation` — the deactivation rule under "Tweaks write through;
  ingredient swaps deactivate" currently says "manually changing the profile".
  It becomes a rule about the recipe's profile ceasing to be the loaded one,
  by any route, and gains the ownership gate.

## Impact

- `src/controllers/maincontroller.cpp` — the `currentProfileChanged` watcher
  (`:1285`), the `recipeReady` handler (`:1182`), and the shared predicate.
- `src/controllers/profilemanager.cpp` — `deleteProfile` (`:1099`) emits a
  deletion signal and reports how many recipes name the profile.
- `src/history/recipestorage.cpp` — a count of recipes by profile title, for
  the delete warning.
- `qml/` — the delete confirmation gains the recipe count; `RecipeDrinkCard`
  (`:87-88`, which prints the title today with no idea whether it resolves)
  marks a missing profile; a failed activation becomes visible instead of a
  reverting pill.
- `tests/` — existing test files gain slots; no new file (a new `tst_*.cpp`
  costs ~1.4 s of build forever, a new slot costs milliseconds). The QML is
  verified manually, per project convention.
- `docs/CLAUDE_MD/RECIPES.md` and the wiki manual.

No schema change, no migration, no stored state added. Existing shots keep
their recorded `recipe_id`; this stops new ones from being recorded wrong.
