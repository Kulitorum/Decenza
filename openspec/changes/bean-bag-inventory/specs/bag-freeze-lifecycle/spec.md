## ADDED Requirements

### Requirement: Bag tracks current freeze/defrost state
A bag SHALL store `frozenDate` (nullable date) and `defrostDate` (nullable date) representing the bag's current portion's lifecycle. These fields do NOT accumulate — only the current portion is tracked. The full defrost history is reconstructable from the shot snapshot fields.

#### Scenario: Bag with active frozen portion
- **WHEN** a bag has `frozenDate` set and `defrostDate` set
- **THEN** the bag card SHALL display the defrost age: "Def {N}d" (days since `defrostDate`)

#### Scenario: Bag frozen but not yet defrosted
- **WHEN** a bag has `frozenDate` set but `defrostDate` is null
- **THEN** the bag card SHALL display "Frozen" without a defrost age

#### Scenario: Bag not frozen
- **WHEN** both `frozenDate` and `defrostDate` are null
- **THEN** no freeze-related indicators SHALL appear on the bag card

### Requirement: "Next Portion" action stamps defrostDate
The system SHALL provide a "Next Portion" action on frozen bag cards (where `frozenDate` is non-null). Activating it SHALL set `defrostDate` to the current date with no dialog required.

#### Scenario: One-tap defrost
- **WHEN** the user activates "Next Portion" on a frozen bag card
- **THEN** `defrostDate` SHALL be set to today's date
- **AND** the bag card SHALL update to show the new defrost age immediately
- **AND** no dialog or confirmation SHALL be required

#### Scenario: Multiple portions over time
- **WHEN** the user activates "Next Portion" multiple times across different dates
- **THEN** each activation overwrites `defrostDate` with the current date
- **AND** the bag card always shows the most recent defrost age
- **AND** prior defrost events are preserved implicitly via shot snapshots

### Requirement: Freeze fields captured in shot snapshot
When a shot is saved, the active bag's `frozenDate` and `defrostDate` SHALL be written into the shot record so that the shot permanently records the thermal history of those beans.

#### Scenario: Shot taken from a defrosted bag
- **WHEN** a shot is saved and the active bag has non-null `frozenDate` and `defrostDate`
- **THEN** the shot record SHALL include both dates in its snapshot
- **AND** these fields SHALL be preserved even if the bag is later marked empty

### Requirement: Freeze toggle available in Change Beans dialog
The bag creation form (in the Change Beans dialog) SHALL include a freeze toggle (always visible — no expander).

#### Scenario: Creating a bag with freeze enabled
- **WHEN** the user enables the freeze toggle
- **THEN** the form SHALL show a `frozenDate` date picker (defaulting to today)
- **AND** on bag creation, the bag SHALL have `frozenDate` set and `defrostDate` null

#### Scenario: Creating a bag without freeze
- **WHEN** the freeze toggle is not enabled
- **THEN** `frozenDate` and `defrostDate` SHALL both be null on the created bag
