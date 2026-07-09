# step-exit-arbitration Specification

## Purpose
Defines the arbiter that decides, on profile frames carrying both a tablet-owned weight exit and a firmware-owned sensor exit, whether to send `SkipToNext` immediately or defer to the firmware based on the live sensor reading's proximity to and trend toward its threshold — so a mixed-exit frame advances exactly once instead of racing the two mechanisms. Weight-only and firmware-only frames are unaffected, and arbiter deferral state resets per shot and per frame.

## Requirements
### Requirement: Arbitrate tablet weight-exit against firmware exit on mixed frames

When a profile frame carries both a tablet-owned weight exit (`exitWeight > 0`) and a firmware-owned exit condition (`exitIf` true with a pressure/flow over/under threshold), the app SHALL decide whether to send the tablet `SkipToNext` immediately or defer to the firmware, so that the frame advances exactly once. The decision SHALL be based on the live sensor reading's proximity to the firmware exit threshold and whether the reading is trending toward that threshold.

A frame's weight exit SHALL fire at most once per frame occurrence (the existing per-frame skip-once guarantee is preserved).

#### Scenario: Firmware far from its threshold — fire immediately

- **WHEN** the projected weight reaches the frame's weight exit on a mixed frame, and the live firmware sensor reading is outside the proximity window of the firmware exit threshold
- **THEN** the app SHALL send `SkipToNext` immediately (the weight exit is the intended transition; there is no race risk)

#### Scenario: Firmware near and trending — defer to firmware

- **WHEN** the projected weight reaches the frame's weight exit on a mixed frame, and the live firmware sensor reading is within the proximity window of the firmware exit threshold and trending toward it across the most recent samples
- **THEN** the app SHALL defer sending `SkipToNext`, allowing the firmware to own the frame transition

#### Scenario: Firmware near but not trending — fire

- **WHEN** the projected weight reaches the frame's weight exit on a mixed frame, and the live firmware sensor reading is within the proximity window but is not trending toward the firmware exit threshold
- **THEN** the app SHALL send `SkipToNext` (the firmware is unlikely to fire on its own)

#### Scenario: Deferral cap reached — fire regardless

- **WHEN** the app has deferred the tablet skip for the maximum number of samples (≈300 ms) on a given frame and the firmware has still not advanced the frame
- **THEN** the app SHALL send `SkipToNext` so that the weight exit is never lost

#### Scenario: Firmware advances during deferral — no double skip

- **WHEN** the app is deferring the tablet skip on a mixed frame and the machine-reported profile frame advances before the deferral cap is reached
- **THEN** the app SHALL NOT send `SkipToNext` for the frame the firmware already left, so the profile advances exactly once

### Requirement: Weight-only and firmware-only frames are unaffected

The arbiter SHALL only apply to frames that have both a weight exit and a firmware exit. Frames with only a weight exit, or only a firmware exit, SHALL behave exactly as before.

#### Scenario: Weight-only frame fires unchanged

- **WHEN** the projected weight reaches the weight exit on a frame whose firmware exit is absent (`exitIf` false or no exit threshold)
- **THEN** the app SHALL send `SkipToNext` immediately without consulting the arbiter

#### Scenario: No-op firmware exit treated as weight-only

- **WHEN** a frame declares a firmware exit whose effective threshold is non-actionable (e.g. pressure-over with a value ≤ 0)
- **THEN** the app SHALL treat the frame as weight-only and fire the tablet skip immediately

### Requirement: Arbiter state resets per shot and per frame

Arbiter deferral state SHALL be cleared at the start of each shot and SHALL not carry across frames the machine has already passed.

#### Scenario: Reset at shot start

- **WHEN** a new extraction begins
- **THEN** all arbiter deferral state SHALL be cleared

#### Scenario: Discard passed-frame state on frame advance

- **WHEN** the machine-reported profile frame advances to a new frame
- **THEN** deferral state for frames below the new frame SHALL be discarded, since the firmware never revisits them

