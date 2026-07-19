## 1. Shared component: pagination affordance in `PresetPillRow`

- [x] 1.1 Add opt-in inputs to `qml/components/PresetPillRow.qml`: `property int pageCount: 1`, `property int pageIndex: 0`, and `signal pageChangeRequested(int delta)`.
- [x] 1.2 Render a left (`ArrowLeft.svg`) and right (same asset mirrored, e.g. `Image.mirror: true`) arrow flanking the pill content, colorized via the existing `ThemedIcon`/`MultiEffect` (`Theme.iconColor`) pattern. Arrows exist only when `pageCount > 1`; left visible when `pageIndex > 0`, right when `pageIndex < pageCount - 1`.
- [x] 1.3 Reserve a fixed-width arrow gutter on both sides and reduce `effectiveMaxWidth` accordingly **only** when `pageCount > 1`; when `pageCount <= 1` keep today's full-width math (verify the ≤5 layout is pixel-identical).
- [x] 1.4 Wire arrow taps to emit `pageChangeRequested(-1)` / `pageChangeRequested(+1)`.
- [x] 1.5 Accessibility: each arrow is an `AccessibleButton`/`AccessibleTapHandler` with role button, focusable, internationalized `Accessible.name` ("Previous"/"Next" + context), and a press action. Add translation keys with fallbacks.
- [x] 1.6 Confirm the steam/espresso/hotwater/flush/equipment rows are visually and behaviorally unchanged (defaults → `pageCount = 1` → no arrows, no gutter).

## 2. Recipes — inline idle row (`IdlePage.qml`)

- [x] 2.1 Stop truncating: change the recipe `onInventoryReady` to keep the full MRU list (drop `slice(0, 5)`); add `property int recipePageIndex: 0` and a derived `readonly property var visibleRecipes` = the current 5-item window.
- [x] 2.2 Point `recipePresetLoader`'s `PresetPillRow` at `visibleRecipes` for `presets`, `selectedIndex`, and the `onPresetSelected` index lookup; pass `pageCount = Math.ceil(inventoryRecipes.length / 5)` and `pageIndex = recipePageIndex`.
- [x] 2.3 Handle `onPageChangeRequested`: clamp `recipePageIndex + delta` into `[0, pageCount-1]`.
- [x] 2.4 Reset `recipePageIndex` to 0 when the recipe row opens (`activePresetFunction` becomes "recipes"); clamp it into range on every `inventoryReady`.

## 3. Beans — inline idle row (`IdlePage.qml`)

- [x] 3.1 Mirror task group 2 for bags: keep the full MRU list (drop `slice(0, 5)`), add `property int beanPageIndex: 0` and `visibleBags`.
- [x] 3.2 Point `beanPresetLoader`'s `PresetPillRow` at `visibleBags` for `presets`, `selectedIndex`, `onPresetSelected`; pass `pageCount`/`pageIndex`.
- [x] 3.3 Handle `onPageChangeRequested` (clamp) and reset `beanPageIndex` to 0 when the beans row opens; clamp on `inventoryReady`.

## 4. Recipes & Beans — compact bottom-bar popups

- [x] 4.1 `qml/components/layout/items/RecipesItem.qml`: keep the full MRU list, add a page index + `visibleRecipes` window, wire `pageCount`/`pageIndex`/`onPageChangeRequested`, and reset the page in the popup `onOpened`.
- [x] 4.2 `qml/components/layout/items/BeansItem.qml`: same for bags (full list, page index, `visibleBags`, wiring, reset on open).

## 5. Verify

- [x] 5.1 Build via Qt Creator MCP; resolve any warnings (no WARN lines).
- [x] 5.2 Drive the app: with >10 recipes, open the Recipes row, confirm the next arrow appears, paging shows the next five in MRU order, no arrow appears past the ends, and selecting/starting still works on any page. Repeat for beans with >5 bags.
- [x] 5.3 Regression: with ≤5 recipes and ≤5 bags, confirm no arrows appear and the rows look identical to before. Confirm steam/espresso/hotwater/flush/equipment rows are unchanged.

## 6. Docs

- [ ] 6.1 Update the wiki Manual idle-screen section to describe the Recipes/Beans pagination arrows (clone `Kulitorum/Decenza.wiki.git` if needed; hold the push per the release-timing convention).
