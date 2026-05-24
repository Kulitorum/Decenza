## 1. Canonical id mapping (ScaleFactory)

- [x] 1.1 Add `ScaleFactory::scaleTypeId(ScaleType)` in `scalefactory.{h,cpp}` returning the canonical id for every `ScaleType`; each value MUST equal the matching `ScaleDevice::type()` (e.g. `Acaia`→`acaia`, `AcaiaPyxis`→`acaiapyxis`, `DecentScale`→`decent`, `DecentScaleWifi`→`decent-wifi`).
- [x] 1.2 Add `ScaleFactory::normalizeScaleTypeId(const QString&)` as the inverse-of-`scaleTypeName` map plus identity for already-id inputs plus pass-through for unrecognized strings; keep `acaiapyxis` distinct from `acaia` (do NOT route through `resolveScaleType`, which consolidates Pyxis).
- [x] 1.3 Round-trip unit test: for every `ScaleType`, assert `normalizeScaleTypeId(scaleTypeName(t)) == scaleTypeId(t)`; and for each constructed scale class assert `normalizeScaleTypeId(dev->type()) == dev->type()`.

## 2. Write path stores ids

- [x] 2.1 Change `BLEManager::getScaleType(const QBluetoothDeviceInfo&)` to return `scaleTypeId(detectScaleType(device))` (id) instead of `scaleTypeName(...)`.
- [x] 2.2 Verify the `main.cpp` `scaleDiscovered` handler stores the id in the known-scale `type` field and the human label in `name`/`displayName` (BLE `name` stays `device.name()`); fix any spot that assumed `type` was a display name.
- [x] 2.3 Confirm `ScaleFactory::createScale(device, type)` and the `resolveScaleType(type)` comparison (main.cpp ~1484) behave correctly with an id `type` (resolveScaleType already accepts ids — add a regression assertion if practical).

## 3. Normalize at lookup boundaries

- [x] 3.1 Rekey the `SettingsCalibration::sensorLag()` table to ids, add a `decent-usb` entry (0.38), call `normalizeScaleTypeId()` on the argument first, and remove the display-name keys.
- [x] 3.2 Call `normalizeScaleTypeId()` on the incoming `scaleType` at the top of each SAW public method (`addSawLearningPoint`, `sawLearnedLagFor`, `getExpectedDripFor`, `sawModelSource`, `isSawConverged`, `sawLearningEntriesFor`, `resetSawLearningForProfile`) and the `currentScale` accessor, so all keys are canonical regardless of caller.

## 4. One-time migration

- [x] 4.1 Add `SettingsCalibration::migrateScaleTypeIds()` that rewrites to ids: `saw/learningHistory[].scale`; the `saw/perProfileHistory` and `saw/perProfileBatch` map keys (`profile::scaleType`) and each entry's `scale`; and `saw/globalBootstrapLag/*` sub-keys. Apply the collision rule: merge legacy entries into an existing id bucket newest-first, then the normal trim (no loss).
- [x] 4.2 In `Settings` init, behind a new `scale/typeIdsMigrated` flag, normalize `scale/type` and every `knownScales/scales[].type`, then call `migrateScaleTypeIds()`, then set the flag; make the whole pass idempotent.
- [x] 4.3 Sequence this migration to run after the existing `knownScales/migrated` migration.

## 5. Peripheral callers

- [x] 5.1 Normalize `scaleType` to an id on device import in `settingsserializer.cpp`.
- [x] 5.2 Confirm MCP `reset_saw_learning_for_profile` (`mcptools_control.cpp`) and `settings_get` (`mcptools_settings.cpp`) flow through the normalized SAW API; no change needed if Task 3.2 covers them (verify with a quick read).

## 6. Tests (tests/tst_saw_settings.cpp + scale factory)

- [x] 6.1 Migration rekeys legacy `"<profile>::Decent Scale"` → `"<profile>::decent"` with identical medians and the same `sawModelSource`/`sawLearnedLagFor`.
- [x] 6.2 Migration is idempotent (second run with flag set is a no-op).
- [x] 6.3 Collision merge: both display-name and id buckets present → merged newest-first + trimmed, no entries lost beyond the trim limit.
- [x] 6.4 Already-id values (`decent-wifi`, `decent-usb`) are left unchanged by migration.
- [x] 6.5 `sensorLag()` returns 0.38 for `decent`/`decent-wifi`/`decent-usb` with no "Unknown scale type" warning; a legacy display name still resolves via normalization.
- [x] 6.6 `scale/type` and `knownScales/scales[].type` migration test (display name → id; `name` unchanged).

## 7. Validation

- [x] 7.1 Build via Qt Creator (cross-platform files; confirm 0 new warnings).
- [x] 7.2 `tst_saw_settings` and the scale-factory round-trip test pass.
- [x] 7.3 `openspec validate normalize-scale-type-ids --strict` passes.
