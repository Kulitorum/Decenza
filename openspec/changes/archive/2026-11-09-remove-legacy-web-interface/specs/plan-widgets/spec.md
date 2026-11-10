# Spec Delta

## MODIFIED Requirements

### Requirement: Recipe item defaults off and is offered by both layout editors
The Recipe (`recipe`) display item SHALL default OFF. It SHALL NOT appear in the canonical default item order, and no widget configuration saved before this change SHALL gain it. It SHALL appear only when a user adds it to an instance's `shotPlanItems` list. The shared item catalog (`allKeys`) SHALL include `recipe`.

#### Scenario: Recipe is off in the default layout

- **WHEN** a Shot Plan widget uses the canonical default item order (no `shotPlanItems` saved, or the pre-change defaults)
- **THEN** the recipe name is not shown, and the widget renders exactly as before this change

#### Scenario: Recipe appears in both editors' available items

- **WHEN** a user opens the in-app Shot Plan Settings, or the shared browser layout editor, for a Shot Plan widget that does not yet show Recipe
- **THEN** "Recipe" is offered in the Available items and can be added to the Shown list, and once shown can be reordered and removed like any other item


### Requirement: Both layout editors offer the Recipe item
Both layout editors SHALL offer `recipe` as an addable and removable item with a translatable "Recipe" label: the in-app Shot Plan Settings chip editor and the shared browser layout editor. The two editors SHALL stay in sync on the item set and its label.

#### Scenario: Web editor offers Recipe
- **WHEN** a user opens the shared browser layout editor for a Shot Plan widget
- **THEN** "Recipe" is offered in the same item set and label as the in-app editor

