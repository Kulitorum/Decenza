# Deliberate UGS Grinder Calibration (Phase 2)

> Split from `fix-grinder-calibration-cross-profile` (archived 2026-07-07). Phase 1 —
> the #1223 harm-reduction fix (within-coffee conversion key, extrapolation cap,
> directional-only fallback) — shipped in PR #1236. This change holds the deferred
> Phase 2. **Status: parked pending a go/no-go decision** — see Open Question below.

## Why

Phase 1 made grind advice honest but conservative: on real databases the mined
within-coffee data rarely clears the publish gates, so most users get directional
guidance ("coarser, pull a reference shot") rather than a numeric grind setting.
The original UGS spec's answer is a *deliberate* calibration: pull two anchor
shots on profiles far apart on the UGS scale once per grinder+burrs, store the
resulting Conversion Key, and unlock bounded full-range numeric guidance. The
mechanism's prerequisite validation data already exists — the #1223 reporter's
5,434-shot database was obtained and used to confirm Phase 1 offline.

## What Changes

- **Two-anchor capture workflow**: record a fine anchor and a coarse anchor
  (profiles ≥ minimum UGS span apart); compute `conversionKey = Δsetting/ΔUGS`;
  reject anchors that are too close.
- **Persistent per-grinder calibration**: a new `SettingsGrinder` domain
  sub-object (settings-architecture rules) storing calibration records per
  `(grinderModel, grinderBurrs)`: both anchors, UGS values, settings, key,
  timestamp.
- **Consumption in `buildGrinderCalibrationBlock`**: a stored key takes
  precedence over the mined within-coffee key → `confidence: "calibrated"`,
  validated range = the deliberate anchor span; the per-coffee intercept and
  extrapolation cap still apply.
- **Validation gate**: long-hop numeric output (lever → turbo distances) stays
  behind a default-off gate until the deliberate-calibration mechanism is
  validated against ≥1 independent database fixture in the `shot_eval` corpus.
- Entirely opt-in: no stored calibration = Phase 1 behavior, byte-identical.

## Capabilities

### New Capabilities

- `grinder-ugs-calibration`: opt-in deliberate two-anchor UGS calibration —
  capture, persistence, precedence over the mined key, and the validation gate
  that keeps long-hop numeric guidance disabled until confirmed on independent
  data. (Delta spec carried over verbatim from the parent change.)

### Modified Capabilities

<!-- none — the dialing-context-payload requirements shipped with Phase 1 already
     describe consuming "a Phase 2 stored Conversion Key when present" -->

## Impact

- **Code**: new `SettingsGrinder` domain (`src/core/`), a capture workflow
  surface (UI TBD in design — likely a guided flow, not a settings knob), and
  the precedence path in `src/ai/dialing_blocks.cpp`; optional MCP exposure.
- **Users**: opt-in ritual (two shots, once per grinder+burrs) in exchange for
  full-range numeric grind targets. This adds a user-facing workflow — weigh
  against the prefer-fewer-settings principle before building.
- **Validation asset in hand**: the markpalmos database (DF83V + Mignon
  Specialita) is already Phase-1-validated offline via `tools/calib_analysis.py`
  and is the designated fixture for enabling the long-hop gate.

## Open Question (the reason this is parked)

Is the two-shot calibration ritual worth the UI surface it adds? Phase 1's
directional guidance may be sufficient for most users (the UGS design doc's own
conclusion). Decide before implementing; if the answer is no, delete this change
rather than archiving it.
