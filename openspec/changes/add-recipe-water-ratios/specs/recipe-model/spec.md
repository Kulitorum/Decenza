# Spec Delta

## MODIFIED Requirements

### Requirement: Hot-water block is an opt-in water-vessel snapshot
A recipe MAY carry a hot-water block; `hasWater` enables it and requires a vessel. The block SHALL snapshot its name, temperature, flow and either fixed amount with weight/volume mode or a water-to-espresso-target-yield ratio. Ratio amounts SHALL be recipe-specific and resolve to grams. The block SHALL carry `order`, before or after espresso, defaulting to after. Steam and hot-water blocks SHALL remain independent.

#### Scenario: Enabling hot water requires a vessel
- **WHEN** the user turns on added hot water for a recipe
- **THEN** the recipe references a water vessel and stores its selected amount specification, temperature and flow as a by-value snapshot

#### Scenario: Water order distinguishes long black from Americano
- **WHEN** the user sets the hot-water order to `before` (long black) or `after` (Americano)
- **THEN** the recipe's hot-water block records that order and returns it unchanged when the recipe is loaded

#### Scenario: Water vessel preset edited after recipe creation
- **WHEN** the user reorders, edits, or deletes entries in the global water-vessel presets after a recipe was saved
- **THEN** the recipe's hot-water behavior is unchanged, because its fixed amount or ratio, temperature and flow were snapshotted by value

#### Scenario: Steam and hot water coexist on one recipe
- **WHEN** a recipe carries both a milk steam block and a hot-water block
- **THEN** both are stored and neither overrides the other

#### Scenario: Ratio is stored rather than flattened
- **WHEN** a recipe with a 40 g espresso target saves a 2.5× water ratio
- **THEN** reopening it shows Ratio and 2.5×, rather than a fixed 100 g amount

#### Scenario: Shared vessel serves independent recipes
- **WHEN** two recipes use the same vessel name with water ratios 2 and 3
- **THEN** each retains its own ratio and editing either does not change the other or the shared preset's fixed amount automatically

## ADDED Requirements

### Requirement: Water amount specifications round-trip across recipe surfaces
Recipe storage, native/browser editing, MCP and HTTP recipe JSON SHALL preserve the fixed-or-ratio discriminator and the ratio value. Existing blocks without an amount discriminator SHALL retain fixed semantics. A name-only legacy block SHALL continue resolving its fixed settings from the named preset. Saving an unrelated field SHALL NOT flatten or discard a water ratio.

#### Scenario: Existing fixed-volume recipe
- **WHEN** a legacy hot-water block stores 150 ml in volume mode and no amount discriminator
- **THEN** it loads and pours as fixed 150 ml until explicitly edited

#### Scenario: MCP ratio round trip
- **WHEN** a client creates or updates a recipe with a 2.7× water ratio and reads it back
- **THEN** the returned hot-water block retains ratio mode and 2.7, with no fixed amount treated as authoritative

#### Scenario: Edit an unrelated field through an older form
- **WHEN** the retained legacy web form changes the name of a recipe carrying ratio water
- **THEN** the saved recipe retains its ratio amount specification

### Requirement: Water ratios are validated consistently
A water ratio SHALL be finite and positive, require an espresso yield basis, and resolve to a supported whole-gram water target. Conflicting fixed and ratio specifications and unknown amount modes SHALL be rejected with an explanatory error at every write boundary. Invalid saves SHALL NOT partially update the recipe.

#### Scenario: Conflicting amount inputs
- **WHEN** a recipe write supplies both an authoritative fixed water amount and a ratio
- **THEN** the write fails with a message identifying the conflict

#### Scenario: Invalid ratio
- **WHEN** a recipe write supplies zero, a negative or non-finite ratio, or a ratio on profile-less hot-water tea
- **THEN** the write fails with a message identifying the invalid ratio or missing espresso basis

#### Scenario: Ratio result exceeds supported weight
- **WHEN** the configured ratio and espresso target would require more water than the supported weight target
- **THEN** the editor or write response identifies the unsupported result rather than silently changing the ratio

### Requirement: History preserves designed water and the amount used
Shots from ratio-water recipes SHALL retain both the water ratio specification and the resolved water target used for that drink. These SHALL remain distinct from actual water dispensed. Cloning, promoting a shot, exporting and importing SHALL preserve the designed ratio; legacy fixed snapshots SHALL remain fixed.

#### Scenario: Promote a ratio-water shot
- **WHEN** a shot designed at 2.5× with a 40 g espresso target is saved and promoted after the current espresso target has changed
- **THEN** the promoted recipe retains 2.5× and the shot retains its original 100 g water target
- **AND** neither is inferred from the measured espresso or water output

#### Scenario: Clone and transfer
- **WHEN** a ratio-water recipe is cloned or transferred through backup/import
- **THEN** its ratio amount specification survives unchanged
