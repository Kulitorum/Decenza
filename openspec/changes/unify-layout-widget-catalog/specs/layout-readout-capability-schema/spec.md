# layout-readout-capability-schema Delta

## ADDED Requirements

### Requirement: The schema declares each type's default display mode

The readout capability schema SHALL additionally declare, for each type that supports `displayMode`, which mode an absent stored value means (the type's "today's rendering" default — `icon` for `batteryLevel` and `scaleBattery`, `text` for the rest). All consumers — the unified readout options editor, the web editor's option forms, and the widget item components — SHALL derive the default from the schema instead of hand-coding the type list. The invariant is unchanged: an absent stored `displayMode` always renders exactly as the widget did before per-instance display modes existed.

#### Scenario: Battery default comes from the schema

- **WHEN** a `batteryLevel` or `scaleBattery` instance has no stored `displayMode`
- **THEN** the item renders icon+value, the unified editor opens with "Icon + value" selected, and the web editor's selector shows the same — all three reading the schema's declared default

#### Scenario: Text-default readouts are unaffected

- **WHEN** any other displayMode-capable readout has no stored `displayMode`
- **THEN** it renders value-only (its pre-existing form) and both editors show "Value only" selected

#### Scenario: One declaration, no hand-coded type checks

- **WHEN** a future readout type is declared with an `icon` default in the schema
- **THEN** the editors and the web editor honor it without any `type === "..."` checks being added outside the schema
