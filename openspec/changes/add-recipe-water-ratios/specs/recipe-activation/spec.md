# Spec Delta

## MODIFIED Requirements

### Requirement: Hot-water settings apply on recipe activation
Activating a recipe with hot water SHALL select its snapshotted vessel and apply temperature, flow and its fixed or yield-relative amount through the existing hot-water path. Ratio water SHALL resolve after the recipe's espresso target is established. A missing preset SHALL be recreated as a fixed preset using the snapshot's resolved amount. Hot water SHALL take no steam-heater hold or force the heater on for a milk-less recipe.

#### Scenario: Activation configures hot water from the vessel
- **WHEN** a recipe with a hot-water block is activated from any surface
- **THEN** the snapshotted vessel becomes selected and its temperature, resolved amount and flow apply to the live hot-water settings

#### Scenario: Milk-less hot-water recipe does not warm the steam heater
- **WHEN** a recipe with a hot-water block and no milk block is activated and the user has `keepSteamHeaterOn` disabled
- **THEN** the steam heater is not forced on by the hot-water block

#### Scenario: Missing vessel preset falls back to the snapshot
- **WHEN** a recipe's hot-water block references a water-vessel preset that has since been deleted
- **THEN** activation applies the by-value snapshot stored on the recipe rather than failing
- **AND** a ratio snapshot recreates a fixed preset at its currently resolved grams while retaining the ratio on the recipe

#### Scenario: Hot water applied with the rest of the bundle
- **WHEN** a recipe with a hot-water block is activated
- **THEN** the hot-water block applies alongside profile, bean, equipment, grind, dose/yield/temperature and steam through the same single activation path

#### Scenario: Resolve after the new recipe's yield lands
- **WHEN** a recipe with an 18 g dose, 1:2 espresso yield and 2.5× added water replaces a recipe targeting 50 g espresso
- **THEN** its espresso target is 36 g and added water is 90 g
- **AND** the old target never produces a retained 125 g water target

## ADDED Requirements

### Requirement: Ratio water follows the effective espresso target
While a ratio-water recipe is active, its added-water target SHALL update when its effective espresso target changes, including per-brew yield overrides and dose changes that move a ratio-based yield. Activation, restoration or editing the ratio SHALL initialize or refresh the result. The saved ratio and shared preset's fixed amount SHALL NOT be rewritten by this calculation.

#### Scenario: Change target yield
- **WHEN** an active recipe uses 2.5× added water and its espresso target changes from 40 g to 42 g
- **THEN** the added-water target changes from 100 g to 105 g
- **AND** the recipe's ratio remains 2.5

#### Scenario: Dose changes a ratio-based espresso target
- **WHEN** an active recipe uses 1:2 espresso yield and 2.5× added water and its dose changes from 18 g to 20 g
- **THEN** the espresso target changes from 36 g to 40 g and added water from 90 g to 100 g

#### Scenario: Dose does not change an absolute espresso target
- **WHEN** the same dose change occurs with an absolute espresso target of 40 g
- **THEN** added water stays at 100 g

#### Scenario: Override does not persist a recipe edit
- **WHEN** the user temporarily changes the active recipe's espresso target to 44 g
- **THEN** its 2.5× added water becomes 110 g for that setup
- **AND** neither its stored espresso yield nor its stored water ratio is changed by the override

#### Scenario: Edit only the water ratio
- **WHEN** the active recipe's ratio is explicitly edited from 2.5 to 3 with the espresso target staying at 40 g
- **THEN** its added-water target becomes 120 g without requiring reactivation

#### Scenario: Restore after restart
- **WHEN** a ratio-water recipe is restored as active after restart
- **THEN** its water target is initialized from the restored effective espresso target rather than a stale cached water amount

### Requirement: Water resolution is based on target changes rather than drinks
Ratio-water calculation SHALL use the current intended espresso target, never measured shot output, tare readings or drip settling. Repeating a drink with unchanged recipe water settings and espresso target SHALL reuse its resolved water target. An in-progress water pour SHALL NOT be retargeted automatically; a target change during that pour SHALL apply after the drink completes.

#### Scenario: Actual shot differs from target
- **WHEN** an espresso targeting 40 g finishes at 41.7 g with 2.5× added water configured
- **THEN** the water target remains 100 g

#### Scenario: Repeat an unchanged drink
- **WHEN** another drink starts with the same 40 g target and 2.5× water specification
- **THEN** it reuses 100 g water without recomputing or rewriting the preset merely because a drink started

#### Scenario: Yield changes during water dispensing
- **WHEN** the water is already dispensing toward 100 g and the configured espresso target changes to 42 g
- **THEN** that water pour retains its target and the next drink uses 105 g

### Requirement: Ratio water cannot pour with an invalid resolved target
Ratio-water results SHALL be rounded to whole grams and validated against supported weight limits without silent clipping. If the effective espresso target becomes unusable or the result unsupported, recipe water SHALL be unavailable with an explanatory error until corrected. A prior valid cached amount SHALL NOT be used as a fallback. Standalone fixed vessels SHALL remain usable.

#### Scenario: Round consistently
- **WHEN** the espresso target is 41 g and the water ratio is 2.5
- **THEN** added water resolves to 103 g in previews and machine settings

#### Scenario: Target change makes the ratio unusable
- **WHEN** a target change makes ratio water exceed the supported weight limit
- **THEN** starting that recipe's water, including its automatic follow-on, is refused with an explanation
- **AND** correcting the target or ratio makes it available again

### Requirement: Recipe water settings stay isolated from standalone presets
Automatic ratio updates SHALL affect only the active recipe's effective water settings. Selecting a different vessel SHALL keep existing recipe-deactivation behavior. Re-selecting the recipe's own vessel SHALL retain its recipe water specification. Standalone editing SHALL continue operating on fixed preset values even when a recipe uses that vessel by ratio.

#### Scenario: Re-select the recipe's own vessel
- **WHEN** a recipe uses a shared 200 g preset at 2.5× of a 40 g espresso target and the user re-selects that vessel
- **THEN** the recipe stays active and its effective added water stays 100 g

#### Scenario: Select a different standalone vessel
- **WHEN** the user selects a different vessel while a ratio-water recipe is active
- **THEN** the recipe deactivates and the selected vessel's fixed settings apply
- **AND** later espresso target changes do not change that standalone vessel's amount
