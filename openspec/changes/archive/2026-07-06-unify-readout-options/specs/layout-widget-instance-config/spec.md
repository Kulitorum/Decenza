# layout-widget-instance-config Delta

## MODIFIED Requirements

### Requirement: Per-instance display mode for readout widgets

The `machineStatus`, `temperature`, `steamTemperature`, `waterLevel`, `clock`, `scaleWeight`, `batteryLevel`, `scaleBattery`, `doseWeight`, and `milkWeight` widgets SHALL each support a per-instance `displayMode` property with values `text` (default) and `icon`, as declared in the layout readout capability schema. In `icon` mode the widget SHALL render a tinted icon ahead of its value, using its existing icon asset; in `text` mode it SHALL render exactly as it does today. Widgets whose default rendering already includes an icon (`batteryLevel`, `scaleBattery`) SHALL treat their current rendering as the default and offer the other form via the mode. `profileName` SHALL NOT expose `displayMode` (no meaningful icon form) — the capability schema declares it color-only. The mode SHALL be read from the item's stored properties (`modelData`), persist per instance, and apply in any zone the widget is placed in.

#### Scenario: Default is today's rendering

- **WHEN** one of these widgets has no `displayMode` set (existing layouts)
- **THEN** it SHALL render identically to today

#### Scenario: Icon mode renders an icon

- **WHEN** a widget's `displayMode` is `icon`
- **THEN** it SHALL render its icon ahead of the value, tinted to the surrounding text/contrast color

#### Scenario: Newly-optioned readouts honor the mode

- **WHEN** a `doseWeight` or `milkWeight` instance has `displayMode` set to `icon`
- **THEN** it SHALL render its icon ahead of the value like the other readouts

#### Scenario: Works in any zone

- **WHEN** an icon-mode widget is placed in any zone (status bar, a bottom bar, the lower-mid bar, etc.)
- **THEN** it SHALL render its icon form there — the mode is not gated by a zone style

#### Scenario: Two instances differ

- **WHEN** the same widget type is placed twice with different `displayMode` values
- **THEN** each instance SHALL render according to its own mode

#### Scenario: Edited via the unified readout editor

- **WHEN** a user opens one of these widgets' options in the in-app or web layout editor
- **THEN** the unified readout options editor SHALL expose the `displayMode` choice for that instance (and for `scaleWeight`, its existing `dataMode` and ratio-suffix options as well)
- **AND** the chosen mode SHALL persist for that instance only

#### Scenario: Disconnected state

- **WHEN** the widget's device is disconnected
- **THEN** it SHALL show its existing placeholder ("—"/"--") in either mode, without errors

### Requirement: Single source of truth for configurable widget types

Which widget types have configurable options, and which option keys each type supports, SHALL be defined by the layout readout capability schema (see `layout-readout-capability-schema`) and consumed by every site that needs it — the in-app has-options indicator, the in-app open-options gesture/affordance, the in-app options editor's section selection, and the web editor's indicator, open affordance, and option forms. Adding a new configurable widget type or a new option key SHALL require updating only the schema for the editors' behavior to stay consistent.

#### Scenario: Indicator and open behavior agree

- **WHEN** the editors render and a widget type is configurable per the capability schema
- **THEN** that type SHALL both display the has-options indicator AND respond to the open-options affordance/gesture

#### Scenario: Non-configurable type is inert everywhere

- **WHEN** a widget type is not configurable per the capability schema
- **THEN** it SHALL neither show the indicator nor open an instance editor in either editor

#### Scenario: Editor sections follow the schema

- **WHEN** the in-app or web editor opens a configurable readout instance
- **THEN** the option controls presented SHALL be exactly those the schema declares for that type
