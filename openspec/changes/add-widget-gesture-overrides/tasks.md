## 1. Shared action dispatch

- [x] 1.1 Extract `CustomItem.executeActionString()` into a place both `CustomItem.qml` and the dedicated `items/*Item.qml` can call (QML singleton or shared JS module), keeping `CustomItem` as a caller of the same implementation — not a copy
- [x] 1.2 Verify the extraction is behaviour-preserving against the existing Custom widgets before any new caller is added: every action category (`navigate`, `command`, `togglePreset`), the parameterized `command:loadProfile:<file>` prefix path, and the unknown-target warning
- [x] 1.3 Add a `_gestureAction(gesture, fallbackFn)` helper the ten items share: run the stored override through the dispatcher if one exists, else invoke the widget's existing local function

## 2. Capability schema

- [x] 2.1 Declare `longPressAction` / `doubleclickAction` option keys for the ten action widget types in the capability schema (`settings_network.cpp`), so they become configurable types
- [x] 2.2 Declare the per-type slot rule in the same table: the reserved destination action id for the one-slot widgets (recipes, beans, steam, hotwater, flush, espresso, equipment), absent for the two-slot widgets (history, autofavorites, settings)
- [x] 2.3 Expose the rule to QML and to the web editor through the existing accessors, so neither editor hard-codes which widget reserves which gesture or names a destination

## 3. Compiled render format

- [x] 3.1 In `LayoutItemDelegate.qml`'s compiled merge (~:293), apply stored gesture keys OVER the compiled defaults — currently the merge starts from `{id, type}` and copies only compiled keys, so stored per-instance properties are discarded
- [x] 3.2 Gate the override to the two gesture keys only: a stored `content`/`emoji`/`backgroundColor` on a compiled widget must stay discarded, or an old layout could resurrect stale identity values that compiled defaults have since changed

## 4. Dedicated render format

- [x] 4.1 Route `onAccessibleLongPressed` / `onAccessibleDoubleClicked` through `_gestureAction()` in each of `RecipesItem`, `BeansItem`, `SteamItem`, `HotWaterItem`, `FlushItem`, `EspressoItem`, `EquipmentItem`, `HistoryItem`, `AutoFavoritesItem`, `SettingsItem`
- [x] 4.2 Confirm each still needs `supportDoubleClick: true` where it now gains a double-click override (History/Favorites/Settings have no double-click handler today)
- [x] 4.3 Check every touched file for `pragma ComponentBehavior: Bound` and delegates with required properties — adding properties to a delegate's base type has silently broken `modelData` in unrelated files here, and nothing but opening the screen catches it

## 5. In-app editor

- [x] 5.1 Add a Gestures section to the instance editor for these types, with a long-press row and a double-click row opening the existing action `SelectionDialog`
- [x] 5.2 Implement the reserved-slot presentation: on a one-slot widget, once one gesture carries an override the other is shown non-editable, labelled with the destination it opens (resolved via `layoutActionLabels()`, not a hand-written string)
- [x] 5.3 Clearing the only override releases both slots

## 6. Web editor

- [x] 6.1 Add the same two rows to the web instance editor, driven by the injected action catalog and the injected slot rule
- [x] 6.2 Match the reserved-slot presentation, so a one-slot widget reads the same on both surfaces

## 7. Verification

- [x] 7.1 Build via `mcp__qtcreator__build` and run the full suite via `mcp__qtcreator__run_tests` (scope `all`)
- [x] 7.2 Run the qmllint gate (`qmllint_check`) — every touched QML file is on the clean list
- [x] 7.3 Test: every gesture-capable type routes its gestures through the shared helper (a missed file is a widget that ignores its override in one format only — extend an existing test file, do not add a new `tst_*.cpp`)
- [x] 7.4 Test: the compiled merge applies stored gesture keys and still discards stored identity keys
- [x] 7.5 Test: the slot rule — a one-slot type reports one editable slot and a reserved destination; a two-slot type reports two and none
- [x] 7.6 VERIFIED — Recipes end to end (picker, Default/None, reserved-slot locking, override firing), then moved centerTop (compiled CustomItem path) → bottomRight (dedicated RecipesItem path) with the override intact: long-press still opened filtered history, double-click still opened Recipes. Moved back. In the running app: set an override on a one-slot widget, confirm the reserved gesture still opens the page; move the widget between a compiled zone and a dedicated-format zone and confirm identical behaviour
- [ ] 7.7 In the running app: confirm an untouched layout behaves exactly as before on every widget and every gesture
- [x] 7.8 In the web editor at `localhost:8888/layout`: set an override, reopen in-app, confirm same action and same reserved slot

## 8. Documentation

- [x] 8.1 Update the wiki manual's Widget Actions section — the built-in widgets alongside Custom, and the one-slot rule stated as a feature ("the other gesture always opens the page")
- [x] 8.2 Update the `CLAUDE.md` layout-widget registration note if the schema gains a new field shape
- [ ] 8.3 Archive this change with `openspec archive add-widget-gesture-overrides` as the last commit on the feature branch
