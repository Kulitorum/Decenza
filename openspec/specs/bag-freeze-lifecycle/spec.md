# bag-freeze-lifecycle Specification

## Purpose
Defines how a bag tracks its current portion's storage lifecycle — the freeze/defrost pair (`frozenDate`/`defrostDate`) and the non-frozen pair (`storageHint`/`openedDate`) — the "Thaw" action and the first shot that record a portion entering active use, the freeze toggle and storage-hint dropdown in the bag creation form, and the capture of all four fields into each shot's snapshot so a shot permanently records the beans' storage and thermal history.

## Requirements

### Requirement: Bag tracks current freeze/defrost state

A bag SHALL store, for its current portion only, `frozenDate`, `defrostDate`, `storageHint` (`counter`, `airtight`, `vacuum-sealed` or `fridge`) and `openedDate`, all nullable; shot snapshots keep the history. These are three independent axes (freezer, container, use), and none SHALL gate, hide or clear another. Whether a bag is frozen SHALL be decided by `frozenDate` alone; beans are frozen in portions, so a thawed bag stays frozen.

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
- **AND** the opened line SHALL NOT be suppressed by the presence of a thaw date — the two describe independent events, and a thawed portion's first shot stamps `openedDate` in exactly this state
- **AND** the same SHALL hold for every surface rendering these fields (bag card and bean summary)

#### Scenario: Bag with no lifecycle state at all
- **WHEN** `frozenDate`, `defrostDate`, and `openedDate` are all null
- **THEN** no freeze- or open-related indicators SHALL appear on the bag card

#### Scenario: A frozen bag carries an out-of-freezer plan
- **WHEN** a bag has `frozenDate` set, `defrostDate` null, and `storageHint = "vacuum-sealed"`
- **THEN** all three values SHALL coexist
- **AND** the freshness aging anchor SHALL remain unaffected — `storageHint` contributes no date, so a plan with no thaw date yields no aging anchor

#### Scenario: Storage hint is a plan on a frozen bag

- **WHEN** a frozen bag carries `storageHint = "vacuum-sealed"`
- **THEN** it SHALL mean where a thawed portion goes, and SHALL be settable and kept in every freeze state; the enum has no `"frozen"` value

#### Scenario: Beans in use are tested by portion, not by freezer contents

- **WHEN** any feature needs "beans are in use at room temperature right now"
- **THEN** it SHALL test `frozenDate` empty OR `defrostDate` set, never "nothing is in the freezer", which is never true of a frozen bag
- **AND** "Thaw" SHALL stay available on a thawed bag for the next portion

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

The bag creation form (in the Change Beans dialog) SHALL include an always-visible freeze toggle and an always-visible `storageHint` dropdown (Counter / Airtight container / Vacuum-sealed / Fridge), since a storage plan matters most while the bag is frozen. The freeze toggle SHALL NOT hide, disable or clear `storageHint` or `openedDate`.

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
- **THEN** no `openedDate` picker SHALL appear — a bag being created has no portion in use yet, so the field is edit-mode only and a portion's first shot is what sets it
- **AND** `openedDate` SHALL NOT be written on the create path

#### Scenario: Toggling freeze on preserves a previously-selected storageHint
- **WHEN** the user selects `storageHint = "airtight"` and then enables the freeze toggle
- **THEN** `storageHint` SHALL remain `"airtight"` on save — the plan survives freezing, because it describes what happens when the beans come back out

#### Scenario: Saving a frozen bag never discards storage fields
- **WHEN** a bag with `frozenDate` set, `storageHint = "vacuum-sealed"`, and `openedDate` set is opened in the dialog and saved without the user touching either field
- **THEN** `storageHint` and `openedDate` SHALL both be written back unchanged
- **AND** this SHALL hold for values originally set through the `bag_update` MCP tool rather than the dialog

### Requirement: A shot stamps the current portion's opened date
When a shot is saved with an active bag whose current portion has no opened date, the system SHALL set the bag's `openedDate` to the shot's local date, and the shot's snapshot SHALL carry it. The rule SHALL be defined once in C++ (`CoffeeBag::openedDateForShot`). The date SHALL remain editable in the Change Beans dialog.

#### Scenario: First shot from a never-frozen bag
- **WHEN** a shot is saved and the active bag has no `frozenDate` and no `openedDate`
- **THEN** the bag's `openedDate` SHALL be set to the shot's date
- **AND** the shot's snapshot SHALL carry that `openedDate`

#### Scenario: Later shots leave the date alone
- **WHEN** a shot is saved and the active bag's `openedDate` is on or after its `defrostDate` (or it has no `defrostDate`)
- **THEN** `openedDate` SHALL NOT change

#### Scenario: First shot from a newly thawed portion
- **WHEN** a shot is saved and the active bag's `openedDate` is earlier than its `defrostDate`
- **THEN** `openedDate` SHALL be set to the shot's date

#### Scenario: Frozen bag with no thaw recorded
- **WHEN** a shot is saved and the active bag has `frozenDate` set and `defrostDate` null
- **THEN** `openedDate` SHALL NOT be set — the shot is taken as one serving out of the freezer with the rest put back

### Requirement: Bag lifecycle dates cannot be in the future
A bag's `roastDate`, `frozenDate`, `defrostDate` and `openedDate` SHALL NOT be later than the device's local date. The rule SHALL be defined once in C++ (`CoffeeBag::futureDateError`) and enforced at the storage write boundary, with MCP and the web API returning its message as an error. The app's date pickers and typed date fields for these dates SHALL NOT accept a date after today. Existing stored values are not rewritten.

#### Scenario: A future thaw date is refused
- **WHEN** any surface writes a bag with `defrostDate` set to tomorrow
- **THEN** the write SHALL be refused with a message naming the thaw date
- **AND** the stored bag SHALL be unchanged

#### Scenario: Today is allowed
- **WHEN** a lifecycle date is set to today's local date
- **THEN** the write SHALL succeed

#### Scenario: The web Thaw button uses the local date
- **WHEN** the user taps Thaw on the web page in the evening in a timezone behind UTC
- **THEN** `defrostDate` SHALL be the user's local date, not the next UTC day

### Requirement: Unfreezing a bag clears its thaw date on every surface
Clearing a bag's `frozenDate` SHALL also clear its `defrostDate`, enforced in storage so the app, the web page and MCP behave the same. `openedDate` and `storageHint` SHALL be unaffected.

#### Scenario: Web or MCP unfreezes a thawed bag
- **WHEN** a bag with `frozenDate`, `defrostDate` and `openedDate` set is updated with `frozenDate = ""` from any surface
- **THEN** `defrostDate` SHALL be cleared
- **AND** `openedDate` and `storageHint` SHALL keep their values

### Requirement: A card offers "Freeze" on a bag not yet frozen
A bag card in inventory with no `frozenDate` SHALL offer "Freeze" where a frozen bag offers "Thaw" (`InventoryBag::cardActions`, so the web page offers it too). It opens a date picker defaulted to today, no earlier than the roast date, and sets only `frozenDate`: it records no thaw, because shots can be ground straight from the freezer.

#### Scenario: Freezing a bag in use
- **WHEN** the user taps Freeze on a bag with shots and confirms today
- **THEN** `frozenDate` SHALL be today and `defrostDate` SHALL stay unset
- **AND** the card SHALL then offer Thaw instead of Freeze

### Requirement: Restock carries the storage habits of the bag it replaces
Restocking a bag (app or web, both through `CoffeeBag::restockTemplate`) SHALL carry its `storageHint`, and a bag that had a `frozenDate` SHALL start frozen as of today. Roast, thaw and opened dates, notes and start weight SHALL NOT carry over.

#### Scenario: Restocking a frozen, vacuum-sealed bag
- **WHEN** the user restocks a bag with `frozenDate` set and `storageHint = "vacuum-sealed"`
- **THEN** the new-bag form SHALL open with freezing on, today as the frozen date, and "Vacuum-sealed" as the storage type, and with the roast date empty

### Requirement: Bag lifecycle dates must be in order
A write SHALL be refused when it would put `frozenDate` before `roastDate`, `defrostDate` before `frozenDate`, or `openedDate` before `roastDate` (`CoffeeBag::lifecycleOrderError`). Only pairs the write touches are checked, so an unrelated edit is never refused for an older record. The app's thaw, freeze and editor date pickers SHALL not offer days before the date they must follow.

#### Scenario: A thaw before the freeze
- **WHEN** any surface sets `defrostDate` earlier than the stored `frozenDate`
- **THEN** the write SHALL be refused, and the web and MCP SHALL say "Thaw date is before the frozen date"

### Requirement: The post-shot review shows the beans' storage dates
The post-shot review's bean summary SHALL show the shot's frozen, thaw and opened dates with the roast date, as the bag card does ("Roasted · Frozen <date>" or "Thawed <date> (Nd) · Opened <date> (Nd)"), so a missed thaw is visible after the shot it affected.

#### Scenario: A shot from a frozen bag with no thaw recorded
- **WHEN** the review page opens for that shot
- **THEN** its bean summary SHALL read "Frozen <date>"
