## MODIFIED Requirements

### Requirement: Recipes idle widget mirrors the Beans button
The system SHALL provide a Recipes layout widget structurally mirroring the Beans widget: tap toggles a pill row of the most-recently-used non-archived recipes (MRU-ordered, no favorite flag), tapping a pill activates that recipe, and double-tap or long-press opens the Recipes management page. The active recipe's pill SHALL be highlighted. When no recipes exist, a plain tap SHALL go directly to the Recipes management page. The widget SHALL meet the accessibility rules that the Beans widget follows (focus trap in the pill popup, announcements, `AccessibleTapHandler`).

The pill row SHALL show as many recipes as comfortably fit within **at most two rows** at the row's current available width, and SHALL paginate the remainder of the MRU list via prev/next arrows. The number of recipes per page SHALL be computed **live** from the actual (measured) pill widths — so longer descriptive names mean fewer pills per page — and MAY differ from one page to the next. The previous arrow SHALL be shown only when a previous page exists (not on the first page) and the next arrow only when a further page exists (not on the last page); when every recipe fits within two rows neither arrow SHALL appear and the row SHALL be visually identical to the non-paginated row. Paging SHALL change only which recipes are visible — it SHALL NOT activate a recipe, change the selection, or reorder the list. Opening the widget SHALL start on the first page (the most-recent recipes that fit). When the recipe list changes, the current page SHALL be clamped to remain within range.

#### Scenario: Quick switch
- **WHEN** the user taps the Recipes widget and selects a pill
- **THEN** that recipe activates (full bundle incl. steam) and the pill row closes with the selection highlighted on next open

#### Scenario: Empty state
- **WHEN** the user taps the widget with zero recipes
- **THEN** the Recipes management page opens directly

#### Scenario: MRU ordering
- **WHEN** a recipe is activated
- **THEN** it moves to the front of the pill list, and the first page shows the most-recent recipes that fit within two rows

#### Scenario: Page size follows name length
- **WHEN** recipes have long descriptive names (bean + type + profile) such that fewer fit within two rows
- **THEN** the first page shows only as many as fit, the rest paginate, and different pages MAY show different numbers of recipes

#### Scenario: Paging to reach older recipes
- **WHEN** more recipes exist than fit within two rows and the user taps the next arrow
- **THEN** the row shows the next group of recipes (however many fit) in MRU order without activating any recipe or changing the current selection

#### Scenario: Arrows appear only as appropriate
- **WHEN** the recipe pill row is on the first page
- **THEN** the previous arrow SHALL be hidden
- **AND** the next arrow SHALL be shown only if more recipes exist than fit within two rows

#### Scenario: Everything fits needs no arrows
- **WHEN** every recipe fits within two rows at the current width
- **THEN** the pill row SHALL show no pagination arrows and appear exactly as the non-paginated row did

#### Scenario: Never more than two rows
- **WHEN** any page of recipe pills is shown
- **THEN** the pills SHALL occupy at most two rows
