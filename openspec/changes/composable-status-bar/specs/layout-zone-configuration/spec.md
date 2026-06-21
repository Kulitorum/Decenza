## ADDED Requirements

### Requirement: Status bar zone honors per-zone options

The top `statusBar` zone SHALL render through the shared bar renderer so it supports the same per-zone options as the other bar zones — item distribution, alignment, and style preset — rather than a special-cased renderer. Its default rendering (no options set) SHALL remain visually identical to today, including its surface background.

#### Scenario: Status bar default unchanged

- **WHEN** the `statusBar` zone has no per-zone options set
- **THEN** it SHALL render exactly as it does today (surface background, spacer-distributed content)

#### Scenario: Status bar respects distribution and alignment

- **WHEN** a user sets the `statusBar` zone distribution to `equalWidth` or its alignment
- **THEN** the top bar SHALL lay its widgets out accordingly, the same as a bottom bar zone

#### Scenario: Status bar respects style preset

- **WHEN** a user sets the `statusBar` zone style to a non-default preset
- **THEN** the top bar SHALL render with that style's background and contrast text

### Requirement: Reset a zone to default

The zone-options panel SHALL offer a "Reset to default" action (alongside "Clear zone") that restores a zone's widgets and per-zone options to the default layout. For zones absent from the default layout (e.g. `lowerMidBar`), reset SHALL leave the zone empty.

#### Scenario: Reset restores default widgets

- **WHEN** a user chooses "Reset to default" for a zone present in the default layout
- **THEN** that zone's widgets SHALL be restored to the default set
- **AND** the zone's distribution / alignment / style / offset / scale SHALL revert to their defaults
- **AND** other zones SHALL be unaffected

#### Scenario: Reset of an empty-by-default zone

- **WHEN** a user resets a zone that is empty in the default layout (e.g. `lowerMidBar`)
- **THEN** the zone SHALL become empty

### Requirement: Compact status bar populate preset

The zone-options "populate from preset" action SHALL include a **"Compact status bar"** preset that fills a zone with `machineStatus` · `temperature` · `steamTemperature` · `scaleWeight` · `batteryLevel` (all with `displayMode: icon`) and a spacer-centred `sleep` widget. It SHALL be offered for the `statusBar` zone and SHALL set the zone's widgets together, non-destructively to other zones. The preset SHALL render with the existing status-bar renderer (no zone-style requirement).

#### Scenario: Populate the status bar with the compact preset

- **WHEN** a user opens zone options for `statusBar` and chooses "Compact status bar"
- **THEN** the zone SHALL be filled with the readout widgets in `icon` display mode plus a centred Sleep
- **AND** the result SHALL reproduce the #1362 compact bar look
- **AND** only that zone's widgets SHALL change

#### Scenario: Populated widgets remain individually editable

- **WHEN** a zone has been populated with the compact preset
- **THEN** each widget SHALL remain individually removable, reorderable, and per-instance configurable (e.g. flipping a readout back to `text`, or toggling the Sleep quit option)
