## MODIFIED Requirements

### Requirement: Recipes idle widget mirrors the Beans button
The system SHALL provide a Recipes layout widget structurally mirroring the Beans widget: tap toggles a pill row of the most-recently-used non-archived recipes (MRU-ordered, no favorite flag), tapping a pill activates that recipe, and double-tap or long-press opens the Recipes management page. The active recipe's pill SHALL be highlighted. When no recipes exist, a plain tap SHALL go directly to the Recipes management page. The widget SHALL meet the accessibility rules that the Beans widget follows (focus trap in the pill popup, announcements, `AccessibleTapHandler`).

The pill row SHALL show at most five recipes at a time and SHALL paginate the full MRU list in pages of five via prev/next arrows. The previous arrow SHALL be shown only when a previous page exists (not on the first page) and the next arrow only when a further page exists (not on the last page); with five or fewer recipes neither arrow SHALL appear and the row SHALL be visually identical to the non-paginated row. Paging SHALL change only which five recipes are visible — it SHALL NOT activate a recipe, change the selection, or reorder the list. Opening the widget SHALL start on the first page (the most-recent five). When the recipe list changes, the current page SHALL be clamped to remain within range.

#### Scenario: Quick switch
- **WHEN** the user taps the Recipes widget and selects a pill
- **THEN** that recipe activates (full bundle incl. steam) and the pill row closes with the selection highlighted on next open

#### Scenario: Empty state
- **WHEN** the user taps the widget with zero recipes
- **THEN** the Recipes management page opens directly

#### Scenario: MRU ordering
- **WHEN** a recipe is activated
- **THEN** it moves to the front of the pill list, and the first page shows the five most recent

#### Scenario: Paging to reach older recipes
- **WHEN** the user has more than five recipes and taps the next arrow
- **THEN** the row shows the next five recipes in MRU order without activating any recipe or changing the current selection

#### Scenario: Arrows appear only as appropriate
- **WHEN** the recipe pill row is on the first page
- **THEN** the previous arrow SHALL be hidden
- **AND** the next arrow SHALL be shown only if more than five recipes exist

#### Scenario: Few recipes need no arrows
- **WHEN** five or fewer recipes exist
- **THEN** the pill row SHALL show no pagination arrows and appear exactly as the non-paginated row did
