## MODIFIED Requirements

### Requirement: Tweaks write through; ingredient swaps deactivate
While a recipe is active, changes to dose, yield, temperature override, steam values, milk weight, or the hot-water selection (the chosen water vessel and its values) SHALL write through to the active recipe (no dirty state, matching bag semantics). Grind changes SHALL route to the bag when inherited and to the pin when pinned. Manually changing the profile, the active bag/bean, or the equipment package SHALL deactivate the recipe (event-based, no timers); the recipe itself SHALL be unchanged by deactivation.

#### Scenario: Dose tweak while active
- **WHEN** the user changes dose while a recipe is active
- **THEN** the recipe's stored dose updates

#### Scenario: Hot-water tweak while active
- **WHEN** the user changes the selected water vessel (or its values) while a recipe is active
- **THEN** the recipe's stored hot-water block updates to the new vessel snapshot, and re-selecting the same vessel does not deactivate the recipe

#### Scenario: Inherited grind tweak while active
- **WHEN** the user adjusts grind while a recipe with inherited grind is active
- **THEN** the bag's grind updates (sibling recipes follow) and the recipe stores no pin

#### Scenario: Profile swap deactivates
- **WHEN** the user manually selects a different profile while a recipe is active
- **THEN** the active recipe clears, its pill deselects, and the recipe's stored fields are unchanged

## ADDED Requirements

### Requirement: Hot-water settings apply on recipe activation
Activating a recipe with a hot-water block SHALL re-select the snapshotted water vessel into the live brew settings and apply its values (temperature, amount by weight or volume, and flow) via the existing hot-water settings path, so the machine is configured to dispense the specified hot water. If the named vessel preset no longer exists, activation SHALL recreate it from the by-value snapshot rather than fail. Unlike the steam heater, hot water needs no multi-minute pre-warm, so activation SHALL NOT introduce any heater hold for the hot-water block. A recipe with a hot-water block but no milk block SHALL NOT force the steam heater on. Activation SHALL remain a single path shared by all surfaces and SHALL apply the hot-water block alongside profile, bean, equipment, grind, dose/yield/temperature, and steam.

#### Scenario: Activation configures hot water from the vessel
- **WHEN** a recipe with a hot-water block is activated from any surface
- **THEN** the snapshotted vessel becomes the selected water vessel and its temperature, amount, and flow are applied to the live hot-water settings

#### Scenario: Milk-less hot-water recipe does not warm the steam heater
- **WHEN** a recipe with a hot-water block and no milk block is activated and the user has `keepSteamHeaterOn` disabled
- **THEN** the steam heater is not forced on by the hot-water block

#### Scenario: Missing vessel preset falls back to the snapshot
- **WHEN** a recipe's hot-water block references a water-vessel preset that has since been deleted
- **THEN** activation applies the by-value snapshot stored on the recipe rather than failing
