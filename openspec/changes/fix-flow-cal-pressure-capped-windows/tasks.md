## 1. Formula selection fix

- [x] 1.1 In `MainController::computeAutoFlowCalibration()` (`src/controllers/maincontroller.cpp`), add a `kFlowTargetDeviationThreshold = 0.10` constant next to the existing algorithm thresholds (~line 3070-3100).
- [x] 1.2 For windows classified `isFlowProfile == true`, compute the relative deviation of `meanMachineFlow` from `profileTargetFlow` and compare against the new threshold. Implemented as a standalone, unit-testable predicate `autoFlowCalWindowMissedTarget()` in `autoflowcalclassifier.{h,cpp}` (same module that already owns window classification), rather than inlining the math in `maincontroller.cpp` — keeps it testable without a full `MainController` instance, which no existing test harness constructs for this function.
- [x] 1.3 When deviation is within threshold: keep existing behavior (target-flow formula, ratio guard compares weight flow against target flow). `isFlowProfile` is left unchanged, so the existing code path is taken byte-for-byte.
- [x] 1.4 When deviation exceeds threshold: reclassify the window (`isFlowProfile = false`) so it falls through to the existing pressure-branch formula and ratio guard unmodified — reuses the `else` branch already in the function rather than duplicating it.
- [x] 1.5 Add a `qDebug()` line at the re-routing decision point naming measured flow, target flow, the deviation, and which formula was selected, placed immediately before the existing "Window-level ratio sanity check" block (after window-mode classification, before the ratio guard so the reclassification is visible in the log ahead of it).
- [x] 1.6 The target-achieved path is provably unchanged by construction: `autoFlowCalWindowMissedTarget()` returning false leaves `isFlowProfile` untouched, so every line after it (ratio guard, ideal formula, logging) executes exactly as before. Covered by test 4.1 below rather than a separate manual diff-check.

## 2. Settings migration

- [x] 2.1 Add a new one-time migration inline in the `Settings` constructor (`src/core/settings.cpp` — migrations run inline there, not via a separate `runMigrations()`), following the `calibration/v3FlowProfileReset` pattern immediately above it, gated on a new key `calibration/v4AchievedFlowFormulaReset`.
- [x] 2.2 Migration clears `calibration/flowCalBatch` to `"{}"` only — do **not** call `resetAllProfileFlowCalibrations()` or reset `flowCalibrationMultiplier`. Added `SettingsCalibration::clearAllFlowCalPendingIdeals()` (parallels the existing `resetAllProfileFlowCalibrations()`) so the migration goes through the calibration store's own API rather than poking the settings key directly.
- [x] 2.3 Log the migration action (`qDebug()`) matching the style of the adjacent v2/v3 migration log lines.

## 3. Documentation

- [x] 3.1 Update `docs/CLAUDE_MD/AUTO_FLOW_CALIBRATION.md` "Window-level Classification" section to document the achieved-flow deviation check and when a flow-classified window is re-routed to the pressure formula.
- [x] 3.2 Add a short "v4 Migration (Pressure-Capped Flow Window Fix)" subsection alongside the existing v2/v3 migration write-ups, describing the pending-batch-only clear and why it differs from the earlier full resets.
- [x] 3.3 Checked the wiki manual (`Kulitorum/Decenza.wiki.git`, `Manual.md` "Auto Flow Calibration" section, ~line 720) — it does have a page, contrary to this task's assumption, but it only describes the user-facing contract (per-profile, automatic, 5-shot batches, Settings → Calibration location), none of which changes. This fix is an internal correctness improvement to the ideal computation, not a new setting, workflow, or user-visible behavior — no manual update needed.

## 4. Tests

Note on scope: `computeAutoFlowCalibration()` itself has no test harness (no friend-class
access, no existing test constructs a `MainController` to exercise it — confirmed by grep before
writing these). Rather than build that infrastructure, the deviation logic was factored into a
standalone predicate (`autoFlowCalWindowMissedTarget()`, task 1.2) that's directly unit-testable,
matching the existing pattern of `classifyAutoFlowCalWindow()` in the same file. Tests below target
that predicate and the migration's primitive, not a full `MainController` integration path.

- [x] 4.1 In `tests/tst_autoflowcal.cpp`, add `flowWindowAchievesTarget_notReclassified` — measured flow within 10% of target returns `false` (no reclassification; regression guard for 1.6's "unchanged common case" claim).
- [x] 4.2 Add `flowWindowMissesTarget_reclassified` — the exact #1823 shot538 numbers (measured 1.0688 vs target 1.7, 37% deviation) return `true`.
- [x] 4.3 Add `flowTargetDeviationThreshold_boundaryIsExclusive` (exactly 10% in each direction is not "missed"; just past is) and `flowTargetDeviation_nonPositiveTargetNeverMisses` (guards the divide-by-target).
- [x] 4.4 Not a separate test: reclassification's only effect is `isFlowProfile = false`, which routes the window through the pre-existing pressure-branch formula and ratio guard — untouched, already-relied-upon code with no test gap introduced by this change. Covered structurally by 4.1-4.3 confirming when the flag flips.
- [x] 4.5 Add `issue1823Batch_splitsTargetMetFromTargetMissed` — reproduces all 8 real windows from the issue's debug log (target 1.7 ml/s) and asserts the predicate splits the 3 target-met (99-100%) from the 5 target-missed (57-67%) windows exactly as observed — pins the threshold choice to the real evidence it came from.
- [x] 4.6 Add `clearAllFlowCalPendingIdeals_clearsBatchesButNotMultipliers` — verifies `SettingsCalibration::clearAllFlowCalPendingIdeals()` (the migration's primitive) clears pending batches for multiple profiles while leaving per-profile and global multipliers untouched. The migration's own gating (fires once per process, on first `Settings` construction) has no test precedent for v2/v3 either, so a full constructor-level migration test isn't a new coverage gap this change introduces.

## 5. Build and verification

- [x] 5.1 Build via `mcp__qtcreator__build` (target `all`) — 0 errors, 0 warnings.
- [x] 5.2 Run full suite via `mcp__qtcreator__run_tests` (scope `all`) — 113/113 passed, 0 failed, 0 skipped (34.7s). Caught one real bug first: the initial `flowTargetDeviationThreshold_boundaryIsExclusive` test asserted behavior at an exact 10% tie (`2.20` vs `2.00`), which isn't exactly representable in `double` and failed — fixed by testing clearly-inside/outside values instead of the unrepresentable tie (test bug, not a production defect).
- [x] 5.3 Ran `scripts/check_log_markers.py` directly — clean: "77 covered file(s) log through marked helpers... markers registered: Bluetooth, DE1, Equipment, Font, MCP, Network, Refractometer, SAW, Scale, Screensaver, Storage, Theme". The new log line matches the existing unmarked "Auto flow cal: ..." convention in `maincontroller.cpp` (which is in `MARKER_ONLY_GLOBS`, not `COVERED_GLOBS` — no registered marker to apply here), so it was never at risk of tripping rules 2/5.
- [ ] 5.4 Manual verification on a connected DE1 with a pressure-capped flow profile is a physical-machine action for Jeff to run when convenient — not done here. The #1823 regression test (4.5) numerically confirms the fix against real captured telemetry in the meantime.

## 6. Archive

- [ ] 6.1 Once merged, run `openspec archive fix-flow-cal-pressure-capped-windows` as the last commit on the feature branch, per the project's OpenSpec workflow.
