## Why

The Recipes page has a search and sort bar; Beans had none, so a long bag list could only be scrolled. The app's six search fields were also separate copies, two of them carrying identical clear-button and sort code.

## What Changes

- Beans (app and web) gets the Recipes search and sort. A search matches any text in the bag, its bean details included, and covers the finished bags. Sort: Last used, Roast date, Coffee, Roaster; persisted (and carried by settings export/import).
- Shared components: `SearchField` (field, clear buttons, debounce), used by Recipes, Beans, Shot History, the profile picker, Settings search and Change Beans; `SortControls` (sort picker and direction), used by Recipes, Beans and Shot History. The fields that had no clear button gain one.
- The sort comparator and the web tokenizer each move to one shared place.

## Capabilities

### Modified Capabilities
- `bag-inventory-view`: search and sort.
- `shotserver-bags`: web search and sort.

## Impact

New `qml/components/SearchField.qml`, `SortControls.qml`; `RecipeSearch.js` (`buildBagHaystack`, `sortedCopy`); `BeanInfoPage`, `RecipesPage`, `ShotHistoryPage`, `ProfilePicker`, `SettingsSearchDialog`, `ChangeBeansDialog`; `SettingsNetwork` (`bagSortField`/`bagSortDirection`), `settingsserializer.cpp`; `shotserver_bags.cpp`, `shotserver_recipes.cpp`, `webtemplates/management_js.h`; tests `tst_recipesearch`, `tst_settings`.
