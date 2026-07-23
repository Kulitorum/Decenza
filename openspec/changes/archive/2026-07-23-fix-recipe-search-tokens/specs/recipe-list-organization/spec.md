## MODIFIED Requirements

### Requirement: Recipe search
The recipes management page SHALL provide a search field that filters the displayed recipe cards to those whose name, coffee/bean identity (roaster name or coffee name), or profile title match the entered text. Matching SHALL be token-based: the query SHALL be split into tokens on whitespace, and a recipe SHALL match only when every token appears as a substring in its combined searchable text (name, roaster name, coffee name, and profile title). Before matching, the query and the searchable text SHALL both have the punctuation characters `-`, `/`, and `.` removed, so a token like `df` matches text like `D-Flow / Q` (which collapses to `dflow  q`). A single token MAY appear in one field while another token appears in a different field. Matching SHALL be case-insensitive and SHALL update as the user types (debounced). The field SHALL provide a clear control that empties the search and restores the full list. The search filter SHALL apply to both the active and archived recipe grids.

#### Scenario: Filtering by recipe name
- **WHEN** the user types text that appears in a recipe's name
- **THEN** that recipe's card remains visible and cards not matching the text (by name, coffee/bean, or profile) are hidden

#### Scenario: Filtering by coffee or profile
- **WHEN** the user types text matching a recipe's roaster name, coffee name, or profile title
- **THEN** that recipe's card remains visible even if the text does not appear in its name

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
- **WHEN** the search text matches no recipe cards
- **THEN** the page shows a "no matches" empty state instead of an empty grid
