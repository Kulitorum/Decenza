## ADDED Requirements

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
