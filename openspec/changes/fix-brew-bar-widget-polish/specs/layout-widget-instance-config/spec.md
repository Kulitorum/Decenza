## ADDED Requirements

### Requirement: Configurable ratio suffix for the scale weight widget

The `scaleWeight` widget SHALL gain a per-instance `showRatio` boolean property controlling whether its weight reading is suffixed with the active brew-by-ratio value (`1:X.X`). The default SHALL preserve the widget's current behaviour (suffix shown when brew-by-ratio is active), so existing layouts are unchanged. When `showRatio` is disabled, the widget SHALL show only the weight (and its unit), letting layouts that already surface the ratio elsewhere — a `ratioQuickSelect` pill or the status bar — avoid showing it 2–3 times.

#### Scenario: Default preserves current behaviour

- **WHEN** a `scaleWeight` widget has no `showRatio` set (existing layouts)
- **THEN** it SHALL append the `1:X.X` ratio whenever brew-by-ratio is active, exactly as it does today

#### Scenario: Ratio suppressed

- **WHEN** `showRatio` is disabled on a `scaleWeight` instance and brew-by-ratio is active
- **THEN** the widget SHALL display only the weight and its unit, with no `1:X.X` suffix

#### Scenario: Ratio suffix irrelevant when not brewing by ratio

- **WHEN** brew-by-ratio is not active
- **THEN** the widget SHALL display only the weight regardless of the `showRatio` setting

#### Scenario: Option is selected in the editor

- **WHEN** a user opens a `scaleWeight` instance in either editor
- **THEN** the editor SHALL present a control to toggle the ratio suffix
- **AND** the chosen value SHALL persist for that instance only

### Requirement: Compact scale weight widget is self-identifying

A `scaleWeight` widget rendered in a compact/bar zone SHALL be visually identifiable as a scale reading without relying on its full-mode "Scale Weight" label. The widget SHALL convey its identity through its existing `icon` display mode (a scale icon ahead of the value), and the seeded bar presets that place a compact `scaleWeight` SHALL default it to a self-identifying presentation. This SHALL NOT introduce a new user-facing setting beyond the existing `displayMode`.

#### Scenario: Compact scale shows an identifying icon

- **WHEN** a `scaleWeight` widget is rendered in a compact/bar zone with `displayMode` set to `icon`
- **THEN** it SHALL render a scale icon ahead of the value so the number is recognizable as a scale weight

#### Scenario: Seeded bar presets are self-identifying

- **WHEN** the layout system seeds a bar zone that includes a compact `scaleWeight` widget
- **THEN** that seeded instance SHALL default to a self-identifying presentation (icon ahead of the value)

#### Scenario: Existing instances are unaffected

- **WHEN** an existing layout already contains a compact `scaleWeight` widget with its own `displayMode`
- **THEN** that instance SHALL continue to render according to its stored mode, unchanged
