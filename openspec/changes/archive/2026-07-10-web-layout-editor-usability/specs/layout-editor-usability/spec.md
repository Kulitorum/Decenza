# layout-editor-usability Delta

## ADDED Requirements

### Requirement: Web editor page is usable at common viewport widths

The web layout editor page SHALL present its zone editors, preview, and library panel so that all are visible and operable at common desktop and tablet viewport widths. Instructional text SHALL NOT consume content-area space that displaces or shrinks the editing panels, and panels SHALL NOT overlap or cover one another at any supported width. At viewports too narrow to show panels side by side, the page SHALL reflow (stack or collapse panels) rather than overlap them.

#### Scenario: No dead space or crushed zones at desktop width

- **WHEN** the web layout editor is viewed at a typical desktop width (e.g. 1400px or wider)
- **THEN** the zone editors, preview, and library panel SHALL share the content area without a large unused region
- **AND** the zone cards SHALL NOT be compressed by non-editing content

#### Scenario: Panels do not overlap at tablet width

- **WHEN** the web layout editor is viewed at a narrower width (e.g. 1024px)
- **THEN** the library panel SHALL NOT cover the zone editors
- **AND** every zone card SHALL remain reachable and operable

### Requirement: Zone position and scale controls are labeled

The zone offset (move up/down) and zone scale (smaller/larger) controls SHALL identify their function to the user — via visible labels or tooltips in the web editor and accessible names in the in-app editor — so their purpose is discoverable without trial and error.

#### Scenario: Web offset and scale controls carry tooltips

- **WHEN** a user hovers the zone offset or scale controls in the web layout editor
- **THEN** a tooltip (or visible label) SHALL state what the control adjusts (vertical position or zone scale)

### Requirement: Layout edits preserve access to Settings

Layout mutations from any editing surface (in-app editor or web editor/API) SHALL NOT leave the home screen without at least one way to reach Settings (a `settings` widget or a custom widget with a navigate-to-settings action). When an edit would remove the last Settings access, the system SHALL restore one (for example by re-adding a Settings widget to a default zone).

#### Scenario: Web removal of the last Settings widget is compensated

- **WHEN** a web layout edit removes the last widget providing Settings access
- **THEN** the system SHALL ensure a Settings access widget is present on the home screen afterwards

#### Scenario: In-app guarantee is unchanged

- **WHEN** a user leaves the in-app layout editor with no Settings access configured
- **THEN** the system SHALL add a Settings widget as it does today

## MODIFIED Requirements

### Requirement: Editor guidance matches actual interactions

The layout editors SHALL present help/instruction text that accurately describes how the editor works after this change. The text SHALL NOT instruct users to reorder via selecting and tapping arrows, and SHALL NOT imply that only `custom` widgets are configurable. It SHALL mention dragging to reorder and the visible options affordance for configurable widgets. The in-app and web editors SHALL be consistent in the guidance they give. The web editor's guidance SHALL describe the options affordance as an activatable control (the gear button) and SHALL NOT describe an interaction the editor does not support.

#### Scenario: In-app instructions describe drag and options

- **WHEN** a user views the layout editor instruction text in-app
- **THEN** it SHALL describe reordering by dragging and configuring a widget via its visible options affordance
- **AND** it SHALL NOT reference move arrows or claim only Custom widgets are editable

#### Scenario: Web editor guidance is consistent

- **WHEN** a user views the web layout editor
- **THEN** its guidance SHALL describe the same interactions (drag to reorder, tap the gear to open options) as the in-app editor
- **AND** every interaction the guidance names SHALL actually work as described

### Requirement: Live home-screen preview in the in-app editor

The in-app layout editor SHALL show a live preview of the home screen rendered from the current layout configuration, using the same zone/widget components the real home screen uses so the preview is faithful. The web layout editor SHALL also show a live home-screen preview at the device aspect ratio, rendered from the same layout configuration data (zone membership and order, distribution/alignment/style, offsets, scales, and widget labels/colors); it MAY be a faithful approximation rather than pixel-identical, since it does not render the real QML components. Both previews SHALL update as the user adds, removes, reorders, or configures widgets.

#### Scenario: Preview reflects the current layout

- **WHEN** the in-app layout editor is open
- **THEN** a scaled preview of the home screen SHALL be shown, laying out the configured zones and widgets

#### Scenario: Preview updates live

- **WHEN** a user adds, removes, reorders, or reconfigures a widget
- **THEN** the preview SHALL update to reflect the change without leaving the editor

#### Scenario: Web preview shows the configured layout

- **WHEN** the web layout editor is open
- **THEN** a preview at the home-screen aspect ratio SHALL show the configured zones with their widgets in order, honoring zone distribution, alignment, style, offset, and scale

#### Scenario: Web preview updates after each edit

- **WHEN** a user makes any layout edit in the web editor
- **THEN** the web preview SHALL update to reflect the change without a page reload
