## Purpose

Defines how the favorites list is ordered — by the user's hand, alphabetically, or by recent use — and how existing users keep the order they built.

## ADDED Requirements

### Requirement: Favorites order setting
The app SHALL persist a favorites order mode with values `custom`, `alpha` and `usage`. When the setting is absent it SHALL resolve to `custom` if the favorites list is non-empty and to `usage` otherwise, without rewriting the favorites list.

#### Scenario: Existing user keeps order
- **WHEN** an upgraded install has favorites and no order setting
- **THEN** the mode resolves to `custom` and the favorites list order is byte-for-byte unchanged

#### Scenario: New user gets usage order
- **WHEN** an install has no favorites and no order setting
- **THEN** the mode resolves to `usage`

### Requirement: Stored list is the display order
Every consumer of the favorites list — the idle-page profile pills in both rendering paths and the picker's Favorites view — SHALL display the stored list order as-is. The mode SHALL govern who writes that order: `custom` — only the reorder dialog; `alpha` — the app re-sorts by title (case-insensitive, locale-aware) whenever a favorite is added or renamed; `usage` — the app re-sorts by each favorite's most recent shot descending, never-used favorites last alphabetically, at startup and after every shot save. The selected-favorite index SHALL keep pointing at the same profile across any re-sort.

#### Scenario: Usage mode after a shot
- **WHEN** the mode is `usage` and a shot completes on a favorite that was third
- **THEN** after the save that favorite is first in the stored list and first among the idle pills

#### Scenario: Selection survives re-sort
- **WHEN** a re-sort moves the currently selected favorite
- **THEN** the idle page still marks the same profile as selected

#### Scenario: Custom is never rewritten
- **WHEN** the mode is `custom` and a shot completes
- **THEN** the favorites order is unchanged

### Requirement: Reorder dialog
Choosing Custom… from the picker's sort control (Favorites on) SHALL open a dialog listing the favorites with drag handles for reordering and a remove control per row, styled with the app theme. Confirming SHALL write the new order and set the mode to `custom`. Choosing Custom… while the mode is already `custom` SHALL reopen the dialog.

#### Scenario: Drag to reorder
- **WHEN** the user drags a favorite from fifth to first and confirms
- **THEN** the stored list places it first, the mode is `custom`, and the idle pills show it first

### Requirement: Mode switch semantics
Switching from `custom` to `alpha` or `usage` SHALL re-sort immediately. Switching back to `custom` SHALL keep whatever order is current at that moment; the earlier hand order is not restored.

#### Scenario: Round trip
- **WHEN** the user switches custom → usage → custom
- **THEN** the list stays in the usage order it had when the switch back happened
