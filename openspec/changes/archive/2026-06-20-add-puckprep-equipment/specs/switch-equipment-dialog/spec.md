## ADDED Requirements

### Requirement: The dialog SHALL offer an optional puck-prep checkbox section
The Switch Equipment dialog SHALL provide a puck-prep section rendered as a row of
labeled checkboxes (`WDT`, `Shaker`, `Puck screen`, `Bottom paper filter`, `RDT`),
NOT a vendor picker. The section SHALL be optional — leaving every box unchecked
SHALL create/save a package with no puck-prep item.

#### Scenario: Set puck-prep flags
- **WHEN** the user ticks `WDT` and `Shaker` and saves
- **THEN** the package SHALL carry a puck-prep item with those flags set and the others cleared

#### Scenario: Create a package with no puck prep
- **WHEN** the user leaves every puck-prep box unchecked
- **THEN** the package SHALL be created/saved with no puck-prep item

### Requirement: Puck-prep checkboxes SHALL be accessible
Each puck-prep checkbox SHALL expose an accessible role of CheckBox, an accessible
name, and its checked state to assistive technology.

#### Scenario: Checkbox announced with state
- **WHEN** a screen reader focuses a puck-prep checkbox
- **THEN** it SHALL announce the checkbox label and whether it is checked

### Requirement: Editing puck prep SHALL honor package-identity semantics
Changing the puck-prep flags on an existing package SHALL apply the package-identity
rules (fork on a used package, edit-in-place on an unused one, dedup to an existing
matching grinder + basket + puck-prep combination).

#### Scenario: Changing puck prep on a used package
- **WHEN** the user changes a puck-prep flag on a package that has recorded shots and saves
- **THEN** the dialog flow SHALL result in the forked/deduped package per the identity rules, and set it active
