# Spec Delta

## ADDED Requirements

### Requirement: Storage keys stay byte-identical across the domain split

Each domain sub-object's `QSettings` operations SHALL use the same key strings the property used before migration. Key strings SHALL remain byte-identical across the settings-store consolidation.

#### Scenario: Existing user settings persist across the migration
- **WHEN** a user upgrades from a build with `Settings::sawLearnedLag` to a build with `SettingsCalibration::sawLearnedLag`
- **THEN** the previously-saved SAW learning history (`saw/learningHistory`, `saw/perProfileHistory`, `saw/perProfileBatch`, `saw/globalBootstrapLag/<scale>`) is read correctly on first launch
- **AND** the previously-saved flow calibration values (`calibration/flowMultiplier`, `calibration/autoFlowCalibration`, `calibration/perProfileFlow`, `calibration/flowCalBatch`) are read correctly on first launch
- **AND** no user-visible settings reset occurs

#### Scenario: Key strings match pre-split values
- **WHEN** a property's setter or getter is reviewed in `SettingsCalibration`
- **THEN** the string passed to `m_settings.value(...)` / `m_settings.setValue(...)` is byte-identical to the pre-migration string in `settings.cpp`

#### Scenario: Sub-objects share one store handle source
- **WHEN** any `Settings<Domain>` constructor is reviewed
- **THEN** its `m_settings` member is declared `mutable AppSettings` and default-constructed, rather than initialised from a literal organization/application pair
- **AND** the strings `"DecentEspresso"` and `"DE1Qt"` do not appear in any `settings_<domain>.cpp`

## REMOVED Requirements

### Requirement: Storage Key Stability
**Reason**: Its scenario requiring the legacy-store migration to precede `Settings` construction describes a migration that is removed.
**Migration**: None. Replaced by "Storage keys stay byte-identical across the domain split".

### Requirement: Legacy store migrates before sub-objects read it
**Reason**: The legacy-store migration is removed. Every installation at or above the upgrade floor (2.0.7) has already run it, and older installations are refused before `Settings` is constructed (see `upgrade-floor`).
**Migration**: Install Decenza 2.1.0 first; it still performs this migration.
