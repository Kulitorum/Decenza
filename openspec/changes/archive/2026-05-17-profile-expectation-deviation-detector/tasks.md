## 1. Prerequisite & wiring

- [ ] 1.1 Confirm `capture-dialin-coaching-guidance` has landed: `ProfileExpectation` exposes the two-sided `GrindTooCoarse:`/`GrindTooFine:` arms, `PressureShape:`, preinfusion-dripping bounds, and the structured per-profile `Suppress:` set. If not, this change is blocked — do not stub the data here.
- [ ] 1.2 Trace how the save / load / detail callers (`ShotHistoryStorage::saveShot`, `loadShotRecordStatic`, `convertShotRecord`) resolve the shot's profile, and where the resolved `ProfileExpectation` for that profile can be obtained without main-thread KB access on a background path.
- [ ] 1.3 Add an optional `ProfileExpectation` parameter to `ShotAnalysis::analyzeShot` (default = absent), mirroring the `expectedFrameCount` precedent; thread it from the save/load/detail callers. Confirm every existing caller compiles unchanged with the defaulted parameter.

## 2. Detector implementation

- [ ] 2.1 Implement the profile-deviation detector function inside the single `ShotAnalysis::analyzeShot` cascade (no second pass, no separate orchestrator). Input: observed pressure/flow/time/preinfusion-dripping + the optional `ProfileExpectation`.
- [ ] 2.2 Match logic: compare observed values to the centred envelope, the `GrindTooCoarse:`/`GrindTooFine:` signatures, and the `PressureShape:` qualifier, with a single configurable firing margin constant (exposed for the §4 shadow tuning; consumers must not inline it).
- [ ] 2.3 Hard AND-gate: fire only if a `ProfileExpectation` exists AND match-with-margin AND NOT (cascade already fired pour-truncated/channeling) AND NOT covered by the profile `Suppress:` set AND NOT grind-tolerant-by-design AND NOT bean-freshness unknown/very-fresh. Any ambiguity → silent.
- [ ] 2.4 On fire, contribute exactly one `summaryLines` entry: low-authority type (`[observation]` default; `[caution]` only if §3 review decides), taste-deferring wording, distinct phrasing for the arm-match case vs the clean-but-off-`PressureShape:` case. Do not touch any badge column or `deriveBadgesFromAnalysis`.
- [ ] 2.5 Absent-expectation path: strict no-op — assert the analysis result is byte-identical to pre-change for the same shot.

## 3. Wording & severity review

- [ ] 3.1 Finalize the `summaryLines` `type` against `ShotAnalysisDialog.qml` styling (lean `[observation]`, lowest authority); confirm it does not visually read as a badge/warning.
- [ ] 3.2 Internationalize the new line text via the project translation mechanism; confirm it never asserts a verdict and always defers to taste; confirm it never renders alongside a contradicting fired mechanical detector.

## 4. Shadow validation & tuning

- [ ] 4.1 Add a `shot_eval`-harness dry-run mode that computes the line over the `tests/data/shots/` regression corpus without rendering, reporting the per-shot firing decision.
- [ ] 4.2 Run it over the known-good corpus; record the false-positive count and tune the §2.2 firing margin to a conservative recorded target. Capture the chosen margin + resulting rate in the change log.
- [ ] 4.3 Add a permanent regression assertion that fails if the corpus firing rate exceeds the recorded conservative threshold.

## 5. Tests

- [ ] 5.1 `tst_shotanalysis`: arm-match fires; each gate independently suppresses (suppress-catalogue, cascade-fired pour-truncated/channeling, grind-tolerant, bean-freshness unknown/very-fresh, sub-margin); ambiguous → silent.
- [ ] 5.2 Absent/partial `ProfileExpectation` → strict no-op; result byte-identical to pre-change baseline.
- [ ] 5.3 Save / load / detail recompute parity: same shot + same resolved expectation yields the same line in the same position on all three paths; `analyzeShot` still invoked exactly once on the canonical detail-load path.
- [ ] 5.4 Invariant: no new column / record field / Visualizer payload field; the only trace is the in-memory/recomputed `summaryLines` entry.

## 6. Verification

- [ ] 6.1 Build via Qt Creator MCP (0 errors, 0 warnings); full Qt Test suite green plus the new assertions and the §4.3 corpus regression guard.
- [ ] 6.2 Spot-check the in-app Shot Summary (no AI / advisor disabled) on: a real off-guideline shot (line appears, taste-deferring), an intentional-behavior shot whose profile suppresses it (no line), a legacy/imported shot with no resolvable expectation (no line, unchanged behavior).
- [ ] 6.3 Confirm the AI advisor incidentally sees the same `summaryLines` entry (consistency, not a separate copy) and that disabling the advisor does not change the line.
