## ADDED Requirements

### Requirement: dialInSessions SHALL be scoped to the resolved shot's equipment package

The shot history that `dialInSessions` is built from SHALL include only shots whose equipment
package matches the resolved shot's. The match SHALL treat "no package recorded" as a package
value in its own right, so that a shot with no equipment package matches other shots with no
equipment package and no others.

An equipment package identifies the grinder (brand, model, burrs), the basket, and the puck-prep
technique set together; changing any of them yields a different package. A different package
therefore excludes a shot from the history regardless of how similar its bean, profile, or grind
setting is: the same numeric grind setting on a different basket does not describe the same
extraction, so pooling them teaches a grind ordering that does not exist.

The bean, profile-knowledge-id and time-window scoping that already govern this history SHALL be
unchanged. Sessions SHALL continue to be grouped by time gaps within the matched set.

#### Scenario: Shots on a second basket are excluded from the session history

- **GIVEN** a user with two equipment packages sharing one grinder — package A with a
  straight-wall 18 g basket, package B with a stepped 58→46 mm basket
- **AND** a history containing shots on both packages, same bean and same profile
- **WHEN** `dialing_get_context` resolves a shot recorded on package B
- **THEN** `dialInSessions` SHALL contain only shots recorded on package B
- **AND** SHALL NOT contain any shot recorded on package A

#### Scenario: A user with no equipment package sees an unchanged history

- **GIVEN** a user who has never created an equipment package, so every shot has no package
  recorded
- **WHEN** `dialing_get_context` resolves any of their shots
- **THEN** `dialInSessions` SHALL contain the same shots it contained before this requirement —
  every bean/profile/window match
- **AND** no shot SHALL be excluded on equipment grounds

#### Scenario: A package fork empties the history until the new set accumulates shots

- **GIVEN** a user whose entire history is on one equipment package
- **WHEN** they change a component of that package — grinder burrs, basket, or puck prep — and
  pull a shot on the resulting new package
- **THEN** `dialInSessions` for that shot SHALL be empty
- **AND** subsequent shots on the new package SHALL accumulate into it normally

### Requirement: bestRecentShot SHALL be selected from the resolved shot's equipment package

The `bestRecentShot` anchor SHALL be selected only from shots whose equipment package matches
the resolved shot's, in addition to the profile, rating and time-window criteria it already
applies. "No package recorded" SHALL match other shots with no package recorded and nothing
else.

`bestRecentShot` is presented to the model as the outcome to reproduce, so an anchor from
different equipment is a target the user cannot hit at the settings it reports. When no rated
shot on the matching equipment exists in the window, the block SHALL be omitted rather than
falling back to a rated shot from other equipment.

#### Scenario: A highly rated shot on other equipment is not offered as the anchor

- **GIVEN** a user whose highest-rated recent shot on this profile was pulled on equipment
  package A
- **WHEN** `dialing_get_context` resolves a shot on package B, and package B has its own rated
  shots in the window
- **THEN** `bestRecentShot` SHALL be the highest-rated shot from package B
- **AND** SHALL NOT be the package A shot, however much higher its rating

#### Scenario: No rated shot on this equipment omits the block

- **GIVEN** a user with rated shots on package A only
- **WHEN** `dialing_get_context` resolves a shot on package B
- **THEN** `bestRecentShot` SHALL be absent from the response
- **AND** SHALL NOT fall back to the package A shot

### Requirement: grinderContext observed settings SHALL be scoped to the equipment package

The observed-settings list, explored range and typical step reported in `grinderContext` SHALL
be drawn only from shots on the resolved shot's equipment package, in addition to the grinder,
beverage-type and bean scoping already applied. "No package recorded" SHALL match other shots
with no package recorded and nothing else.

This list is what the model uses to judge whether a proposed setting is plausible for the user's
grinder. Pooling across packages presents settings from two baskets as one continuous range, so
a setting that is normal on one basket and absurd on another reads as equally reasonable.

The existing cross-bean fallback, which widens to all beans when the bean-scoped result is too
sparse, SHALL remain — but SHALL stay within the equipment package when it widens.

#### Scenario: Settings from another basket are not in the observed range

- **GIVEN** a user whose shots on package A used settings 7.5–10 and whose shots on package B
  used settings 16–17, all on one grinder
- **WHEN** `dialing_get_context` resolves a shot on package A
- **THEN** `grinderContext.settingsObserved` SHALL contain only the package A settings
- **AND** the reported explored range SHALL NOT extend to 17

#### Scenario: The cross-bean fallback stays within the package

- **GIVEN** a resolved shot on package A whose bean has fewer than two recorded settings
- **WHEN** `grinderContext` widens to the user's other beans
- **THEN** the widened result SHALL contain only shots from package A

### Requirement: grinderCalibration SHALL mine its pairs and anchor from one equipment package

The within-batch paired slopes that produce the UGS conversion key, and the current-batch anchor
shot the numeric recommendation is built on, SHALL be drawn only from shots on the resolved
shot's equipment package. The block SHALL NOT pool shots from other packages that happen to
share the grinder model and burrs.

This block emits a numeric grind setting presented as a recommendation, so a pair that straddles
two baskets does not merely widen an estimate — it produces a specific wrong number stated as
fact. When the package-scoped pool has too little signal to qualify, the block SHALL degrade to
directional guidance (finer / coarser, pull a reference shot) exactly as it already does for any
other insufficient-signal case, and SHALL NOT widen to other packages to recover a number.

#### Scenario: A cross-basket pair does not contribute to the conversion key

- **GIVEN** two shots of the same roast batch on the same grinder, one on package A and one on
  package B
- **WHEN** `grinderCalibration` mines within-batch pairs for a shot on package A
- **THEN** that pair SHALL NOT contribute to the conversion key
- **AND** only pairs whose members share package A SHALL contribute

#### Scenario: Insufficient package-scoped signal degrades to directional

- **GIVEN** a resolved shot whose equipment package has too few qualifying same-batch shots
- **AND** the user's other packages on the same grinder have plenty
- **WHEN** `grinderCalibration` is built
- **THEN** it SHALL report directional guidance only
- **AND** SHALL NOT emit a numeric setting derived from the other packages

## MODIFIED Requirements

### Requirement: dialInSessions SHALL hoist common shot identity to a session-level context

Each session in `dialInSessions` SHALL carry a `context` object holding the identity fields shared by every shot in the session: `grinderBrand`, `grinderModel`, `grinderBurrs`, `basketBrand`, `basketModel`, `puckPrep`, `beanBrand`, `beanType`, `frozenDate`, `defrostDate`, `storageHint`, `openedDate`. When a field's value is identical across all shots in the session, it SHALL appear in `context` only — not on the per-shot entries. When a field's value differs on a particular shot in the session, that shot's entry SHALL carry the field directly, overriding the session context for that shot.

The basket and puck-prep fields SHALL be present so the model can name the equipment the session's shots were pulled on. Because the history is scoped to one equipment package, they are shared across every shot in a session by construction and hoist to `context` in practice; the per-shot override mechanism SHALL still apply to them, so no reader depends on that being true.

The first shot of every session SHALL be the reference for the `context` object's values when at least one shot in the session has a non-empty value for the field. When no shot in the session has a non-empty value, the field SHALL be omitted from `context` entirely.

The session's `shotCount`, `sessionStart`, `sessionEnd`, and `shots[]` array SHALL remain. Per-shot entries SHALL continue to carry shot-variable fields (`id`, `timestamp`, `doseG`, `yieldG`, `durationSec`, `grinderSetting`, `notes`, `enjoyment0to100`, `temperatureOverrideC`, `targetWeightG`, `changeFromPrev`).

#### Scenario: All shots share identity → context has all fields, shots have no overrides

- **GIVEN** a 3-shot session where every shot has the same grinder (Niche Zero, 63mm Kony) and bean (Northbound Single Origin)
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context` SHALL contain `grinderBrand: "Niche"`, `grinderModel: "Zero"`, `grinderBurrs: "63mm Kony"`, `beanBrand: "Northbound"`, `beanType: "Single Origin"`
- **AND** none of `shots[0]`, `shots[1]`, `shots[2]` SHALL contain any of those five fields

#### Scenario: Session context names the basket and puck prep the shots were pulled on

- **GIVEN** a 3-shot session on an equipment package with a Graph Coffee "Stepped 58→46mm" basket and puck prep of shaker + puck screen + RDT
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context` SHALL contain `basketBrand: "Graph Coffee"` and `basketModel: "Stepped 58→46mm"`
- **AND** `session.context.puckPrep` SHALL carry the recorded technique set
- **AND** none of the per-shot entries SHALL carry those fields

#### Scenario: A package with no basket recorded omits the basket fields

- **GIVEN** a session whose equipment package has no basket recorded
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context` SHALL omit `basketBrand` and `basketModel` entirely
- **AND** SHALL NOT emit them as empty strings

#### Scenario: One shot in a session has a different bean → only that shot carries the override

- **GIVEN** a 3-shot session where shot 1 and 3 are on Northbound and shot 2 is on Prodigal
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context.beanBrand` SHALL be `"Northbound"` (the value shared by the first shot and at least half of the others)
- **AND** `shots[1]` SHALL carry `beanBrand: "Prodigal"` directly
- **AND** `shots[0]` and `shots[2]` SHALL NOT carry `beanBrand`
- **AND** the same logic SHALL apply field-by-field independently (a shared grinder with a differing bean still hoists the grinder fields)

#### Scenario: Single-shot session puts identity in context, shot is empty of identity

- **GIVEN** a session with one shot
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context` SHALL carry the shot's full identity (whichever of the twelve fields are non-empty)
- **AND** `shots[0]` SHALL NOT carry any of the hoisted fields

#### Scenario: A session spans a thaw event — the differing shot carries its own defrostDate

- **GIVEN** a 3-shot session where shots 1-2 were pulled before a thaw (`defrostDate = "2026-05-01"`) and shot 3 after a new thaw (`defrostDate = "2026-05-13"`)
- **WHEN** `dialing_get_context` builds the session
- **THEN** `session.context.defrostDate` SHALL be `"2026-05-01"` (the value shared by the first shot and the majority)
- **AND** `shots[2]` SHALL carry `defrostDate: "2026-05-13"` directly, using the identical override mechanism `beanBrand`/`grinderBrand` already use
