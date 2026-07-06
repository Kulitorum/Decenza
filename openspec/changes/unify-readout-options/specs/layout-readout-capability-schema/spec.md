# layout-readout-capability-schema Specification

## ADDED Requirements

### Requirement: Widget option capabilities are declared in one schema

The set of per-instance option keys each layout widget type supports (for example `displayMode`, `color`, `dataMode`, `showRatio`) SHALL be declared in a single capability schema, defined in exactly one place in the codebase. The schema SHALL map widget type → list of supported option keys. A widget type absent from the schema (or mapped to an empty list, for types whose editor is bespoke, such as `custom`, `sleep`, and screensavers, which the schema SHALL still gate as configurable) has no readout options.

Adding a new option to an existing readout type, or adding a new readout type with standard options, SHALL require changing only the schema plus the code that renders the option's effect — no editor, gate, or web-mirror edits.

#### Scenario: Schema declares scale weight's full option set

- **WHEN** the schema entry for `scaleWeight` is read by any consumer
- **THEN** it lists `dataMode`, `displayMode`, `showRatio`, and `color`

#### Scenario: New option key needs only a schema edit to appear everywhere

- **WHEN** an option key is added to a type's schema entry
- **THEN** the in-app options editor, the has-options gate, and the web editor all reflect it without any per-consumer list being edited

### Requirement: All configurable-type consumers derive from the schema

`SettingsNetwork::typeHasOptions()`, the in-app layout editor's has-options indicator and open-options affordance, and the web layout editor's has-options behavior and per-type option forms SHALL all derive from the capability schema rather than maintaining independent type lists. Hand-synchronized mirrors of the configurable-type set SHALL be removed.

#### Scenario: Gate agrees with the schema

- **WHEN** a widget type has a non-empty schema entry or is schema-gated as bespoke-configurable
- **THEN** `typeHasOptions()` returns true for it, its chip shows the has-options indicator, and the options affordance opens an editor — in both the in-app and web editors

#### Scenario: No independent type lists remain

- **WHEN** the configurable-type set changes in the schema
- **THEN** no other code location must be edited for the in-app and web editors' has-options behavior to stay consistent

### Requirement: One unified readout options editor

A single readout options editor SHALL replace the per-type readout popups (`DisplayModeEditorPopup`, `ScaleWeightEditorPopup`). When opened for a widget instance it SHALL render exactly the option sections the type's schema entry declares, reading and writing the instance's stored properties through the existing item-property mechanism. Option sections SHALL render identically for every type that declares them (same controls, same shared color picker, same labels).

#### Scenario: Editor shows only declared sections

- **WHEN** the editor opens for a `temperature` instance (schema: `displayMode`, `color`)
- **THEN** it presents display-mode and color controls and nothing else
- **WHEN** it opens for a `scaleWeight` instance
- **THEN** it additionally presents the data-mode and ratio-suffix controls

#### Scenario: Retired popups are gone

- **WHEN** any readout widget's options are opened in the in-app editor
- **THEN** the unified editor opens; `DisplayModeEditorPopup` and `ScaleWeightEditorPopup` no longer exist in the codebase

#### Scenario: Same option looks the same on every type

- **WHEN** the color section renders for any two readout types
- **THEN** both present the identical shared 6-choice palette picker
