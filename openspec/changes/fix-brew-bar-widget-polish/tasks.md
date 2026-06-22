## 1. Live milk readout (#1)

- [x] 1.1 In `qml/components/layout/items/MilkWeightItem.qml`, source the value from `root.Window.window.sessionMeasuredMilkG` when > 0, else `Settings.brew.lastSteamMilkG`, else "—"; guard for missing `Window.window`/property so it never errors.
- [x] 1.2 Update the `Accessible.name` to reflect the displayed (live-or-committed) value. (Already binds `valueText`, which now reflects the live-or-committed value.)
- [ ] 1.3 Verify: while steaming, the widget tracks the live milk on the scale; when idle it shows the last committed session weight; with no data it shows "—".

## 2. Suppressible ratio on the scale widget (#2)

- [x] 2.1 In `qml/components/layout/items/ScaleWeightItem.qml`, add a per-instance `showRatio` property read from `modelData` defaulting to `true`; gate the `" 1:" + ratio` suffix in `weightText()` on it.
- [x] 2.2 Add a "Show ratio" toggle to `qml/components/layout/ScaleWeightEditorPopup.qml`, persisting via `Settings.network.setItemProperty(itemId, "showRatio", …)` and loading the current value in `openForItem`. (Also threaded the value through `SettingsLayoutTab.qml`'s caller.)
- [x] 2.3 Add the `showRatio` control to the web editor's scaleWeight options in `src/network/shotserver_layout.cpp`.
- [x] 2.4 Add the i18n key(s) for the new toggle label (e.g. `layoutEditor.scaleShowRatio`) with an English fallback. (Inline `TranslationManager.translate` fallbacks; no separate registry exists, matching sibling keys.)
- [ ] 2.5 Verify: default instances still show `1:X.X` when brewing by ratio; disabling `showRatio` hides only the suffix and persists per instance across restart.

## 3. Self-identifying compact scale (#3)

- [x] 3.1 Seed `displayMode: "icon"` on the `lmb_scale` preset in `qml/components/layout/ZoneOptionsPopup.qml` (line ~46).
- [x] 3.2 Seed the same `displayMode: "icon"` on the `lmb_scale` preset in `src/network/shotserver_layout.cpp` (line ~347) so the in-app and web seeds match.
- [ ] 3.3 Verify: a freshly-seeded lower-mid bar shows the compact scale with its scale icon ahead of the value; existing layouts with a stored `displayMode` are unchanged.

## 4. Lower-mid bar overlap (#4)

- [ ] 4.1 Reproduce the overlap from the issue screenshots; determine whether it is intra-zone (`LayoutBarZone`) or a vertical collision with the in-page size/preset row, and record which. **BLOCKED:** screenshots not attached to the issue; inspection alone can't disambiguate (the "size buttons" don't map to any lower-mid bar widget, hinting at a vertical collision). Awaiting screenshots before fixing — see design Open Questions.
- [ ] 4.2 Apply the layout fix per the determined cause (e.g. correct `LayoutBarZone` scale/transform-origin vs. anchored-width interaction, or `RatioQuickSelectItem` width binding, or the height-gating clearance) — event/layout-driven, no timers or post-hoc nudges.
- [ ] 4.3 Verify: size controls, the "Ratio" label, and the ratio pill render side by side without stacking across distribution/alignment/item-size/scale states.

## 5. Validation

- [x] 5.1 Quick compile check via Qt Creator MCP (build the matched project). (Clean build: 0 errors, 0 warnings; all touched QML AOT-compiled, `shotserver_layout.cpp` linked.)
- [ ] 5.2 Confirm no new QML TypeErrors/warnings in the running app log for the touched widgets. (Pending a real-app launch by the user.)
- [x] 5.3 Run `openspec validate fix-brew-bar-widget-polish --strict` and resolve any issues. (Valid.)
