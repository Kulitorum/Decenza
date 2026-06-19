## ADDED Requirements

### Requirement: A package MAY own an optional puck-prep component
A package SHALL be able to own at most one `equipment_items` row of
`kind="puckprep"`, whose `attrs` is a checklist config of boolean flags
(`wdt`, `shaker`, `puckScreen`, `paperFilter`, `rdt`). It SHALL have no brand/model.
A package with no puck-prep item SHALL remain valid (it is optional, like the
basket). Adding puck prep SHALL NOT require a DB schema migration — it is a new
row of an existing kind.

#### Scenario: Package with puck prep
- **WHEN** a package is created/edited with one or more puck-prep flags set
- **THEN** the package SHALL own one `kind="puckprep"` item carrying those flags

#### Scenario: All flags unchecked = no puck-prep item
- **WHEN** a package's puck prep has every flag cleared
- **THEN** the package SHALL own no `kind="puckprep"` item (the item is deleted / not created)
- **AND** the package SHALL remain valid

### Requirement: The distribution rollup SHALL be derived, not stored
The puck-prep item SHALL persist only its boolean flags. A `distribution` rollup
(`none` | `light` | `thorough`) SHALL be derived at read time as a pure function of
the flags (WDT → thorough; else shaker → light; else none), never stored. Adjusting
the rollup logic SHALL NOT require any data change.

#### Scenario: Distribution derived from flags
- **WHEN** a package's puck prep has `wdt` set
- **THEN** resolving the package SHALL expose `distribution = "thorough"`

#### Scenario: Shaker without WDT
- **WHEN** a package's puck prep has `shaker` set and `wdt` cleared
- **THEN** resolving the package SHALL expose `distribution = "light"`

### Requirement: Package identity SHALL include the normalized puck-prep config
The package identity used for dedup and copy-on-write SHALL include the puck-prep
config, compared as a NORMALIZED flag-set (canonical, order-independent), where "no
puck prep" is a distinct identity value. Two packages identical in grinder + basket
but differing in puck-prep flags SHALL be distinct packages.

#### Scenario: Toggling a flag on a used package forks
- **WHEN** a package that has recorded shots has a puck-prep flag toggled
- **THEN** a new package SHALL be forked under copy-on-write (old preserved, `supersededBy` set), exactly as for a grinder or basket identity edit

#### Scenario: Returning to a prior prep dedups
- **WHEN** the user returns to a previously used grinder + basket + puck-prep combination
- **THEN** the existing matching package SHALL be selected rather than a duplicate created

#### Scenario: Order-independent comparison
- **WHEN** two puck-prep configs set the same flags in any internal order
- **THEN** they SHALL compare as the same identity

### Requirement: SettingsDye SHALL resolve and bridge the active puck prep
`SettingsDye` SHALL expose the active package's puck-prep flags (and derived
`distribution`) for display, and SHALL carry the flags through create/update/switch
alongside the grinder and basket identity.

#### Scenario: Active puck prep resolved
- **WHEN** the active package has a puck-prep item
- **THEN** `SettingsDye` SHALL expose its flags and the derived `distribution`

### Requirement: Device import SHALL carry puck-prep items
Device-to-device equipment import SHALL copy `kind="puckprep"` items alongside
grinder and basket items, and SHALL include the puck-prep config in the full-identity
merge-dedup match.

#### Scenario: Imported package retains its puck prep
- **WHEN** a package with a puck-prep item is imported from a source database
- **THEN** the destination package SHALL own an equivalent puck-prep item

#### Scenario: Same gear, different prep does not merge on import
- **WHEN** a source and dest package share grinder + basket but differ in puck-prep flags
- **THEN** the import SHALL NOT merge them (the source imports as a distinct package)
