# theme-font-size-defaults Specification

## Purpose

Keep the font size a user sees, the size an editor reports, and the size a log records as one
value. These three surfaces previously carried their own default tables, so they could disagree
without anything failing — and a user reporting a layout problem would describe sizes that did not
match what the UI was actually rendering. This capability also covers resetting those sizes without
collateral damage to the user's colours.

## Requirements
### Requirement: Single source of truth for font size defaults

Font size defaults SHALL be declared in exactly one place and consumed by every surface that needs
them — the QML theme, the web theme editor's reported values, and startup override logging.

Duplicated default tables allow the value the UI renders at and the value the editor reports to
drift apart, producing a discrepancy that is invisible until a user reports it.

#### Scenario: Editor reports the size the UI renders

- **WHEN** a font size role has never been customised
- **THEN** the value shown in the web theme editor is the same value the QML theme renders at,
  because both read the same declaration

#### Scenario: Changing a default updates every surface

- **WHEN** a default font size is changed in the codebase
- **THEN** the QML theme, the web theme editor, and override logging all reflect the new value
  without further edits

### Requirement: Font sizes can be reset without discarding colours

The web theme editor SHALL offer a reset that restores font sizes to their defaults while leaving
the user's theme colours unchanged.

#### Scenario: Fonts-only reset

- **WHEN** the user triggers the font sizes reset
- **THEN** every font size role returns to its default value
- **AND** the user's customised theme colours are unchanged

#### Scenario: Reset is reachable

- **WHEN** the user is viewing the font size controls
- **THEN** the fonts-only reset is presented with those controls, not only via a combined
  whole-theme reset

### Requirement: Destructive reset states what it clears

A reset action that discards user customisation SHALL name every category it discards before the
user confirms it.

#### Scenario: Combined reset names both categories

- **WHEN** the user triggers the reset that clears both theme colours and font sizes
- **THEN** the confirmation names both categories, so a user resetting one does not silently lose
  the other

### Requirement: In-app pointer to the web theme editor resolves

Where the application directs users to a web theme editor page, the referenced path SHALL match a
route the server actually serves.

#### Scenario: Hint path is served

- **WHEN** a user follows the in-app hint to the web theme editor
- **THEN** the referenced path loads the theme editor rather than returning a not-found response
