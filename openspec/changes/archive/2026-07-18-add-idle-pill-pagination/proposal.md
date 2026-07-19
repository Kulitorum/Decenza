## Why

The idle-screen Recipes and Beans pill rows show only the **5 most-recently-used** items ([#1548](https://github.com/Kulitorum/Decenza/issues/1548)). A user with 11+ recipes (or many bags) cannot reach the rest from the home screen, defeating the point of quick-switch: it becomes faster to pick from Profiles/History or set things by hand. Recipe names can be long, so simply widening to fit more is not viable.

## What Changes

- The idle-screen **Recipes** pill row gains **prev/next pagination arrows** that page through the *entire* MRU-ordered recipe list, five at a time (page 1 = the current top five).
- The idle-screen **Beans** pill row gets the **same** pagination.
- Arrows appear **only as appropriate**: the left arrow is hidden on the first page, the right arrow is hidden on the last page. With ≤5 items neither arrow shows and the layout is byte-for-byte identical to today.
- The paging affordance is added **once** to the shared `PresetPillRow` component (opt-in via new page metadata), so both the inline idle rows and the compact bottom-bar popups (`RecipesItem`, `BeansItem`) reuse a single implementation. Non-paginated pill rows (steam/espresso/hotwater/flush/equipment) are unaffected — pagination stays off by default.
- Equipment is intentionally left out of this change (its inventory is typically small); the mechanism makes adding it later trivial.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `recipe-quick-switch`: the recipes idle widget no longer caps visible pills at the five most-recent — it paginates the full non-archived MRU list in pages of five, via prev/next arrows shown only when more than one page exists.
- `bag-inventory-view`: the idle-page bean widget no longer shows only the five most-recent inventory bags — it paginates the full MRU inventory in pages of five, with the same prev/next arrows.

## Impact

- **QML (behavior):** `qml/components/PresetPillRow.qml` (gains optional pagination arrows + gutter reservation + `pageChangeRequested` signal), `qml/pages/IdlePage.qml` (recipe + bean loaders: keep the full MRU list, derive a windowed slice, hold a page index), `qml/components/layout/items/RecipesItem.qml` and `qml/components/layout/items/BeansItem.qml` (mirror in the compact popups).
- **Assets:** reuse `resources/icons/ArrowLeft.svg`, mirrored horizontally for the right arrow — no new asset.
- **Data:** none. `RecipeStorage`/`BagStorage` already return the complete MRU-ordered inventory (no LIMIT); only the QML-side `slice(0, 5)` truncation changes to a windowed slice.
- **Accessibility:** new arrows need `AccessibleButton` semantics (role, name "Previous/Next recipes|beans", focusable, press action) and should announce the new page's contents, consistent with the existing pill-row announcements.
- **Docs:** wiki Manual idle-screen section updated to describe the pagination arrows.
