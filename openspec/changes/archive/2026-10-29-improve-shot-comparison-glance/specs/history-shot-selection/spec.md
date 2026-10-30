# Spec Delta

## Purpose

Keep Shot History selection predictable and usable while people find and compare shots.

## ADDED Requirements

### Requirement: Selection does not move the history list

Selecting the first shot or clearing the last selection SHALL NOT change the list viewport's position or size, or move visible rows on screen solely because selection controls appear or disappear.

#### Scenario: First selection
- **WHEN** a user selects a shot in a scrolled history list
- **THEN** the visible rows stay in place and the selection count and actions become available

#### Scenario: Last selection cleared
- **WHEN** the user clears the last selected shot
- **THEN** the visible rows stay in place

### Requirement: Selection and search remain usable together

Shot History SHALL retain search, sorting, selection count, Clear, Delete and Compare while selection is active, including on narrow screens. Compare SHALL require at least two selected shots. Search and sort SHALL preserve selected IDs, including shots temporarily outside the results.

#### Scenario: Search while selecting
- **GIVEN** a selected shot
- **WHEN** a search hides that shot from the results
- **THEN** it remains selected and is counted until explicitly cleared or deleted

#### Scenario: Narrow screen
- **WHEN** selection is active on a narrow screen
- **THEN** the selection actions and search and sort remain reachable without overlapping or clipping
