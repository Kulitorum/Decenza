## Why

The Beans page now reads "Add [Bag of Coffee] [Bag of Tea]", while Recipes and Equipment kept filled blue "Add Recipe" / "+ Add Equipment" buttons, so the three inventory pages looked inconsistent. Separately, every page drew a focus ring on its first button as it opened, though nobody had used the keyboard.

## What Changes

- Recipes and Equipment use the Beans pattern: an "Add" label, then a dark button naming the kind with its icon ("Recipe" with a cup, "Equipment" with a grinder). The label is one shared component. Same on the web pages.
- The focus ring shows only for keyboard focus (Qt's `visualFocus`), or on any focus while a screen reader is on. Key handlers that move focus pass `Qt.TabFocusReason` so keyboard users keep it. ACCESSIBILITY.md records the rule.

## Capabilities

### Modified Capabilities
- `equipment-inventory-view`: the create control's wording and style.
- `switch-equipment-dialog`: the name of the control that opens it from the Equipment window.

## Impact

`qml/components/AddLabel.qml` (new), `FocusIndicator.qml`, `BeanInfoPage.qml`, `RecipesPage.qml`, `EquipmentPage.qml`, the Steam / Hot Water / Flush key handlers, `shotserver_recipes.cpp`, `shotserver_equipment.cpp`, `docs/CLAUDE_MD/ACCESSIBILITY.md`; wiki manual (button names).
