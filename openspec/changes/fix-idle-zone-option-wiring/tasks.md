## 1. Center zone (`LayoutCenterZone.qml`)

- [x] 1.1 Honor `alignment` via the auto-spacer mechanism (leading spacer for `center`|`right`, trailing for `center`|`left`); default `center` stays byte-identical
- [x] 1.2 Add `zoneStyle`, passing `Theme.zoneTextColor` / `Theme.zoneValueBold` to the delegate
- [x] 1.3 Document why the zone has no `distribution` (fixed-cell sizing vs. the button-width cap)
- [x] 1.4 Remove the empty `Component.onCompleted` block

## 2. Idle screen wiring (`IdlePage.qml`)

- [x] 2.1 Add a single `zoneOpts(zone)` accessor; fold `zoneItemSize()` and `lowerMidBarOptions` onto it
- [x] 2.2 Wire `distribution` / `alignment` / `style` into `topLeft`, `topRight`, `bottomLeft`, `bottomRight`
- [x] 2.3 Wire `alignment` + `style` into the three center zones
- [x] 2.4 Drop the three hand-rolled `center*Alignment` properties and restore the center-scales block

## 3. Editors

- [x] 3.1 `ZoneOptionsPopup.qml`: hide the Distribution row for center zones (`canDistribute`)
- [x] 3.2 `ZoneOptionsPopup.qml`: stop `populateBrewBar()` writing a distribution a center zone ignores
- [x] 3.3 `shotserver_layout.cpp`: hide the distribution `<select>` for center zones (`!zone.hasOffset`)
- [x] 3.4 `shotserver_layout.cpp`: `pvJustify()` ignores distribution for center zones so the web preview stops depicting an effect the idle screen never applies
- [x] 3.5 `LayoutPreview.qml`: pass `zoneStyle` to the three center zones

## 4. Docs

- [x] 4.1 Wiki manual: note that Distribution is a bar-zone option

## 5. Verification

- [x] 5.1 Build clean (0 errors, 0 warnings)
- [ ] 5.2 On-device: confirm left/right alignment takes effect on each center zone and on all four top/bottom bar zones
- [ ] 5.3 On-device: confirm a default layout renders identically to before (no regression for untouched options)
- [ ] 5.4 On-device: confirm the Distribution control is absent for center zones in both editors
- [ ] 5.5 On-device: check whether right-aligning `centerTop` visually detaches the action buttons from the centered inline preset pill row below them (`IdlePage.qml` uses `Layout.alignment: Qt.AlignHCenter` for that row) — decide whether the preset row should follow the zone's alignment
