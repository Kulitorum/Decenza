## Context

`DialingBlocks::buildGrinderCalibrationBlock` (`src/ai/dialing_blocks.cpp:713`) computes a cross-profile grinder calibration that both `dialing_get_context`/`dialing_get_grinder_calibration` (MCP) and the in-app advisor (`aimanager.cpp`) feed to the model. Today it:

- queries all-time shots on the same grinder+burrs with a weak "clean" filter (`final_weight ≥ 15`, no quality badge);
- groups by resolved KB id, takes the **all-time median** grinder setting per profile;
- picks the widest-UGS-span consistent **two-profile pair** as `fineAnchor`/`coarseAnchor`;
- derives `conversionKey = Δsetting / ΔUGS` and emits a numeric `rgs` for *every* KB profile, labelling out-of-range entries `source: "extrapolated"` but still numeric.

Offline replay on a real 928-shot DB reproduced #1223 exactly: `conversionKey = −2.4` (wrong sign for a Niche, where lower = finer), TurboTurbo `rgs = −2.4`. Root cause analysis (see proposal) established the real model:

```
grind(profile, coffee) ≈ coffee_baseline(coffee)  +  profile_offset(profile)
                          └ dominant, 5↔12, per-bean ┘   └ UGS·conversionKey ┘
```

The original UGS spec (videoblurb.com/UGS) already encodes this: a per-grinder **Span / Conversion Key** measured once from two distant anchors, re-anchored per coffee by dialing one profile on the new bean ("maintain the span"). Empirical within-coffee analysis confirmed the Conversion Key is coffee-independent and tight (~1.4–1.5 steps/UGS across independent beans with baselines 5.5–12) — but only over the ≤1.5-UGS spans incidental history actually contains.

## Goals / Non-Goals

**Goals:**

- Phase 1: never emit a wrong-signed or out-of-range numeric grind recommendation; degrade to directional/qualitative guidance instead. Fix #1223 for every provider and surface.
- Phase 1: when a numeric recommendation *is* given, anchor it on the current coffee and a within-coffee-derived conversion key.
- Phase 1: constrain the model — UGS is relative/directional; no click arithmetic beyond the validated range.
- Phase 2: provide a high-accuracy full-range numeric path (deliberate two-anchor calibration) without trusting it until validated on independent data.
- Keep both consumer surfaces (`buildGrinderCalibrationBlock` is shared) byte-equivalent.

**Non-Goals:**

- Modelling the nonlinear/quadratic UGS↔setting curve (literature: Al-Shemmeri, Gagne). Linear-within-validated-range + a hard cap is sufficient and honest.
- Community/cross-user calibration aggregation (ADR rejected: 40% puck-prep variance).
- Changing the KB UGS values themselves, the profile resolver, or any BLE/machine path.
- Auto-detecting bean changes from telemetry (separate open question in the UGS ADR).

## Decisions

**D1 — Within-coffee paired conversion key, not pooled medians.**
Group qualifying shots by `(coffeeBatch, profile)`. For each `coffeeBatch` with ≥2 distinct-UGS profiles, take per-profile medians and form within-coffee pairwise slopes `Δsetting/ΔUGS`.

**D1b — Coffee identity SHALL be batch-level, not bean-level.**
`coffeeBatch` = `beanBrand + beanType + roastDate` when `roastDate` is a real value; when `roastDate` is empty or the `"--"` undated sentinel, fall back to `beanBrand + beanType` with **single-linkage 90-day clustering** (per bean, shots ordered by time, a gap > 90 days starts a new batch). This is a sliding window, NOT a fixed calendar bucket — two shots a few days apart must never land in different batches due to a bucket boundary. *Why:* `coffee_baseline` shifts materially across roast batches of the same bean (the user's own data: same bean dialed 5 vs. 11 across batches). A within-coffee pair only cancels the baseline if both profiles were pulled on the **same batch**; pairing across batches reintroduces the exact confounder the within-coffee design removes. Empirically this matters: bean-only keying on the real DB produced spreadRatio 0.30 (falsely publishes); correct batch keying produced spreadRatio 0.93 (correctly directional) — bean-only keying was averaging across batches and masking the noise. The same batch rule applies to the D3 anchor. *Alternative rejected:* bean-only identity (original prototype) — falsely confident; date-parse normalization — fragile across the mixed stored formats (`MM.DD.YYYY`, `YYYY-M-D`), raw-string batch equality + 90-day undated fallback is robust and matches `bestRecentShot`'s existing 90-day philosophy. The conversion key is the Theil–Sen (median) of all such pairs pooled across coffees. The conversion key is a per-`(grinderModel, grinderBurrs)` runtime output — NEVER a shipped constant (slope magnitude varies wildly per grinder+burrs; Al-Shemmeri: 13.4 vs 58.4 microns/step across grinders). *Why:* cancels `coffee_baseline`, which empirically swung the pooled slope from −12 to +10. Theil–Sen matches the codebase's existing median-everywhere philosophy and is outlier-robust. *Alternative rejected:* OLS over pooled points (what we have) — confounded; OLS per coffee — only one coffee in real data has ≥3 points.

**D1a — The spread gate SHALL be dimensionless (grinder-portable).**
The "are these pairwise slopes consistent enough to publish a number" gate SHALL compare `IQR(pairwiseSlopes)` to a fraction of `|median(pairwiseSlopes)|`, NOT to an absolute steps/UGS threshold: `publish iff IQR ≤ maxSpreadRatio · |conversionKey|`. *Why:* slope magnitude is grinder-specific, so an absolute IQR threshold (e.g. 1.0 steps/UGS) silently never trips on coarse-stepped grinders and always trips on fine ones — a per-grinder defect. The ratio is the only dimensionless, portable form. `minPairSpan` and `cap` remain absolute because they live on the **UGS axis**, which is the universal scale by construction; only the slope-derived gate needed this. *Alternative rejected:* absolute `maxSlopeSpread` (original design) — grinder-dependent, fails portability.

**D2 — Dialed-in quality filter.**
A shot anchors calibration only if `final_weight ≥ 15` AND no quality badge AND (`enjoyment ≥ 50` OR `|final_weight − targetWeight| ≤ 0.10·targetWeight` OR has refractometer reading). *Why:* the current filter keeps undershoot/aborted experiments (real data: 7 of 11 Adaptive-v2 "anchor" shots were 21–27 g on a 36 g target, unrated). *Trade-off:* fewer anchor shots → more often "unavailable", which is the correct, honest outcome.

**D3 — Per-coffee anchor (intercept), generalized.**
The numeric intercept is the user's most recent dialed-in shot whose `coffeeBatch` (D1b) matches the resolved shot's, on any known-UGS profile (not necessarily Cremina). Projection: `rgs(target) = anchorSetting + (UGS_target − UGS_anchorProfile) · conversionKey`. *Why:* `coffee_baseline` is the dominant term and is batch-specific; this is the original spec's "maintain the span" generalized so the user dials their daily profile, not a mandated anchor profile. If no recent dialed-in shot exists for the current shot's `coffeeBatch` → direction only (intercept unknown).

**D4 — Mandatory extrapolation cap (the core #1223 fix).**
Let `W` = widest validated anchor ΔUGS (Phase 1: largest within-coffee pair span; Phase 2: the deliberate calibration's span). A profile gets a numeric `rgs` only if its UGS lies within `[minAnchorUGS − C, maxAnchorUGS + C]`, `C ≈ 1.5–2` UGS. Outside → `source: "directional"`, no number, with a coarser/finer hint and "pull a reference shot". *Why:* every #1223 failure was a 4–8× extrapolation; the literature says it's nonlinear there. This single rule stops both wrong-sign and correct-sign-but-wild outputs. *Alternative rejected:* a confidence score the model may ignore — Gemini and Claude both ignored prose; a hard structural cap removes the bad number entirely.

**D5 — Directional-only / unavailable as first-class payload states.**
`grinderCalibration` gains `confidence` (`"calibrated" | "approximate" | "directional"`) and `usageConstraint` (a short string the prompt repeats verbatim). When no validated within-coffee anchor exists, the MCP tool returns `available: false` with a reason; the advisor path emits a directional section, never a numeric table. **BREAKING**: out-of-range `profiles[]` entries lose numeric `rgs`. *Why:* the data path, not the model, must enforce honesty.

**D5a — Directional `direction` is anchor-free, KB-ordering-only, grinder-convention-free.**
Because the no-anchor directional state is the *primary* Phase-1 output (D1b makes most real data land here), its correctness is load-bearing. `direction` SHALL be `sign(UGS_target − UGS_currentShotProfile)` from KB UGS values alone — NOT "relative to the nearest anchor" (there may be none), NOT dependent on the conversion key, intercept, or the grinder's finer-direction convention. It SHALL be expressed only as the grind-size term finer/coarser, never a dial-number delta — grind-size ordering is grinder-independent and cannot hit the #1223 sign trap; a dial-number translation can. When the current shot's profile has no canonical UGS, `direction` is withheld (the block flags "current profile not UGS-placed") rather than guessed. *Why:* guarantees the fallback every user hits is always correct with zero history/calibration. *Alternative rejected:* "relative to nearest anchor" (original wording) — undefined in the no-anchor case, which is the common case.

**D6 — AI-usage constraints in the rendered prompt.**
The calibration section states explicitly: UGS is a relative ordering, not grinder clicks; numbers are valid only inside the stated calibrated range; outside it, give direction and ask the user to pull a reference shot. Mirror the wording into the KB `cross-profile-grind-ordering` prose so both surfaces agree. *Why:* Jeff's explicit requirement — the model must not reuse UGS for arithmetic it was never intended for; the original spec and KB already say UGS is directional, the prompt must enforce it.

**D7 — Phase split.**
Phase 1 ships the corrected block + cap + constraints using only mined within-coffee data (no new UI, no new storage) — it strictly reduces harm and is releasable alone. Phase 2 adds `grinder-ugs-calibration`: a deliberate two-distant-anchor capture, a persisted Conversion Key (new `SettingsGrinder` domain sub-object per the settings-architecture rules), and consumption in the block. Phase 2's long-hop numeric output stays gated behind a validation flag until the mechanism is confirmed on ≥1 independent dataset (the #1223 reporter's DB). *Why:* Phase 1 is the urgent correctness fix; Phase 2 is an accuracy improvement whose mechanism still needs external validation.

## Risks / Trade-offs

- **[Phase 1 makes the feature "unavailable" for many single-style users]** → Correct behavior: qualitative direction is still given; a fabricated number is strictly worse. The ADR already concluded most users are served by qualitative Tier-1 guidance.
- **[Within-coffee conversion key from thin data (real user: 3 usable pairs)]** → Require ≥N validated pairs and an IQR-spread gate before publishing any number; otherwise directional-only. Thresholds tuned against the real DB in tasks.
- **[Cap value C is a heuristic]** → Choose conservatively (≈1.5 UGS), encode as a single named constant, cover with a regression test; it only ever suppresses numbers, never invents them.
- **[BREAKING payload shape for `extrapolated` entries]** → Internal AI-facing payload, no third-party API contract; update both shared consumers together; assert byte-equivalence in tests.
- **[Phase 2 mechanism may not generalize across grinders]** → That is exactly why Phase 2 numeric long-hop output is validation-gated and disabled by default until confirmed.
- **[`roastDate` stored in mixed formats (`MM.DD.YYYY` and `YYYY-M-D` both seen) → batch false-splits]** (review S5a) → raw-string equality can split one real batch into two if the user typed the date differently, yielding fewer within-coffee pairs. This fails *safe* — fewer pairs → more conservative directional-only, never a wrong number — so it is accepted over fragile date-parsing. Documented so it is a known false-negative, not a surprise; revisit only if it suppresses the feature for users who do have same-batch cross-profile data.
- **[Directional correctness depends entirely on KB UGS values being right]** (review S5c) → D5a makes anchor-free direction the primary Phase-1 output, with no numeric sanity net to catch a wrong KB UGS (KB UGS placement has had bugs — see #1198/#1205). A wrong KB UGS now yields a confidently-wrong *direction*. Mitigation: the KB-prose task (3.4) includes a UGS-ordering spot-check against the cross-profile reference, and the regression corpus asserts known orderings (e.g. TurboTurbo coarser than D-Flow). Residual risk accepted: a wrong KB UGS is a KB-data bug, out of scope for this change's algorithm, but the dependency is now explicit.

## Migration Plan

1. Phase 1 lands behind no flag (pure harm reduction) — old numeric extrapolations replaced by directional markers. Verify both surfaces byte-equivalent; regression test the #1223 scenario.
2. Request the #1223 reporter's scrubbed `shots.db`; add it and the local DB as `shot_eval` calibration fixtures.
3. Phase 2 lands with the deliberate-calibration capture + storage, but the consuming gate (`useDeliberateCalibrationForLongHop`) defaults off until the fixture set confirms the mechanism. Rollback for Phase 2 = flip the gate off; Phase 1 needs no rollback path (no behavior is worse than today).

## Open Questions

- ~~Final numeric thresholds~~ **RESOLVED (task 1.2; revised per D1a, and per independent-review S3)** via `tools/calib_analysis.py` against the real 928-shot DB. Chosen Phase 1 constants: `minPairSpan = 0.75` UGS, `minEndpointSamples = 2` (raised from 1 — a single-shot "median" is one noisy point; the safe directional outcome at n=1 on one DB is luck not robustness, and a different DB could coincidentally publish off two single shots), `minValidatedPairs = 3`, `maxSpreadRatio = 0.6` (dimensionless: `IQR ≤ 0.6·|conversionKey|`), `cap = 1.5` UGS. Provenance: `cap` is literature-backed (Al-Shemmeri/Gagne, the UGS ADR Appendix A) and grinder-independent — it alone defeats #1223 (TurboTurbo UGS 6 is 4.5 past the validated range regardless of the other gates). `minPairSpan`/`cap` are on the universal UGS axis. `maxSpreadRatio` is dimensionless and therefore grinder-portable. `minEndpointSamples`/`minValidatedPairs` are counts. None hardcodes a slope — `conversionKey` is always a per-grinder runtime output. Justification on the real DB (with **batch-aware keying D1b**, `minEndpointSamples=2`): within-coffee path yields 2 same-batch pairs (slopes 1.5, 1.67; conversionKey 1.58; ratio 0.11) — tight and correct-signed, but only 2 pairs < `minValidatedPairs=3` → **correctly DIRECTIONAL**. The bean-only counterfactual that justifies D1b is reproducible with the checked-in tool via `--bean-only`: it yields conversionKey ≈ 1.38–1.5 and **PUBLISHES** at both n=1 (ratio 0.30) *and* the stricter n=2 (3 pairs, ratio 0.28) — i.e. ignoring roast batch fabricates a confident number regardless of the sampling gate, while correct batch keying degrades to honest direction. D1b is therefore load-bearing independently of S3. The real DB Phase-1 outcome is directional-only — the intended posture (honest direction, no fabricated number). The legacy pooled path still yields `conversionKey = −2.4` (the #1223 failure) → eliminated by the cap regardless. Net: the real DB Phase-1 outcome is directional-only, which is the safe and correct result. Starting values from one dataset (n=1 user); a second independent DB (Phase 2 / #1223 reporter) may refine the data-tuned ones, but they must never be loosened to where the −2.4 case publishes a number, and batch keying must never be relaxed to bean-only.
- Was the current numeric block an intentional decision overriding the UGS ADR (which preferred qualitative Tier-1), or drift? Affects only proposal framing, not the technical fix; reconstruct from `AI_ADVISOR.md` + `git log` of `dialing_blocks.cpp` during implementation.
- Phase 2 anchor profile choice: mandate Cremina/Rao-Allongé specifically, or any two profiles ≥ K UGS apart? Lean to the latter (less friction; consistent with D3).
