# bag-freeze-lifecycle Specification

## Purpose
Defines how a bag tracks its current portion's storage lifecycle — the freeze/defrost pair (`frozenDate`/`defrostDate`) and the non-frozen pair (`storageHint`/`openedDate`) — the "Thaw" action and the first shot that record a portion entering active use, the freeze toggle and storage-hint dropdown in the bag creation form, and the capture of all four fields into each shot's snapshot so a shot permanently records the beans' storage and thermal history.

## Requirements
### Requirement: Bag tracks current freeze/defrost state
A bag SHALL store `frozenDate` (nullable date), `defrostDate` (nullable date), `storageHint` (nullable enum: `counter` / `airtight` / `vacuum-sealed` / `fridge`) and `openedDate` (nullable date) for its current portion only. These fields SHALL NOT accumulate; the full defrost and open history is reconstructable from the shot snapshot fields.

#### Scenario: Bag with active frozen portion
- **WHEN** a bag has `frozenDate` set and `defrostDate` set
- **THEN** the bag card SHALL display the absolute thaw date and the defrost age together: "Thawed {date} ({N}d)" (locale-formatted date, days since `defrostDate`)

#### Scenario: Bag frozen but not yet defrosted
- **WHEN** a bag has `frozenDate` set but `defrostDate` is null
- **THEN** the bag card SHALL display "Frozen" without a defrost date or age

#### Scenario: Bag never frozen, opened date set
- **WHEN** a bag has no `frozenDate`/`defrostDate` but has `openedDate` set
- **THEN** the bag card SHALL display the absolute opened date and age together: "Opened {date} ({N}d)"

#### Scenario: A thawed bag displays its opened date alongside its thaw date
- **WHEN** a bag has `frozenDate` set, `defrostDate` set, and `openedDate` set
- **THEN** the bag card SHALL display BOTH "Thawed {date} ({N}d)" and "Opened {date} ({N}d)"
- **AND** the opened line SHALL NOT be suppressed by the presence of a thaw date — the two describe independent events and the card offers "Mark Opened" in exactly this state, so suppressing it would make that action write-only
- **AND** the same SHALL hold for every surface rendering these fields (bag card and bean summary)

#### Scenario: Bag with no lifecycle state at all
- **WHEN** `frozenDate`, `defrostDate`, and `openedDate` are all null
- **THEN** no freeze- or open-related indicators SHALL appear on the bag card

#### Scenario: A frozen bag carries an out-of-freezer plan
- **WHEN** a bag has `frozenDate` set, `defrostDate` null, and `storageHint = "vacuum-sealed"`
- **THEN** all three values SHALL coexist
- **AND** the freshness aging anchor SHALL remain unaffected — `storageHint` contributes no date, so a plan with no thaw date yields no aging anchor

### Requirement: The freezer, container and use fields never gate each other
The freezer fields (`frozenDate`, `defrostDate`) record whether and when the current portion left the freezer, `storageHint` records how the bag is kept out of the freezer, and `openedDate` records when the portion started being used at room temperature. No field SHALL gate, hide or clear another, and `openedDate` MAY be set on a bag with no freezer fields.

#### Scenario: Setting one axis preserves the others
- **WHEN** a frozen bag has a storage hint and an opened date
- **THEN** all three axes remain set, and changing one does not clear the others

### Requirement: storageHint is an out-of-freezer plan in every freeze state
`storageHint` SHALL be settable and retained in every freeze state, describing the plan for when the portion is out of the freezer. The enum SHALL NOT have a `frozen` value, and whether a bag is frozen SHALL be determined solely by `frozenDate` being set.

#### Scenario: Storage hint on a never-frozen bag
- **WHEN** a never-frozen bag has a storage hint set
- **THEN** the hint is retained and the bag is not reported as frozen

### Requirement: Frozen bags stay frozen across portions
Beans are frozen in portions and pulled out one at a time. `frozenDate` SHALL describe how the bag is stored, `defrostDate` SHALL record when the current portion left the freezer, and `isFrozen` SHALL stay true after a thaw. A gate needing "beans are in use at room temperature" SHALL test `frozenDate` empty OR `defrostDate` set, and SHALL NOT test that nothing is in the freezer.

#### Scenario: Room-temperature gate on a thawed frozen bag
- **WHEN** a frozen bag has a thawed portion (`frozenDate` set and `defrostDate` set)
- **THEN** the room-temperature gate passes and the bag remains frozen

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
The bag creation form in the Change Beans dialog SHALL include a freeze toggle that is always visible. The `storageHint` dropdown (Counter / Airtight container / Vacuum-sealed / Fridge) SHALL also always be visible, regardless of the toggle. The freeze toggle SHALL NOT hide, disable or clear `storageHint` or `openedDate`.

#### Scenario: Creating a bag with freeze enabled
- **WHEN** the user enables the freeze toggle
- **THEN** the form SHALL show a `frozenDate` date picker (defaulting to today)
- **AND** the `storageHint` dropdown SHALL remain visible with any selected value intact
- **AND** on bag creation, the bag SHALL have `frozenDate` set, `defrostDate` null, and whatever `storageHint` the user selected (null only if they selected none)

#### Scenario: Creating a bag without freeze
- **WHEN** the freeze toggle is not enabled
- **THEN** `frozenDate` and `defrostDate` SHALL both be null on the created bag
- **AND** the form SHALL show the `storageHint` dropdown, which SHALL be written to the created bag

#### Scenario: openedDate is not offered on the create form
- **WHEN** the bag creation form is shown (either freeze state)
- **THEN** no `openedDate` picker SHALL appear — a bag being created has no portion in use yet, so the field is edit-mode only and the "Mark Opened" card action is the everyday path
- **AND** `openedDate` SHALL NOT be written on the create path

#### Scenario: Toggling freeze on preserves a previously-selected storageHint
- **WHEN** the user selects `storageHint = "airtight"` and then enables the freeze toggle
- **THEN** `storageHint` SHALL remain `"airtight"` on save — the plan survives freezing, because it describes what happens when the beans come back out

#### Scenario: Saving a frozen bag never discards storage fields
- **WHEN** a bag with `frozenDate` set, `storageHint = "vacuum-sealed"`, and `openedDate` set is opened in the dialog and saved without the user touching either field
- **THEN** `storageHint` and `openedDate` SHALL both be written back unchanged
- **AND** this SHALL hold for values originally set through the `bag_update` MCP tool rather than the dialog

### Requirement: "Mark Opened" action records the current portion's start date
The system SHALL provide a "Mark Opened" action on bags with a portion out of the freezer, that is where `frozenDate` is null or `defrostDate` is set. It SHALL NOT appear while a frozen bag has no thaw recorded. Activating it SHALL open a calendar picker defaulted to today's date, not the existing `openedDate`, and picking a date SHALL set `openedDate`. A thawed bag SHALL offer both "Thaw" and "Mark Opened" and keep both indefinitely.

#### Scenario: Marking a bag opened defaults the picker to today
- **WHEN** the user activates "Mark Opened" on an eligible bag card, including one that already has an `openedDate` set from a previous portion
- **THEN** the calendar picker SHALL open with today's date selected, regardless of any existing `openedDate`
- **AND** confirming that default SHALL set `openedDate` to today with a single additional tap

#### Scenario: Picking a different date than today
- **WHEN** the user activates "Mark Opened" and navigates the calendar to a different date before confirming
- **THEN** `openedDate` SHALL be set to the picked date, not today
- **AND** the bag card SHALL update to show the new opened date/age immediately

#### Scenario: Available on a thawed bag alongside Thaw
- **WHEN** a bag has `frozenDate` set AND `defrostDate` set (a portion has been pulled out)
- **THEN** its card SHALL offer both "Thaw" and "Mark Opened"
- **AND** "Thaw" SHALL remain available for the next portion — the bag is still frozen; thawing one portion does not empty the freezer
- **AND** setting `openedDate` SHALL leave `frozenDate` and `defrostDate` untouched

#### Scenario: Not visible before the first portion is pulled
- **WHEN** a bag has `frozenDate` set AND `defrostDate` null
- **THEN** "Mark Opened" SHALL NOT appear on its card — no portion has come out of the freezer yet, so there is nothing to have opened; "Thaw" is the applicable action

#### Scenario: Re-marking opened on a new portion of the same bag
- **WHEN** the user activates "Mark Opened" again later and confirms the defaulted-to-today picker
- **THEN** each open event overwrites `openedDate` with that day's date
- **AND** the bag card always shows the most recent opened date/age
- **AND** prior open events are preserved implicitly via shot snapshots

