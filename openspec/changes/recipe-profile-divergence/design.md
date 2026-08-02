## Context

The rule "the active recipe's profile is the loaded profile" is enforced at one
site, by one comparison, reachable by one route.

```
route                              reaches the watcher?
─────────────────────────────────  ────────────────────
user selects a different profile   yes  — currentProfileChanged, cache is warm
user edits the profile in place    n/a  — title unchanged, nothing to enforce
user deletes the recipe's profile  NO   — deleteProfile emits nothing
startup restores the recipe        NO   — cache empty when the profile loads
```

Two facts make the startup hole structural rather than a race to be tightened.

**The profile is always loaded first.** `ProfileManager`'s constructor
(`profilemanager.cpp:131`) performs the startup load inline and sets
`m_startupLoadDone` at `:366` — all of it inside `new ProfileManager(...)` at
`maincontroller.cpp:130`. The recipe row is requested asynchronously at the very
end of `MainController`'s constructor (`:1388`). So on every launch the loaded
profile is settled before the recipe cache exists, and `currentProfileChanged`
has already been emitted — to a watcher that is either not yet connected or
guarded off by `m_activeRecipe.isEmpty()` (`:1286`).

**The shot stamp does not read the cache.** `metadata.recipeId` comes from
`m_settings->dye()->activeRecipeId()` (`:3675`), which is restored synchronously
from settings. A shot pulled in the window — or any time after it, since nothing
ever re-checks — is stamped with a recipe whose profile it did not run.

## Goals / Non-Goals

**Goals:**

- The invariant holds by any route the profile can stop matching.
- One predicate, asked at every site — the "centralize anything produced at more
  than one site" rule, applied before the second hand-written comparison exists.
- A recipe that owns no profile is never deactivated by a profile change.

**Non-Goals:**

- Repairing shots already recorded with a mismatched recipe. They are history;
  rewriting them would be a migration inventing facts.
- Clearing a recipe's dangling `profile_title` when its profile is deleted. A
  recipe naming a profile the user might re-import is not corrupt, and
  deactivation already leaves the recipe row untouched by existing rule.
- Refusing to delete a profile that a recipe names. Blocking cleanup to protect
  a pointer is the wrong trade; deactivating is enough.
- Anything about in-place profile edits. The title is unchanged, the recipe
  still points at the right profile, and no watcher should fire.

## Decisions

### One predicate: does the active recipe own the loaded profile?

A single `MainController` helper answers it, and all three sites call it:

- empty `profileTitle` → the recipe owns no profile choice → never a mismatch
- otherwise → compare against `m_profileManager->currentProfile().title()`

This is what the bag and equipment watchers already do (`hasBeanLink` at
`:1268`, `recipeEq > 0` at `:1281`), and what the comment above the watcher
block already claims the profile watcher does. Making it a named function rather
than a fourth inline comparison is the point: three copies of a comparison are
free to drift, and the drift is invisible until a shot is stamped wrong.

*Consequence:* the profile watcher gains the ownership gate as a side effect of
routing through the helper, rather than as a separate fix that could be
forgotten.

### The predicate is a pure function so it can be tested

No test in the suite constructs a `MainController` — the watchers are lambdas in
a constructor that needs BLE, every storage, and the QML engine. A predicate
written as a member reading `m_activeRecipe` and `m_profileManager` inherits
that, and would ship with the same coverage the current watcher has: none.

So the predicate takes the two titles and returns the answer, with no `this`.
The three sites each read their own state and call it. What stays untested is
the wiring — which signal reaches which handler — and that is the same manual
verification the QML surfaces get. What becomes testable is every rule the bug
was made of: the empty-title ownership gate, the comparison, and the exact
trim/case semantics.

*Consequence:* the empty-title check has one definition. The codebase currently
spells this rule three different ways in nearby code — `trimmed().isEmpty()` at
`maincontroller.cpp:1211` and `:1237`, and nothing at all in the watcher at
`:1288`. The function fixes which one is right.

### Deletion reconciles through a signal, not a call

`ProfileManager` does not know recipes exist and must not learn. It emits that a
profile was deleted; `MainController` — which already owns both the recipe cache
and every other deactivation watcher — decides what that means.

*Alternative considered:* reuse the existing `profilesChanged()`. Rejected: it
fires on every catalog refresh, including import, migration and startup scan, so
the handler would have to re-derive "did the one I care about disappear" on each
one. A deletion signal carries the fact directly.

*Alternative considered:* have `deleteProfile` load a replacement profile, so
the existing `currentProfileChanged` watcher fires and handles it. Rejected —
deleting a profile from a management screen must not silently switch what the
machine is about to brew, and it would make the fix depend on a side effect
rather than state the rule.

### Reconcile when the restored row arrives, not on a timer or a retry

`recipeReady` (`:1182`) is the exact moment the missing half of the comparison
becomes available. It already deactivates on the row being empty or archived —
a row that names a profile which is not loaded belongs in the same branch. No
flag, no deferred re-check, no "has startup finished" state: the data arriving
IS the event. (Per the project's standing rule, a timer here would be a guard
around a missing condition.)

`recipeReady` handles cache refresh and the startup restore only — activation
goes through `recipeActivationReady` (stated at `:1204-1206`), so the reconcile
cannot fight the activation that is in the middle of making them match.

### An external edit that re-points the profile also deactivates

`recipeReady` also fires after an external edit (composer, MCP, web). If that
edit changed `profileTitle`, the loaded profile is no longer the recipe's, and
the honest answer is the same one the watcher gives when the profile moves
instead of the recipe. Both directions of the same divergence, one rule.

### Deletion deletes; the recipe is honest about it

A profile the user deletes is gone. The recipe that named it does not quietly
acquire a frozen copy to keep working — it shows that its profile is missing,
and the user re-points it in the recipe editor, which already edits
`profileTitle` (`RecipeWizardPage`, `fProfileTitle`). Nothing new is built for
repair; the delete path only informs.

*Alternative considered:* snapshot the profile's JSON into every affected recipe
before deleting the file, using the `profile_json` fallback `applyActivatedRecipe`
already consults (`:1568`). It would work — and it is the wrong contract. A user
who deletes their profile has deleted it; a recipe that silently keeps brewing
from a hidden copy is a reference that became a snapshot without anyone saying
so, which is the exact confusion `RECIPES.md` records elsewhere.

*Alternative considered:* refuse the delete while recipes name the profile.
Rejected — it makes other objects' references a veto over the user's own
library.

### "Missing profile" is derived, never stored

The marker is computed where the recipe is displayed, from the in-memory profile
catalog (`findProfileByTitle`, a linear scan over `m_allProfiles`,
`profilemanager.cpp:1014` — no I/O, safe per card). Nothing scans the recipe
table, nothing writes a flag, and no migration marks anything.

That is what makes it correct for every route into the state — deletion, a
mistyped `profileTitle` from MCP or the web, a restored database, a device
migration arriving without its profiles — and what makes it self-correcting: the
instant the profile is re-imported, the marker is gone, with no repair step.

**The dependency is the trap here.** `findProfileByTitle` is a `Q_INVOKABLE`, so
a binding that calls it records **no dependency** and would never re-evaluate
when the catalog changes — a deleted profile would leave every card still
looking fine until the page was rebuilt. This is the `translate` failure
documented in CLAUDE.md, and it fails silently and only at runtime. The binding
must depend on the catalog-changed signal, or the reachability must be exposed
as a property rather than an invokable.

### The delete warning counts; it does not enumerate

The confirmation says how many recipes use the profile. Listing them invites a
per-recipe repair flow in the delete dialog, which is explicitly not being
built. The count is what the user needs to decide; the recipe list is where the
consequences are then visible.

The count comes from a query on `profile_title`, on a discrete user action,
against a table with tens of rows — inline is correct, and threading it would be
the cache that costs more than the thing it avoids.

### A failed activation says which profile is missing

`recipeActivated(id, false)` is emitted today and no QML handles it — the pill
reverts and nothing explains why. The signal already exists and already carries
the failure; only the surfacing is absent. The message names the missing profile
title, because that is the string the user has to go and fix in the editor.

*Consequence:* this covers activation failures that have nothing to do with
profiles too — a recipe row that vanished (`:1541`) currently fails just as
silently.

## Risks / Trade-offs

- **A user deleting a profile loses their recipe selection.** Correct, and
  quieter than the alternative — today they keep a selection that silently
  mislabels every subsequent shot. Deactivation already has a visible signal
  (the pill deselects).
- **The startup reconcile could deactivate on a launch where the profile
  genuinely failed to load for an unrelated reason** (a corrupt file refused by
  the validator, `loadProfile` `:1564`). The recipe is then dropped for what is
  arguably a transient fault. Accepted: the loaded profile really is not the
  recipe's, and pretending otherwise is what produced the bad shots. The user
  re-selects the recipe once the profile is fixed.
- **Profile-less recipes stop deactivating on profile changes.** This is a
  behaviour change users could notice, and it is the documented intent
  (`recipe-activation`: "a recipe without that rung doesn't own the choice").
- **Recipes that are already broken start looking broken.** Users will discover
  recipes they thought were fine — recipe 43 in the development database is one.
  That is the point: they were already unactivatable, just not visibly so.
- **The marker depends on the catalog being loaded.** If a recipe list can be
  shown before `m_allProfiles` is populated, every recipe would momentarily read
  as missing its profile. `ProfileManager`'s constructor scans the catalog
  before `MainController` finishes building, so this should not arise — but it
  is the failure mode to check when verifying, because it is loud and wrong
  rather than quiet and wrong.

## Migration Plan

None. No schema, no persisted state, no version gate.

Already-recorded shots keep their `recipe_id` and their frozen `profile_json` —
they remain the honest record of what ran, and the mismatch between them stays
readable for anyone looking. Only new shots are affected.

## Open Questions

- Should the shot record note that its recipe's profile did not match, rather
  than relying on the recipe never being wrong? A defence in depth at the point
  of record. Out of scope; noted because this bug was only found by comparing
  the two fields after the fact.
- `deleteProfile` also leaves `m_currentProfile` holding a profile whose file is
  gone until the next load. That is a separate latent issue with its own blast
  radius (upload, favorites, `_current.json`) and is deliberately not touched
  here.
