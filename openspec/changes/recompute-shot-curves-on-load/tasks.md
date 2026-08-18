## 1. Share the formulas

- [x] 1.1 Add `Conductance::resistance(pressureBar, flowMlS)` to `src/ai/conductance.h`, matching the live clamp/threshold in `shotdatamodel.cpp:218-221` (`flow > 0.05`, clamp `15.0`).
- [x] 1.2 Add a darcy-resistance equivalent (e.g. `Conductance::darcyResistanceSample(pressureBar, flowMlS)`) to `src/ai/conductance.h`, matching `shotdatamodel.cpp:230-233` (`flow > 0.05 && pressure > 0.05`, clamp `19.0`).
- [x] 1.3 Update `shotdatamodel.cpp`'s live per-sample path (`ShotDataModel::addSample`) to call both new helpers instead of inlining the formulas.
- [x] 1.4 Update `shothistorystorage.cpp`'s `computeDerivedCurves()` darcy loop to call the new helper instead of its own inline copy.

## 2. Recompute resistance and drop the legacy-only gate

- [x] 2.1 Add resistance recomputation to `computeDerivedCurves()` in `shothistorystorage.cpp`, using `Conductance::resistance()` over `record.pressure`/`record.flow`.
- [x] 2.2 Drop the `needsDerivedCurves` gate (`record.conductance.isEmpty()`) at the `loadShotRecordStatic` call site. Implemented per design.md's actual decision: `computeDerivedCurves()` is now called unconditionally from inside `decompressSampleData()` (which needs the parsed `QJsonObject` in scope for task 3.3 anyway) rather than from `loadShotRecordStatic` after it returns — same net effect (unconditional recompute on every load), narrower diff.

## 3. Persist corrections back to storage

- [x] 3.1 Snapshot the four curves (`resistance`, `conductance`, `darcyResistance`, `conductanceDerivative`) as parsed from the blob, before recompute overwrites `record`'s copies — same shape as the existing `storedChanneling`/`storedGrindIssue`/etc. snapshot at `shothistorystorage.cpp:3357-3360`.
- [x] 3.2 After recompute, compare each curve to its snapshot with a tolerance (`1e-6`, plus a length check) rather than exact equality.
- [x] 3.3 When any curve differs, patch just those four keys in the blob's already-parsed `QJsonObject` (not a full rebuild from `ShotRecord` — it doesn't carry every blob field, e.g. `weightFlow`) and recompress.
- [x] 3.4 In `loadShotRecordStatic`, when a corrected blob was produced, run `UPDATE shot_samples SET data_blob = ? WHERE shot_id = ?` on the existing `db` connection — same shape as the badge-persist block at `shothistorystorage.cpp:3472-3489` (log a `qWarning` on failure, don't fail the load).

## 4. Tests

- [x] 4.1 In `tests/tst_sampleblobseries.cpp`: a blob whose stored `resistance` doesn't match `pressure/flow` decodes with the corrected value in the returned `ShotRecord`.
- [x] 4.2 In `tests/tst_sampleblobseries.cpp`: a blob that's already correct produces no corrected-blob output (asserts the no-write case, guarding against floating-point-noise false positives).
- [x] 4.3 DB round trip for `loadShotRecordStatic`'s persist-on-load, added to `tests/tst_sampleblobseries.cpp` (not `tst_shotrecord_cache.cpp`, which turned out to have no DB round trip at all — this file already owns the blob/derived-curve theme and already has the `ShotHistoryStorage`/`ShotDataModel` plumbing): loading a shot with a stale stored `resistance` updates `shot_samples.data_blob`; loading it again afterward performs no further write.
- [x] 4.4 Confirm existing `tests/tst_sampleblobseries.cpp` cases (mix-goal round trip, legacy-absent-key handling) still pass unmodified — this change must not touch unrelated series.

## 5. Verify

- [x] 5.1 Build and run the full suite via the Qt Creator MCP (`mcp__qtcreator__run_tests`, scope `all`) — see CLAUDE.md Building. 113/113 passed, 0 failures, 0 warnings.
- [ ] 5.2 Sanity-check against real data: pull a shot recorded in the known-bad window (2026-01-29–2026-02-21) via the `de1` MCP tools, confirm the corrected `resistance` curve is no longer identical to `darcyResistance`. Blocked on Jeff: the `de1` MCP tools talk to whatever Decenza instance is actually running, which is still the pre-fix build — per project convention the assistant never launches the app. Needs Jeff to build and run this branch, then this check can run against the live instance.
