# Spec Delta

## Purpose

Let users add and edit water vessels where they design a recipe, with clear fixed-amount or yield-relative water controls and consistent standalone preset editing.

## ADDED Requirements

### Requirement: Recipe water window offers vessel management
The recipe's Hot Water window SHALL offer visible Add vessel and Edit actions. Its vessel editor SHALL support name, amount specification, temperature and flow, with fixed amounts supporting weight or volume. These actions SHALL be available when creating and editing recipes in both native and browser clients.

#### Scenario: Add without leaving the recipe
- **WHEN** a user opens Add vessel in the recipe's Hot Water window and saves a valid vessel
- **THEN** the vessel is available in the shared preset list and selected in the recipe draft
- **AND** the user remains in the recipe editing flow

#### Scenario: Edit the selected recipe vessel
- **WHEN** a user opens Edit on the selected vessel
- **THEN** the fields show the recipe draft's vessel settings, including its saved ratio or fixed amount
- **AND** saving updates the vessel and current draft without rewriting other recipes' snapshots

### Requirement: Recipe water amount has an editable ratio option
For recipes with an espresso target, the vessel editor SHALL offer Fixed amount and Ratio to espresso yield. The ratio SHALL be editable, defaulting to 2.5 for a newly added vessel. The editor SHALL label the result as added water and show both the multiplier and the derived grams. Existing vessels SHALL retain their saved amount specification on edit.

#### Scenario: Change the suggested ratio
- **WHEN** a new recipe vessel starts at 2.5× with an espresso target of 40 g and the user changes the ratio to 3
- **THEN** the preview shows 120 g of added water and saving preserves the ratio 3

#### Scenario: Reopen an existing fixed vessel
- **WHEN** a recipe's vessel stores 150 g fixed water and the user edits its name
- **THEN** the editor retains 150 g and Fixed amount until the user explicitly changes them

#### Scenario: Switch amount modes deliberately
- **WHEN** a user switches a ratio vessel to Fixed amount
- **THEN** the resolved grams seed the fixed field and can be edited
- **AND** switching to Ratio offers an editable 2.5× default without changing the shared preset's fixed amount automatically

### Requirement: Water ratio chooser offers presets and a custom value
The water ratio chooser SHALL use the same interaction and control presentation as the espresso ratio chooser. It SHALL initially offer 2×, 2.5× and 3×, highlight the selected value, and allow a custom ratio with decimal precision. Water-specific labels SHALL describe espresso yield to added water; choosing or editing one SHALL NOT change espresso ratio settings or presets.

#### Scenario: Pick a quick choice
- **WHEN** the user selects 2× for a 40 g espresso target
- **THEN** the recipe draft shows 80 g added water
- **AND** the espresso dose-to-yield ratio and its presets are untouched

#### Scenario: Use a custom ratio
- **WHEN** the user enters 2.7× for a 40 g espresso target
- **THEN** the preview shows 108 g and the custom ratio survives saving and reopening
- **AND** no quick choice is incorrectly marked as selected

### Requirement: Standalone vessels keep fixed amounts
The standalone Hot Water page SHALL offer only fixed weight or volume amounts for vessel creation and editing. Newly added vessels SHALL default to 100 g. Editing SHALL preserve the preset's existing settings. Recipe ratio choices SHALL NOT appear in this editor or replace the shared preset's fixed amount.

#### Scenario: Add on the standalone page
- **WHEN** a user adds a vessel from the standalone Hot Water page
- **THEN** its initial amount is 100 g and no ratio option is offered

#### Scenario: Recipe use does not overwrite the standalone amount
- **WHEN** a 200 g shared vessel is used in a recipe at 2.5× and its espresso target changes from 40 g to 42 g
- **THEN** opening the shared vessel for standalone editing still shows its fixed 200 g
- **AND** the recipe's added water is 105 g

### Requirement: Ratio requires an espresso target
Profile-less hot-water tea SHALL use fixed amounts. A recipe with no usable positive espresso target SHALL show why ratio water cannot yet be resolved and SHALL NOT save an unusable ratio amount. Fixed amounts SHALL remain available.

#### Scenario: Hot-water tea has no ratio basis
- **WHEN** a user adds a vessel to a profile-less hot-water tea recipe
- **THEN** the initial amount is 100 g and only fixed amounts are offered

#### Scenario: Espresso target is unavailable
- **WHEN** a user chooses ratio water before a usable espresso target exists
- **THEN** the editor identifies the missing target and requires a target or fixed amount before saving

### Requirement: Vessel edits preserve drafts and report conflicts
Vessel editing SHALL have explicit Save and Cancel behavior and reject blank or duplicate names and invalid amounts. A stale or failed shared-preset save SHALL retain the user's draft and report the failure. Successful preset saving SHALL NOT implicitly save the enclosing recipe or start/apply a machine operation.

#### Scenario: Cancel an editor
- **WHEN** the user changes fields and cancels before saving
- **THEN** neither the shared preset nor the recipe draft is changed by that editor

#### Scenario: Concurrent preset edit
- **WHEN** another client changes a vessel after the editor opens and the first client saves
- **THEN** the stale save is refused with a recoverable message and the entered fields remain available

#### Scenario: Save a vessel before cancelling the recipe
- **WHEN** a user explicitly saves a new shared vessel and later discards the enclosing recipe
- **THEN** the saved vessel remains available but no recipe is created or updated
