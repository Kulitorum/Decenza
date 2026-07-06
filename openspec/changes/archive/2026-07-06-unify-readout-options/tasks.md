# Tasks: Unify Readout Widget Options

## 1. Capability schema (C++ source of truth)

- [x] 1.1 Add the static type→option-keys table in `src/core/settings_network.cpp` (readouts with their keys; bespoke types — `custom`, `sleep`, screensavers, `shotPlan` — marked bespoke-configurable)
- [x] 1.2 Reimplement `SettingsNetwork::typeHasOptions()` as a lookup into the table; delete the hand-kept `QSet`
- [x] 1.3 Add `Q_INVOKABLE QStringList optionKeysForType(const QString&)` to `SettingsNetwork` and expose it to QML
- [x] 1.4 Build with `-DBUILD_TESTS=ON --target all` and fix any `typeHasOptions`/settings_network test stubs

## 2. Unified QML editor

- [x] 2.1 Create `qml/components/layout/ReadoutOptionsPopup.qml` with capability-gated sections (dataMode, displayMode, showRatio, color via shared `WidgetColorPicker`); add to `CMakeLists.txt` qt_add_qml_module list
- [x] 2.2 Route all readout types to the new popup in `SettingsLayoutTab.qml` (bespoke types keep their existing popups)
- [x] 2.3 Delete `DisplayModeEditorPopup.qml` and `ScaleWeightEditorPopup.qml`; remove from `CMakeLists.txt`
- [x] 2.4 Verify stored item JSON round-trips unchanged for the six already-optioned types (same keys/values as the old popups wrote)

## 3. Extend options to the five bare readouts

- [x] 3.1 `ProfileNameItem`: read per-instance `color` (color-only per schema)
- [x] 3.2 `DoseWeightItem` + `MilkWeightItem`: read `color` and `displayMode` (icon mode = existing beans / pitcher SVG ahead of value; verify qrc paths)
- [x] 3.3 `BatteryLevelItem` + `ScaleBatteryItem`: read `color` and `displayMode` (unset = today's icon+value; alternate mode = value-only); named color overrides charge-level tinting
- [x] 3.4 Confirm defaults: with no stored options, all five render pixel-identical to today

## 4. Web editor

- [x] 4.1 Serialize the capability table into the served layout page (or `/api/layout/capabilities`); delete the JS `typeHasOptions` mirror and its keep-in-sync comment in `src/network/shotserver_layout.cpp`
- [x] 4.2 Drive the web editor's per-type option forms from the capability keys; add forms for the five newly-optioned readouts
- [x] 4.3 Manually exercise every readout type in the web editor (open, edit, save, reload) and confirm round-trip with the in-app editor

## 5. Accessibility & i18n

- [x] 5.1 New popup controls: `Accessible.role`/`name`/`focusable`/`onPressAction` per ACCESSIBILITY.md; reuse existing translation keys where the old popups had them, add `Tr` keys for new labels
- [x] 5.2 Label display-mode choices by effect ("Icon + value" / "Value only"), not internal names

## 6. Docs, tests, wrap-up

- [x] 6.1 Update CLAUDE.md widget-registration note (schema entry is the options registration point) and any layout docs in `docs/CLAUDE_MD/` that name the deleted popups
- [x] 6.2 Add/extend unit coverage: schema↔typeHasOptions agreement, optionKeysForType for known + unknown types
- [x] 6.3 Run the full test suite; ask Jeff to launch the app and verify the layout editor (per no-local-launch rule)
