# layout-readout-capability-schema Specification

## Purpose
The single source of truth for which per-instance option keys (display mode, color, data mode, ratio suffix, etc.) each layout readout widget type supports, and each type's default display mode when unset. The in-app has-options gate, the unified readout options editor, and the web layout editor all derive their behavior from this schema instead of maintaining independent, hand-synchronized type lists.
## Requirements
### Requirement: Widget option capabilities are declared in one schema

Per-instance option keys for each layout widget type (for example `displayMode`,
`color`, `dataMode`, `showRatio`) SHALL be declared in one capability schema
mapping type to option keys. A type absent from the schema has no readout
options. Bespoke-editor types with no keys (`custom`, `sleep`, screensavers)
SHALL still be gated as configurable. Adding an option SHALL change only the
schema and the code that renders its effect.

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

### Requirement: The schema declares each type's default display mode

The schema SHALL declare, for each `displayMode`-capable type, the mode an
absent stored value means: `icon` for `batteryLevel` and `scaleBattery`, `text`
for the rest. Every consumer (the unified options editor, the web editor's
option forms and the widget item components) SHALL take that default from the
schema, not a hand-coded list. An absent `displayMode` SHALL render exactly as
before per-instance display modes existed.

#### Scenario: Battery default comes from the schema

- **WHEN** a `batteryLevel` or `scaleBattery` instance has no stored `displayMode`
- **THEN** the item renders icon+value, the unified editor opens with "Icon + value" selected, and the web editor's selector shows the same — all three reading the schema's declared default

#### Scenario: Text-default readouts are unaffected

- **WHEN** any other displayMode-capable readout has no stored `displayMode`
- **THEN** it renders value-only (its pre-existing form) and both editors show "Value only" selected

#### Scenario: One declaration, no hand-coded type checks

- **WHEN** a future readout type is declared with an `icon` default in the schema
- **THEN** the editors and the web editor honor it without any `type === "..."` checks being added outside the schema

