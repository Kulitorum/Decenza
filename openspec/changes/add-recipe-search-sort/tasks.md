## 1. Settings persistence (C++)

- [x] 1.1 Add `recipeSortField` (QString) and `recipeSortDirection` (QString) Q_PROPERTYs to `SettingsNetwork` (`src/core/settings_network.h`), with getters/setters/NOTIFY signals mirroring the existing `shotHistorySort*` pair.
- [x] 1.2 Implement the getters/setters in `src/core/settings_network.cpp` with defaults `field = "dateUsed"`, `direction = "DESC"`.
- [x] 1.3 Serialize/deserialize the two new keys in `src/core/settingsserializer.cpp` alongside the shot-history sort keys.

## 2. Recipes page — search + sort controls (QML)

- [x] 2.1 Add a control bar to `qml/pages/RecipesPage.qml` above the card grids (below the header row): a debounced `StyledTextField` search field with an inline clear button (mirror `ShotHistoryPage.qml`'s `searchTimer` pattern), a sort-field `SelectionDialog` picker, and an ascending/descending toggle button using `qrc:/icons/SortAscending.svg` / `SortDescending.svg`.
- [x] 2.2 Bind sort field/direction to `Settings.network.recipeSortField` / `recipeSortDirection` (read on load, write on change) so the preference persists; keep search text as a transient local property that resets empty on page entry.
- [x] 2.3 Define the sort-key set — date used (`lastUsedEpoch`), date created (`createdEpoch`), coffee/bean (`roasterName` + `coffeeName`), profile (`profileTitle`), name (`name`) — with a label map for the picker (internationalized via `TranslationManager`/`Tr`).
- [x] 2.4 Surface the existing `created_at` column read-only: add a `createdEpoch` field to `Recipe` (`recipestorage.h`) and a `COL_EPOCH_RO("created_at", createdEpoch)` `kCols` entry (writable=false; appended last to keep the positional read aligned). No new column, no migration.

## 3. Filter + sort logic (QML)

- [x] 3.1 Add a computed `visibleRecipes` (and `visibleArchivedRecipes`) derived from the source arrays: filter by case-insensitive substring match against name, roaster/coffee, and profile title; then sort a shallow copy by the chosen key and direction.
- [x] 3.2 Comparators: numeric for `lastUsedEpoch`; case-insensitive string compare for name/profile/bean; recipes with a blank sort field sort to the end in both directions.
- [x] 3.3 Feed the active and archived `Repeater`s from the derived lists (not the raw source arrays); confirm re-filtering on each keystroke is smooth.

## 4. Empty state + accessibility

- [x] 4.1 Add a "no matches" empty state shown when a non-empty library filters to zero cards, distinct from the existing "no recipes yet" starter tiles.
- [x] 4.2 Give the search field, sort picker button, direction toggle, and clear button `Accessible.role`, `Accessible.name`, `Accessible.focusable`, and `Accessible.onPressAction` per `docs/CLAUDE_MD/ACCESSIBILITY.md`; fix any pre-existing violations in the file touched.

## 5. Docs

- [x] 5.1 Update the GitHub wiki Manual recipes section to document searching and sorting recipes (edit made locally in `../Decenza.wiki/Manual.md`; push held per the release-timing convention).

## 6. Verification

- [ ] 6.1 Build in Qt Creator and exercise the recipes page: search by name/coffee/profile, sort by each key, toggle direction, confirm persistence across restart, confirm the archived grid follows, and confirm the "no matches" state.
