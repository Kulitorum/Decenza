## Why

A shot record does not say what flow calibration multiplier the shot was pulled at. The value
exists — `SettingsCalibration::effectiveFlowCalibration(profileFilename)`
(`src/core/settings_calibration.cpp:213`) resolves per-profile over global, and
`ProfileManager::applyFlowCalibration()` writes it to the DE1 — but nothing about it is stored
with the shot. Every curve in `shots` is therefore uninterpretable on its own: reported flow
(`shots.flow`) is a calibrated quantity, and without the multiplier there is no way to recover the
raw sensor reading or to compare two shots pulled at different multipliers.

The cost is concrete and recurring. Diagnosing
[Kulitorum/Decenza#1872](https://github.com/Kulitorum/Decenza/issues/1872) (auto flow calibration
walking a user's hand-set 1.35 down toward ~1.17 on D-Flow) required pairing the user's shot data
with their system debug log, because only the log's `Auto flow cal: … currentFactor=` line carries
the multiplier. A shot file alone was not enough, and for any shot where auto-cal skipped — no
physical scale, no qualifying steady window, a stream-force rejection — the multiplier appears in
no record at all. The same gap blocks retrospective analysis of the algorithm itself: the open
question in #1872 is whether delivered flow responds to the multiplier, and answering it from
history means knowing each shot's multiplier, which today's database cannot supply.

## What Changes

- **NEW** `shots.flow_calibration` (REAL, nullable) — the effective flow calibration multiplier in
  force while the shot was pulled. NULL on every shot recorded before this change and on imported
  shots whose source has no such column; NULL means "not recorded", never 1.0.
- Schema migration 39 adds the column. Additive and idempotent, no backfill: the multiplier a past
  shot ran at is not recoverable from any stored field, and inventing 1.0 would be a fabricated
  measurement in a column whose entire purpose is measurement provenance.
- The value is **latched at shot start**, not read at save time. `ProfileManager::latchForShot()`
  snapshots it alongside the dose/target/anchor it already latches, because
  `MainController::computeAutoFlowCalibration()` runs at shot end *before* the save
  (`maincontroller.cpp:4079` vs `:4267`) and can write a new per-profile multiplier first — a
  save-time read would record a value the shot was never pulled at.
- Plumbed through the existing shot-context channel: `ShotMetadata` → `ShotSaveData` → the INSERT,
  and back out through `loadShotRecordStatic()` → `ShotRecord` → `ShotProjection`, so MCP tools,
  AI analysis payloads and shot export carry it. Sparse-emitted in the projection map (omitted when
  not recorded) per the existing convention for optional provenance fields.
- Carried verbatim by the device-to-device transfer copy and the shot importer, like the recipe and
  yield-anchor provenance columns before it.
- No UI, no Visualizer payload change, no new setting, no change to how the multiplier is computed
  or applied. This change only records what was already in force.

## Capabilities

### Modified Capabilities
- `auto-flow-calibration`: adds the requirement that the multiplier a shot ran under is recorded on
  that shot, and defines the NULL-means-unrecorded contract. The computation, windowing, formula
  selection and batching behaviour in that capability are untouched.

## Impact

- `src/history/shothistorystorage.cpp` — migration 39, save INSERT, load SELECT (append at index
  56), transfer-copy INSERT, importer INSERT
- `src/history/shothistory_types.h` — `ShotSaveData::flowCalibration`, `ShotRecord::flowCalibration`
- `src/history/shothistorystorage_serialize.cpp`, `src/history/shotprojection.{h,cpp}` — projection
  field and sparse map emit
- `src/network/visualizeruploader.h` — `ShotMetadata::flowCalibration` (local history only; not part
  of the upload payload, like `storageHint` / `recipeId` / `yieldMode`)
- `src/controllers/profilemanager.{h,cpp}` — latch and accessor
- `src/controllers/maincontroller.cpp` — read the latched value into `ShotMetadata`
- `tests/tst_shothistory*.cpp` — round-trip and migration coverage
- `docs/CLAUDE_MD/AUTO_FLOW_CALIBRATION.md` — document the recorded field
- No wiki manual change: nothing user-visible ships here.
