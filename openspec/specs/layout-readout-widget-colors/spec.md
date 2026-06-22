# layout-readout-widget-colors Specification

## Purpose
TBD - created by archiving change rename-clock-widget-add-colors. Update Purpose after archive.
## Requirements
### Requirement: Per-instance color override for readout widgets

Each readout layout widget — Clock, Temperature, Steam Temp, Water Level, Machine Status, and Scale Weight — SHALL gain a per-instance `color` property selectable from a fixed set: `default` (the default), `white`, `green`, `red`, `blue`, and `orange`. The named non-default values SHALL map to existing semantic theme colors used elsewhere on the page (so the widget matches its surroundings and honours custom themes): `white` → theme text color, `green` → pressure color, `red` → temperature color, `blue` → flow color, `orange` → warning color. A non-default choice SHALL tint both the widget's value text and its optional icon. The value SHALL be read from the item's stored properties (`modelData`), persist per instance, and apply in any zone the widget is placed in.

#### Scenario: Default preserves the current color

- **WHEN** a readout widget has `color` set to `default` or unset (existing layouts, or a freshly added widget)
- **THEN** it SHALL render in exactly the color it uses today

#### Scenario: Default preserves dynamic coloring

- **WHEN** a Machine Status or Scale Weight widget has `color` set to `default` or unset
- **THEN** its color SHALL continue to vary with state as it does today (Machine Status by machine phase; Scale Weight by tap/ratio/flow-scale state)

#### Scenario: Named color is a full static override

- **WHEN** a readout widget's `color` is set to `white`, `green`, `red`, `blue`, or `orange`
- **THEN** its value text and icon SHALL be tinted with the mapped theme color in all states, replacing any dynamic state coloring

#### Scenario: Per-instance independence

- **WHEN** two readout widgets are placed with different `color` values
- **THEN** each SHALL render in its own color independently

### Requirement: Readout color is editable in both editors

The per-instance options editor for each readout widget SHALL expose the `color` choice alongside its existing options (display mode for all; data mode additionally for Scale Weight), in both the native QML editor and the web layout editor. The native editors SHALL present the choice through a single shared color picker so all readout widgets offer an identical palette. Selecting a color SHALL persist it to the item's properties immediately and update the rendered widget.

#### Scenario: Editing color in the native editor

- **WHEN** the user opens the options editor for any readout widget and selects a color
- **THEN** the color SHALL be saved to that instance and the widget SHALL update to the chosen color

#### Scenario: Editing color in the web editor

- **WHEN** the user selects a color for a readout chip in the web layout editor
- **THEN** the color SHALL be persisted to that instance and reflected in the chip preview (named colors tint the label; `default` leaves it untinted)

### Requirement: Single source of truth for the readout color palette

The color names, their theme-color mapping, and the picker UI SHALL be defined once and reused by every readout widget and editor, so the palette cannot drift between widgets. An unknown or unrecognised stored `color` value SHALL degrade gracefully to the widget's default (natural) color rather than failing.

#### Scenario: Unknown stored value degrades gracefully

- **WHEN** a readout widget is loaded with a `color` value outside the known palette
- **THEN** it SHALL render in its default (natural) color with no error

