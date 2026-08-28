## 1. Shot-start latch

- [x] 1.1 Add `m_latchedFlowCalibration` (double, default 0.0) and a `latchedFlowCalibration()` accessor to `ProfileManager` (`src/controllers/profilemanager.h`, beside `latchedTargetG()` / `m_latchedTargetG`).
- [x] 1.2 In `ProfileManager::latchForShot()` (`src/controllers/profilemanager.cpp:509`), snapshot `m_settings->calibration()->effectiveFlowCalibration(m_baseProfileName)` into it, guarded by the existing `if (m_settings)` block that already latches yield mode/anchor. Leave 0.0 when there is no settings object — the "unrecorded" sentinel.
- [x] 1.3 Comment at the latch stating WHY it is latched rather than read at save: `computeAutoFlowCalibration()` runs at `maincontroller.cpp:4078`, before the metadata build and save, and can write a new per-profile multiplier first. Cite both line numbers.
- [x] 1.4 Do not add a new log line for the latched value. `profilemanager.cpp` is in neither `COVERED_GLOBS` nor `MARKER_ONLY_GLOBS` in `scripts/check_log_markers.py`, so a bare `qDebug` would pass the gate — but the value is already in the log via auto-cal's `currentFactor=` line, and the column is the record this change is adding. If a line is added anyway, extend the existing `[Yield] latched …` call rather than opening a second one.

## 2. Carry to the save path

- [x] 2.1 Add `double flowCalibration = 0;` to `ShotMetadata` (`src/network/visualizeruploader.h`), in the local-history-only group beside `yieldMode`/`yieldAnchorValue`, with a comment marking it local-history-only (not part of the Visualizer payload — DYE has no such field).
- [x] 2.2 Add `double flowCalibration = 0;` to `ShotSaveData` (`src/history/shothistory_types.h`), beside `yieldAnchorValue`.
- [x] 2.3 In `MainController::onShotEnded()` (`src/controllers/maincontroller.cpp`, the block at ~`:4096` that sets `metadata.yieldMode` / `metadata.yieldAnchorValue`), set `metadata.flowCalibration = m_profileManager->latchedFlowCalibration();`.
- [x] 2.4 Leave the second `saveShot()` call site (`maincontroller.cpp:4465`) alone: it is the dev fake-shot gesture, whose curves are invented and which deliberately avoids writing made-up values into real user data (see its own comment refusing `setDyeDrinkWeight()`). It stores no multiplier, which reads as unrecorded — the correct answer for a shot the machine never pulled. Add a one-line comment there saying so, so a later edit doesn't "fix" the omission.
- [x] 2.5 In `ShotHistoryStorage::saveShot()` (`src/history/shothistorystorage.cpp`, the `ShotSaveData` population at ~`:2332`), copy `data.flowCalibration = metadata.flowCalibration;`.

## 3. Schema and persistence

- [x] 3.1 Migration 39 in `runMigrations()` (`src/history/shothistorystorage.cpp`, after the migration-38 block ending ~`:2100`), gated `currentVersion >= 38 && currentVersion < 39`: `ALTER TABLE shots ADD COLUMN flow_calibration REAL` under a `hasColumn("shots", "flow_calibration")` guard.
- [x] 3.2 Stamp the version inside one transaction (`DELETE FROM schema_version` + `INSERT … VALUES (39)`, both checked), gated on the column being present afterwards — it is a schema fact. Log completion in the style of the adjacent migrations; `shothistorystorage.cpp` is in `MARKER_ONLY_GLOBS`, so keep the existing `ShotHistoryStorage: …` prefix and no bracketed marker.
- [x] 3.3 Add `flow_calibration` to the save INSERT (`:2492`), binding `data.flowCalibration > 0 ? QVariant(data.flowCalibration) : QVariant()` so an unrecorded value stores NULL, matching the `yield_anchor_value` binding at `:2558`.
- [x] 3.4 Append `s.flow_calibration` to the END of the `loadShotRecordStatic()` SELECT (`:3294`) and read it as index 56 (`:3399` reads 55 today). Comment that it is appended so existing positional indices are unchanged, as the taste-axis and yield-anchor columns did.
- [x] 3.5 Add `double flowCalibration = 0;` to `ShotRecord` (`src/history/shothistory_types.h`), documenting that 0 means "not recorded" and that 1.0 is a real multiplier, so the two must never collapse.
- [x] 3.6 Carry the column in the device-to-device transfer copy (`:4451` index lookups + `:4472` INSERT): `srcRecord.indexOf("flow_calibration")` with the existing `srcValueOrNull()` helper, so a pre-39 source yields NULL.
- [x] 3.7 Carry the column in the shot importer INSERT (`:5069`), binding NULL when the record has no value.

## 4. Projection surfaces

- [x] 4.1 Add `flowCalibration` to `ShotProjection` (`src/history/shotprojection.h`) as a `Q_PROPERTY(double … MEMBER flowCalibration)`, beside `yieldAnchorValue`.
- [x] 4.2 Map it in `convertShotRecord()` (`src/history/shothistorystorage_serialize.cpp:84` area).
- [x] 4.3 Sparse-emit in `ShotProjection::toVariantMap()` (`src/history/shotprojection.cpp`): emit `flowCalibration` only when `> 0`, with a comment that emitting 0 or 1.0 for an unrecorded shot would hand a consumer (notably an AI payload) a fabricated measurement.
- [x] 4.4 Read it back in `ShotProjection::coerce()` / `fromVariantMap` (`shotprojection.cpp:233` area) so the round-trip is symmetric.
- [x] 4.5 Confirm no MCP tool schema or description needs changing — the field rides in existing shot payloads, so `McpSurfaceVersion` (`src/mcp/mcpserver.h`) does NOT move. State the finding either way; if any tool declares its shot fields explicitly, bump per the budget-check rule in `CLAUDE.md`.

## 5. Tests

New test SLOTS in existing files only — no new `tst_*.cpp` (a new test file costs ~1.4 s of build forever; these all fit existing harnesses):

- [x] 5.1 `tests/tst_dbmigration.cpp` — migration 39 adds `shots.flow_calibration` and stamps the version; a database already at 39 is untouched. Follow the `ALTER TABLE … DROP COLUMN` + re-run shape used for `yield_anchor_value` at `:786`.
- [x] 5.2 `tests/tst_dbmigration.cpp` — a pre-39 row reads back unrecorded (NULL), not 1.0, after migrating.
- [x] 5.3 `tests/tst_shotprojection.cpp` — a projection with `flowCalibration = 1.35` emits it; one with 0 omits the key entirely (mirrors the existing `yieldAnchorValue` sparse-emit case at `:253`).
- [ ] 5.4 (NOT done — see 5.5) Save/load round trip: a `ShotSaveData` with `flowCalibration = 1.35` reads back 1.35 on `ShotRecord`; one with 0 reads back 0 and stores SQL NULL. Place it wherever the save/load round trip is already exercised; if no such harness exists, add the slot to the storage test that comes closest rather than creating a file.
- [x] 5.5 Ordering regression added as `shotLatchFreezesFlowCalibrationAgainstLateWrites()` in `tests/tst_profilemanager.cpp`, on the existing `McpTestFixture`. The end-to-end ordering could NOT be driven — no harness constructs `MainController` — so it asserts the property the fix rests on: latch, write a new multiplier, latched value unchanged; next `latchForShot()` re-resolves.

## 6. Documentation

- [x] 6.1 `docs/CLAUDE_MD/AUTO_FLOW_CALIBRATION.md` — short subsection: the multiplier is recorded per shot in `shots.flow_calibration`, latched at shot start, NULL means unrecorded and never 1.0.
- [x] 6.2 No wiki manual entry — nothing user-visible ships here (per `CLAUDE.md`, the manual covers user-visible behaviour; this is a diagnostic record).

## 7. Verification

- [x] 7.1 Build via `mcp__qtcreator__build` — succeeded.
- [x] 7.2 Full suite — 116 passed, 0 failed, 0 warnings.
- [x] 7.3 Confirmed `scripts/check_log_markers.py` and `scripts/check_test_source_duplication.py` pass locally, since `text-invariants.yml` gates `src/**` per-PR and nothing blocks a merge on a red run.
