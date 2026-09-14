## Context

See proposal.md for motivation. What shapes the approach:

- `ProfileSelectorPage.qml` (1554 lines) and the wizard's profile step (`RecipeWizardPage.qml:2721-2898`, model at `:1608-1750`) share nothing. The wizard uses `Flow` + `Repeater`, so every tile is instantiated; each has `MultiEffect` icons.
- `Settings.app.favoriteProfiles` is an ordered JSON list of `{name, filename}`. `selectedFavoriteProfile` is an ABSOLUTE index into it; `profilemanager.cpp:356,1973` recompute it by position, and IdlePage/EspressoItem/LayoutItemDelegate read it as an index.
- "Selected" — the two settings `selectedBuiltInProfiles` (opt-in) and `hiddenProfiles` (opt-out), read through `ProfileManager::isProfileInSelectedList()` — is **removed** (D8). Favorites are the only membership now; the auto-load gate (`loadAutoLoadProfileIfNeeded`) and MCP `auto_load` check `isFavoriteProfile` instead. A one-time startup merge folds whatever the old Selected list named into favorites, once, before it is deleted from under existing users.
- Shot history stores the profile TITLE in `shots.profile_name` (`shothistorystorage.cpp:2438`); `idx_shots_profile` exists.
- Bean ranking exists: `ShotHistoryStorage::requestRankedProfilesForBean(brand, type, roast, teaType)`, async, `rankedProfilesForBeanReady` with `withBean` and `similar`.
- `Profile::fromJson` defaults an absent `beverage_type` to `"espresso"` (`profile.cpp:945`); the tcl path likewise (`:1352`). Absence is lost at parse.
- `ProfilePreviewPopup` already takes `profileFilename` + `profileData` and renders `ProfileGraph`.
- Idle pill rows freeze order while open (#1673, `IdlePage.qml:293`) because activating a recipe pill re-sorts. Profiles will re-sort only on shot save.

## Goals / Non-Goals

**Goals:**
- One component, two thin hosts. The host supplies bean identity, initial chips, a beverage constraint, and receives `profileChosen(filename)`.
- Virtualised grid: 100+ cards must open without a hitch on the Decent tablet.
- No change to the favorites data shape or to `selectedFavoriteProfile` index semantics.
- No new persisted UI state beyond `favoriteProfileOrder`.

**Non-Goals:**
- A ShotServer `/profiles` page. None exists today.
- Re-tagging profiles already on disk.
- Touching MCP tool surfaces (`profiles_list`, `auto_load`).
- Replacing the wizard's ranking logic; it is reused as-is.

## Decisions

**D1. Stored favorites list IS the display order; the mode governs who writes it.**
Alternative: sort at consumers and switch `selectedFavoriteProfile` to a filename key. That touches four QML readers, ProfileManager sync points, the serializer and MCP. Rewriting the stored order instead touches nothing that reads it. `ProfileManager` re-sorts in `alpha` on add/rename and in `usage` on startup and `shotSaved`, then re-syncs the selected index by filename before writing. Cost accepted: switching custom → usage → custom keeps the usage order.

**D2. Usage from shot history, not a load stamp.**
One threaded query: `SELECT profile_name, MAX(timestamp), COUNT(*) FROM shots GROUP BY profile_name`, `withTempDb()`, result posted queued as `QVariantMap title → {lastTimestamp, count}` held on `ProfileManager` (`profileUsage` property). Refreshed at startup and on shot save. Keyed by title because that is what history stores; a rename drops to never-used until the next shot. Serves usage order, the Recently-used sort and the card usage line. No `last_used` column, no stamp in `loadProfile`. Measure the query on a large database first; `MAX(timestamp)` reads row pages that carry the blobs, so cost tracks table bytes. No index unless the measurement says so, per CLAUDE.md.

**D3. Filtering in C++, one predicate, one facet counter.**
`ProfileManager` exposes `filterProfiles(chips, search, beverageConstraint)` and `facetCounts(chips, search, beverageConstraint)` over the in-memory catalogue. Chips are a plain map `{favorites, sources[], beverages[]}`. One definition of the beverage bucket mapping (`Profile::beverageBucket()` beside `beverageGroup()`), used by the predicate, the facet counter and the tests. Alternative: JS in the component, as PR #1944 did. Rejected: two hosts, tests, and the same mapping would be needed in the C++ facet counter anyway.

**D4. Layout: one recommended row in the `GridView` header.**
`GridView` has no section headers, but it has a `header` delegate that scrolls with the content. The bean row (exact-bean matches first, then recommendations) is short, so it is a horizontal row of cards in the grid header; the grid virtualises the "All" section. Card height fixed (three rows: title with actions, meta + usage, reason or derivation) so a grid row never staggers.

**D5. Sort control is grid-only; favorites order has its own button.**
The first cut bound the Sort dropdown to the favorites order setting whenever the Favorites chip was on. Live check: nobody would guess the chip unlocks a setting. Now the dropdown holds a component-local `sortMode` (`usage` default, `alpha`) and a Favorites… button beside it opens `ProfileFavoritesOrderDialog`, which carries the A–Z / Recently used / Custom choice and the drag list. One door, always visible, labelled for what it changes.

**D6. Wizard beverage constraint hides the Beverage chip group.**
The drink type already fixed the beverage; showing chips that can only narrow inside it is noise. The constraint is a host property (`allowedBeverageTypes`), empty on the selector. The recipe-wizard spec's tea temperature-proximity order is dropped in favour of the shared sort control.

**D7. Inference at import, never in `fromJson`.**
`Profile::inferBeverageType(title, steps)` is a static beside `beverageGroup()`. `VisualizerImporter::parseVisualizerProfile` and `ProfileImporter` check the RAW payload for an empty/absent key and call it. `fromJson`'s default stays, so every other reader is untouched. Keyword pass first, shape second, espresso last. Logged at INFO through the registered import helper so the user-visible log says what was deduced.

**D8. Selected removed; favorites are the only membership; one-time merge.** The maintainer decided against carrying "Selected" forward as a second, separate membership concept alongside Favorites — it duplicated what the star already does and gave cards a second badge nobody could explain at a glance. `selectedBuiltInProfiles`/`hiddenProfiles`, `ProfileManager::isProfileInSelectedList()`, the card's check badge and the ⋮ "Add to/Remove from Selected" entry are all deleted. Auto-load eligibility and the MCP `auto_load` gate key on `isFavoriteProfile` instead. `SettingsApp::addFavoriteProfile` no longer un-hides/selects (nothing to un-hide/select); the eager-clear that hiding or de-selecting the auto-load profile used to do moves to `removeFavoriteProfile`. A one-time upgrade (`ProfileManager::mergeSelectedIntoFavoritesIfNeeded()`, gated by `SettingsApp::selectedMergedIntoFavorites()`, run once at startup after the catalogue loads) computes the old Selected set one last time from the two legacy keys and appends whatever wasn't already a favorite, alphabetically by title, after the existing favorites — so existing positions and `selectedFavoriteProfile`'s index survive untouched, capped at 50 with a `DIAG_WARN` naming how many were left out.

**D9. `+` menu.** Visualizer / Tablet-Files / New collapse into one `AccessibleButton` opening a `DecenzaDialog` with three actions, same accessible pattern as the ⋮ dialog.

## Risks / Trade-offs

- [Usage query cost on a large database] → measure on a realistic DB and a 2× one before merging; thread it regardless; add `(profile_name, timestamp)` only if the measurement demands it, with the numbers at the call site.
- [Title-keyed usage misses after rename] → accepted and stated in the spec; next shot heals it.
- [Two profiles sharing a title share usage] → already true of the shot list; accepted.
- [Re-sort moving the selected favorite index] → re-sync by filename in the same write; scenario covered.
- [Tea without "tea" in the title inferred as pourover] → same beverage group for the ratio rule; Filter chip still finds it; editor exposes `beverage_type` (verify, task).
- [`pragma ComponentBehavior: Bound` + required properties on delegates] → GridView delegate declares every role required in the same edit; open the screen, the compiler cannot catch it.
- [Emoji/colour glyphs on cards] → source letter is plain text, icons are themed SVGs via `ColoredIcon`, no emoji.

## Migration Plan

No data migration. `favoriteProfileOrder` is a new settings key with a resolve-when-absent rule. Serializer gains the key for backup/restore. Rollback is a code revert; stored favorites are never reshaped except by the user's own reorder or by usage/alpha re-sorts, which are the feature.

## Open Questions

- Whether the derivation caption reads "derived from X" or a shorter form; wording only, decided at implementation.
