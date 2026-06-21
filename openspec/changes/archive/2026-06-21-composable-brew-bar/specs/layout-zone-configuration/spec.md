## ADDED Requirements

### Requirement: Open zone options in the editors

The layout editors SHALL let a user open a zone-options panel for **any** zone — by double-click or long-press on the zone (not on a widget) in the in-app editor, and by an equivalent zone-options affordance in the web editor. Zone options SHALL persist using the existing per-zone settings storage pattern (a zone-keyed map in the layout JSON, alongside `offsets` and `scales`).

#### Scenario: Double-click or long-press opens zone options in-app

- **WHEN** a user double-clicks or long-presses the body of a zone (not a widget) in the in-app layout editor
- **THEN** a zone-options panel SHALL open for that zone

#### Scenario: Web editor opens zone options

- **WHEN** a user activates the zone-options affordance for a zone in the web editor
- **THEN** the editor SHALL load that zone's current options and present controls to change them

#### Scenario: Options apply to all zones

- **WHEN** a user opens zone options
- **THEN** the panel SHALL be available for every zone, not only the new `lowerMidBar` zone

#### Scenario: Options persist per zone

- **WHEN** a user changes a zone option and saves
- **THEN** that option SHALL be stored per zone in the layout JSON and SHALL survive app restarts
- **AND** other zones SHALL be unaffected

### Requirement: Populate a zone from a built-in preset

The zone-options panel SHALL offer a "populate from preset" action that fills a zone with a built-in widget arrangement in one step. The set of presets SHALL include a **"Brew bar"** preset that reproduces the PR #1364 view: the `profileName`, `scaleWeight` (context-aware data mode), `ratioQuickSelect`, `doseWeight`, and `milkWeight` widgets, with `equalWidth` distribution and the `accentBar` zone style. Populating SHALL set the zone's widgets and the relevant zone options together, and SHALL be available for the `lowerMidBar` zone.

#### Scenario: Populate the lower-mid bar with the brew-bar preset

- **WHEN** a user opens zone options for `lowerMidBar` and chooses the "Brew bar" preset
- **THEN** the zone SHALL be filled with the brew-bar widgets in order (profile, scale, ratio, dose, milk)
- **AND** the zone's distribution SHALL be set to `equalWidth` and its style to `accentBar`
- **AND** the result SHALL match the PR #1364 bar

#### Scenario: Populate is non-destructive to other zones

- **WHEN** a user populates a zone from a preset
- **THEN** only that zone's widgets and options SHALL change
- **AND** other zones SHALL be unaffected

#### Scenario: Populated widgets remain individually editable

- **WHEN** a zone has been populated from a preset
- **THEN** each widget SHALL be individually removable, reorderable, and (where applicable) per-instance configurable, like any manually added widget

### Requirement: Item distribution option

Each zone SHALL support an item-distribution option with at least the values `packed` (default, current behaviour), `equalWidth` (every widget gets an equal-width cell regardless of content width), and `spaced` (evenly spaced / justified). The default SHALL preserve current behaviour so existing layouts are unchanged.

#### Scenario: Default preserves current behaviour

- **WHEN** a zone has no distribution option set (existing layouts)
- **THEN** it SHALL lay out widgets exactly as it does today

#### Scenario: Equal-width distribution

- **WHEN** a zone's distribution is `equalWidth`
- **THEN** every widget SHALL occupy an equal-width cell across the zone, independent of its content width

#### Scenario: Spaced distribution

- **WHEN** a zone's distribution is `spaced`
- **THEN** widgets SHALL be evenly distributed across the zone's width

### Requirement: Zone alignment option

Each zone SHALL support an alignment option (`left` / `center` / `right`) that positions its widgets when they do not fill the zone width. The default SHALL preserve current behaviour.

#### Scenario: Alignment positions un-filled content

- **WHEN** a zone's widgets do not fill the available width
- **AND** the zone alignment is set to `left`, `center`, or `right`
- **THEN** the widgets SHALL be positioned accordingly within the zone

#### Scenario: Alignment is ignored under full-width distribution

- **WHEN** a zone uses `equalWidth` or `spaced` distribution (which already fill the width)
- **THEN** the alignment option SHALL have no visible effect and SHALL NOT cause layout errors

### Requirement: Theme-defined zone style presets

Each zone SHALL support a **zone style** option chosen from named presets defined in `Theme.qml` (never hardcoded colors in the zone or widgets). At minimum the presets SHALL include:
- `standard` — the default; transparent background with the normal theme text styling (matches today's look).
- `accentBar` — matches the PR #1364 bar: an accent-filled background with contrasting text and emphasised (bold) values.

Each preset SHALL bundle the zone background and the text/value treatment so widgets in the zone stay readable on the chosen background across light, dark, and custom palettes. Themes SHALL be able to define their own preset values so the styles track the active theme.

#### Scenario: Standard preset by default

- **WHEN** a zone has no zone-style option set
- **THEN** the zone SHALL render with the `standard` preset (transparent background, normal text), identical to today

#### Scenario: Accent-bar preset matches the PR look

- **WHEN** a zone's style is set to `accentBar`
- **THEN** the zone SHALL render a full-width accent-colored fill using `Theme` tokens
- **AND** widget labels and values within the zone SHALL use the matching contrast color
- **AND** widget values SHALL render with the preset's emphasis (e.g. bold), matching the PR #1364 bar

#### Scenario: Style follows the active theme

- **WHEN** the user changes the app theme (light / dark / custom)
- **THEN** the selected zone style's colors and contrast text SHALL update to the new theme without further configuration

#### Scenario: Presets are selectable in both editors

- **WHEN** a user opens zone options in the in-app or web editor
- **THEN** the available zone-style presets (incl. `standard` and `accentBar`) SHALL be selectable
- **AND** the chosen preset SHALL persist per zone
