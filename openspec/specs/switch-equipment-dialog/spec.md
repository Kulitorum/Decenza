# switch-equipment-dialog Specification

## Purpose
TBD - created by archiving change add-equipment-packages. Update Purpose after archive.
## Requirements
### Requirement: Switch Equipment dialog
The system SHALL provide a `SwitchEquipmentDialog.qml` (mirroring `ChangeBeansDialog.qml`) that lets the user pick an existing equipment package or create a new one. It SHALL be openable from the Equipment window ("Add Equipment") and from Brew Settings ("Switch Equipment").

#### Scenario: Pick an existing package
- **WHEN** the user opens the dialog with packages in inventory
- **THEN** it SHALL list the packages for selection
- **AND** selecting one SHALL set it as the active bag's equipment package

#### Scenario: Create a new package
- **WHEN** the user creates a new package
- **THEN** the dialog SHALL collect grinder brand, model, and burrs with registry-backed suggestions (the same `knownGrinderBrands`/`knownGrinderModels`/`suggestedBurrs` sources the old Brew Settings grinder fields used, plus distinct values from shot history)
- **AND** `rpmCapable` SHALL be derived from the registry for the entered identity
- **AND** the new package SHALL be created and set as the active bag's equipment package

### Requirement: Switching equipment applies the package's last dial
When a package is selected as the active bag's equipment, the dialog flow SHALL apply that package's `lastGrindSetting`/`lastRpm` to the active bag's grind setting and rpm (per the equipment-package-model dual-memory rule), so Brew Settings reflects a non-blank, editable dial immediately.

#### Scenario: Selecting a package pre-fills the dial
- **WHEN** the user selects a package whose `lastGrindSetting` is "3.0" and `lastRpm` is 1200
- **THEN** the active bag's grind setting SHALL become "3.0" and rpm 1200
- **AND** both SHALL remain editable in Brew Settings

### Requirement: Editing a package from the dialog uses reference semantics
The dialog SHALL allow editing an existing package's grinder identity. Edits SHALL apply to the package row (reference semantics), so all bags and shots pointing at it resolve to the edited values; `rpmCapable` SHALL re-derive on a brand/model change.

#### Scenario: Editing identity flows to history
- **WHEN** the user edits a package's grinder model via the dialog
- **THEN** the package row SHALL be updated and all referencing bags and shots SHALL resolve to the new model

### Requirement: The dialog SHALL offer an optional vendor-first basket picker
The Switch Equipment dialog SHALL provide a basket section, mirroring the grinder
flow, that is **vendor-first and two-level**: the user picks a basket brand, then a
basket model (size is part of the model name; there is no third level analogous to
burrs). Basket selection SHALL be **optional** — the dialog SHALL provide a clear
"no basket" choice, and a package MAY be created or saved with no basket.

#### Scenario: Pick a basket brand then model
- **WHEN** the user opens the basket section and selects a brand
- **THEN** the model field SHALL offer that brand's models from `knownBasketModels(brand)`
- **AND** selecting a model SHALL set the package's basket identity

#### Scenario: Create a package without a basket
- **WHEN** the user leaves the basket section empty (or clears it)
- **THEN** the package SHALL be created/saved with a grinder item and no basket item

### Requirement: The basket model row SHALL render a differentiator subtitle
The basket model picker SHALL render, per suggestion, a second line (derived from
the basket's `summary()`) describing the axis that distinguishes siblings, not a
bare model name — because basket models within a brand are often similar and may
share a dose. The subtitle SHALL surface the functional differentiator (e.g. wall
profile / effective bed diameter / material), and SHALL NOT reduce to a mere dose
echo where dose does not distinguish the models.

#### Scenario: Same-dose siblings remain distinguishable
- **WHEN** a brand offers two models with overlapping dose ranges that differ only by another axis (e.g. two "14g" stepped baskets differing in taper degree)
- **THEN** each model row SHALL display a subtitle that distinguishes them by that axis (e.g. effective bed diameter / body), not by dose alone

#### Scenario: Jargon model names are explained
- **WHEN** a brand offers models whose names are jargon (e.g. "Convex Billet", "Tapered Billet")
- **THEN** each model row SHALL display a subtitle conveying the wall profile and material

### Requirement: SuggestionField SHALL support an optional per-suggestion description
`SuggestionField` SHALL accept suggestions as either plain strings (current
behavior, used by the brand level and existing grinder fields) or `{value,
description}` entries, rendering the description as a secondary line in the dropdown
row. Accessibility metadata SHALL incorporate the description so it is announced.

#### Scenario: Description rendered and announced
- **WHEN** suggestions are provided as `{value, description}` entries
- **THEN** each dropdown row SHALL show the value and its description
- **AND** the row's accessible name/description SHALL include the description text

### Requirement: Editing the basket honors package identity semantics
Adding, changing, or clearing the basket on an existing package SHALL apply the
package-identity rules from the equipment-package-model capability (fork on a used
package, edit-in-place on an unused one, dedup to an existing matching combo).

#### Scenario: Changing the basket on a used package
- **WHEN** the user changes the basket of a package that has recorded shots and saves
- **THEN** the dialog flow SHALL result in the forked/deduped package per the identity rules, and set it active

