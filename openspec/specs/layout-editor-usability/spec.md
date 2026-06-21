# layout-editor-usability Specification

## Purpose
TBD - created by archiving change improve-layout-editor-usability. Update Purpose after archive.
## Requirements
### Requirement: Editor guidance matches actual interactions

The layout editors SHALL present help/instruction text that accurately describes how the editor works after this change. The text SHALL NOT instruct users to reorder via selecting and tapping arrows, and SHALL NOT imply that only `custom` widgets are configurable. It SHALL mention dragging to reorder and the visible options affordance for configurable widgets. The in-app and web editors SHALL be consistent in the guidance they give.

#### Scenario: In-app instructions describe drag and options

- **WHEN** a user views the layout editor instruction text in-app
- **THEN** it SHALL describe reordering by dragging and configuring a widget via its visible options affordance
- **AND** it SHALL NOT reference move arrows or claim only Custom widgets are editable

#### Scenario: Web editor guidance is consistent

- **WHEN** a user views the web layout editor
- **THEN** its guidance SHALL describe the same interactions (drag to reorder, open options) as the in-app editor

### Requirement: Confirmation for destructive layout actions

The editors SHALL guard destructive layout actions with a confirmation step so a single tap cannot irreversibly discard a user's configured layout. Resetting the layout to default SHALL require explicit confirmation before it takes effect. Removing a configured widget instance (a widget whose type has options, or which holds non-default per-instance settings) SHALL require confirmation; removing a widget with no configuration MAY remain a single action. This applies in both editors where the corresponding action exists.

#### Scenario: Reset to default asks for confirmation

- **WHEN** a user activates "Reset to Default"
- **THEN** the editor SHALL ask the user to confirm before discarding the current layout
- **AND** if the user cancels, the current layout SHALL be left unchanged

#### Scenario: Removing a configured widget asks for confirmation

- **WHEN** a user removes a widget instance that has options or non-default per-instance settings
- **THEN** the editor SHALL ask the user to confirm before removing it
- **AND** if the user cancels, the widget SHALL remain in place with its settings intact

#### Scenario: Removing an unconfigured widget is direct

- **WHEN** a user removes a widget instance that has no options and no per-instance settings
- **THEN** the editor MAY remove it without a confirmation step

### Requirement: Discoverable, sorted widget picker

The add-widget picker SHALL organize widgets into labeled categories (for example actions, readouts, utility, screensavers) with visible group headings, and SHALL order widgets within each category in a predictable way (for example alphabetically by display name). The picker SHALL provide a way to narrow the list by typing (a search/filter field). This applies to both editors.

#### Scenario: Picker shows labeled categories

- **WHEN** a user opens the add-widget picker
- **THEN** widgets SHALL be presented under visible category headings
- **AND** widgets within a category SHALL appear in a consistent, predictable order

#### Scenario: Picker can be filtered by typing

- **WHEN** a user types into the picker's filter field
- **THEN** the list SHALL narrow to widgets whose name matches the typed text
- **AND** clearing the filter SHALL restore the full categorized list

#### Scenario: Both editors group and sort consistently

- **WHEN** a user opens the picker in either the in-app or the web editor
- **THEN** the same categories and within-category ordering SHALL be used

### Requirement: Accessible, consistent editor controls

Interactive controls in the layout editor (in the files modified by this change) SHALL be operable by assistive technology and SHALL follow the project's UI conventions: each control SHALL expose an accessible role, name, focusability, and press action, and SHALL use SVG icon assets rather than Unicode glyphs as icons. Raw `Rectangle`+`MouseArea` controls SHALL be replaced with the shared accessible components (e.g. `AccessibleButton` / `StyledIconButton`) where a control is added or modified.

#### Scenario: Every editor control is screen-reader operable

- **WHEN** a screen-reader user navigates the layout editor
- **THEN** each interactive control (reorder, remove, open options, add widget, zone options, zone move/scale) SHALL announce a role and name and SHALL be activatable

#### Scenario: Icons are SVG assets, not glyphs

- **WHEN** an editor control displays an icon
- **THEN** the icon SHALL be an SVG asset from `qrc:/icons/`, not a Unicode glyph character

### Requirement: Live home-screen preview in the in-app editor

The in-app layout editor SHALL show a live preview of the home screen rendered from the current layout configuration, using the same zone/widget components the real home screen uses so the preview is faithful. The preview SHALL update as the user adds, removes, reorders, or configures widgets. (The web editor is exempt: its editing surface is already viewed on the device running the app.)

#### Scenario: Preview reflects the current layout

- **WHEN** the in-app layout editor is open
- **THEN** a scaled preview of the home screen SHALL be shown, laying out the configured zones and widgets

#### Scenario: Preview updates live

- **WHEN** a user adds, removes, reorders, or reconfigures a widget
- **THEN** the preview SHALL update to reflect the change without leaving the editor

### Requirement: Stable, discoverable chip remove control

Selecting a widget chip SHALL NOT change the chip's size or cause the zone's chips to reflow ("jump"). The remove control SHALL be discoverable without first selecting the chip — it SHALL be revealed on hover (pointer devices) and on selection (touch), rather than only after a click.

#### Scenario: Selecting a chip does not move other chips

- **WHEN** a user selects a widget chip
- **THEN** the chip's size SHALL stay the same and other chips SHALL NOT shift position

#### Scenario: Remove is revealed without a click on pointer devices

- **WHEN** a user hovers a widget chip with a pointer
- **THEN** the remove control SHALL be revealed without requiring a prior click

### Requirement: Only one widget-options editor open at a time

When a user opens the options editor for a widget, any previously open widget-options editor SHALL close, so that exactly one options editor is active at a time and the user is never faced with stacked or ambiguous editors.

#### Scenario: Opening options closes a prior options editor

- **WHEN** a widget-options editor is open and the user opens the options editor for a different widget
- **THEN** the previously open options editor SHALL close before (or as) the new one opens

