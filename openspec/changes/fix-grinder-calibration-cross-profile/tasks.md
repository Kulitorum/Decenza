## 1. Phase 1 — Threshold calibration (do first, decides constants)

- [x] 1.1 Promote the offline prototype (`/tmp/calib_proto.py`, `calib_paired.py`, `calib_line.py`) into a checked-in analysis script under `tests/data/` or `tools/`, runnable read-only against a `shots.db` — `tools/calib_analysis.py`
- [x] 1.2 Tune and record the named constants against the real local DB: `minPairSpan` (ΔUGS), `minEndpointSamples`, `minValidatedPairs`, `maxSpreadRatio` (dimensionless `IQR/|conversionKey|`, per D1a), and the extrapolation `cap` (≈1.5 UGS). Document chosen values + the data that justified them in `design.md` Open Questions resolution
- [x] 1.3 Confirm on the real DB that the tuned constants turn the #1223 case (`conversionKey = −2.4`, TurboTurbo numeric) into a directional, no-number result

## 2. Phase 1 — buildGrinderCalibrationBlock rewrite

- [x] 2.1 Add the dialed-in qualification filter (final_weight ≥ 15, no badge, AND enjoyment ≥ 50 OR within 10% of targetWeightG OR has refractometer) replacing the `final_weight ≥ 5`/no-badge-only filter
- [x] 2.2 Implement `(coffee, profile)` grouping (coffee = beanBrand + beanType) and within-coffee per-profile median settings
- [x] 2.3 Implement within-coffee pairwise slope collection with `minPairSpan` / `minEndpointSamples` (=2) exclusion and Theil–Sen (median) conversion key; undated batches use single-linkage 90-day clustering (sliding window, not fixed bucket — review S5b)
- [x] 2.4 Implement the publish gate: directional-only unless `≥ minValidatedPairs` surviving pairs AND dimensionless spread `IQR(slopes) ≤ maxSpreadRatio · |conversionKey|` (per D1a — grinder-portable, NOT an absolute threshold); never emit a `conversionKey` whose sign contradicts the grinder finer-direction
- [x] 2.5 Implement the per-coffee anchor: most recent dialed-in shot on the current coffee on a known-UGS profile; `rgs = anchorSetting + (UGS_target − UGS_anchor)·conversionKey`; directional-only when no current-coffee anchor
- [x] 2.6 Implement the mandatory extrapolation cap: numeric `rgs` only within `[loUGS − cap, hiUGS + cap]`; outside → `source: "directional"` + `direction`, no number
- [x] 2.7 Rework the block shape: add `confidence`, `usageConstraint`, optional `coffeeAnchor`, optional `conversionKey`/`calibratedUgsRange`; replace `source: "extrapolated"` numeric entries with `source: "directional"` (no `rgs`); keep ordering by UGS ascending
- [x] 2.8 Remove now-dead anchor-pair selection / pooled-median code paths

## 3. Phase 1 — Surfaces & prompt constraints

- [x] 3.1 Update `dialing_get_grinder_calibration` (MCP) to return the directional/`available:false` structured response with a `reason` (give direction + pull a reference shot), never a numeric table when not validated
- [x] 3.2 Update `aimanager.cpp` calibration rendering to emit the usage-constraint directives and repeat the block `usageConstraint` verbatim; render directional profiles as finer/coarser + "pull a reference shot", no numbers
- [x] 3.3 Verify byte-equivalence of the calibration section across ALL THREE surfaces for identical input: in-app advisor, `dialing_get_context` (MCP), and `ai_advisor_invoke` (`mcptools_ai.cpp` — routes via the shared `enrichUserPromptObject`, confirm no divergence) — review S4
- [x] 3.4 Tighten the KB `cross-profile-grind-ordering` prose so its UGS-is-directional wording matches the new usage constraints (data-only touch-up; run the KB validator). Also spot-check KB UGS ordering for the profiles in the regression corpus (review S5c — directional correctness now depends on KB UGS being right, with no numeric sanity net)

## 4. Phase 1 — Tests

- [x] 4.1 Unit/`shot_eval` regression: #1223 scenario — low-UGS anchor, far-UGS request → directional, no number, no negative/out-of-range value anywhere
- [x] 4.2 Unit: wrong-signed pooled slope can no longer be produced (within-coffee path + sign gate)
- [x] 4.3 Unit: near-profile within validated window → `source: "derived"` with anchored `rgs`; no-current-coffee-anchor → directional
- [x] 4.4 Unit: extrapolation cap boundary (just inside numeric, just outside directional)
- [x] 4.5 Unit: byte-equivalence assertion across both surfaces
- [x] 4.6 Real-data validation done via **live decenza MCP** on the real 928-shot DB (shot 961: legacy `-2.4` → `confidence:directional`, TurboTurbo `coarser`/no number). Binary scrubbed-DB fixture deferred — privacy cost of committing real shot history; superseded by the #1223 reporter's DB as the Phase-2 second fixture (task 6.6)
- [x] 4.7 Run the Decenza test suite via Qt Creator MCP (build + run_tests), fix to green, no WARN lines

## 5. Phase 1 — Release

- [ ] 5.1 Open a PR (not a push to main); run `/pr-review-toolkit:review-pr`; address findings
- [ ] 5.2 Comment on issue #1223 summarizing the Phase 1 fix and requesting the reporter's scrubbed `shots.db` as a second regression fixture / Phase 2 validation dataset

## 6. Phase 2 — Deliberate UGS calibration (validation-gated)

- [ ] 6.1 Add a `SettingsGrinder` domain sub-object (settings-architecture rules: not on `Settings` directly; `qmlRegisterUncreatableType`; `Settings.grinder.*` QML access) storing per-`(grinderModel, grinderBurrs)` calibration records
- [ ] 6.2 Implement the two-anchor capture (record fine + coarse anchor shots, reject anchors closer than the minimum UGS span, compute + persist Conversion Key with both anchors + timestamp)
- [ ] 6.3 Make `buildGrinderCalibrationBlock` prefer a stored Conversion Key when present → `confidence: "calibrated"`, validated range = deliberate anchor span; per-coffee anchor + cap still apply
- [ ] 6.4 Add the default-off long-hop validation gate; with gate off, calibrated confidence holds only within the Phase 1 window and falls back to directional beyond it
- [ ] 6.5 Tests: stored-key precedence, anchors-too-close rejection, no-calibration leaves Phase 1 untouched, gate-off keeps long-hop directional, gate-on (after fixture validation) permits bounded long-hop numbers
- [ ] 6.6 When the #1223 reporter's DB arrives: add as an independent `shot_eval` fixture, validate the within-coffee + deliberate mechanism on it, and only then consider enabling the long-hop gate
