## Why

On a profile frame that carries **both** a tablet-owned weight exit (`exitWeight > 0`) and a firmware-owned exit (`exitIf` with a pressure/flow over/under condition), the app can prematurely truncate the shot. The tablet's weight-triggered skip is a blind `SkipToNext` command (it advances whatever frame the DE1 is currently in, with no frame index). If the firmware's own exit fires in the BLE round-trip window between the tablet deciding to skip and the command landing, the DE1 advances the frame on its own *and* the late tablet skip advances it again — a double frame-advance. On short 2–3 frame profiles this ends the shot a stage (or two) early.

This is confirmed in the field, not theoretical: imported Visualizer profiles "soup" and "soup no bloom" have a `fill` frame with `pressure_over 2.0` **and** `weight 1.0`. The 1 g weight target is reached almost instantly — at the same moment chamber pressure climbs through 2.0 bar — so the two exits collide within the same ~100 ms window and double-skip the fill frame, collapsing the profile past its intended pour/hold stage.

## What Changes

- Introduce a **step-exit arbiter**: when a frame's weight threshold is reached and that same frame also has a firmware exit, consult the arbiter before sending `SkipToNext`.
  - **Firmware far from its threshold** → fire the tablet skip immediately (weight is the intended exit; no race risk).
  - **Firmware near its threshold and trending toward it** → defer the tablet skip (up to a max-deferral cap, ~300 ms) so the firmware owns the transition.
  - **Max deferral reached** → fire regardless, so a skip is never lost.
- Frames with **only** a weight exit (no firmware exit) are unaffected — they fire exactly as today.
- Feed per-frame firmware-exit metadata into `WeightProcessor` at shot start so the worker can arbitrate.
- No new user-facing setting. The arbiter is always on; it is a pure correctness fix.
- Add `WeightProcessor` unit tests, including a regression case built from the "soup" frame layout (pressure_over 2.0 + weight 1.0 on the fill frame).

## Capabilities

### New Capabilities
- `step-exit-arbitration`: On a mixed-exit frame (weight exit + firmware exit), decide whether the tablet should send `SkipToNext` now or defer to the firmware, based on proximity and trend of the live sensor relative to the firmware exit threshold.

### Modified Capabilities
<!-- None: per-frame weight-exit behavior is not currently specced; this introduces it. -->

## Impact

- **Code**:
  - `src/machine/weightprocessor.{h,cpp}` — per-frame skip path (`weightprocessor.cpp:311`) gated through the arbiter; new firmware-exit metadata in `configure()`.
  - New `StepExitArbiter` (deciding logic), invoked from the weight worker.
  - `src/main.cpp` (~719–737) — build and pass a per-frame firmware-exit descriptor alongside `frameExitWeights`.
- **Behavior**: only affects mixed-exit frames; weight-only and firmware-only frames behave exactly as before. No protocol or profile-format change.
- **Tests**: `tests/tst_weightprocessor.cpp` gains arbiter coverage and a "soup" regression case.
- **Docs**: `docs/CLAUDE_MD/RECIPE_PROFILES.md` (weight-exit vs firmware-exit interaction) updated to document the arbitration.
