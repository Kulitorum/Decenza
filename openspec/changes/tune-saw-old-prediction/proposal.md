# Change: Tune Stop-At-Weight Old Prediction Model

## Why

Phase 0 evaluation of the archived `update-saw-prediction-model` proposal established two findings on a 63-shot real-shot corpus, validated end-to-end through `Settings::getExpectedDripFor` via `tools/saw_parity/`:

1. Replacing the OLD weighted-average with a regression-based predictor (linear, MAD-trimmed, or LOWESS-style) makes predictions worse, not better, across every flow bucket.
2. The OLD weighted-average has one knob that's mistuned for real-shot data: the Gaussian flow-similarity σ. At σ=1.5 ml/s, stall-recovery shots and high-drip outliers bleed across flow buckets and degrade low- and mid-flow predictions.

A subsequent Phase 0 sweep + production validation found:

- σ=0.25 ml/s gives **production-validated** overall MAE −6%, high-flow MAE −14%, and shot 887 specific over-prediction reduced from +0.90 g to +0.57 g (−37%).
- Replacing the scalar bootstrap path with a Gaussian-weighted aggregated pool was tested in Phase 0 (env-var-gated) and **failed the gate**: overall MAE delta vs scalar at σ=0.25 was −0.004 g (well below the 0.01 g threshold), and shot 887 specifically got *worse* (+0.57 → +0.94 g). Smart-pool redistributes error rather than reducing it. See `analysis.md` for the 2×2 grid.

This proposal therefore ships only the σ change.

## What Changes

**One coordinated tuning** to the existing OLD model — no algorithm replacement, no regression, no new prediction structure, no bootstrap-pool restructuring.

**Drop the Gaussian flow-similarity σ from 1.5 → 0.25 ml/s.** Three call sites:
- `Settings::getExpectedDrip` (global pool path)
- `Settings::getExpectedDripFor` (per-pair graduated path)
- `WeightProcessor::getExpectedDrip` (live SAW threshold)

The current code spells σ as `flowDiff² / 4.5` (= 2σ² for σ=1.5). New value: `flowDiff² / 0.125` (= 2σ² for σ=0.25). One numeric change per site.

The pre-graduation bootstrap path (`flow × globalSawBootstrapLag(scale)` capped at 8 g, falling through to `flow × (sensorLag + 0.1)` capped at 8 g) is unchanged.

## Impact

**Affected specs**: `stop-at-weight-learning` (new capability — establishing the contract for SAW prediction).

**Affected code**:
- `src/core/settings.cpp` — three sites for σ change (the global-pool path, the per-pair graduated path, and the magic constant in the `getExpectedDripFor` per-pair branch)
- `src/machine/weightprocessor.cpp` — one site for σ change
- New tests in `tests/tst_saw_settings.cpp` — existing tests train and query at the same flow value where σ doesn't affect the result, so σ has zero test coverage today. New tests probe σ explicitly.

**Migration**: zero. Existing pool entries are read as-is; the smoother's σ value is the only thing that changes.

**User-visible behavior**:
- Headline shot 887 case: production-validated improvement from +0.90 g to +0.57 g over-prediction. (Still a +0.57 g over-prediction — σ tuning isn't a complete fix; see "What this proposal does not address" below.)
- High-flow shots benefit most (~14% MAE reduction), because tighter σ excludes mid-flow medians from polluting high-flow predictions.
- Mid-flow and low-flow MAE improve by 3-4% on the corpus.
- Brand-new install behavior is unchanged (bootstrap path doesn't depend on σ).

**Risk**: low. One-constant tuning to an existing algorithm with all clamps preserved (drip ∈ [0.5, 20] g) and existing fallback paths preserved (lag-fallback when pool degenerate). Rollback is `git revert <commit-sha>` with no data loss.

**What this proposal does not address**:
- **Worst-case error (≈1.7 g for stall-recovery shots)** — that's a feature problem (e.g., puck-state proxy, flow-trajectory feature), not a tuning problem. Out of scope.
- **Speed of personalization** (pair-specific predictions in <10 shots) — Phase 0 showed pending-batch warmup at any threshold loses to the existing cross-pair bootstrap on the corpus. Decoupled from this proposal.
- **Smart bootstrap pool restructuring** — Phase 0 showed it fails the gate (helps high-flow but hurts the headline low-flow case). Decoupled and dropped.
- **Generalization beyond the corpus** — 63 shots from one user, two profiles, one scale. The post-deploy shadow logging in Phase 2 is the actual generalization test.
