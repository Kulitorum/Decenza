# recipe-quick-switch Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
### Requirement: Recipes idle widget mirrors the Beans button
The system SHALL provide a Recipes layout widget structurally mirroring the Beans widget: tap toggles a pill row of the five most-recently-used non-archived recipes (MRU-ordered, no favorite flag), tapping a pill activates that recipe, and double-tap or long-press opens the Recipes management page. The active recipe's pill SHALL be highlighted. When no recipes exist, a plain tap SHALL go directly to the Recipes management page. The widget SHALL meet the accessibility rules that the Beans widget follows (focus trap in the pill popup, announcements, `AccessibleTapHandler`).

#### Scenario: Quick switch
- **WHEN** the user taps the Recipes widget and selects a pill
- **THEN** that recipe activates (full bundle incl. steam) and the pill row closes with the selection highlighted on next open

#### Scenario: Empty state
- **WHEN** the user taps the widget with zero recipes
- **THEN** the Recipes management page opens directly

#### Scenario: MRU ordering
- **WHEN** a recipe is activated
- **THEN** it moves to the front of the pill list, and only the five most recent appear

### Requirement: Bean button coherence
Activating a recipe SHALL set the active bag (via bean resolution), so the Beans widget's pill selection reflects the recipe's bean without additional wiring. Deactivation by ingredient swap SHALL deselect the recipe pill while leaving bag selection as the user set it.

#### Scenario: Bag pill follows recipe
- **WHEN** a recipe linked to bean X is activated
- **THEN** the Beans widget shows bean X's bag as selected

### Requirement: Management page
The Recipes management page SHALL list all non-archived recipes with create, edit, clone, and archive/delete actions (delete only for recipes with no shots), and provide access to archived recipes for reference.

#### Scenario: Archive from management page
- **WHEN** the user archives a used recipe
- **THEN** it disappears from the list default view and the MRU pills, and remains visible in shot history provenance

