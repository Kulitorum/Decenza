## Context

`scaleType` is used in two incompatible roles at once: a **persistence/lookup key** (per-(profile, scale) SAW learning, `sensorLag()`, `globalBootstrapLag`, the known-scales registry, the `scale/type` setting) and, for BLE scales, a **display label**. The conflation comes from `BLEManager::getScaleType(QBluetoothDeviceInfo)`, which returns `ScaleFactory::scaleTypeName()` (the human label) and stores that into the `type` field. WiFi and USB already avoid this — they store ids (`decent-wifi`, `decent-usb`) with the label in a separate `name` field.

Consequence: renaming any BLE display name silently re-keys that scale, orphaning its SAW learning and missing its `sensorLag()` entry, with no compile-time signal. The #1260 WiFi rename only escaped this because WiFi was already id-keyed.

Relevant existing machinery:
- `ScaleDevice::type()` already returns the canonical id per scale class.
- `ScaleFactory::resolveScaleType(QString)` maps display-name *or* id → `ScaleType` enum (but **consolidates** Acaia Pyxis → `Acaia`).
- `ScaleFactory::scaleTypeName(ScaleType)` maps enum → display name.
- `Settings` already has a one-time migration pattern guarded by `knownScales/migrated` (settings.cpp:234).
- Flow calibration is **per-profile only** (no scale dimension) — out of scope.

## Goals / Non-Goals

**Goals:**
- `scaleType` is always a canonical type-id in storage and lookups; the display name lives only in the `name` field.
- A single source of truth converts any scale string (legacy display name or id) → canonical id.
- Existing users' SAW learning and known-scale entries survive the change via a transparent one-time migration.
- No user-visible change.

**Non-Goals:**
- Changing SAW algorithm behavior or the `(profile, scale)` model itself (the `stop-at-weight-learning` spec is unchanged).
- Touching flow calibration (not scale-keyed).
- Changing the `name` source for BLE known-scale entries (still `device.name()`).
- Re-measuring `sensorLag()` priors per transport (separate tuning concern).

## Decisions

### D1 — Canonical id mapping = inverse of `scaleTypeName`, not `resolveScaleType`
Add `ScaleFactory::scaleTypeId(ScaleType)` (enum → id, mirroring `scaleTypeName`) and `ScaleFactory::normalizeScaleTypeId(QString)`. `normalizeScaleTypeId` resolves via a **direct inverse-of-`scaleTypeName` table** (plus identity for already-id inputs, plus pass-through for unrecognized strings).

*Why not build on `resolveScaleType`?* `resolveScaleType` consolidates `Acaia Pyxis → Acaia`, which would collapse the distinct `acaiapyxis` id (`AcaiaScale::type()` returns `acaiapyxis` when `m_isPyxis`). Using the inverse-of-`scaleTypeName` map keeps `"Acaia Pyxis" → "acaiapyxis"`, matching `type()` exactly and preserving the distinction. A round-trip test (`type()` → `scaleTypeName` → `normalizeScaleTypeId` == `type()`) guards against drift.

### D2 — Fix the write path so new data is always id-keyed
`BLEManager::getScaleType(QBluetoothDeviceInfo)` returns `scaleTypeId(detectScaleType(device))` instead of `scaleTypeName(...)`. The `scaleDiscovered(device, type)` emission and `main.cpp`'s handler then carry the id in `type`; the display name continues to be computed separately for the `name`/`displayName` field (WiFi already does this; BLE keeps `device.name()`).

### D3 — Normalize defensively at the lookup boundaries
`sensorLag()` and the SAW public API (`addSawLearningPoint`, `sawLearnedLagFor`, `getExpectedDripFor`, `sawModelSource`, `isSawConverged`, `sawLearningEntriesFor`, `resetSawLearningForProfile`) call `normalizeScaleTypeId()` on their incoming `scaleType` before keying. This makes every read/write canonical regardless of caller, so the migration is only responsible for *existing* data, and any stray display-name caller can't regress. `sensorLag()`'s table is rekeyed to ids and gains `decent-usb`.

*Alternative considered:* normalize only inside `settings.scaleType()`. Rejected — it would silently change a public getter consumed by UI/MCP/serializer and wouldn't fix legacy data already stored under display-name keys.

### D4 — One-time migration, flag-guarded, at startup
A single migration (guarded by a new flag, e.g. `scale/typeIdsMigrated`, alongside the existing `knownScales/migrated`) rewrites display-name values → ids in:
- `scale/type`
- each `knownScales/scales[].type`
- SAW: `saw/learningHistory` entries' `scale` field; the `saw/perProfileHistory` and `saw/perProfileBatch` map keys (`profile::scaleType`) and their per-entry `scale` field; `saw/globalBootstrapLag/<scaleType>` sub-keys.

SAW-key rewriting lives in `SettingsCalibration::migrateScaleTypeIds()` (it owns the SAW schema); the `scale/type` + known-scales rewrite lives in `Settings`. Both run once from `Settings` init under one flag.

**Collision rule:** if a profile already has both `profile::<DisplayName>` and `profile::<id>` buckets, merge legacy entries into the id bucket newest-first, then apply the existing trim. Same for the global pool.

## Risks / Trade-offs

- **Buggy migration corrupts SAW learning** → Idempotent flag; conservative merge (never overwrite/drop beyond the existing trim); the legacy `saw/learningHistory` global pool remains a read-path fallback; covered by `tst_saw_settings` migration tests. Ship in the beta/pre-release channel first. (QSettings has no cheap snapshot, so tests are the primary safety net; users can always "Reset all" SAW for a clean slate.)
- **`scaleTypeId()` drifts from `ScaleDevice::type()`** → Round-trip unit test over every `ScaleType`.
- **Acaia Pyxis ambiguity** → D1 keeps `acaiapyxis` distinct from `acaia`, matching `type()`; documented and tested.
- **A persisted reference outside the audited sites** → Audited callers: `settingsserializer` (device import) normalizes on read; MCP `reset_saw_learning_for_profile` / `settings_get` go through the normalized SAW API. No other persistence keys on `scaleType`.
- **Per-transport SAW stays isolated (decent vs decent-wifi vs decent-usb)** → Intended, not a regression: transports have genuinely different round-trip latency, so isolation is correct.

## Migration Plan

1. Add `scaleTypeId()` + `normalizeScaleTypeId()` + round-trip test.
2. Switch `BLEManager::getScaleType(device)` to return the id (D2).
3. Normalize at `sensorLag()` and the SAW API boundary; rekey `sensorLag()` table to ids; add `decent-usb` (D3).
4. Add the flag-guarded one-time migration with the collision rule (D4).
5. Normalize on device import in `settingsserializer`.
6. Tests: round-trip; migration preserves/rekeys/merges SAW history; known-scales + `scale/type` migration; id-keyed `sensorLag`.
7. Ship to beta/pre-release; migration runs on first launch, no user action. Rollback: the legacy pool fallback + "Reset all" bound worst-case damage.

## As-built notes (refinements during implementation)

- **Dedicated `ScaleTypeIds` unit.** The `ScaleType` enum + `scaleTypeId()` / `scaleTypeName()` / `normalizeScaleTypeId()` live in a new dependency-free `src/ble/scales/scaletypeids.{h,cpp}` (only `<QString>`), so `settings*.cpp` and the SAW unit test link it without pulling QtBluetooth or the 14 concrete scale drivers. `ScaleFactory` keeps thin forwarders for its existing API.
- **`DecentScaleUsb` added to the enum.** The Half Decent Scale is one physical scale over three transports, so the enum now has `DecentScale` (BLE), `DecentScaleWifi`, and `DecentScaleUsb` — `decent-usb` / "Half Decent Scale (USB)" now map through the enum rather than the unknown-passthrough fallback.
- **Read-boundary normalization mechanism.** Rather than editing every method body, per-pair reads/writes normalize through a single choke point — `sawPairKey()` — and the three global-pool matchers (`isSawConverged`, `sawLearningEntries`, `sawModelSource`) take the scale type by value and normalize at the top. `currentScaleType()`, `sensorLag()`, and `globalSawBootstrapLag`/`setGlobalSawBootstrapLag` normalize inline. This was necessary, not just defensive: the existing SAW tests pass the display name `"Decent Scale"` and rely on read/write symmetry.
- **`getScaleType(device)` returns the id** (chosen scope), which also repairs the previously-dead WiFi→BLE `isFallbackCandidate` match at `blemanager.cpp` (it compared `scaleType == "decent"` against a display name).

## Open Questions

- Should the display-name entries in `sensorLag()` be deleted immediately (relying on D3 normalization) or kept one release as belt-and-suspenders? Leaning delete, since D3 normalizes the input anyway.
- Confirm no third-party/QML code reads `Settings.scaleType` expecting a display name (grep shows MCP + serializer only, both fine with ids).
