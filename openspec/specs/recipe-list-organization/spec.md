# recipe-list-organization Specification

## Purpose
Search and sort for the recipe list, in the app and on the web `/recipes` page, so a long recipe library stays easy to find things in.

## Requirements

### Requirement: Recipe search
The recipes page SHALL provide a search field filtering recipe cards by name, coffee (roaster or coffee name), profile title and drink-type label. A recipe SHALL match only when every whitespace token is a case-insensitive substring of the combined searchable text; tokens MAY match different fields. `-`, `/` and `.` SHALL be ignored on both sides. A clear control SHALL restore the full list. This SHALL apply to active and archived grids.

#### Scenario: Filtering by recipe name
- **WHEN** the user types text that appears in a recipe's name
- **THEN** that recipe's card remains visible and cards not matching the text (by name, coffee/bean, or profile) are hidden

#### Scenario: Filtering by coffee or profile
- **WHEN** the user types text matching a recipe's roaster name, coffee name, or profile title
- **THEN** that recipe's card remains visible even if the text does not appear in its name

#### Scenario: Filtering by drink type
- **WHEN** the user types a drink-type word (for example `latte` or `tea`)
- **THEN** recipes of that drink type remain visible and recipes of other drink types are hidden

#### Scenario: Multi-token query spanning coffee and profile
- **WHEN** the user types a query with two tokens where one token matches the recipe's coffee/bean identity and the other matches its profile title (for example `Yirg Df` against a Yirgacheffe recipe on a `D-Flow / Q` profile)
- **THEN** that recipe's card remains visible, because every token is found across the recipe's combined searchable text

#### Scenario: Abbreviation across a punctuation boundary
- **WHEN** the user types a token that only matches once punctuation is removed (for example `df` against a `D-Flow / Q` profile title, where `D-Flow` collapses to `dflow`)
- **THEN** that recipe's card remains visible, because `-`, `/`, and `.` are removed from both the query and the searchable text before matching

#### Scenario: All tokens required
- **WHEN** the user types a multi-token query where at least one token matches no field of a recipe
- **THEN** that recipe's card is hidden, because every token must be found for the recipe to match

#### Scenario: Case-insensitive matching
- **WHEN** the user types text in any letter case
- **THEN** matching ignores case differences between the query and the recipe fields

#### Scenario: Clearing the search
- **WHEN** the user activates the clear control
- **THEN** the search text is emptied and every recipe card (subject to the active/archived section) is shown again

#### Scenario: No matches
- **WHEN** the search text matches no recipe card, active or archived
- **THEN** the page shows a "no matches" empty state instead of an empty grid

#### Scenario: Matches only among archived recipes
- **WHEN** the search text matches only archived recipes
- **THEN** the page does not say nothing matches, and "Show archived (N)" counts the matches

#### Scenario: Search updates during IME composition
- **WHEN** the user types into the search field while the input method still holds an uncommitted word
- **THEN** the list SHALL filter on every edit, including the in-progress word

### Requirement: Recipe sort
The recipes page SHALL let the user order the cards by date used, date created, coffee/bean, profile or name, and toggle ascending or descending. The order SHALL apply to both the active and archived grids and to search results.

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
The recipes management page SHALL persist the chosen sort key and sort direction across app sessions. The web `/recipes` page SHALL open in the same saved order and offer the same sort keys, and a sort chosen there SHALL be saved as the same preference. A saved sort key the page does not offer SHALL show and sort as date used. The search text SHALL NOT be persisted and SHALL start empty on each visit to the page.

#### Scenario: Sort preference restored
- **WHEN** the user sets a sort key and direction and later reopens the app or the recipes page
- **THEN** the previously chosen sort key and direction are applied

#### Scenario: Search resets on entry
- **WHEN** the user reopens the recipes page
- **THEN** the search field is empty and the full list (in the persisted sort order) is shown

#### Scenario: Web page shares the sort
- **WHEN** the user picks a sort on the web `/recipes` page and then opens Recipes in the app
- **THEN** the app shows the recipes in that order
