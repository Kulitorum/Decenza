# Design: Deliberate UGS Grinder Calibration (Phase 2)

> Carried over from `fix-grinder-calibration-cross-profile` design (D7 and the
> Phase-2 threads). Read that archived change
> (`openspec/changes/archive/2026-07-07-fix-grinder-calibration-cross-profile/`)
> for the full Phase-1 context: the grind model
> (`grind ≈ coffee_baseline + UGS·conversionKey`), decisions D1–D6, and the
> tuned constants (`minPairSpan 0.75`, `minValidatedPairs 3`,
> `maxSpreadRatio 0.6`, `cap 1.5` UGS).

## Context

Phase 1 (shipped, PR #1236) derives the Conversion Key from mined within-coffee
shot history and, on real databases, correctly degrades to directional-only —
the mined data rarely spans enough UGS with enough validated pairs. The
deliberate calibration replaces the *mined* key with a *measured* one: two
anchor shots chosen to be far apart on the UGS axis, giving a wide validated
span from two intentional data points instead of many incidental ones.

Validation asset: the #1223 reporter's database (markpalmos, DF83V + Mignon
Specialita, 5,434 shots) is in hand and Phase-1-validated offline via
`tools/calib_analysis.py` — legacy code reproduces his exact bad number
(TurboTurbo rgs 22), shipped code goes directional. It is the designated
second fixture for enabling the long-hop gate. The binary DB is NOT committed
(privacy); it is used offline.

## Goals / Non-Goals

**Goals:**
- Opt-in two-anchor capture with a persisted per-`(grinderModel, grinderBurrs)`
  Conversion Key.
- Stored key takes precedence over the mined key; per-coffee anchor and
  extrapolation cap still apply.
- Long-hop numeric output gated off by default until validated on the fixture.
- Zero impact when no calibration is stored.

**Non-Goals:**
- Modelling the nonlinear UGS↔setting curve; linear within the calibrated span
  plus the cap remains the posture.
- Community/cross-user calibration aggregation (rejected in the UGS ADR:
  ~40% puck-prep variance).
- Changing Phase 1 behavior for uncalibrated users.

## Decisions (inherited, to revisit at implementation)

- **Storage**: new `SettingsGrinder` domain sub-object per the
  settings-architecture rules (never on `Settings` directly;
  `qmlRegisterUncreatableType`; QML access `Settings.grinder.*`).
- **Precedence**: stored key → `confidence: "calibrated"`, validated range =
  deliberate anchor span; mined path untouched otherwise.
- **Gate**: `useDeliberateCalibrationForLongHop` default off; with the gate off
  a stored calibration holds calibrated confidence only within the Phase-1
  window and falls back to directional beyond it.

## Open Questions

- **Go/no-go** (blocks everything): is the ritual worth the UI surface? See
  proposal.
- Anchor profile choice: mandate specific profiles (Cremina / Rao Allongé) or
  any two ≥ K UGS apart? Parent design leaned to the latter (less friction,
  consistent with the per-coffee anchor generalization).
- Capture UX: guided flow from the grinder/equipment page vs. a post-shot
  "use this as an anchor" action. Decide with the go/no-go — this determines
  how much UI the feature costs.
- Minimum anchor span K: parent design suggests deriving from `minPairSpan`
  economics but deliberately larger (the whole point is a wide span); pick and
  pin in tests.
