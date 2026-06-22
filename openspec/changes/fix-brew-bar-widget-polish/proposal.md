## Why

The newly-shipped composable lower-mid bar and status bar ([#1368](https://github.com/Kulitorum/Decenza/issues/1368) / [#1369](https://github.com/Kulitorum/Decenza/issues/1369)) work well, but field testing surfaced four small rough edges in the brew/status readout widgets ([#1379](https://github.com/Kulitorum/Decenza/issues/1379)): the milk widget never shows live milk, the scale widget repeats the ratio that's already shown elsewhere, a compact scale reading has no label or icon to identify it, and the lower-mid bar's contents can overlap in some states. Each is confined to the item widgets and degrades the polish of a feature users are seeing for the first time.

> **Rollout note:** Items 1–3 ship first. Item 4 (lower-mid bar overlap) is blocked on the reporter's screenshots — inspection alone can't disambiguate intra-zone overlap from a vertical collision with the in-page preset row — so it stays open within this change until a concrete repro is available. The change is not archived and the issue is not closed until item 4 lands.

## What Changes

- **Live milk readout** — the `milkWeight` widget shows the in-session measured milk (`sessionMeasuredMilkG`) while steaming, falling back to the last committed session weight (`Settings.brew.lastSteamMilkG`) when idle, so it gives live feedback during a steam instead of staying "—" until a session ends.
- **Suppressible ratio on the scale widget** — the `scaleWeight` widget gains a per-instance `showRatio` option so its `1:X.X` suffix can be turned off where the ratio is already shown by a ratio pill or the status bar, ending the 2–3× duplication.
- **Self-explanatory compact scale** — a `scaleWeight` widget rendered in a compact/bar zone identifies itself (its full "Scale Weight" label is full-mode only today, leaving compact instances as a bare dot + number), so the number is recognizable at a glance.
- **No overlap in the lower-mid bar** — the lower-mid bar lays its widgets out without the size buttons, "Ratio" label, and ratio pill stacking on top of each other in some states. _(Blocked on screenshots; lands after items 1–3.)_

## Capabilities

### New Capabilities

_None — all changes refine existing widget/zone behaviour._

### Modified Capabilities

- `layout-brew-widgets`: the measured-milk widget reflects live in-session milk while steaming, not only the last completed session.
- `layout-widget-instance-config`: the `scaleWeight` widget gains a per-instance `showRatio` toggle (default preserves today's behaviour), and its compact rendering becomes self-identifying.
- `layout-lower-mid-bar-zone`: the zone renders its widgets without overlap across the affected states. _(Deferred within this change — see rollout note.)_

## Impact

- QML widgets: `qml/components/layout/items/MilkWeightItem.qml`, `ScaleWeightItem.qml`.
- Instance editor: `qml/components/layout/ScaleWeightEditorPopup.qml` (and its web-editor counterpart in `src/.../shotserver_layout.cpp`) gain the `showRatio` control.
- Zone rendering: `qml/components/layout/LayoutBarZone.qml` and/or `RatioQuickSelectItem.qml` for the overlap fix (item 4, pending repro).
- No new Settings properties, no BLE/protocol changes, no migration. Existing layouts render unchanged except where a user opts into the new `showRatio` setting.
