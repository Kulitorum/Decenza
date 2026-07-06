# Restore live milk weigh readout

## Why

Since 1.8.1, the steam preset pills no longer show the live net-milk weight (scale reading minus the saved empty-pitcher weight) that shipped in #677 and that users relied on to weigh milk directly on the pills: press Steam, set the pitcher on the scale, add milk, read the number. PR #1396 rewrote the idle-page steam pill row and dropped the `pillSuffixFn` readout; the replacement model from #1365 (weight-timed steaming) only *captures* a settled weight, is off by default, and displays nothing for users who never calibrated. Reported as regression [#1424](https://github.com/Kulitorum/Decenza/issues/1424).

## What Changes

- Restore the live `" (Xg)"` net-milk suffix on the idle-page steam preset pill row, using the 1.8.0 math: `max(0, scaleWeight − pitcherWeightG)`, shown only when the preset has a saved pitcher weight and a real (non-flow) scale is connected.
- Keep the restored readout consistent with the surviving copy in the compact-mode `SteamItem` preset popup (same math, same gates).
- Display only — no writes to settings, no tare, no interaction with milk auto-capture (`sessionMeasuredMilkG`), and no change to steam-duration programming. Weight-timed steaming behavior is untouched whether its toggle is on or off.
- The display intentionally does **not** use `SettingsBrew::netMilkForPitcher()`: that helper's 50–1500 g validity window is right for steam-time scaling but wrong for a plain weigh readout (it would blank small milk amounts).

## Capabilities

### New Capabilities
- `live-milk-readout`: live net-milk weight display on steam preset pills (scale minus saved pitcher weight), independent of weight-timed steaming capture.

### Modified Capabilities

(none — `weight-timed-steaming` requirements are unchanged; this is a parallel display-only capability)

## Impact

- `qml/pages/IdlePage.qml` — steam preset pill row (`steamPresetLoader`): add suffix function + scale-weight-driven refresh (same `pillSuffixVersion` mechanism `PresetPillRow` already supports).
- `qml/components/layout/items/SteamItem.qml` — reference implementation already present in the compact popup; verify consistency, no behavior change expected.
- No C++ changes, no settings schema changes, no BLE traffic.
- Known cosmetic edge case (pre-existing, unchanged): selecting steam auto-tares the scale, so a loaded pitcher already resting on the scale reads 0 until lifted and replaced — the existing "Place (or lift and replace) the milk pitcher" prompt covers this.
