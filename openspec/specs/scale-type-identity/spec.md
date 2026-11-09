# scale-type-identity Specification

## Purpose
Requires that every persisted scale identity — the `scale/type` setting, `knownScales` entries, and all SAW learning keys — be keyed on the stable canonical type-id returned by `ScaleDevice::type()` (e.g. `decent`, `decent-wifi`) rather than the human-readable display name, so renaming a scale's label never breaks its learned calibration data. `ScaleFactory` owns the single id/display-name mapping and a `normalizeScaleTypeId()` helper, and a one-time idempotent migration rekeys any legacy display-name-keyed data without loss.

## Requirements

### Requirement: scaleType is a canonical type-id

The system SHALL persist and key on the stable canonical scale type-id (the value returned by `ScaleDevice::type()`, e.g. `decent`, `acaia`, `decent-wifi`, `decent-usb`) for every per-scale storage location, including the `scale/type` setting, `knownScales` entries, and all SAW learning keys. The human-readable display name SHALL be stored only in the `name` field and SHALL NOT be used as a key.

#### Scenario: BLE discovery stores the id, not the display name
- **WHEN** a BLE scale is discovered and registered as a known scale
- **THEN** the persisted `scaleType` (and the known-scale entry's `type` field) SHALL be the canonical type-id (e.g. `decent`)
- **AND** the human-readable label (e.g. `Decent Scale`) SHALL be stored in the entry's `name` field

#### Scenario: Renaming a display name does not change any key
- **WHEN** a scale's display name is changed in a later release (e.g. `Decent Scale (WiFi)` → `Half Decent Scale (WiFi)`)
- **THEN** that scale's `scaleType` and all of its per-(profile, scale) SAW keys, `globalBootstrapLag` key, and `sensorLag()` lookup SHALL be unchanged

#### Scenario: SAW keys use the type-id
- **WHEN** SAW records learning for a scale
- **THEN** `learningHistory[].scale`, the `perProfileHistory` and `perProfileBatch` keys, and `globalBootstrapLag/<scaleType>` all use the canonical type-id

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

### Requirement: Normalization is internal-only

Normalizing `scaleType` to ids SHALL NOT change any user-visible label. The discovered-scales list, the known-devices picker, and all scale labels SHALL continue to display the human-readable name from the `name` field.

#### Scenario: UI label is unchanged after normalization
- **WHEN** a known scale whose `type` was migrated from a display name to an id is shown in the UI
- **THEN** the displayed label SHALL be the unchanged human-readable `name` (e.g. `Decent Scale`), not the id
