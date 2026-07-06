# Tasks: restore-live-milk-weigh

## 1. Restore the live suffix on the idle steam pill row

- [x] 1.1 In `qml/pages/IdlePage.qml` `steamPresetLoader`, add a suffix-refresh version counter driven by `MachineState.onScaleWeightChanged` (active only while the steam pill row is loaded), mirroring the mechanism in `SteamItem.qml`'s popup and the 1.8.0 code.
- [x] 1.2 Add `pillSuffixFn`, `pillSuffixVersion`, and `pillSuffixMaxWidth: Theme.scaled(60)` to the steam `PresetPillRow`: suffix `" (" + Math.round(Math.max(0, MachineState.scaleWeight - pitcherWeightG)) + "g)"`, gated on `ScaleDevice.connected && !ScaleDevice.isFlowScale`, `pitcherWeightG > 0`, and not `preset.disabled`.
- [x] 1.3 Verify the suffix composes correctly with the existing `pillLabelFn` ("Small Pitcher (150g)") and that pill widths don't jitter as digits change.

## 2. Consistency and non-interference checks

- [x] 2.1 Confirm the SteamItem compact popup suffix (`qml/components/layout/items/SteamItem.qml`) uses identical math/gates; align if drifted (no behavior change expected).
- [x] 2.2 Manually verify with weight-timed steaming OFF (default): suffix live-updates while adding milk; steam still programs the preset's fixed duration.
- [x] 2.3 Manually verify with weight-timed steaming ON: settle-capture value and scaled steam time are unchanged by the suffix; suffix shows sub-50 g amounts that scaling ignores.
- [x] 2.4 Run the app and confirm no new QML warnings/TypeErrors from the pill row (clean-log rule).

## 3. Wrap-up

- [x] 3.1 Update the wiki Manual steam section if it documents the pill readout (check wording against 1.8.0 behavior).
- [x] 3.2 Archive this OpenSpec change on the feature branch before merge (`/opsx:archive`), linking PR to issue #1424.
