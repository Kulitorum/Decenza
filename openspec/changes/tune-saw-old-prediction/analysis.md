# Analysis: Tune Stop-At-Weight Old Prediction Model

## Phase 0 production validation

The simulator and corpora carried over from `update-saw-prediction-model`'s archived Phase 0 work. Phase 0 here closes the simulator-vs-production validation gap that the prior change discovered: the simulator's standalone port lacks production's IQR-based batch rejection and converged-mode pre-batch outlier rejection, and these gaps inflated earlier σ-tuning claims by ~2×. To validate without that bias, the smart-pool branch was implemented inline in `Settings::getExpectedDripFor` behind an env-var gate (`DECENZA_SAW_BOOTSTRAP_MODE=smart`), so `tools/saw_parity/` could A/B both σ values and both bootstrap structures through identical Settings/state on the 63-shot corpus.

### 2×2 grid (production code, real `Settings::getExpectedDripFor`)

| | **scalar bootstrap** (current production) | **smart-pool bootstrap** (proposed) |
|---|---|---|
| **σ=1.5** (cell A — current shipping) | overall 0.370, low 0.670, mid 0.195, high 0.686, shot887 +0.90 g | overall 0.389, low 0.670, mid 0.203, high 0.786, shot887 +0.90 g |
| **σ=0.25** | overall 0.348, low 0.641, mid 0.189, high 0.592, shot887 +0.57 g | overall 0.344, low 0.682, mid 0.201, high 0.429, shot887 +0.94 g |

### Cell-by-cell deltas vs cell A (current shipping config)

| Cell | Description | Overall MAE Δ vs A | Notes |
|---|---|---|---|
| **A** | σ=1.5, scalar | 0.000 g (baseline) | current production |
| **B** | σ=1.5, smart-pool | **+0.019 g (worse)** | wide Gaussian degenerates smart-pool to flat average over noisier raw entries; loses to scalar's pair-medians |
| **C** | σ=0.25, scalar | **−0.022 g (−6%)** | σ-tuning alone — production-validated win |
| **D** | σ=0.25, smart-pool | −0.026 g | barely beats cell C; gains are non-uniform |

### Detailed cell D vs cell C (the smart-pool-incremental analysis)

| Metric | C (scalar) | D (smart-pool) | Δ |
|---|---|---|---|
| **overall** | 0.348 | 0.344 | −0.004 g (4× below threshold) |
| **low-flow (<1.5)** | **0.641** | 0.682 | **+0.041 g worse** ❌ |
| mid [1.5,3) | 0.189 | 0.201 | +0.012 g worse |
| **high-flow (≥3)** | 0.592 | **0.429** | −0.163 g better ✅ |
| **shot 887 signed err** | **+0.573** | +0.938 | **+0.365 g worse** ❌ |
| worst-case error | 2.558 | 2.558 | unchanged |

Smart-pool *redistributes* error rather than reducing it: high-flow improves dramatically (0.59 → 0.43, −28%) but low-flow gets worse (0.64 → 0.68, +6%) and the headline shot 887 over-prediction case gets *worse* (+0.57 → +0.94, +64%). Net overall MAE delta is −0.004 g — well below the 0.010 g gate threshold.

### Why smart-pool helps high-flow but hurts low-flow

At σ=0.25 the Gaussian smoother is local. For a high-flow query (~5 ml/s), the scalar bootstrap returns a constant slope `flow × globalSawBootstrapLag` that doesn't account for stall-recovery shots' larger drips at high flow — but those *are* present in the smart pool's nearby-flow entries, so smart-pool weights them heavily and predicts more accurately. Hence the −0.16 g win at high flow.

For a low-flow query (~1.2 ml/s, like shot 887), the situation reverses. Scalar bootstrap returns `1.22 × 0.5s ≈ 0.61 g` — close to the actual 0.20 g shot 887 produced. Smart-pool's local smoother weights the nearest committed median, which for shot 887 happens to be a 0.7-1.0 g drip at flow=1.4-1.5 ml/s — pulling the prediction up to ~1.13 g, *worse* than scalar's flow-blind constant. The very locality that helps high-flow hurts low-flow when nearby-flow medians don't reflect the true low-flow drip behavior.

### Gate evaluation (per `tasks.md` Phase 0)

> **Gate**: smart-pool at σ=0.25 must beat scalar at σ=0.25 by at least **0.01 g overall MAE** AND beat scalar at σ=1.5 by at least **0.02 g overall MAE**.

- Smart-pool@σ=0.25 vs scalar@σ=0.25: **−0.004 g** vs threshold 0.010 g → **FAIL**
- Smart-pool@σ=0.25 vs scalar@σ=1.5: −0.026 g vs threshold 0.020 g → PASS
- Combined: **GATE FAILS** — both criteria are required

The smart-pool simulator estimate (−0.07 g on bootstrap shots, ~30% improvement) was inflated by ~3× when measured against real production code. The mechanism: the simulator's pool included batches that production's IQR rejection would have dropped, biasing the simulator's smart-pool toward more-extreme entries that happened to produce locally-better predictions on this corpus.

This is exactly the over-confidence the gate was designed to catch.

### Decision: Decision B (from `tasks.md` Phase 4)

> **Decision B — Keep σ change, revert smart-pool.** Pick this iff Decision A's criteria fail specifically because of bootstrap-path predictions (perPair shots are fine, smartBootstrap shots are worse than the σ=0.25 + scalar variant captured in shadow logging).

The data fits this decision exactly:
- Cell C (σ=0.25, scalar) is the production-validated win. Overall −6%, high-flow −14%, shot 887 −37%. **Ship this.**
- Cell D (σ=0.25, smart-pool) is a wash with concerning per-bucket regressions including the headline complaint. **Don't ship.**

## Updated proposal scope

This proposal now ships **only the σ change** — three call sites changing `flowDiff² / 4.5` to `flowDiff² / 0.125`. The smart-pool half is dropped.

Tasks.md Phase 1 stays as-is *except* the smart-pool implementation task is removed; only the three σ edits and new tests remain. The proposal.md and design.md should be updated to reflect the narrower scope (mention smart-pool only as "considered and rejected per Phase 0 evidence").

## Frozen reference values for Phase 3

Numbers from cell A (current shipping, σ=1.5 + scalar) and cell C (proposed shipping, σ=0.25 + scalar):

| Metric | Current (cell A) | Proposed (cell C) |
|---|---|---|
| Overall MAE | 0.370 g | 0.348 g |
| Low-flow MAE (<1.5) | 0.670 g | 0.641 g |
| Mid-flow MAE | 0.195 g | 0.189 g |
| High-flow MAE (≥3) | 0.686 g | 0.592 g |
| Shot 887 signed error | +0.90 g | +0.57 g |
| Worst-case error | 2.520 g | 2.558 g |

Phase 3 post-deploy data must show overall MAE ≤ 0.348 g (within noise) and shot-887-class shots showing the predicted improvement before the change is considered validated.

## Files / state

- `src/core/settings.cpp` and `src/machine/weightprocessor.cpp`: reverted to clean σ=1.5 state. Smart-pool helper removed from settings.cpp. No production code change in working tree.
- `tools/saw_parity/main.cpp`: extended with per-source MAE breakdown (kept; useful for Phase 3).
- `tools/saw_replay/main.cpp`: unchanged from prior phase.
- This file (`analysis.md`): records the Phase 0 grid and decision.

Phase 1 (the actual σ change to ship) has not started. Awaiting human approval of the narrowed scope before editing production code.
