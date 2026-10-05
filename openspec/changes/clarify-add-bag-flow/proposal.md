## Why

User feedback on adding a bag: "Add Coffee" reads as adding a bean, not a bag; the search field's only hint disappears once you type; and "Enter manually" throws away the coffee name you just searched for.

## What Changes

- The Beans page (app and web) buttons read **Bag of Coffee** and **Bag of Tea**.
- The Change Beans search shows a label that stays visible while typing, naming what it searches (past tea bags in tea mode).
- **Enter manually** prefills the coffee name with the text you searched for.

Not changed: a separate bag name distinct from the coffee (the bag is identified by roaster + coffee, and that is Visualizer's bag name too), and the prefilled Bean Base link search in the form (kept by the existing requirement so a bag links in one tap).

## Capabilities

### Modified Capabilities
- `bag-inventory-view`: button labels.
- `change-beans-dialog`: persistent search label; manual entry keeps the typed text; tea entry point name.
- `shotserver-bags`: web button labels.

## Impact

`qml/pages/BeanInfoPage.qml`, `qml/components/ChangeBeansDialog.qml`, `src/network/shotserver_bags.cpp`; wiki manual (button names).
