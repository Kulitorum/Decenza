## 1. New `lowerMidBar` zone

- [x] 1.1 Add an empty `lowerMidBar` zone to `SettingsNetwork::defaultLayoutJson()` (`src/core/settings_network.cpp`)
- [x] 1.2 Add a migration block ensuring layouts created before this change gain an empty `lowerMidBar` zone (mirror the existing `statusBar` migration)
- [x] 1.3 Render the `lowerMidBar` zone in `IdlePage.qml` as a full-width band anchored `anchors.bottom: bottomBar.top`, content-sized height
- [x] 1.4 Make the zone collapse to zero height when empty (driven by item count / `implicitHeight`)
- [x] 1.5 Add the runtime height-gate: `visible` only when `itemCount > 0` and available center height ≥ bar height + minimum center content; key off measured height, not device class
- [x] 1.6 Register `lowerMidBar` in the in-app editor zone list (`SettingsLayoutTab.qml`)
- [x] 1.7 Register `lowerMidBar` in the web editor zone list (`shotserver_layout.cpp`, e.g. `{key:"lowerMidBar", label:"Lower Mid Bar", hasOffset:false}`)

## 2. Per-zone configuration (all zones)

- [x] 2.1 Add `getZoneOption`/`setZoneOption` (+ `setZoneItems`) in `SettingsNetwork` over a `layout["zoneOptions"][zone]` map, mirroring `offsets`/`scales`; absent values default to current behaviour
- [x] 2.2 Generalise the bar renderer distribution into a per-zone option: `packed` (default), `equalWidth`, `spaced` — done in `LayoutBarZone.qml`
- [x] 2.3 Add per-zone `alignment` (left/center/right) handling in `LayoutBarZone.qml`, no-op under equal-width/spaced
- [x] 2.4 Add per-zone `zoneStyle` rendering (named theme preset, see group 3) to the zone container, applied to bar zones incl. `lowerMidBar`
- [x] 2.5 In-app: open a zone-options panel via a zone options button + long-press/double-click on the zone label (`LayoutEditorZone.qml` `zoneOptionsRequested` → `ZoneOptionsPopup.qml`)
- [x] 2.6 In-app: build the zone-options controls (distribution, alignment, zone style) writing via `setZoneOption`
- [x] 2.7 Add a "populate from preset" action to the zone-options panel with a built-in **"Brew bar"** preset (profile · scale[contextAware] · ratio · dose · milk + `equalWidth` + `accentBar`); writes widgets + options together via `setZoneItems`/`setZoneOption`, non-destructive to other zones
- [x] 2.8 Web: zone-options controls (distribution/alignment/style selects + "Brew bar" populate button) in the zone header + `/api/layout/zone-option` & `/api/layout/zone-populate` endpoints in `shotserver_layout.cpp`
- [~] 2.9 Verify a zone option / populate set in one editor is reflected in the other (in-app/web parity) — code uses shared storage; on-device cross-check pending

## 3. Theme: configurable zone style presets

- [x] 3.1 Add named zone-style presets to `Theme.qml` as configurable components: `standard` (transparent, normal text) and `accentBar` (accent fill + contrast text + bold values, matching PR #1364), mapped to existing theme tokens — no hardcoded colors (`zoneBackgroundColor`/`zoneTextColor`/`zoneValueBold`)
- [x] 3.2 Add a contrast-text token/helper so widget text on a non-transparent preset stays readable (accentBar → `primaryContrastColor`, etc.)
- [x] 3.3 Have readout widgets pick up the active zone style's text/value treatment (color + emphasis) when placed in a styled zone (via `zoneTextColor`/`zoneValueBold` propagation through `LayoutItemDelegate`)
- [x] 3.4 Ensure presets are theme-overridable (resolve to existing theme tokens, so they track light / dark / custom themes)

## 4. New readout widgets

- [x] 4.1 Create `qml/components/layout/items/ProfileNameItem.qml` (current profile name, "—" placeholder)
- [x] 4.2 Create `qml/components/layout/items/DoseWeightItem.qml` (`Settings.dye.dyeBeanWeight`, "—" when zero)
- [x] 4.3 Create `qml/components/layout/items/MilkWeightItem.qml` (last measured milk weight, "—" when none / weight-timed-steaming absent)
- [x] 4.4 Create `qml/components/layout/items/RatioQuickSelectItem.qml` (pill bound to `lastUsedRatio`, opens `RatioPresetDialog`; preset sets only `lastUsedRatio`, never `brewYieldOverride`) — ported `RatioPresetDialog.qml` + `ratioPreset1/2/3` settings from #1364
- [x] 4.5 Add accessibility to each widget (StaticText for readouts; Button role + "tap to change" hint for the ratio pill)
- [x] 4.6 Register all four widgets in `CMakeLists.txt` qml module file list (+ `RatioPresetDialog.qml`)
- [x] 4.7 Register all four widgets in the `LayoutItemDelegate.qml` type switch
- [x] 4.8 Register all four widgets in the in-app editor palette + chip-label map (`LayoutEditorZone.qml`)
- [x] 4.9 Register all four widgets in the web editor widget list + chip-label map (`shotserver_layout.cpp`)

## 5. Per-instance config: scale weight data mode

- [x] 5.1 Add a `dataMode` per-instance property to `ScaleWeightItem.qml` (read from `modelData`), defaulting to current behaviour
- [x] 5.2 Implement the four modes: `gross`, `netBeans` (minus `doseCupTareWeight`), `netMilk` (minus selected pitcher empty weight), `contextAware` (net milk in steam context else net beans, via machine phase)
- [x] 5.3 Preserve the "—" placeholder when no scale is connected, across all modes (existing warning/`--` path unchanged)
- [x] 5.4 Mark `scaleWeight` as configurable so the in-app editor routes long-press to an instance-editor popup
- [x] 5.5 Build the in-app instance-editor UI for `dataMode` (`ScaleWeightEditorPopup.qml`) writing via `setItemProperty`
- [x] 5.6 Web editor: inline `dataMode` selector on a selected `scaleWeight` chip, persisting via `/api/layout/item`
- [~] 5.7 Verify two `scaleWeight` instances hold different `dataMode` independently — per-instance via `modelData`; on-device check pending

## 6. Supersede PR #1364 mode flag

- [x] 6.1 Remove the hardcoded `brewStatusBar` from `IdlePage.qml` — N/A: not present on `main` (this branch built independently of #1364)
- [x] 6.2 Remove the `simplifiedHome` theme setting — N/A: not present on `main`
- [x] 6.3 Restore `centerMiddle` rendering to the standard zone path — N/A: never altered on this branch
- [ ] 6.4 Confirm the "Brew bar" populate preset (task 2.7) fully reproduces the #1364 view — PENDING (depends on 2.7)

## 7. Verification

- [ ] 7.1 Confirm default (empty) layouts are byte-for-byte unchanged: no `lowerMidBar` space reserved, all zone options default, home screen identical
- [ ] 7.2 Reproduce the PR #1364 bar pixel-for-pixel in the in-app editor (widgets + `equalWidth` + `accent` background) on a tablet that fits
- [ ] 7.3 Reproduce the same bar in the web editor; confirm in-app/web parity for widgets, zone options, and `dataMode`
- [ ] 7.4 Confirm the zone auto-hides on a short viewport and reappears (with config intact) on a tall one
- [ ] 7.5 Confirm equal-width distribution, alignment, and theme background work on at least one other existing zone (not just `lowerMidBar`)
- [ ] 7.6 Confirm ratio quick-select changes only `lastUsedRatio` and leaves the profile target / `brewYieldOverride` untouched
- [x] 7.7 Compile via Qt Creator — build succeeded, 0 errors, 0 warnings
