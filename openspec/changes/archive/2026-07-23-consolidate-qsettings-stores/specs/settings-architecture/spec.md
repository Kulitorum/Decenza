## MODIFIED Requirements

### Requirement: Storage Key Stability

Each domain sub-object's `QSettings` operations SHALL use the same key strings the property used
before being migrated to the sub-object. Key strings SHALL remain byte-identical across the
settings-store consolidation.

Domain sub-objects SHALL NOT construct their own store handle. Each SHALL obtain its handle from
the shared `appSettings()` accessor, which names the canonical store identity in exactly one place
(see the `settings-store-identity` capability). This replaces the previous arrangement in which
each sub-object opened `QSettings("DecentEspresso", "DE1Qt")` directly.

Existing user settings SHALL survive the consolidation, but — unlike the original domain split —
they do so by **migration** rather than by the store being untouched: the one-time legacy-store
migration copies every key from `("DecentEspresso", "DE1Qt")` into the canonical store before any
sub-object reads it. Domain sub-objects therefore SHALL NOT assume the canonical store is
pre-populated at construction on an upgrading installation; the migration SHALL run before the
`Settings` façade and its sub-objects are constructed.

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
- **THEN** its `m_settings` member is initialised from `appSettings()` rather than from a literal organization/application pair
- **AND** the strings `"DecentEspresso"` and `"DE1Qt"` do not appear in any `settings_<domain>.cpp`

#### Scenario: Migration precedes settings construction
- **WHEN** the application starts on an installation that has not yet migrated
- **THEN** the legacy-store migration completes before the `Settings` façade is constructed
- **AND** every domain sub-object's first read observes the migrated values rather than defaults
