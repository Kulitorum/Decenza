# Tasks

## 1. Threshold + gate split

- [x] 1.1 Change `CHOKED_YIELD_RATIO_MAX` in `src/ai/shotanalysis.h` from `0.85` to `0.70`.
- [x] 1.2 Restructure `analyzeFlowVsGoal` in `src/ai/shotanalysis.cpp` so the outer gate is `flowSamples >= 5` (the loose precondition shared by both arms). The flow-choked arm's `pressurizedDuration >= CHOKED_DURATION_MIN_SEC` check moves inside the flow-arm-specific block.
- [x] 1.3 Set `result.hasData = true` whenever ANY arm could speak (flow-arm gates passed OR yield-shortfall fired). The verified-clean path still requires the flow-arm gates passed (preserves the strong "we saw a healthy pour" semantics).
- [x] 1.4 Update the existing `result.sampleCount = flowSamples` assignment so it sets when the choked path fires (any arm), keeping current consumer-visible semantics.

## 2. Tests

- [x] 2.1 New test `grindCheck_yieldArm_firesWithoutSustainedPressurized` — synthesize a shot 745-shape: 35s pour, brief pressurized window (~6s above 4 bar), yield 0.64 ratio. Assert `chokedPuck=true`, `hasData=true`, `verifiedClean=false`, the warning line "Pour produced near-zero flow while pressure held" fires (or the appropriate yield-shortfall summary line), and `grindIssueDetected=true` projects.
- [x] 2.2 New test `grindCheck_yieldArm_doesNotFireOnBorderlineRatio` — synthesize 0.75 yield ratio. Assert it stays silent (boundary case for the 0.70 threshold). Verdict reads "Clean shot. Puck held well." (or "verified" coverage if Arm 1 ran).
- [x] 2.3 New test `grindCheck_flowArm_stillRequiresFifteenSeconds` — synthesize a shot with 0.5 mL/s mean flow but only 10s pressurized. Assert `chokedPuck=false` (flow arm gate not satisfied). Locks in that the flow-arm gate change is one-directional.
- [x] 2.4 Re-verify `analyzeShot_chokedPuck_structuredFieldsMatchProse` and `badgeProjection_*` tests still pass (ratios used are well under 0.5 in the existing fixtures).
- [x] 2.5 Re-validate `tests/data/shots/manifest.json` corpus regression. Specifically check `80s_choked_moderate.json` — its yield ratio is near the 0.7 boundary; if the expected outcome changes, update the manifest entry.

## 3. Docs

- [x] 3.1 Update `docs/SHOT_REVIEW.md` §2.2 (grind detector internals): change the moderate-yield-arm threshold from `< 0.85` to `< 0.70` in both the prose and the example. Add a sentence explaining the audit-driven rationale ("0.85 over-flagged Adaptive v2 fast-pour profiles delivering 71-76% of target by design; 0.70 is the empirical sweet spot from the 500-shot audit").
- [x] 3.2 Update `docs/SHOT_REVIEW.md` §2.2 (grind detector internals): document the gate split — flow arm requires sustained 15s, yield arm runs as soon as any pressurized samples were seen.

## 4. Validation

- [x] 4.1 `openspec validate tighten-grind-yield-shortfall-arm --strict --no-interactive` passes.
- [x] 4.2 Build (`mcp__qtcreator__build`) clean.
- [x] 4.3 `tst_shotanalysis` passes (existing + 3 new tests).
- [x] 4.4 `shot_corpus_regression` ctest target passes.
- [x] 4.5 Manual smoke: re-run the Gap A simulation against the live audit data after the build; confirm 5 newly-flagged shots match the expected list (745, 752, 753, 754, 735).
