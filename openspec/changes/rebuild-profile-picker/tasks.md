## 1. C++ foundations

- [x] 1.1 Add `Profile::beverageBucket(beverageType)` (espresso/filter/tea/cleaning) beside `beverageGroup()` in `src/profile/profile.h`; verify with a `_data()` table in `tests/tst_profile.cpp` covering every known type plus empty and unknown
- [x] 1.2 Add `Profile::inferBeverageType(title, steps)` per the `profile-import-beverage-inference` spec; verify with table tests: keyword wins over shape, low pressure → pourover, cold → pourover, default espresso
- [x] 1.3 Call `inferBeverageType` from `VisualizerImporter::parseVisualizerProfile` and both `ProfileImporter` paths only when the raw payload's `beverage_type` is empty/absent, logging the deduction at INFO via the file's registered helper; verify `scripts/check_log_markers.py` passes and a test imports an untagged JSON and reads back the inferred type
- [x] 1.4 Add `favoriteProfileOrder` (`custom|alpha|usage`) to `SettingsApp` with the resolve-when-absent rule, plus serializer export/import; verify tests: absent+favorites → custom, absent+empty → usage, round-trip through the serializer
- [x] 1.5 Add `ShotHistoryStorage::requestProfileUsage()` (threaded `withTempDb`, `GROUP BY profile_name`, emits `profileUsageReady(QVariantMap)`); verify a test saves three shots across two titles and reads back counts and max timestamps
- [x] 1.6 Measure `requestProfileUsage` on a realistic database and a 2× copy; record median and worst case in a comment at the call site; decide on a `(profile_name, timestamp)` index from the numbers only
- [x] 1.7 `ProfileManager`: hold `profileUsage` property, refresh at startup and on shot save; in `usage`/`alpha` modes rewrite the favorites order and re-sync `selectedFavoriteProfile` by filename; verify tests: usage re-sort after a save, alpha re-sort after add/rename, custom never rewritten, selection index follows the profile
- [x] 1.8 `ProfileManager::filterProfiles(chips, search, allowedBeverageTypes)` and `facetCounts(...)`; verify tests: cross-group AND, within-group OR, empty group = all, favorites ⊂ selected, faceted counts, title-only search, beverage constraint hides constrained types

## 2. Shared picker component

- [x] 2.1 Create `qml/components/ProfilePicker.qml` (search, chip rows with counts, sort control, tier rows, `GridView`, empty state) with host properties `beanBrand/beanType/roastLevel/teaType`, `initialChips`, `allowedBeverageTypes`, `showAutoLoadStrip`, and signal `profileChosen(filename)`; add to `CMakeLists.txt`; verify qmllint via the `qmllint_check` target stays at zero
- [x] 2.2 Create `qml/components/ProfileCard.qml` per the card-contents requirement (fixed height, source letter, check badge, modified marker, temp → yield, usage line, derivation caption, pin, sparkle, info, star, ⋮, long-press → `ProfilePreviewPopup`); every interactive element has `Accessible.role/name/focusable/onPressAction`; verify by opening the screen with VoiceOver/TalkBack order sane
- [x] 2.3 Move the profile-actions dialog into the component (Edit / Copy / Rename / Auto-load / Add-Remove Selected / Delete) with the existing toasts and announcements; verify each action from both hosts
- [x] 2.4 Create the favorites reorder dialog (`ProfileFavoritesOrderDialog.qml`) from the old right-hand panel: drag handles, remove, themed; confirm writes order and sets mode `custom`; verify drag from fifth to first reorders idle pills
- [x] 2.5 Wire the dual-role sort control: local `usage|alpha` when Favorites off, bound to `Settings.app.favoriteProfileOrder` plus Custom… when on; verify switching modes reorders the grid and the idle pills

## 3. Hosts

- [x] 3.1 Rewrite `qml/pages/ProfileSelectorPage.qml`: auto-load strip, `+` menu (Visualizer / Tablet-Files / New), picker with Selected on initially, tap → `ProfileManager.loadProfile`; delete the favorites panel, checkbox column, view combo, category grouping; verify page opens, filters compose, current card highlighted
- [x] 3.2 Replace the wizard profile step (`RecipeWizardPage.qml` model + tiles) with the picker: pass bag identity, `allowedBeverageTypes` from the drink template, no chips initially, keep "Just hot water" as a fixed card below the grid, tap → `selectProfile`; verify tea/filter/espresso drink types list only their sets and Beverage chips are hidden
- [x] 3.3 Bean tiers in the selector from `Settings.dye` bean + current bag roast; verify tiers appear with a bean set and vanish without
- [x] 3.4 Expose `beverage_type` in the profile editor metadata so an inferred tag can be corrected; verify changing it and saving updates the picker's bucket

## 4. Verification

- [x] 4.1 Full suite green through `mcp__qtcreator__run_tests` scope `all`
- [x] 4.2 `text-invariants` checks locally: `scripts/check_log_markers.py`, `scripts/check_test_source_duplication.py`, `qmllint_check` target
- [ ] 4.3 Live check on desktop: open Profiles, toggle every chip, search, sort, star, ⋮ actions, long-press preview, reorder dialog, idle pills follow order; open wizard for espresso, filter, tea
- [ ] 4.4 Import an untagged profile JSON via file import and confirm the inferred `beverage_type` and its INFO log line

## 5. Docs

- [ ] 5.1 Rewrite the wiki manual "Profiles" section (short: search, chips, sort, star, ⋮) and add one line under the idle page on favorites order; verify by reading it at half its first-draft length
- [x] 5.2 Update `docs/CLAUDE_MD/RECIPE_PROFILES.md` pointers for the shared picker and the import inference; verify the file references only functions that exist
- [ ] 5.3 Archive the change with `openspec archive rebuild-profile-picker --yes` as the branch's final commit

## 6. Remove Selected

Post-launch maintainer decision: fold "Selected" into favorites entirely rather than keep it as a second membership concept. Favorites are the only membership from here on.

- [x] 6.1 Delete `SettingsApp::selectedBuiltInProfiles`/`hiddenProfiles` (Q_PROPERTYs, accessors, signals, add/remove/is helpers); add `takeLegacySelectedLists()` (raw read, for the merge only) and `selectedMergedIntoFavorites()`/`setSelectedMergedIntoFavorites()`; `addFavoriteProfile` no longer un-hides/selects; the eager auto-load clear moves from `addHiddenProfile`/`removeSelectedBuiltInProfile` to `removeFavoriteProfile`
- [x] 6.2 `ProfileManager::mergeSelectedIntoFavoritesIfNeeded()`: one-time startup merge (constructor, after `refreshProfiles()`, before `persistFavoriteProfileOrderIfAbsent()`) that computes the old Selected set from the legacy keys and appends whatever wasn't already a favorite, alphabetically by title, after the existing favorites, capped at 50 with a `DIAG_WARN` naming the overflow; delete `isProfileInSelectedList()`, the `selected` chip from `profileMatchesFilters()`/`facetCounts()`, the two `*Changed` connects, and `duplicateProfile`'s `addSelectedBuiltInProfile` call; `loadAutoLoadProfileIfNeeded()` now checks `isFavoriteProfile()`
- [x] 6.3 MCP `auto_load` (`src/mcp/mcptools_write.cpp`) `set` gate checks `isFavoriteProfile`, errors `Profile is not a favorite`; update `resources/ai/tools/auto_load.md` and the tool's `filename` property description to match
- [x] 6.4 `SettingsSerializer`: drop `selectedBuiltIns`/`hiddenProfiles` from export; import ignores them if an old backup still carries them
- [x] 6.5 QML: delete the Selected chip, `chipSelected`, `initialChips.selected`, the `selected` key in `buildChips()`, the ⋮ Selected toggle and `profileIsSelected` in `ProfilePicker.qml`; delete `isSelected`/the check badge in `ProfileCard.qml`; `ProfileSelectorPage.qml` opens with `initialChips: ({ favorites: true })`; auto-load strip/⋮ visibility key on `isFavoriteProfile`
- [x] 6.6 Fix stale comments referencing the removed list (`profile.cpp`'s `profile_hide` passthrough note, `profilemanager.h`'s `autoLoadStaleCleared` doc) and `docs/CLAUDE_MD/RECIPE_PROFILES.md`'s Auto-Load eligibility / shared-picker sections
- [x] 6.7 Tests: replace `eagerClearOnAddHiddenProfile`/`eagerClearOnRemoveSelectedBuiltIn` with one favorites-removal eager-clear test; drop the Selected case from the filter test (renamed `filterProfilesSearchIsTitleOnly`); add `selectedMergesIntoFavoritesOnceAtStartup` (seeds the raw legacy keys, checks order + the one-time flag + idempotence on a second construction); fix `tst_tclimport.cpp`'s `isHiddenProfile` comment
