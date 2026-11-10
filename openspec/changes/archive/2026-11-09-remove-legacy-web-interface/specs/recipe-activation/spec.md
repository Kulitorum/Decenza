# Spec Delta

## MODIFIED Requirements

### Requirement: Single activation path shared by all surfaces
Recipe activation SHALL be implemented once in the main controller, reusing the existing shot-load pipeline. QML pill taps and the MCP `recipe_activate` tool SHALL all call this single path. The active recipe id SHALL live in the DYE settings domain beside `activeBagId`. The bag stage SHALL select the recipe's linked bag directly, with no bean-identity resolution at activation time.

#### Scenario: Activation applies the full bundle
- **WHEN** a recipe is activated from any surface
- **THEN** the profile is loaded, the linked bag becomes active, the recipe equipment package is selected when present, dose/yield/temperature apply, grind/rpm apply from the recipe's own owned values (recipe-model), and steam settings are written

#### Scenario: Identical semantics across surfaces
- **WHEN** the same recipe is activated via native QML, browser QML, or MCP
- **THEN** the resulting app and machine state are identical

#### Scenario: Shot-load ordering
- **WHEN** a recipe is activated
- **THEN** the profile is loaded by title with a stored-JSON fallback, the bag is selected before DYE field writes, and the dose is written through the queued dose write

