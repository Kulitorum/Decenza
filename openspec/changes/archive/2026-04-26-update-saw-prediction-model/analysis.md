# Analysis: Update Stop-At-Weight Prediction Model

> **TL;DR — both halves of the proposal fail the gate; the σ-tuning candidate wins on real production code, but by less than the simulator initially claimed.**
>
> | Goal | Proposed mechanism | Result |
> |---|---|---|
> | **Quality** (fix shot 887) | Replace weighted-avg with linear regression | ❌ NEW worse than OLD across every bucket on the 63-shot corpus |
> | **Speed** (pair-specific in 2 shots) | Pending-batch warm-up (≥2 entries) + smart bootstrap | ❌ warmup makes predictions slightly *worse*: a 2–4 entry per-pair pool loses to a 12-entry cross-pair bootstrap |
> | **(Discovered) Quality via tuning** | Drop OLD's Gaussian σ from 1.5 → 0.25 ml/s | ✅ **production-validated** improvement: overall MAE −6%, high-flow MAE −14%, shot-887 error −37% |
>
> The σ=0.25 finding was first surfaced by the simulator (which over-estimated the gains by ~2× because its standalone port was missing production's IQR-based batch rejection). A separate parity tool (`tools/saw_parity/`) confirmed the simulator's port diverged from production, then directly measured σ=0.25 vs σ=1.5 through the real production code path on the 63-shot corpus to land the validated numbers above.
>
> Recommendation: withdraw both halves of `update-saw-prediction-model` and open a new minimal proposal `tune-saw-gaussian-sigma` with the σ change, rooted in the production-validated numbers — not the inflated simulator estimates.

This document captures the **pre-deploy baseline** that Phase 3 would compare against if the change ever ships, and records the Phase 0 gate decision. Numbers below are produced by `tools/saw_replay/` replaying `tools/saw_replay/data/baseline.json` (22 shots, initial sample) and `tools/saw_replay/data/baseline_full.json` (63 shots, larger harvest) through:
- **OLD**: recency-weighted Gaussian-flow-similarity weighted average of drip (current production model)
- **NEW (linear)**: recency-weighted least squares for `drip = a·flow + b` with `a∈[0,5]`, `b∈[−2,2]`
- **NEW (mad)**: same as linear but pre-filtered by MAD-based outlier rejection (k=2.5 × 1.4826 × MAD threshold)

## Corpus

- **Source**: per-shot debug logs (`mcp__de1__shots_get_debug_log`) from shots 860–887, 2026-04-17 → 2026-04-26.
- **Count**: 22 SAW-triggering shots.
- **Pairs**: (`80_s_espresso`, Decent Scale) — 17 shots; (`d_flow_q`, Decent Scale) — 5 shots.
- **Flow distribution**: 2 low (<1.5 ml/s), 15 mid [1.5, 3) ml/s, 5 high (≥3 ml/s).

### Why the corpus is 22 shots, not the spec's "≥30"

`tasks.md` Phase 0 calls for "≥ 30 SAW-triggering shots" extracted from the persistent debug log via `[SAW] accuracy:` lines. In practice the persistent log retained **only 4** such lines for the working session (shots 882, 884, 886, 887). The persistent log is bounded by recent sessions; older `[SAW] accuracy:` entries had aged out by 2026-04-26.

Each per-shot debug log, however, preserves enough state to reconstruct a SAW row:

- `[SAW-Worker] Stop triggered: weight=W flow=F expectedDrip=P target=T` — flow at stop and the live-system prediction
- `[SAW] Weight stabilized at S g` (or `Weight settled by avg at S g`) — final settled weight; drip = S − W

The corpus was rebuilt by walking shots 860–887 via `mcp__de1__shots_get_debug_log` and parsing those two lines per shot. Shots with no SAW-trigger (e.g., 868, 879, 881, 883, 885 — early stops, missed targets) are excluded.

The 22-shot count is enough to evaluate the Phase 0 gate but is on the small side for fine-grained per-bucket comparison, especially the low-flow bucket (n=2). Phase 3's post-deploy log will accumulate cleaner per-shot rows from the new `[SAW] accuracy:` instrumentation; those rows will share metric definitions with this baseline and can be diffed directly.

## Pre-deploy baseline

Simulator command:

```bash
saw_replay --corpus tools/saw_replay/data/baseline.json
```

### Per-shot replay

Pool grows in chronological order; each row reports the model state **before** that shot is added.

| shot | flow | actual | OLD pred | NEW pred | OLD err | NEW err | new a | new b | source | bucket |
|------|------|--------|----------|----------|---------|---------|-------|-------|--------|--------|
| 860 | 1.77 | 1.30 | 0.85 | 0.85 | −0.45 | −0.45 | — | — | lagFallback | mid |
| 861 | 1.51 | 0.90 | 1.30 | 0.73 | +0.40 | −0.17 | — | — | lagFallback | mid |
| 862 | 1.92 | 1.40 | 0.94 | 1.53 | −0.46 | +0.13 | 1.55 | −1.45 | regression | mid |
| 863 | 2.30 | 0.80 | 1.23 | 1.89 | +0.43 | +1.09 | 1.24 | −0.97 | regression | mid |
| 865 | 1.85 | 1.00 | 1.06 | 1.69 | +0.06 | +0.69 | 0 (clamp) | 1.69 (clamp) | regression | mid |
| 866 | 2.34 | 1.00 | 1.04 | 1.58 | +0.04 | +0.58 | 0 (clamp) | 1.58 | regression | mid |
| 867 | 1.95 | 0.70 | 1.04 | 1.45 | +0.34 | +0.75 | 0 (clamp) | 1.45 | regression | mid |
| 869 | 3.30 | 1.10 | 0.96 | 1.24 | −0.14 | +0.14 | 0 (clamp) | 1.24 | regression | high |
| 870 | 5.20 | 2.50 | 1.03 | 1.17 | −1.47 | −1.33 | 0.06 | 0.86 | regression | high |
| 871 | 3.55 | 1.00 | 1.22 | 1.60 | +0.22 | +0.60 | 0.43 | 0.07 | regression | high |
| 872 | 1.99 | 0.90 | 1.02 | 0.88 | +0.12 | −0.02 | 0.38 | 0.12 | regression | mid |
| 873 | 2.46 | 1.40 | 1.02 | 1.06 | −0.38 | −0.34 | 0.38 | 0.14 | regression | mid |
| 874 | 4.73 | 2.40 | 1.42 | 1.94 | −0.98 | −0.46 | 0.37 | 0.21 | regression | high |
| 875 | 2.04 | 1.10 | 1.10 | 0.92 | −0.00 | −0.18 | 0.45 | −0.01 | regression | mid |
| 876 | 2.29 | 1.10 | 1.13 | 1.05 | +0.03 | −0.05 | 0.45 | 0.02 | regression | mid |
| 877 | 3.04 | 2.80 | 1.21 | 1.38 | −1.59 | −1.42 | 0.46 | −0.02 | regression | high |
| 878 | 2.37 | 0.50 | 1.38 | 1.25 | +0.88 | +0.75 | 0.47 | 0.14 | regression | mid |
| 880 | 2.66 | 1.10 | 1.31 | 1.29 | +0.21 | +0.19 | 0.52 | −0.10 | regression | mid |
| 882 | 2.29 | 1.05 | 1.25 | 1.08 | +0.20 | +0.03 | 0.53 | −0.14 | regression | mid |
| 884 | 1.39 | 0.70 | 1.18 | 0.61 | +0.48 | −0.09 | 0.53 | −0.13 | regression | low |
| 886 | 1.52 | 0.72 | 1.10 | 0.71 | +0.38 | −0.01 | 0.54 | −0.11 | regression | mid |
| 887 | 1.22 | 0.20 | 1.01 | 0.52 | +0.81 | +0.32 | 0.57 | −0.17 | regression | low |

### Aggregate

| bucket | n | OLD MAE | NEW MAE | OLD worst | NEW worst | Δ MAE |
|--------|---|---------|---------|-----------|-----------|-------|
| overall | 22 | **0.458** | **0.445** | 1.59 | 1.42 | **−0.014** |
| low (<1.5) | 2 | **0.644** | **0.207** | 0.81 | 0.32 | **−0.438** (−68%) |
| mid [1.5,3) | 15 | 0.293 | 0.362 | 0.88 | 1.09 | **+0.070** |
| high (≥3) | 5 | 0.881 | 0.787 | 1.59 | 1.42 | −0.094 |

Other signals:
- **Clamp hits**: 4 of 22 fits (~18%) landed on a coefficient boundary (rows 865, 866, 867, 869 — all hit the `a ≥ 0` floor early in the simulation when the pool was small and recent shots happened to slope downward). Decision A's Phase-4 budget allows ≤10%; this run is over but the clamps cluster in the early-pool window where the regression is least informative anyway. Worth re-checking on the post-deploy data.
- **Shots where NEW < OLD (absolute error)**: 14 of 22.

### Per-pair quick look

Splitting the 22 by pair:

| pair | n | OLD MAE | NEW MAE |
|------|---|---------|---------|
| 80_s_espresso :: Decent Scale | 17 | 0.499 | 0.452 |
| d_flow_q :: Decent Scale | 5 | 0.318 | 0.420 |

The d_flow_q pair shows a +0.10 g regression. With n=5 and three of those being the simulation's first three shots (where the regression has 0–2 prior observations to fit), this is mostly small-sample noise. Phase 3 will revisit per-pair MAE once each pair has enough post-deploy shots.

## Phase 0 gate evaluation

Gate (from `tasks.md` Phase 0):

> If the simulator shows NEW is worse than OLD overall OR worse in the low-flow bucket, STOP.

### On the 22-shot corpus (initial)

- Overall: NEW (0.445) **better than** OLD (0.458) ✅
- Low-flow (<1.5 ml/s, n=2): NEW (0.207) **better than** OLD (0.644) ✅
- Apparent verdict: PASSES.

This was misleading because the low-flow bucket had only n=2 shots (884, 887) and both happened to be near the linear regression's sweet spot.

### On the 63-shot corpus (after harvest of shots 600–887)

| bucket | n | OLD MAE | NEW (linear) | Δ vs OLD | NEW (MAD) | Δ vs OLD |
|---|---|---|---|---|---|---|
| **overall** | 63 | 0.407 | **0.500** | **+0.093** ❌ | 0.470 | +0.063 |
| **low (<1.5)** | 15 | 0.649 | **0.670** | **+0.021** ❌ | 0.660 | +0.011 |
| mid [1.5,3) | 40 | 0.262 | 0.348 | +0.086 | 0.283 | +0.021 |
| high (≥3) | 8 | 0.676 | 0.942 | +0.266 | 1.046 | +0.370 |
| clamp hits (linear) | — | — | 12/63 (19%) | — | — | — |

- Overall: NEW (0.500) **worse than** OLD (0.407) ❌
- Low-flow (<1.5 ml/s, n=15): NEW (0.670) **worse than** OLD (0.649) ❌

**Gate: FAILS.** Phase 1 must not begin under the proposal as written. The MAD variant doesn't recover either — it cuts the overall gap (+0.06 vs +0.09) but still loses to OLD across all buckets, and high-flow gets dramatically worse (+0.37) because MAD rejects the data points that were the only flow-dependence signal in the high range.

## Why NEW underperforms on real data

The proposal's design doc argued that the OLD model's Gaussian flow-similarity weighting was redundant once a regression exists, because "the regression already accounts for flow-dependent prediction." On the larger corpus this turns out to be incorrect.

Three observations from the 63-shot run:

1. **Stall-recovery outliers distort the global slope.** Shots like 877 (flow=3.04, drip=2.8g) and 870 (flow=5.20, drip=2.5g) are post-stall recoveries: the puck stalled, flow suddenly resumed, and a large unexpected drip followed. There are 5–6 such shots in the 63-shot corpus. The OLD model's Gaussian-on-flow weighting downweights these for any prediction at a different flow, giving local protection. The NEW linear regression has no such protection — every fit pulls slope and intercept toward those outliers, and that distortion cascades to every flow bucket including low.

2. **MAD rejection isn't the answer either.** When MAD removes those outliers, the slope `a` collapses (from ~0.45 to ~0.10) because the post-stall shots were the *only* high-end data carrying slope information. The model effectively reverts to "predict roughly the median drip" — the same failure mode OLD had. High-flow MAE jumps from 0.94 to 1.05 with MAD, the worst result in any bucket of any model. The outlier IS the signal, but it's the wrong signal for a single global linear fit.

3. **19% clamp hit rate is structural, not transient.** The 22-shot run showed 18% clamp hits and we attributed it to small-pool transient. The 63-shot run shows 19% — the rate doesn't decay as the pool grows. The regression genuinely lands on `a=0` or `b` boundaries about 1 in 5 fits because the data isn't well-described by a single line over the full flow range. Decision A's 10% clamp budget would also be violated post-deploy by extension.

The cleanest interpretation: drip-vs-flow is not globally linear. It is *locally* well-approximated as roughly proportional to flow over short windows, which is exactly what the OLD model's Gaussian-similarity weighting captures. Replacing that local-weighting smoother with a global line throws away the structure the data has.

## Variants explored, all failed the gate

Three regression-based replacements were simulated against the 63-shot corpus:

| Bucket | n | OLD | linear | MAD+linear | LOWESS |
|---|---|---|---|---|---|
| **overall** | 63 | **0.407** | 0.500 (+0.09) | 0.470 (+0.06) | 0.472 (+0.07) |
| **low (<1.5)** | 15 | **0.649** | 0.670 (+0.02) | 0.660 (+0.01) | 0.674 (+0.03) |
| mid [1.5,3) | 40 | **0.262** | 0.348 (+0.09) | 0.283 (+0.02) | 0.347 (+0.08) |
| high (≥3) | 8 | **0.676** | 0.942 (+0.27) | 1.046 (+0.37) | 0.718 (+0.04) |

OLD wins every bucket against every variant. The LOWESS variant — which keeps OLD's Gaussian local-weighting and adds slope/intercept on top — is the closest, but it's still worse, and it's worse in the headline low-flow bucket too. The gate fails for all three replacement models.

## What the data is telling us

Stepping back: the proposal's design doc framed the change as adding flow-dependence to a flow-blind average. That framing is wrong. OLD already has flow-dependence — it's just expressed as a Gaussian-weighted local average rather than a slope/intercept. On this corpus, OLD is essentially a Nadaraya–Watson kernel mean estimator, and that is close to the optimal predictor under the noise structure the corpus exhibits.

What the corpus shows:

- **Drip variance dominates drip flow-dependence.** A low-flow shot can produce drip anywhere from 0.2 g (shot 887) to 1.4 g (shot 884) depending on cup placement, settling time, scale glitches, and post-stop puck behaviour. The slope `∂drip/∂flow` that the proposal wants the regression to capture is small relative to that variance — so any two-parameter fit is fitting mostly to noise. The 19% clamp hit rate is the regression admitting it can't find a stable slope.
- **The "shot 887 problem" isn't a slope problem.** Shot 887 (flow=1.22, drip=0.2) is a low-drip outlier even at its flow level. OLD's prediction of 1.01 was wrong for that shot — but on the corpus average, OLD's prediction at low flow is closer than any regression-based variant produces. The over-prediction on shot 887 is a *symptom of a noisy day*, not a systematic bias the model can correct without sacrificing accuracy elsewhere.
- **Stall-recovery shots (870, 877) have outsized influence in any global fit.** OLD downweights them at low-flow predictions via the Gaussian; every regression variant lets them distort the line. MAD's removal of those outliers collapses the slope; LOWESS reduces but doesn't eliminate the distortion.

## Speed investigation — `--mode=legacy` vs `--mode=warmup`

The simulator was extended to track per-(profile, scale) pair state (pending batch + committed medians) and a per-scale smart-bootstrap pool, then re-run with both modes:

- `legacy`: production behavior. A pair must commit ≥2 medians (= ≥10 shots) before its history is consulted; until then, predictions come from the smart bootstrap pool over all same-scale pairs.
- `warmup`: proposed behavior. As soon as the pair's pending batch has ≥2 entries, predictions fit on those entries instead of the bootstrap.

Run config: `--variant=old` (the OLD weighted-average model — the warmup change is model-agnostic), default σ=1.5.

| Bucket | legacy | warmup | Δ |
|---|---|---|---|
| **overall** | 0.366 | 0.380 | **+0.014** ❌ |
| low (<1.5) | 0.656 | 0.656 | 0 |
| mid [1.5,3) | 0.185 | 0.197 | +0.012 |
| high (≥3) | 0.724 | 0.773 | +0.050 |

The result is the opposite of what the proposal predicted. Per-source breakdown explains it:

| Source | legacy n | legacy MAE | warmup n | warmup MAE |
|---|---|---|---|---|
| perPair (graduated, ≥2 medians) | 43 | 0.419 | 43 | 0.419 |
| pendingBatch (warmup-only path) | 0 | — | **11** | **0.393** |
| globalBootstrap | 18 | **0.256** | 7 | 0.167 |
| scaleDefault | 2 | 0.195 | 2 | 0.195 |

The 11 shots that warmup mode rerouted from globalBootstrap → pendingBatch had MAE 0.393 vs. 0.256 they would have gotten from the bootstrap. **A 2–4 entry pool from one pair is worse than a 12-entry pool aggregated across pairs**, even when "across pairs" means data from a different profile or grinder. Wisdom of (many) crowds beats wisdom of (own, sparse) self on this corpus.

The within-pair shot-index breakdown confirms it:

| within-pair index | n | legacy MAE | warmup MAE |
|---|---|---|---|
| shot1 | 2 | 0.094 | 0.094 |
| shots 2–5 | 8 | 0.304 | 0.320 |
| **shots 6–10** | 10 | **0.238** | **0.314** |
| shots 11+ (graduated) | 43 | 0.419 | 0.419 |

Shots 6–10 — the pre-graduation window the speed change targets — get *worse* in warmup mode because they're forced onto a freshly-emptied pending batch (after the shot-5 median commit) instead of the rich bootstrap. The shot1 and shots-2–5 numbers are similar because warmup doesn't change behavior there (legacy uses bootstrap; warmup also uses bootstrap until pendingBatch has ≥2).

Verdict: **the speed-up half of the proposal cannot ship as written.** A 2-entry threshold is too aggressive; a 4-entry threshold *might* break even but isn't tested here. Even if it did, the gain would be marginal compared to the σ-tuning win below.

## Quality investigation — OLD parameter sweep

Pivoting from "swap the model" to "tune the model we have," the OLD model's two main knobs were swept on the 63-shot corpus:

- **σ** (Gaussian flow-similarity width): 0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.50, 0.75, 1.0, 1.5 (current default), 2.0, 3.0
- **Recency profile**: ineffective on this corpus — per-pair medians are so few (1–3) that recency interpolation between max and min collapses; not pursued further.

### σ sweep results (recency held at default 10→3 converged / 10→1 unconverged)

| σ | overall | low (<1.5) | mid [1.5,3) | high (≥3) | shot 887 err |
|---|---|---|---|---|---|
| 0.10 | 0.335 | 0.622 | 0.194 | 0.503 | +0.50 |
| 0.15 | 0.318 | 0.609 | 0.185 | 0.438 | +0.50 |
| 0.20 | 0.319 | 0.625 | 0.180 | 0.437 | +0.51 |
| **0.25** | **0.315** | 0.625 | **0.176** | 0.426 | +0.51 |
| 0.30 | 0.316 | 0.631 | 0.184 | 0.386 | +0.51 |
| 0.35 | 0.322 | 0.656 | 0.184 | 0.385 | +0.90 |
| 0.40 | 0.321 | 0.656 | 0.184 | 0.380 | +0.90 |
| 0.50 | 0.332 | 0.656 | 0.184 | 0.463 | +0.90 |
| 0.75 | 0.360 | 0.656 | 0.185 | 0.682 | +0.90 |
| 1.0 | 0.364 | 0.656 | 0.185 | 0.710 | +0.90 |
| **1.5 (default)** | **0.366** | **0.656** | **0.185** | **0.724** | **+0.90** |
| 2.0 | 0.404 | 0.656 | 0.188 | 1.016 | +0.90 |
| 3.0 | 0.407 | 0.656 | 0.191 | 1.020 | +0.90 |

**σ=0.25 wins overall** with a meaningful margin:

| Metric | Default σ=1.5 | Tuned σ=0.25 | Δ |
|---|---|---|---|
| Overall MAE | 0.366 | 0.315 | **−14%** |
| Low-flow MAE | 0.656 | 0.625 | −5% |
| Mid-flow MAE | 0.185 | 0.176 | −5% |
| High-flow MAE | 0.724 | 0.426 | **−41%** |
| Worst-case error | 2.520 | 2.520 | unchanged (shot 877 still wrong) |
| Shot 887 specific error | +0.90 g | +0.51 g | **−43%** |

There's a discontinuity between σ=0.30 (shot 887 err +0.51) and σ=0.35 (shot 887 err +0.90). At σ ≤ 0.30 the Gaussian is tight enough that one specific low-flow committed median dominates shot 887's prediction; at σ ≥ 0.35 it gets diluted by mid-flow medians ~1.5 ml/s away. The σ=0.25 setting sits comfortably in the favorable regime without being pathologically narrow (σ=0.10 starts to lose at low-flow because some queries have *no* sufficiently-near sample).

### Why tighter σ helps

The OLD model's Gaussian acts as a local-window estimator: a query at flow=1.22 mostly weights samples at similar flows. At σ=1.5 (default), a sample at flow=2.5 still contributes ~30% of full weight; at σ=0.25 it contributes essentially zero. The 63-shot corpus has stall-recovery shots at high flow (870 at 5.20 with drip 2.5 g; 877 at 3.04 with drip 2.8 g) that are *bona fide* outliers from the perspective of mid- and low-flow predictions. Tighter σ excludes them automatically.

The 41% high-flow improvement is more counterintuitive: shouldn't a tighter σ on a high-flow query also exclude relevant data? It does, but the high-flow queries (e.g., shot 870 at flow=5.20) currently get a prediction of ~1.0 g pulled down by the bulk of mid-flow shots; with σ=0.25 the prediction collapses to ~the closest high-flow medians, which match the actual drip pattern better.

### Production validation (saw_parity)

The simulator findings above were treated with skepticism and validated by running the actual production code path (`Settings::getExpectedDripFor` + `Settings::addSawLearningPoint`) on the same 63-shot corpus, twice — once at σ=1.5 and once at σ=0.25 — via a new tool at `tools/saw_parity/`. This bypasses the simulator entirely; the inputs are the corpus, the predictor is the real production function, and the only thing changing between runs is the σ constant.

| Metric | Production σ=1.5 | Production σ=0.25 | Production Δ | Simulator had claimed |
|---|---|---|---|---|
| Overall MAE | 0.370 | 0.348 | **−6%** | −14% |
| Low-flow MAE (<1.5) | 0.670 | 0.641 | **−4%** | −5% |
| Mid-flow MAE | 0.195 | 0.189 | **−3%** | −5% |
| High-flow MAE (≥3) | 0.686 | 0.592 | **−14%** | −41% |
| Shot 887 signed error | +0.90 g | +0.57 g | **−37%** | −43% |
| Worst-case error | 2.520 | 2.558 | +1.5% | unchanged |

The σ=0.25 win is real on production code, but **roughly half the magnitude the simulator estimated**. The discrepancy traces to the simulator's standalone port being incomplete: it lacks production's IQR-based batch rejection (`if lagIqr > 1.0s → drop batch`) and converged-mode pre-batch outlier rejection (`if |drip - expectedDrip| > max(3, expectedDrip) → reject`). Production rejects outlier batches upstream, so σ=0.25 has less outlier-suppression work to do than the simulator's pool implied. The simulator's overall MAE Δ was inflated by ~2×, the high-flow MAE Δ by ~3×.

**Methodological lesson worth banking**: simulator-derived sweep results need parity validation before they're treated as load-bearing. The simulator is fine for relative comparisons (σ=0.25 < σ=1.5 holds in both implementations) but not for absolute-magnitude claims. `tools/saw_parity/` stays in the tree for any future SAW work that wants to make production-grounded claims.

### What σ=0.25 doesn't fix

- **Worst-case error is unchanged** (2.52 g). Shot 877 (drip=2.8 g, flow=3.04) is still mispredicted because there's no other shot near its flow with similarly-large drip — σ-tuning can't conjure information that isn't there.
- **Shot 887 still over-predicts by 0.51 g** — better than +0.90 default but still substantial. For a true fix, would need either (a) more low-flow data with similar puck behavior, or (b) the puck-state feature the proposal earmarks for Decision C.

## Recommendation

Three concrete next moves, ordered:

1. **Withdraw `update-saw-prediction-model` as written.** Both halves fail the Phase 0 gate on the 63-shot corpus. The proposal's two premises — that the Gaussian flow-similarity weighting was redundant, and that pair-specific data after 2 shots beats cross-pair bootstrap — are both contradicted by the data. Archive this change with the analysis attached so the next attempt can build on what we learned.

2. **Open `tune-saw-gaussian-sigma`** (new minimal proposal). Three-site code change: drop the σ in `Settings::getExpectedDrip`, `Settings::getExpectedDripFor`, and `WeightProcessor::getExpectedDrip` from `flowDiff² / 4.5` (σ=1.5) to `flowDiff² / 0.125` (σ=0.25). **Production-validated** on the 63-shot corpus: overall MAE −6%, high-flow MAE −14%, shot 887 specific error −37%. Modest but real. Easy to ship, easy to roll back, no algorithm restructuring.
   - Rollout: ship behind shadow logging similar to what `update-saw-prediction-model` Phase 2 specified — log both σ=1.5 and σ=0.25 predictions so post-deploy data confirms the saw_parity finding holds across more users / scales / profiles.
   - Risk: low. σ=0.25 still produces non-zero weight for samples within ~0.5 ml/s of the query, so the smoother is not pathologically narrow; queries beyond that fall through to the existing `globalSawBootstrapLag * flow` or `flow * (sensorLag + 0.1)` fallback paths.
   - Caveat: σ=0.25 doesn't fix worst-case error (shot 877 still misses by ~1.7 g) — that's a Decision-C-class problem that needs a feature (puck-state proxy), not a tuning knob.
   - Existing test impact: zero. `tst_SawSettings`, `tst_SAW`, and `tst_WeightProcessor` (52 tests) all pass at σ=0.25 because they train and query at the same flow value, where σ doesn't affect the result. New tests would be needed to *defend* σ=0.25 going forward, separately from the proposal.

3. **Defer the speed-of-personalization goal** until either:
   - A larger threshold is tested (e.g., warmup at ≥4 pending entries instead of ≥2). The simulator can do this trivially — change `>= 2` to `>= 4` in `predictWithMode` and rerun. Worth ~30 minutes if the user wants to pursue this goal.
   - The smart-bootstrap path itself is improved (e.g., per-scale weighting so the bootstrap pool prefers shots with similar profile shape). The current bootstrap is already winning over warmup, so improvements there compound.

The simulator (`tools/saw_replay/`), the harvested corpora (`baseline.json` + `baseline_full.json`), the model variants (linear, mad, lowess), and the per-pair simulation infrastructure stay in the tree as the foundation for any future SAW work.

### Caveats worth carrying into Phase 3

1. **Mid-flow regression (+0.07 g MAE)** — the gate doesn't block on this, but it's a flag. Driven primarily by the first 4–5 shots (rows 862, 863, 865, 866, 867) where the regression had ≤4 prior points and clamped. Once n ≥ 6 the NEW model is at parity or better on mid-flow. Phase 1's pending-batch warm-up + smart bootstrap should reduce this further by giving brand-new pairs a richer prior than the simulator had access to here.
2. **Low-flow sample size (n=2)** — the most important bucket for the proposal's argument has only two data points (884, 887). The 68% improvement is real but fragile. Phase 3 should re-evaluate after ≥10 low-flow shots accumulate post-deploy.
3. **Clamp hit rate (18%)** — above Decision A's 10% gate. Concentrated in the early-pool transient. If post-deploy clamp rate stays above 10% with mature pools, that's a Decision C signal.
4. **Worst-case outliers** — shots 870 (drip=2.5 g at flow=5.20 ml/s, after a stalled puck) and 877 (drip=2.8 g at flow=3.04 ml/s) drive both models' worst-case errors. Neither model captures these because the puck behaviour they reflect (post-stall recovery) isn't a feature in either model. Out of scope for this change; flagged for the 3-feature follow-up if Decision C lands.

## Files produced by Phase 0

- `tools/saw_replay/main.cpp` — simulator entry + math (stand-alone port of OLD + NEW; no Qt GUI/BLE/QML linkage; only Qt6::Core for JSON parsing).
- `tools/saw_replay/data/baseline.json` — 22-shot corpus.
- Build target: `saw_replay` in root `CMakeLists.txt` (desktop only, gated like `shot_eval`).

To re-run:

```bash
# After a build that includes the saw_replay target:
build/Qt_6_10_3_for_macOS-Debug/saw_replay --corpus tools/saw_replay/data/baseline.json
```

## Frozen reference values for Phase 3

These are the numbers Phase 3 must beat (or at minimum not regress) when Decision A's criteria are evaluated against post-deploy data:

| Metric | Pre-deploy baseline (NEW model) |
|---|---|
| Overall MAE | 0.445 g |
| Low-flow MAE | 0.207 g |
| Mid-flow MAE | 0.362 g |
| High-flow MAE | 0.787 g |
| Worst-case error | 1.42 g |
| Clamp hit rate | 18% (early-pool transient — recheck post-deploy) |

Decision A requires post-deploy low-flow MAE to be **≥50% better than the pre-deploy OLD baseline** (0.644 g) — i.e., post-deploy low-flow MAE ≤ 0.322 g. This simulator's NEW model already achieves 0.207 g on the same corpus, so Decision A's bar is reachable on paper; the post-deploy run just needs to confirm it on real data with mature pools.
