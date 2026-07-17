# recipe-list-organization Specification

## Purpose
TBD - created by archiving change add-recipe-search-sort. Update Purpose after archive.
## Requirements
### Requirement: Recipe search
The recipes management page SHALL provide a search field that filters the displayed recipe cards to those whose name, coffee/bean identity (roaster name or coffee name), or profile title contains the entered text. Matching SHALL be case-insensitive and SHALL update as the user types (debounced). The field SHALL provide a clear control that empties the search and restores the full list. The search filter SHALL apply to both the active and archived recipe grids.

#### Scenario: Filtering by recipe name
- **WHEN** the user types text that appears in a recipe's name
- **THEN** that recipe's card remains visible and cards not matching the text (by name, coffee/bean, or profile) are hidden

#### Scenario: Filtering by coffee or profile
- **WHEN** the user types text matching a recipe's roaster name, coffee name, or profile title
- **THEN** that recipe's card remains visible even if the text does not appear in its name

#### Scenario: Case-insensitive matching
- **WHEN** the user types text in any letter case
- **THEN** matching ignores case differences between the query and the recipe fields

#### Scenario: Clearing the search
- **WHEN** the user activates the clear control
- **THEN** the search text is emptied and every recipe card (subject to the active/archived section) is shown again

#### Scenario: No matches
- **WHEN** the search text matches no recipe cards
- **THEN** the page shows a "no matches" empty state instead of an empty grid

### Requirement: Recipe sort
The recipes management page SHALL provide a control to choose the order of the recipe cards. The available sort keys SHALL be: date used, date created, coffee/bean, profile, and name. The page SHALL also provide a control to toggle between ascending and descending order. The chosen order SHALL apply to both the active and archived recipe grids. Sorting SHALL be combined with the active search filter (the visible cards are the search matches, in the chosen order).

#### Scenario: Sorting by a chosen key
- **WHEN** the user selects a sort key
- **THEN** the recipe cards are reordered by that key in the current direction

#### Scenario: Toggling direction
- **WHEN** the user toggles the sort direction
- **THEN** the recipe cards reverse between ascending and descending order for the current sort key

#### Scenario: Default order
- **WHEN** the user has never chosen a sort order
- **THEN** the cards are ordered by date used, most recent first, matching the page's prior behavior

#### Scenario: Sort applies within search results
- **WHEN** a search filter is active and a sort key or direction is set
- **THEN** the cards that match the search are shown in the chosen sort order

### Requirement: Sort preference persistence
The recipes management page SHALL persist the chosen sort key and sort direction across app sessions. The search text SHALL NOT be persisted and SHALL start empty on each visit to the page.

#### Scenario: Sort preference restored
- **WHEN** the user sets a sort key and direction and later reopens the app or the recipes page
- **THEN** the previously chosen sort key and direction are applied

#### Scenario: Search resets on entry
- **WHEN** the user reopens the recipes page
- **THEN** the search field is empty and the full list (in the persisted sort order) is shown
