## Why

The Profiles page is one of the oldest screens in the app: a six-way combo box that shows one list at a time, a search box that only works under "All Profiles", a checkbox column, and a separate favorites panel that is the only place favorites can be ordered. With ~100 bundled profiles plus downloads, finding "the downloaded filter profiles" or "the built-in tea profiles" means scrolling or switching views. The recipe wizard's profile step already has the better shape (search, cards, bean-ranked tiers) but shares no code with the Profiles page, so the two drift.

## What Changes

- **One shared profile picker** (`ProfilePicker` component) hosted by both `ProfileSelectorPage` and the recipe wizard's profile step. Cards in a virtualised grid, replacing the selector's rows and the wizard's inline tiles.
- **Search + composable filters.** Search applies everywhere. Filter chips: Selected, ★ Favorites, Source (Built-in / Downloaded / Mine), Beverage (Espresso / Filter / Tea / Cleaning). AND across groups, OR within a group, empty group = all. Each chip shows a faceted count.
- **Bean-ranked tiers on both surfaces.** "Used with this bean" and "Recommended" above the grid, from the existing `requestRankedProfilesForBean` ranking. The selector uses the current DYE bean; the wizard uses its chosen bag.
- **Sort control.** A–Z or Recently used for the grid; with ★ on, the same control edits the favorites order setting and offers Custom….
- **Favorites order becomes a setting**: `custom | alpha | usage`. Existing users with favorites resolve to `custom` (their current order untouched); new users resolve to `usage`. Custom order is edited in a drag-and-drop dialog that replaces the old right-hand favorites panel.
- **Profile usage from shot history**: one threaded `GROUP BY profile_name` query gives last-used and shot count. Feeds usage order, the grid's Recently-used sort, and a "N shots · X ago" line on every card.
- **Cards** carry: source letter, Selected check badge, title with modified marker, temp → yield, usage line, knowledge derivation caption, auto-load pin, sparkle, info, star, ⋮. Current profile pinned first and highlighted. Long-press opens `ProfilePreviewPopup`.
- **⋮ menu** keeps Edit / Copy / Rename / Auto-load / Delete and gains the Selected toggle (Add to / Remove from Selected). Present in both hosts.
- **Empty state** with a Clear filters action that turns every chip off and empties search.
- **Import fills a missing `beverage_type`.** Visualizer, tablet and file imports infer the type from title keywords then step shape when the payload has none. An explicit tag is never overridden.
- **Removed**: the right-hand favorites panel, the checkbox column, the built-in category grouping headers, the six-way view combo, search-only-under-All. Visualizer / Tablet-File / New fold under one `+` menu.
- **BREAKING (UI only)**: the favorites reorder moves from an always-visible panel to a dialog reached via Sort → Custom…. No stored data changes shape.

## Capabilities

### New Capabilities
- `profile-picker`: the shared search/filter/sort/card surface, its chips and counts, ranked tiers, card contents and actions, empty state, host contract (choose vs load, initial chips, beverage constraint).
- `profile-favorites-order`: the favorites order setting, its default resolution for existing vs new users, the stored-list-is-display-order rule, the custom reorder dialog, and idle-pill consumption.
- `profile-usage-history`: the threaded shot-history usage query (last used, shot count per profile title), its refresh points, and its consumers.
- `profile-import-beverage-inference`: inferring `beverage_type` on import when absent, the keyword and shape rules, and the never-override rule.

### Modified Capabilities
- `recipe-wizard`: the profile step is the shared picker; the drink type's beverage filter set becomes a host constraint on the picker; tiers, "Just hot water" and search-regardless behaviour are preserved.
- `profile-auto-load`: selector "rows" become picker cards; the overflow action and marker requirements apply to the card in both hosts; the strip stays selector-only.

## Impact

- QML: new `qml/components/ProfilePicker.qml` (+ card, chip row, custom-order dialog), rewrite of `qml/pages/ProfileSelectorPage.qml`, profile step of `qml/pages/RecipeWizardPage.qml` replaced by the picker. `CMakeLists.txt` `qt_add_qml_module` list.
- C++: `SettingsApp` gains `favoriteProfileOrder`; `ShotHistoryStorage` gains a threaded usage query; `ProfileManager` holds usage, rewrites favorites order in usage mode, exposes filter/facet helpers; `Profile::inferBeverageType()` used by `VisualizerImporter` and `ProfileImporter`.
- Idle page pills (`IdlePage.qml`, `EspressoItem.qml`) read the same favorites list; order now follows the setting. `selectedFavoriteProfile` index semantics unchanged.
- MCP: `auto_load` Selected-list gate unchanged. `profiles_list` unchanged. No web ShotServer page exists for profiles, nothing to sync.
- Settings serializer: one new key. No migration.
- Tests: extend existing `tst_profile*` files for the filter predicate, facet counts, order resolution, usage sort and beverage inference.
- Manual: Profiles page rewritten; one line under the idle page on favorites order.
