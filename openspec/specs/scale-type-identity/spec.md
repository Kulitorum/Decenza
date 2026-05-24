# scale-type-identity Specification

## Purpose
TBD - created by archiving change normalize-scale-type-ids. Update Purpose after archive.
## Requirements
### Requirement: scaleType is a canonical type-id

The system SHALL persist and key on a stable, canonical scale type-id (the value returned by `ScaleDevice::type()`, e.g. `decent`, `acaia`, `decent-wifi`, `decent-usb`) for every per-scale storage location — the `scale/type` setting, `knownScales/scales[].type`, and all SAW learning keys (`learningHistory[].scale`, `perProfileHistory`/`perProfileBatch` keys and entries, `globalBootstrapLag/<scaleType>`). The human-readable display name SHALL be stored only in the separate `name` field and SHALL NOT be used as a key.

#### Scenario: BLE discovery stores the id, not the display name
- **WHEN** a BLE scale is discovered and registered as a known scale
- **THEN** the persisted `scaleType` (and the known-scale entry's `type` field) SHALL be the canonical type-id (e.g. `decent`)
- **AND** the human-readable label (e.g. `Decent Scale`) SHALL be stored in the entry's `name` field

#### Scenario: Renaming a display name does not change any key
- **WHEN** a scale's display name is changed in a later release (e.g. `Decent Scale (WiFi)` → `Half Decent Scale (WiFi)`)
- **THEN** that scale's `scaleType` and all of its per-(profile, scale) SAW keys, `globalBootstrapLag` key, and `sensorLag()` lookup SHALL be unchanged

### Requirement: Single source-of-truth id mapping

`ScaleFactory` SHALL provide a canonical mapping from `ScaleType` to its id string (mirroring the existing `scaleTypeName()` display-name mapping) and a `normalizeScaleTypeId(QString)` helper that converts any legacy display-name OR id input to the canonical id. Normalization SHALL be idempotent and SHALL pass through unrecognized strings unchanged so custom or unknown values are never destroyed.

#### Scenario: Display name normalizes to id
- **WHEN** `normalizeScaleTypeId("Decent Scale")` is called
- **THEN** it SHALL return `decent`

#### Scenario: An id normalizes to itself
- **WHEN** `normalizeScaleTypeId("decent-wifi")` is called
- **THEN** it SHALL return `decent-wifi` (idempotent)

#### Scenario: Unknown string passes through unchanged
- **WHEN** `normalizeScaleTypeId("Some Future Scale")` is called and no `ScaleType` resolves
- **THEN** it SHALL return the input string unchanged

#### Scenario: Round-trips with the canonical accessors
- **WHEN** any supported scale's `type()` id is mapped through `scaleTypeName()` and back through `normalizeScaleTypeId()`
- **THEN** the result SHALL equal the original `type()` id

### Requirement: sensorLag is keyed by type-id

`SettingsCalibration::sensorLag()` SHALL resolve its per-scale lag from the canonical type-id and SHALL include an entry for every shipped Decent transport id (`decent`, `decent-wifi`, `decent-usb`). It SHALL NOT rely on display-name keys to return a non-default value.

#### Scenario: Each Decent transport id resolves without warning
- **WHEN** `sensorLag()` is called with `decent`, `decent-wifi`, or `decent-usb`
- **THEN** it SHALL return the configured lag (0.38 s) without emitting an "Unknown scale type" warning

#### Scenario: Unknown id falls back to the default
- **WHEN** `sensorLag()` is called with an id that has no table entry
- **THEN** it SHALL return the documented default lag (0.38 s)

### Requirement: One-time migration preserves existing learning

On startup, exactly once (guarded by a persisted migration flag, following the existing `knownScales/migrated` pattern), the system SHALL rewrite every persisted display-name `scaleType` value to its canonical id across `scale/type`, `knownScales/scales[].type`, and all SAW storage (`saw/learningHistory` entries' `scale` field, the `saw/perProfileHistory` and `saw/perProfileBatch` map keys of the form `profile::scaleType` plus their per-entry `scale` field, and `saw/globalBootstrapLag/<scaleType>`). No learning data SHALL be lost or duplicated.

#### Scenario: Legacy display-name SAW history is rekeyed and remains readable
- **WHEN** the migration runs over a `perProfileHistory` key `"<profile>::Decent Scale"` with committed medians
- **THEN** the data SHALL be readable afterward under `"<profile>::decent"` with the same medians
- **AND** the post-migration `sawModelSource`/`sawLearnedLagFor` for that pair SHALL return the same model as before the rename

#### Scenario: Migration is idempotent
- **WHEN** the migration flag is already set
- **THEN** the migration SHALL be a no-op on subsequent launches

#### Scenario: Already-id values are left unchanged
- **WHEN** the migration encounters a value that is already a canonical id (e.g. `decent-wifi`, `decent-usb`)
- **THEN** that value and its keyed data SHALL be left unchanged

#### Scenario: Colliding legacy and id buckets merge without loss
- **WHEN** both a display-name-keyed bucket (`"<profile>::Decent Scale"`) and an id-keyed bucket (`"<profile>::decent"`) exist for the same profile
- **THEN** the migration SHALL merge the legacy entries into the id bucket (newest-first) and apply the normal trim, losing no entries beyond the existing trim limit

### Requirement: Normalization is internal-only

Normalizing `scaleType` to ids SHALL NOT change any user-visible label. The discovered-scales list, the known-devices picker, and all scale labels SHALL continue to display the human-readable name from the `name` field.

#### Scenario: UI label is unchanged after normalization
- **WHEN** a known scale whose `type` was migrated from a display name to an id is shown in the UI
- **THEN** the displayed label SHALL be the unchanged human-readable `name` (e.g. `Decent Scale`), not the id

