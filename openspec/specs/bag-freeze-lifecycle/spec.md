# bag-freeze-lifecycle Specification

## Purpose
Defines how a bag tracks its current portion's storage lifecycle — the freeze/defrost pair (`frozenDate`/`defrostDate`) and the non-frozen pair (`storageHint`/`openedDate`) — the "Thaw" and "Mark Opened" actions that record a portion entering active use, the freeze toggle and storage-hint dropdown in the bag creation form, and the capture of all four fields into each shot's snapshot so a shot permanently records the beans' storage and thermal history.

## Requirements
### Requirement: Bag tracks current freeze/defrost state
A bag SHALL store `frozenDate` (nullable date), `defrostDate` (nullable date), `storageHint` (nullable enum: `counter` / `airtight` / `vacuum-sealed` / `fridge`), and `openedDate` (nullable date) representing the bag's current portion's lifecycle. These fields do NOT accumulate — only the current portion is tracked. The full defrost/open history is reconstructable from the shot snapshot fields.

`openedDate` is the non-frozen analogue of `defrostDate`: the day the current portion started being actively used at room temperature. A bag may carry `openedDate` with no `frozenDate`/`defrostDate` at all (never frozen), or may carry both (frozen, later thawed, then also tracked via an opened date if the user wants a second anchor) — the two pairs are independent.

`storageHint` describes non-frozen storage ONLY and never includes a "frozen" value. Whether a bag is frozen is determined solely by `frozenDate` being set — the existing, unambiguous rule the app already uses (`isFrozen`/`fFreeze` are already derived from `frozenDate` presence, not a separate stored flag). This avoids a second, independently-settable "is this frozen" signal that could disagree with `frozenDate`.

#### Scenario: Bag with active frozen portion
- **WHEN** a bag has `frozenDate` set and `defrostDate` set
- **THEN** the bag card SHALL display the absolute thaw date and the defrost age together: "Thawed {date} ({N}d)" (locale-formatted date, days since `defrostDate`)

#### Scenario: Bag frozen but not yet defrosted
- **WHEN** a bag has `frozenDate` set but `defrostDate` is null
- **THEN** the bag card SHALL display "Frozen" without a defrost date or age

#### Scenario: Bag never frozen, opened date set
- **WHEN** a bag has no `frozenDate`/`defrostDate` but has `openedDate` set
- **THEN** the bag card SHALL display the absolute opened date and age together: "Opened {date} ({N}d)"

#### Scenario: Bag with no lifecycle state at all
- **WHEN** `frozenDate`, `defrostDate`, and `openedDate` are all null
- **THEN** no freeze- or open-related indicators SHALL appear on the bag card

### Requirement: "Thaw" action records the latest portion leaving the freezer
The system SHALL provide a "Thaw" action on frozen bag cards ONLY (where `frozenDate` is non-null). Activating it SHALL open a calendar picker defaulted to today's date — NOT pre-set to the bag's existing `defrostDate` — because a new thaw event happening today is overwhelmingly the most probable answer; picking a date (today or otherwise) sets `defrostDate`.

#### Scenario: Thawing a portion defaults the picker to today
- **WHEN** the user activates "Thaw" on a frozen bag card, including one that already has a `defrostDate` set from a previous portion
- **THEN** the calendar picker SHALL open with today's date selected, regardless of any existing `defrostDate`
- **AND** confirming that default SHALL set `defrostDate` to today with a single additional tap

#### Scenario: Picking a different date than today
- **WHEN** the user activates "Thaw" and navigates the calendar to a different date before confirming
- **THEN** `defrostDate` SHALL be set to the picked date, not today
- **AND** the bag card SHALL update to show the new defrost date/age immediately

#### Scenario: Not visible on unfrozen bags
- **WHEN** a bag has no `frozenDate`
- **THEN** no "Thaw" action SHALL appear on its card

#### Scenario: Multiple portions over time
- **WHEN** the user activates "Thaw" again later (a new portion left the freezer) and confirms the defaulted-to-today picker
- **THEN** each thaw overwrites `defrostDate` with that day's date
- **AND** the bag card always shows the most recent defrost date/age
- **AND** prior defrost events are preserved implicitly via shot snapshots

### Requirement: Freeze fields captured in shot snapshot
When a shot is saved, the active bag's `frozenDate`, `defrostDate`, `storageHint`, and `openedDate` SHALL be written into the shot record so that the shot permanently records the storage/thermal history of those beans.

#### Scenario: Shot taken from a defrosted bag
- **WHEN** a shot is saved and the active bag has non-null `frozenDate` and `defrostDate`
- **THEN** the shot record SHALL include both dates in its snapshot
- **AND** these fields SHALL be preserved even if the bag is later marked empty

#### Scenario: Shot taken from a never-frozen, recently-opened bag
- **WHEN** a shot is saved and the active bag has `storageHint = "counter"` and `openedDate` set, with no `frozenDate`/`defrostDate`
- **THEN** the shot record SHALL include `storageHint` and `openedDate` in its snapshot

### Requirement: Freeze toggle available in Change Beans dialog
The bag creation form (in the Change Beans dialog) SHALL include a freeze toggle (always visible — no expander). A `storageHint` dropdown (values: Counter / Airtight container / Vacuum-sealed / Fridge) SHALL be shown only while the freeze toggle is OFF — there is no "Frozen" storage-hint value, so the dropdown has nothing meaningful to say once the bag is frozen; frozen state is fully expressed by the freeze toggle and its `frozenDate` field alone.

#### Scenario: Creating a bag with freeze enabled
- **WHEN** the user enables the freeze toggle
- **THEN** the form SHALL show a `frozenDate` date picker (defaulting to today) and SHALL hide the `storageHint` dropdown
- **AND** on bag creation, the bag SHALL have `frozenDate` set, `defrostDate` null, and `storageHint` null

#### Scenario: Creating a bag without freeze
- **WHEN** the freeze toggle is not enabled
- **THEN** `frozenDate` and `defrostDate` SHALL both be null on the created bag
- **AND** the form SHALL show the `storageHint` dropdown and an `openedDate` date picker (defaulting to today, optional)

#### Scenario: Toggling freeze on clears a previously-selected storageHint
- **WHEN** the user has selected a non-frozen `storageHint` and then enables the freeze toggle
- **THEN** `storageHint` SHALL be cleared to null on save (the dropdown is hidden, not merely disabled-with-a-stale-value)

### Requirement: "Mark Opened" action records the non-frozen portion's start date
The system SHALL provide a "Mark Opened" action on non-frozen bag cards (where `frozenDate` is null) mirroring the existing "Thaw" action. Activating it SHALL open a calendar picker defaulted to today's date — NOT pre-set to the bag's existing `openedDate` — because a new open event happening today is overwhelmingly the most probable answer; picking a date (today or otherwise) sets `openedDate`.

#### Scenario: Marking a bag opened defaults the picker to today
- **WHEN** the user activates "Mark Opened" on a non-frozen bag card, including one that already has an `openedDate` set from a previous portion
- **THEN** the calendar picker SHALL open with today's date selected, regardless of any existing `openedDate`
- **AND** confirming that default SHALL set `openedDate` to today with a single additional tap

#### Scenario: Picking a different date than today
- **WHEN** the user activates "Mark Opened" and navigates the calendar to a different date before confirming
- **THEN** `openedDate` SHALL be set to the picked date, not today
- **AND** the bag card SHALL update to show the new opened date/age immediately

#### Scenario: Not visible on frozen bags
- **WHEN** a bag has `frozenDate` set (frozen, whether or not yet thawed)
- **THEN** "Mark Opened" SHALL NOT appear on its card — "Thaw" is the equivalent action for a frozen bag

#### Scenario: Re-marking opened on a new portion of the same bag
- **WHEN** the user activates "Mark Opened" again later and confirms the defaulted-to-today picker
- **THEN** `openedDate` SHALL be overwritten to that day's date
- **AND** prior opened events are preserved implicitly via shot snapshots, same as `defrostDate`

