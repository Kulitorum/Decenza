## Why

For BLE scales, `scaleType` is populated with the human-readable display name (e.g. `"Decent Scale"`) instead of a stable id, because `BLEManager::getScaleType(device)` returns `ScaleFactory::scaleTypeName()`. That one string is *both* the UI label *and* the persistence key for per-(profile, scale) SAW learning, `sensorLag()`, the global bootstrap lag, and the known-scales registry. So renaming any BLE display name silently orphans that scale's accumulated SAW learning and breaks its `sensorLag()` lookup — with no compile-time signal. The recent WiFi rename (#1260) was safe only because WiFi happens to use the id `decent-wifi`; the next rename of a BLE display name would lose user data. Normalize `scaleType` to stable type-ids everywhere so display names can change freely.

## What Changes

- BLE scale discovery stores the canonical **type-id** (`decent`, `acaia`, `bookoo`, …) in `scaleType`, not the display name. `BLEManager::getScaleType(device)` returns the id; the human label is carried separately in the `name` field (exactly as WiFi/USB already do).
- Add a single source-of-truth `ScaleType → id` mapping in `ScaleFactory` (mirroring the existing `scaleTypeName()`), plus a `normalizeScaleTypeId(QString)` helper that maps any legacy display-name *or* id string to the canonical id.
- `sensorLag()` is keyed by type-id; the display-name keys are removed once migration lands.
- **One-time migration** (guarded by a `…/migrated` flag, mirroring the existing `knownScales/migrated` pattern) rewrites persisted display-name `scaleType` values to ids in: `scale/type`, `knownScales/scales[].type`, the SAW `learningHistory[].scale` field, the `perProfileHistory` / `perProfileBatch` map keys (`profile::scaleType`) and their `scale` fields, and `globalBootstrapLag/<scaleType>`. Existing SAW learning is preserved across the rename.
- No user-facing behavior change: display names, the UI, and the discovered/known-scale lists are unchanged. This is an internal-consistency + data-safety refactor.
- **BREAKING**: none for users (migration is transparent). The internal storage format of `scaleType` changes from display-name to id for BLE scales.

## Capabilities

### New Capabilities
- `scale-type-identity`: defines `scaleType` as a stable, canonical type-id decoupled from the display name; the single-source-of-truth id mapping and normalization helper; the discovery write-path that stores ids; id-keyed `sensorLag()`; and the one-time migration that converts existing display-name-keyed storage (settings + SAW) to ids without losing data.

### Modified Capabilities
<!-- None. `stop-at-weight-learning` requirements are unchanged — its `(profile, scale)` model still isolates per scale; only the string representation of "scale" is normalized, which `scale-type-identity` owns and migrates. No spec-level behavior of SAW changes. -->

## Impact

- **Code**: `src/ble/blemanager.cpp` (`getScaleType(device)` returns id), `src/ble/scales/scalefactory.{h,cpp}` (new `ScaleType → id` map + `normalizeScaleTypeId()`), `src/core/settings.cpp` (one-time migration; known-scales `type` field), `src/core/settings_calibration.cpp` (`sensorLag()` table → ids; SAW storage migration helpers), `src/main.cpp` (`scaleDiscovered` handler stores id, keeps display name separate), `src/core/settingsserializer.cpp` (normalize on device import), `src/mcp/mcptools_control.cpp` / `mcptools_settings.cpp` (scaleType argument / exposure).
- **Storage (QSettings)**: `scale/type`, `knownScales/scales[].type`, `saw/learningHistory`, `saw/perProfileHistory`, `saw/perProfileBatch`, `saw/globalBootstrapLag/*` migrated in place; a new migration flag is added.
- **Tests**: `tests/tst_saw_settings.cpp` (migration preserves history; id keying), scale-factory id round-trip (`type()` ↔ `normalizeScaleTypeId(scaleTypeName())`), known-scales migration.
- **Not affected**: flow calibration (per-profile only, no scale dimension); WiFi (`decent-wifi`) and USB (`decent-usb`) which are already id-keyed.
