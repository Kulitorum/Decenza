# Tasks: Tune Stop-At-Weight Old Prediction Model

## Phase 0 — Production validation (COMPLETE; see analysis.md)

Phase 0 has already been executed. Results are recorded in `analysis.md`. Summary:

- [x] σ=1.5 vs σ=0.25 measured through real production code via `tools/saw_parity/`. Result: σ=0.25 wins (overall MAE 0.348 vs 0.370, −6%; high-flow 0.592 vs 0.686, −14%; shot 887 +0.57 g vs +0.90 g, −37%).
- [x] Smart-pool bootstrap measured (env-var-gated branch in `Settings::getExpectedDripFor`). Result: gate failed. Smart-pool redistributes error (helps high-flow, hurts low-flow including the headline shot). Reverted.
- [x] 2×2 grid {σ=1.5, σ=0.25} × {scalar, smart-pool} captured in analysis.md.
- [x] Decision recorded: ship σ change only (Decision B from the original Phase 4 framework). Smart-pool dropped from this proposal.
- [x] All Phase 0 source edits reverted. Working tree clean for `src/`.

## Phase 1 — Production code change

- [x] Update three call sites for the σ change. **Shipped via PR #870** (commit 407d4c26 "saw: tune Gaussian flow-similarity sigma from 1.5 to 0.25"). Subsequently centralized in `src/machine/sawprediction.h` (`kFlowSimilaritySigma = 0.25`, `kFlowSimilaritySigmaSq2 = 0.125`) via PR #874 so all three call sites share one constant.
- [x] Add new tests to `tests/tst_saw_settings.cpp` that *do* exercise σ. Landed alongside PR #870 — see `tst_saw_settings.cpp:208-258`:
  - `farQueryFlowFallsBackBecauseGaussianAttenuates` (query 2.5 ml/s, training 1.5 ml/s → falls through to scale-default fallback)
  - `sameFlowQueryReturnsTrainingDrip` (flowDiff=0 baseline lock-in)
  - `differentQueryFlowsProduceDifferentPredictions` (mixed flows 1.0/3.0 → predictions diverge)
- [x] Build + run the full ctest suite. **2026-05-11**: build clean (Debug, 0 errors, only pre-existing openssl version warnings), all 2052 autotests pass, 0 warnings.
- [x] Validate against real production data. **2026-05-11**: instead of replaying the 63-shot Phase 0 corpus, sampled 14 real shots post-deploy via the DE1 MCP `shots_get_debug_log` tool. Result: MAE 0.381 g excluding one out-of-scope stall-recovery shot (#902, +4.07 g) — within ~9% of Phase 0's 0.348 g prediction, well within sampling noise. Per-source breakdown matches Phase 0 cell C structure (bootstrap ~0.4 g under-prediction, perProfile ~0.6 g, globalPool ~0.24 g). Recorded in `analysis.md` Phase 3 section.

## Phase 2 — Shadow logging (DROPPED 2026-04-26)

Originally planned to add `oldSigmaDrip` and `predictionSource` to the `[SAW] accuracy:` log line so post-deploy MAE could be A/B'd against σ=1.5. Dropped because:

- Detailed per-shot SAW data only reaches us when a user submits a system log, and users only submit logs when they hit a problem. That biases any "data we'd see" toward σ=0.25 *failures* rather than a clean A/B sample.
- Without telemetry to pull `oldSigmaDrip` from a representative cohort, the field is effectively dead bytes on most users' devices.
- The cost of adding it (extra qExp per shot end, 2 log fields, a new `SawPredictionDetail` API, ongoing maintenance until removed) outweighs its post-deploy value.

If a richer post-deploy signal becomes desirable later (e.g., if shotmap/visualizer telemetry is extended to include SAW prediction fields), this can be re-added as a small, focused change.

## Phase 3 — Decision (after deployment)

Decision criteria are now qualitative, not metric-based:

- [x] **Decision A — Keep the σ change.** **Selected 2026-05-11.** Both criteria met:
  1. No SAW-related issue reports (overshoot/undershoot complaints) in the GitHub Issues tracker since deploy.
  2. Jeff's daily-driver pulls show no visible regression; production MAE on a 13-shot post-deploy sample matches Phase 0's prediction within noise.
- [ ] ~~Decision B — Roll back the σ change.~~ Not triggered.
- [x] Record the decision in `analysis.md` and update `proposal.md` with a final status line. **Done 2026-05-11.**

## Phase 4 — Archive

- [x] After deployment is stable for ≥ 2 weeks (regardless of decision). **2026-05-11**: PR #870 shipped 2026-04-26, 15 days stable; sync + archive performed via `/opsx:archive`.
