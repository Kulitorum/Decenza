## ADDED Requirements

### Requirement: Recipes-first default idle layout

The default idle-page layout SHALL place, in the `centerTop` zone, exactly: **Recipes, Beans, Steam, Hot Water** (in that order); in the `centerMiddle` zone: the Shot Plan widget; in the `bottomLeft` zone: Sleep; and in the `bottomRight` zone: **Flush, History, Equipment, Profiles (type `espresso`), Settings** (in that order). The status bar, `centerStatus`, and `lowerMidBar` zone defaults SHALL be unchanged from their current composition. The Profiles button and Flush SHALL NOT appear in the default center row, and Auto-Favorites SHALL NOT appear anywhere in the default layout.

#### Scenario: Fresh install gets the recipes-first layout

- **WHEN** the app starts with no stored layout configuration
- **THEN** the idle page shows Recipes, Beans, Steam, and Hot Water as the center action row, with Sleep bottom-left and Flush, History, Equipment, Profiles, Settings bottom-right

#### Scenario: No Auto-Favorites in the default

- **WHEN** the default layout is generated
- **THEN** no zone contains an `autofavorites` item (the Auto-Favorites page remains reachable for layouts that already include the widget)

### Requirement: Reset to default applies the recipes-first layout

The whole-layout "Reset to default" actions — in the in-app layout settings and in the web layout editor — SHALL replace the stored layout with the recipes-first default composition.

#### Scenario: In-app reset

- **WHEN** the user confirms "Reset to default" in the layout settings
- **THEN** the stored layout is replaced with the recipes-first default and the idle page re-renders accordingly

#### Scenario: Web editor reset

- **WHEN** the user clicks "Reset to Default" in the web layout editor
- **THEN** the same recipes-first default is applied (both paths call the same reset)

### Requirement: Injection migrations do not distort the new default

The pre-existing layout injection migrations (equipment, recipes) SHALL remain no-ops on the new default layout: since the default already contains both widgets, loading a freshly reset layout SHALL NOT insert duplicates or reorder items.

#### Scenario: Reset layout survives a reload unchanged

- **WHEN** the layout is reset to default and then reloaded from storage
- **THEN** the zone contents are identical to the default composition (no injected duplicates)
