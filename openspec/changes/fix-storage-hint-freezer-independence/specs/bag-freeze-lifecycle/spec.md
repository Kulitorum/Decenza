## MODIFIED Requirements

### Requirement: Bag tracks current freeze/defrost state
A bag SHALL store `frozenDate` (nullable date), `defrostDate` (nullable date), `storageHint` (nullable enum: `counter` / `airtight` / `vacuum-sealed` / `fridge`), and `openedDate` (nullable date) representing the bag's current portion's lifecycle. These fields do NOT accumulate — only the current portion is tracked. The full defrost/open history is reconstructable from the shot snapshot fields.

`openedDate` is the non-frozen analogue of `defrostDate`: the day the current portion started being actively used at room temperature. A bag may carry `openedDate` with no `frozenDate`/`defrostDate` at all (never frozen), or may carry both (frozen, later thawed, then also tracked via an opened date if the user wants a second anchor) — the two pairs are independent.

These fields describe **three orthogonal axes**, and no axis SHALL gate, hide, or clear another:

| Axis | Fields | Question answered |
|---|---|---|
| Freezer | `frozenDate`, `defrostDate` | Is it in the freezer; when did this portion leave? |
| Container | `storageHint` | How is it kept when NOT in the freezer? |
| Use | `openedDate` | When did this portion start being used at room temperature? |

`storageHint` is the **out-of-freezer storage plan** — forward-looking on a frozen bag ("when this is thawed, it goes in a vacuum jar"), descriptive on a thawed or never-frozen one. It SHALL be settable and retained in every freeze state. The enum has no `"frozen"` value, and whether a bag is frozen SHALL remain determined solely by `frozenDate` being set; because the two fields answer different questions, they cannot disagree, and no clearing or hiding is required to keep them consistent.

**Beans are frozen in portions and pulled out one at a time.** A frozen bag therefore keeps portions in the freezer indefinitely: `frozenDate` describes how the BAG is stored, and `defrostDate` records when the CURRENT PORTION left the freezer — not the bag. `isFrozen` staying true after a thaw is correct, and "Thaw" SHALL remain available on a thawed bag to record the next portion coming out (see "Multiple portions over time"). A gate needing "beans are in use at room temperature right now" SHALL test `frozenDate` empty **OR** `defrostDate` set — it SHALL NOT be expressed as "nothing is in the freezer", which is never true of a frozen bag.

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

#### Scenario: A frozen bag carries an out-of-freezer plan
- **WHEN** a bag has `frozenDate` set, `defrostDate` null, and `storageHint = "vacuum-sealed"`
- **THEN** all three values SHALL coexist
- **AND** the freshness aging anchor SHALL remain unaffected — `storageHint` contributes no date, so a plan with no thaw date yields no aging anchor

### Requirement: Freeze toggle available in Change Beans dialog
The bag creation form (in the Change Beans dialog) SHALL include a freeze toggle (always visible — no expander). A `storageHint` dropdown (values: Counter / Airtight container / Vacuum-sealed / Fridge) SHALL be **always visible, regardless of the freeze toggle**, because it records the out-of-freezer storage plan rather than present state — a plan is meaningful, and most useful, precisely while the bag is frozen. The freeze toggle SHALL NOT hide, disable, or clear `storageHint` or `openedDate`; the fields lie on independent axes and there is no state in which the control has nothing to say.

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
The system SHALL provide a "Mark Opened" action on the cards of bags with a portion out of the freezer — that is, where `frozenDate` is null (never frozen) **or** `defrostDate` is set (frozen, current portion thawed) — mirroring the existing "Thaw" action. It SHALL NOT appear while a frozen bag has no thaw recorded (`frozenDate` set AND `defrostDate` null), because no portion has come out yet and there is nothing to have opened. Activating it SHALL open a calendar picker defaulted to today's date — NOT pre-set to the bag's existing `openedDate` — because a new open event happening today is overwhelmingly the most probable answer; picking a date (today or otherwise) sets `openedDate`.

A thawed bag therefore offers BOTH "Thaw" and "Mark Opened", and SHALL keep both indefinitely: beans are frozen in portions and pulled out one at a time, so "Thaw" records the NEXT portion coming out of a bag that remains frozen, while "Mark Opened" records the current portion leaving airtight storage. The two events are distinct and both recur.

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
