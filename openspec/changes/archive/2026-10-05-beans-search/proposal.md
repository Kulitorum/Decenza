## Why

The Recipes page has a search and sort bar; Beans had none, so a long bag list could only be scrolled. The app's search fields were also separate copies: five hand-written fields, three of them with their own clear buttons and two with their own debounce timer, and three copies of the sort controls. The web pages carried their own copies too, and their sort already disagreed with the app's.

## What Changes

- Beans (app and web) gets the Recipes search and sort. A search matches any text in the bag, its bean details included, and its kind ("tea" finds every bag of tea); it covers the finished bags. Sort: Last used, Roast date, Coffee, Roaster; persisted (and carried by settings export/import).
- Shared components: `SearchField` (field and clear buttons), used by Recipes, Beans, Shot History, the profile picker, Settings search and Change Beans; `SortControls` (sort picker and direction), used by Recipes, Beans and Shot History. The fields that had no clear button gain one. The search timers go: in-memory lists filter on every edit, and Shot History queries on every edit, dropping replies a newer query has overtaken.
- One sort comparator per surface, with the same rules (blanks last both ways, ties by id): `RecipeSearch.sortedCopy` in the app and `sortedCopy` in the shared web script, which also carries the tokenizer and the search and sort controls for `/recipes` and `/beans`. The web pages open in the device's saved sort and save a change back.

## Capabilities

### New Capabilities
None.

### Modified Capabilities
- `bag-inventory-view`: search and sort.
- `shotserver-bags`: web search and sort.
- `recipe-list-organization`: search updates on every edit; the web page shares the saved sort.

## Impact

New `qml/components/SearchField.qml`, `SortControls.qml`; `RecipeSearch.js` (`buildBagHaystack`, `sortedCopy`); `BeanInfoPage`, `RecipesPage`, `ShotHistoryPage`, `ProfilePicker`, `SettingsSearchDialog`, `ChangeBeansDialog`; `SettingsNetwork` (`bagSortField`/`bagSortDirection`), `settingsserializer.cpp`, `shotserver_settings.cpp` (sort keys in `POST /api/settings`); `shotserver_bags.cpp`, `shotserver_recipes.cpp`, `webtemplates/management_js.h`; tests `tst_recipesearch`, `tst_settings`.
