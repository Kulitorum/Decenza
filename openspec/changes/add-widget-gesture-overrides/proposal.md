## Why

The Custom widget can be assigned any action on tap, long-press and double-click. The
built-in action widgets — Recipes, Beans, Steam, Hot Water, Equipment, Flush, Profiles,
History, Favorites, Settings — cannot: their gestures are hard-coded in
`LayoutItemDelegate.compileToCustom()` and in each widget's own QML.

That is the wrong way round for the widgets people actually keep on their home screen. A
user who wants "long-press Beans to see this bean's shot history" has to delete the Beans
widget and hand-build a Custom one, losing the live bean state, the pill row and the
highlight-when-no-bag behaviour that made the widget worth having.

The action catalog that would drive this already exists and is already centralized
(`layoutActionTable()`), and these widgets already render through `CustomItem` in the
center and action zones — so the dispatch half is done. What is missing is storage,
the two editors, and the same treatment for the dedicated `items/*Item.qml` files used
in the other zones.

## What Changes

- Ten built-in action widgets gain **per-instance gesture overrides**, choosing any action
  from the existing Custom-widget catalog. Two groups, by how their gestures are spent
  today:
  - **One overridable slot** — Recipes, Beans, Steam, Hot Water, Equipment, Flush,
    Profiles. Tap runs the operation (`togglePreset:`), and long-press *and* double-click
    both open the widget's page. The user picks **either** long-press or double-click to
    override; the other stays locked to opening the page, so the page is always reachable.
  - **Two overridable slots** — History, Favorites, Settings. Tap already opens their page,
    so both gestures are free.
- The editor shows the locked slot as such (greyed, labelled with the page it opens),
  rather than silently refusing the second override.
- Overrides work in **both widget formats**: the compiled `CustomItem` rendering used in
  center/action zones, and the dedicated `items/*Item.qml` used elsewhere.
- **Defaults are unchanged.** A widget with no stored override behaves exactly as it does
  today, including existing layouts and library items.
- Both editors get the affordance: the in-app layout editor and the ShotServer web layout
  editor, driven by the same catalog and the same capability declaration.

## Capabilities

### New Capabilities

- `layout-widget-gesture-overrides`: which built-in widgets accept gesture overrides, the
  one-slot vs two-slot rule and why it exists, how a stored override composes with the
  widget's compiled defaults, and that both render formats honour it.

### Modified Capabilities

- `layout-widget-instance-config`: the set of types that expose per-instance options grows
  from readouts + bespoke-editor types to include the ten action widgets, so the
  has-options indicator and the instance editor appear on them.

## Impact

- `qml/components/layout/LayoutItemDelegate.qml` — the compiled-item merge currently
  starts from `{id, type}` and copies only compiled keys, so stored per-instance
  properties are **discarded**; stored gesture overrides must be applied over the compiled
  defaults.
- `qml/components/layout/items/{Recipes,Beans,Steam,HotWater,Equipment,Flush,Espresso,History,AutoFavorites,Settings}Item.qml`
  — the non-compiled format: each hard-codes its own `onAccessibleLongPressed` /
  `onAccessibleDoubleClicked`; these route through the override when one is stored.
- `src/core/settings_network.cpp` — declare the gesture option keys for these types in the
  capability schema, and the per-type slot rule (which gesture is lockable, and what the
  locked one does) so both editors derive it rather than hard-coding a list.
- `qml/components/layout/ReadoutOptionsPopup.qml` (or a gesture section reachable from it)
  — the in-app editing UI, reusing the existing action picker.
- `src/network/shotserver_layout.cpp` — the same section in the web editor, reusing the
  already-injected action catalog.
- No new settings, no schema change, no BLE impact. Stored under the existing
  `setItemProperty` / `/api/layout/item` mechanism.
- Wiki manual — the widget-actions section gains the built-in widgets alongside Custom.
