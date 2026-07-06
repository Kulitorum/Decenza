# Design: restore-live-milk-weigh

## Context

Version 1.8.0's idle-page steam pills carried a live `" (Xg)"` net-milk suffix (`PresetPillRow.pillSuffixFn`, added in #677). PR #1396 rewrote the `steamPresetLoader` pill row in `qml/pages/IdlePage.qml` (adding `pillLabelFn` for "<name> Pitcher" labels) and dropped the suffix; nothing replaced it for users who haven't opted into weight-timed steaming (#1365, off by default). The only surviving copy of the readout is in the compact-mode `SteamItem` preset popup (`qml/components/layout/items/SteamItem.qml:208`). Regression report: #1424.

`PresetPillRow` still supports everything needed: `pillSuffixFn`, `pillSuffixVersion` (bump to re-evaluate pill text on scale changes), and `pillSuffixMaxWidth` (reserve layout space).

## Goals / Non-Goals

**Goals**
- Restore the 1.8.0 live weigh experience on the idle-page steam pill row, byte-for-byte the same math and gates as 1.8.0 / the SteamItem popup.
- Zero interaction with the weight-timed steaming capture pipeline.

**Non-Goals**
- No change to `SettingsBrew::netMilkForPitcher()` or `scaledSteamTime()` — their 50–1500 g window is correct for duration scaling.
- No new net-milk readout on the SteamPage settings view (it shows the raw scale reading; adding net milk there can be a follow-up if requested).
- No new settings, widgets, or layout registrations.

## Decisions

1. **Inline the 1.8.0 math rather than reuse `netMilkForPitcher()`.**
   `max(0, scaleWeight − pitcherWeightG)` with no validity floor. The C++ helper returns 0 outside 50–1500 g, which would blank small amounts and read as "broken" — the exact complaint in #1424. Two call sites (IdlePage + SteamItem popup) already share this inline form; a third copy is acceptable QML-side duplication (alternative — adding a `displayMilkForPitcher()` Q_INVOKABLE — rejected as C++ churn for a one-line expression).

2. **Reuse the `pillSuffixVersion` bump mechanism for reactivity.**
   Same as 1.8.0 and the SteamItem popup: a `Connections` on `MachineState.onScaleWeightChanged` increments a version counter only while the steam pill row is active, forcing `pillLayoutName()` re-evaluation. Alternative — binding pill text directly to `MachineState.scaleWeight` — rejected: `PresetPillRow` computes pill widths imperatively and the version mechanism exists precisely to refresh text without a full layout recalc.

3. **Gates identical to 1.8.0:** `ScaleDevice.connected && !ScaleDevice.isFlowScale`, `preset.pitcherWeightG > 0`, and no suffix for `preset.disabled`. The suffix coexists with #1396's `pillLabelFn` ("Small Pitcher (150g)") — suffix appends after the label, which `PresetPillRow.pillLayoutName()` already handles.

## Risks / Trade-offs

- [Suffix shows 0 g when the scale was auto-tared with the loaded pitcher already on it] → Pre-existing 1.8.0 behavior (auto-tare on steam select existed then too); the "Place (or lift and replace) the milk pitcher" prompt already instructs the recovery. No code change.
- [Displayed net milk can differ from what auto-capture latches (gross−tare vs settle-delta)] → Cosmetic only; capture path reads nothing from the display. Documented in the spec's independence requirement.
- [Pill width jitter as digits change] → `pillSuffixMaxWidth: Theme.scaled(60)` reserves space, same as 1.8.0.

## Migration Plan

Pure QML display change, no persisted state — ships in a normal release, trivially revertable.

## Open Questions

None.
