# grind-value-entry Specification

## Purpose

Governs how grind and RPM values are entered and edited on every surface, in the app and the ShotServer. One shared picker control serves all surfaces, steps resolve against the grinder that owns each value, and an emptied grind is treated as an explicit commit.

## Requirements
### Requirement: A single shared control SHALL accept grind and RPM on every surface
Every surface that accepts a grinder dial-in value SHALL use one shared control rather than a per-site text field. The control SHALL offer `pill` (the brew bar's capsule) and `field` (a bordered control sized to drop in where a text input sits today). Both SHALL be tap-to-open, displaying the current value and opening the picker when activated.

#### Scenario: Field presentation opens the picker

- **GIVEN** a surface using the `field` presentation
- **WHEN** the user activates the control
- **THEN** the picker SHALL open
- **AND** the surface SHALL NOT offer an inline text input for the same value

#### Scenario: Host owns the value

- **GIVEN** the control is placed on a surface that edits a past shot's recorded grind
- **WHEN** the user commits a value
- **THEN** the control SHALL emit the picked value to its host
- **AND** it SHALL NOT write `Settings.dye.dyeGrinderSetting` or `Settings.dye.dyeGrinderRpm` of its own accord

#### Scenario: Brew bar keeps its pill

- **GIVEN** the `grindQuickSelect` layout widget
- **WHEN** it is rendered after adopting the shared control
- **THEN** its appearance, zone handling, background-image treatment and write-through path SHALL be unchanged

### Requirement: Hosts own the value and grinder identity
The value, its writer and the grinder identity the value belongs to SHALL all be supplied by the host. The control SHALL NOT read or write `Settings.dye` directly, so one control serves the live dial-in, a past shot's value, a bag's default and a recipe's pinned value. Adopting hosts SHALL NOT retain inline grind or RPM text inputs; all typing SHALL happen inside the picker's text mode.

#### Scenario: Past shot value uses the same control
- **WHEN** a past shot's grind value is edited
- **THEN** the shared control opens with the host's value and grinder identity, not the live dial-in

### Requirement: Adopting surfaces
The adopting QML surfaces SHALL be `BrewDialog`, `PostShotReviewPage`, `ChangeBeansDialog` and `RecipeWizardPage`.

#### Scenario: Each adopting surface opens the shared picker
- **WHEN** a grind or RPM value is edited on any of the four surfaces
- **THEN** the shared picker control is used

### Requirement: Every grinder-derived behaviour SHALL resolve against the value's own grinder
The grinder is a property of the value being edited, not of the application. Each surface SHALL resolve step size, observed-setting suggestions, notation and RPM capability against the grinder that **owns the value**, never against the globally-active grinder.

#### Scenario: Reviewing an old shot uses that shot's grinder

- **GIVEN** a shot recorded on a compound-notation grinder, and a different, plain-numeric grinder now active
- **WHEN** the user edits that shot's grind in post-shot review
- **THEN** the step, the suggestions, the notation and the RPM gate SHALL all resolve against the shot's grinder
- **AND** the value SHALL be stepped and formatted in the shot's grinder's notation

#### Scenario: The recipe editor uses the recipe's grinder

- **GIVEN** a recipe whose selected equipment package differs from the active grinder
- **WHEN** the user edits the recipe's grind or RPM
- **THEN** both SHALL resolve against the recipe's selected package
- **AND** changing the active grinder elsewhere SHALL NOT change what the recipe editor offers

#### Scenario: Capability comes from one function, per-context arguments

- **GIVEN** a package whose stored `rpmCapable` flag disagrees with `grinderRpmCapable(brand, model)` for the same grinder
- **WHEN** the RPM half's visibility is decided
- **THEN** it SHALL follow `grinderRpmCapable()` called with that surface's own grinder identity

### Requirement: Each surface resolves against its owning grinder context
The brew bar pill and `BrewDialog` SHALL use the active grinder, since they edit the live dial-in. `PostShotReviewPage` SHALL use the grinder recorded on that shot. `ChangeBeansDialog` SHALL use the bag's linked equipment. `RecipeWizardPage` SHALL use the package selected for that recipe.

#### Scenario: Reviewing an old shot uses that shot's grinder
- **WHEN** an old shot's grind is edited in `PostShotReviewPage`
- **THEN** steps and suggestions resolve against the grinder recorded on that shot

### Requirement: Notation resolves against the owning grinder
Notation SHALL be resolved against the owning grinder. Stepping a compound value such as `"3+2"` with a plain-numeric grinder's rules produces a wrongly formatted result, not merely a wrong increment.

#### Scenario: Compound value stepped with the owning grinder's notation
- **WHEN** a compound value such as `"3+2"` is stepped on a compound-notation grinder
- **THEN** the result keeps the compound format

### Requirement: RPM capability comes from one function
RPM capability SHALL be determined by `Settings.dye.grinderRpmCapable(brand, model)` on every surface, called with each surface's own context, rather than by a per-package boolean flag that can drift from it.

#### Scenario: Capability is asked per context
- **WHEN** RPM capability is needed on a surface
- **THEN** `grinderRpmCapable` is called with that surface's own grinder context

### Requirement: The picker SHALL offer keyboard entry behind a visible toggle
`GrindPickerDialog` SHALL provide a single control in its header that switches both wheels to text fields and back. The control SHALL be visible whenever the dialog is open, and SHALL NOT require a hidden gesture (double-tap, long-press) to reach text entry. Its icon SHALL show the destination: a keyboard glyph while the wheels are shown, a picker glyph while the fields are shown.

#### Scenario: Toggle is visible and reversible

- **WHEN** the picker is open on the wheels
- **THEN** a keyboard-glyph button SHALL be shown in the header
- **AND** activating it SHALL replace both wheels with text fields and change the icon to a picker glyph
- **AND** activating it again SHALL return to the wheels

#### Scenario: Typing does not commit

- **GIVEN** the picker is in text mode with a typed grind value
- **WHEN** the user activates Cancel
- **THEN** no value SHALL be written
- **AND** activating Done instead SHALL write the typed value

#### Scenario: Separate input modes per half

- **GIVEN** an RPM-capable grinder and the picker in text mode
- **WHEN** the two fields are shown
- **THEN** the grind field SHALL accept arbitrary text
- **AND** the RPM field SHALL accept digits only

### Requirement: One toggle switches separate grind and RPM fields
One toggle SHALL switch both halves together. The grind and RPM fields SHALL remain separate inputs: grind SHALL accept free text, and RPM SHALL accept digits only.

#### Scenario: RPM field rejects non-digits
- **WHEN** the user types a non-digit into the RPM field
- **THEN** the input does not accept it

### Requirement: Typing never commits
Text entry SHALL NOT change the commit contract: typing SHALL apply nothing, and the existing Done action SHALL remain the only commit path.

#### Scenario: Done commits typed text
- **WHEN** the user types a value in text mode and presses Done
- **THEN** the typed value is committed by Done, and not before

### Requirement: The wheel SHALL NOT gate what a grind value can be
A typed value SHALL be accepted verbatim and stored unchanged. The control SHALL NOT round, snap, clamp or reject a typed grind value on the basis of the step size, the candidate rows, a grinder's printed dial maximum or minimum, or a notation it cannot parse.

#### Scenario: Off-step value survives

- **GIVEN** a grinder whose history-derived step is `0.25` and a current value of `8`
- **WHEN** the user types `8.13` and commits
- **THEN** the stored value SHALL be `8.13`, not `8.25` or `8`

#### Scenario: Re-seed rebases the ladder on the typed value

- **GIVEN** a step of `0.25` and a typed value of `8.13`
- **WHEN** the user switches back to the wheels
- **THEN** the offered candidates SHALL be centred on `8.13` (…`7.88`, `8.13`, `8.38`…)
- **AND** the wheel SHALL NOT show `8.13` snapped onto the previous `0.25` lattice

#### Scenario: Value beyond a printed dial maximum is accepted

- **GIVEN** a Niche Zero, whose dial is printed 0–50 but is stepless and turns past 50
- **WHEN** the user types a filter-range value above `50`
- **THEN** it SHALL be stored unchanged and SHALL NOT be clamped or flagged

#### Scenario: Finer than zero is reachable on a stepless collar

- **GIVEN** a Niche Zero at `0.25` with a step of `0.25`, whose zero is a user-set calibration reference
- **WHEN** the wheel builds its candidates
- **THEN** the offered values SHALL include negatives (…`-0.5`, `-0.25`, `0`, `0.25`…)
- **AND** the user SHALL be able to keep spinning finer rather than being stopped at `0`

#### Scenario: Spinning is not bounded by the window

- **GIVEN** the same grinder at `9`
- **WHEN** the user spins the wheel down continuously
- **THEN** `-1` SHALL be reachable in that one picker session, without closing and reopening the dialog

#### Scenario: Compound notation still skips negatives

- **GIVEN** a click-indexed compound grinder (e.g. a 1Zpresso) at a low setting
- **WHEN** stepping would produce a negative position
- **THEN** that candidate SHALL be skipped, as today
- **AND** the same SHALL hold when its current value is written as a plain number rather than `a+b`

#### Scenario: Unparseable notation is stored as text

- **WHEN** the user types a value the stepper cannot parse (e.g. `medium-fine`)
- **THEN** it SHALL be stored unchanged
- **AND** the wheel SHALL fall back to observed history rather than refusing the value

### Requirement: Plain-numeric candidates may be negative
Row generation SHALL NOT refuse a candidate for being negative on a plain-numeric grinder. A stepless collar grinder whose zero is a user-set calibration reference (such as the Niche Zero) can legitimately be dialled finer than zero, and both parsers already accept a leading `-`.

#### Scenario: Finer than zero is reachable on a stepless collar
- **WHEN** a stepless-collar grinder's value is stepped below zero
- **THEN** negative candidates are offered and stored

### Requirement: The wheel window is effectively unbounded
The wheel's window SHALL be wide enough that spinning is effectively unbounded. The user SHALL never have to close and reopen the picker to keep spinning toward a reachable value, so the window spans hundreds of steps each way, anchored on the current value. The window is an implementation buffer, not a limit.

#### Scenario: Spinning across the window does not require reopening
- **WHEN** the user spins the wheel toward a reachable value far from the current one
- **THEN** the spin continues without closing the picker

### Requirement: Stepper floors are keyed on the grinder's notation
The only real limits SHALL live in the stepper. Grinders whose registry notation is Compound SHALL floor at zero, keyed on the grinder's notation and NOT on the current value's written form, so negative candidates are still skipped even when the current value is logged as a plain number. Letter notations SHALL clamp at their alphabet.

#### Scenario: Compound grinder logged as a plain number still floors at zero
- **WHEN** a Compound-notation grinder's current value is logged as a plain number such as `2.5`
- **THEN** negative candidates are still skipped

### Requirement: Returning from text mode re-seeds the wheel
On returning from text mode to the wheels, the wheels SHALL re-seed centred on the typed value, generating candidates around it rather than returning to the previous lattice.

#### Scenario: Re-seed centres on the typed value
- **WHEN** the user returns from text mode to the wheels after typing a value
- **THEN** the wheels are centred on the typed value

### Requirement: The picker SHALL open in text mode when the wheel cannot express the value
When the dialog opens and the grind wheel has no lattice to spin AND the grinder has **no numeric basis**, it SHALL open with the text fields already shown and focused, rather than an empty or unusable wheel. This applies exactly when a lattice and a numeric basis are both absent: no current value and no observed history, or an unparseable current value with no observed history to anchor on.

#### Scenario: New bag opens ready to type

- **GIVEN** a new bag with no grind value and no observed history for the grinder
- **WHEN** the grind control is activated
- **THEN** the picker SHALL open in text mode with the grind field focused
- **AND** for an RPM-capable grinder the RPM half SHALL be in text mode too — its always-generatable anchor rows SHALL NOT force wheel mode
- **AND** it SHALL NOT display a message directing the user to set a grind elsewhere

#### Scenario: Unparseable current value opens in text mode

- **GIVEN** a recorded grind value the stepper cannot parse AND no observed history for the grinder
- **WHEN** the picker opens
- **THEN** it SHALL open in text mode showing that value, editable

#### Scenario: Typed first value populates the wheel

- **GIVEN** the picker opened in text mode with no prior value
- **WHEN** the user types a value and switches to the wheels
- **THEN** the wheels SHALL generate candidates centred on the typed value

### Requirement: Empty or unparseable values with history stay on the wheel
An **empty** current value, when the grinder has observed numeric history, SHALL stay on the wheels as a wide, median-anchored wheel. A **non-empty but unparseable** value SHALL also stay on the wheels when history exists, keeping the user's own value as the centred row via the observed-history fallback. It SHALL NOT be re-anchored on the median.

#### Scenario: Unparseable value keeps its own value
- **WHEN** a non-empty unparseable value is opened and observed history exists
- **THEN** the user's value is the centred row and is not replaced by the median

### Requirement: Text mode is reserved for a wheel with nothing to offer
Text mode SHALL be reserved for when the wheel has nothing to offer: no lattice and no observed history. Because candidate history loads asynchronously, an auto-entered text mode SHALL be promoted to the wheel when history warms while the dialog is open, text to wheel only and never the reverse. A user who has switched to or begun typing in text mode SHALL NOT be promoted.

#### Scenario: Cold cache is promoted when history warms
- **WHEN** the dialog opened in auto-entered text mode and candidate history then loads
- **THEN** the picker promotes to the wheel

### Requirement: The RPM half does not decide the picker mode
The RPM half SHALL NOT decide the mode, since its rows always generate, seeded from the neutral anchor when unset. The grind half is the trigger, and both halves SHALL switch together.

#### Scenario: Grind half triggers the mode for both halves
- **WHEN** the grind half falls back to text mode
- **THEN** the RPM half switches to text mode with it

### Requirement: Grind and RPM entry semantics SHALL be uniform across surfaces
All surfaces accepting a dial-in SHALL agree on the following by inheriting them from the shared control, not restating them per site. RPM "unset" SHALL be represented as `0`, not an empty string. Soft-keyboard avoidance SHALL be owned once, by the picker, which contains the only text inputs.

#### Scenario: Unset RPM is uniform

- **GIVEN** a bag form and a shot-review form, both with no RPM recorded
- **WHEN** each reads its RPM value
- **THEN** both SHALL represent the unset state as `0`

#### Scenario: Suggestions are available where a value is created

- **GIVEN** observed grind settings exist for the grinder that surface owns
- **WHEN** the user creates a new bag or a new recipe
- **THEN** those observed settings SHALL be offered

#### Scenario: Keyboard avoidance lives in the picker, not the hosts

- **GIVEN** any adopting surface on a touch device
- **WHEN** the user reaches text entry
- **THEN** the typing SHALL happen inside the picker, whose keyboard avoidance keeps the focused field and the commit actions visible
- **AND** no host surface SHALL require its own grind/RPM keyboard registration

### Requirement: Observed settings are candidates in the owning grinder's context
Observed grind settings SHALL be offered as picker candidates via the observed-history fallback, resolved against the surface's own grinder context. Results MAY differ between surfaces only because they own different grinders.

#### Scenario: Two surfaces with different grinders show different suggestions
- **WHEN** two surfaces own different grinders
- **THEN** their suggestions may differ, and that difference is correct

### Requirement: Committing an emptied grind SHALL clear the value where blank is meaningful
Committing Done with an emptied grind or RPM field SHALL clear the stored value (grind to empty, RPM to `0`) on EVERY host, with no exceptions. The picker SHALL treat an empty commit as explicit input, which SHALL NOT be silently discarded.

#### Scenario: Recipe blank-adopts-bag survives the picker

- **GIVEN** a recipe being created with a linked bag
- **WHEN** the user clears the grind field in the picker and commits
- **THEN** the recipe SHALL be created with no pinned grind, adopting the bag's dial as today

#### Scenario: Bag grind can be cleared

- **GIVEN** a bag with a recorded grind default
- **WHEN** the user empties the grind field and commits
- **THEN** the bag's grind SHALL be stored as unset

#### Scenario: The live dial-in is clearable from the pill

- **GIVEN** the brew-bar pill's picker in text mode, with an RPM set
- **WHEN** the user empties the RPM field and commits
- **THEN** the live dial-in's RPM SHALL be cleared to `0`
- **AND** the pill SHALL stop showing the RPM half

### Requirement: Blank is a valid stored grind value
Blank SHALL remain a valid stored grind value. A recipe with no pinned grind adopts the linked bag's dial on create, and a bag's or a shot's grind may simply be unset.

#### Scenario: Unset bag grind stays unset
- **WHEN** a bag has no grind value
- **THEN** the grind stays empty and nothing is substituted

### Requirement: Web grind and RPM inputs SHALL offer the same stepped candidates
The ShotServer shot, bag and recipe edit forms SHALL offer stepped candidate values through a native `<input list>` and `<datalist>` pair, not a free-standing text input. Free text SHALL remain accepted, so values outside the candidate list, including non-numeric notations, SHALL still be enterable and saved. The markup SHALL be produced by a shared helper, not inlined per page.

#### Scenario: RPM field hidden for a non-RPM grinder on the web

- **GIVEN** a bag whose selected package is a Niche Zero (not RPM-capable)
- **WHEN** the `/beans` edit dialog resolves its grind candidates
- **THEN** the RPM field SHALL be hidden
- **AND** selecting an RPM-capable package SHALL bring it back

#### Scenario: Candidates offered on the web

- **GIVEN** a grind value with a derivable step
- **WHEN** the shot, bag, or recipe edit form is rendered
- **THEN** the grind input SHALL offer the stepped candidates as a `<datalist>`

#### Scenario: Web candidates resolve against the record's grinder

- **GIVEN** a shot recorded on a different grinder than the currently active one
- **WHEN** its web edit form is rendered
- **THEN** the offered candidates SHALL derive from the shot's grinder, not the active one

#### Scenario: Free text still accepted on the web

- **WHEN** the user types a grind value absent from the candidate list
- **THEN** the form SHALL accept and save it unchanged

#### Scenario: Shared helper, not per-page markup

- **WHEN** the three forms render their grind inputs
- **THEN** the markup SHALL come from one shared helper in the common ShotServer layer

### Requirement: Web candidates are computed server-side per record
Candidates SHALL be computed server-side by the existing C++ stepping machinery, resolved against each record's own grinder (the shot's, the bag's linked package, the recipe's selected package), never the active one. The stepping logic SHALL NOT be reimplemented in JavaScript.

#### Scenario: Candidates for a bag follow the bag's linked package
- **WHEN** a bag's candidates are requested
- **THEN** they are computed against the bag's linked package

### Requirement: Candidates come from a dedicated endpoint
Candidates SHALL be served by a dedicated `GET /api/grind-candidates` endpoint taking the grinder identity as parameters, not embedded per record in list payloads. The page SHALL re-request when the selected equipment changes, so candidates follow the grinder the value will be ground on.

#### Scenario: Changing equipment refreshes candidates
- **WHEN** the user changes the selected equipment in a bag or recipe dialog
- **THEN** the page re-requests candidates for the new grinder

### Requirement: The web does not reproduce the wheel
The web surfaces SHALL NOT reproduce the wheel. The wheel is a touch-first affordance, and the web forms are used with a keyboard, where a text input with candidates is the better control.

#### Scenario: Web grind input is a text input with candidates
- **WHEN** a grind value is edited on the web
- **THEN** a text input with candidates is used instead of a wheel

### Requirement: The web RPM input follows the capability gate
The web RPM input SHALL follow the same capability gate as the app forms, sourced from `grinderRpmCapable()`. The candidates endpoint SHALL fill RPM candidates only when that gate holds for the record's grinder. An empty RPM candidate list SHALL hide the RPM field's row, while the input stays in the DOM, loaded and saved, so a stale stored RPM is never silently cleared.

#### Scenario: Stale stored RPM survives a hidden row
- **WHEN** the RPM row is hidden because the grinder is not RPM-capable
- **THEN** the stored RPM value is still loaded and saved

### Requirement: The picker's text mode SHALL be accessible and usable on touch devices

The header toggle SHALL expose a Button role, an accessible name reflecting its
destination, and a press action. Each text field SHALL expose an editable-text
role and an accessible name identifying which half it edits. Because the picker
is a modal dialog, it SHALL provide its own means of dismissing the soft keyboard
and SHALL keep the focused field and the commit actions visible while the
keyboard is shown.

#### Scenario: Toggle is announced by its destination

- **WHEN** a screen reader inspects the toggle while the wheels are shown
- **THEN** it SHALL expose a Button role and a name indicating it opens text entry
- **AND** while the fields are shown, a name indicating it returns to the picker

#### Scenario: Fields are identified

- **WHEN** a screen reader inspects the two text fields
- **THEN** each SHALL expose an editable-text role and a name identifying it as the grind or the RPM field

#### Scenario: Keyboard can be dismissed inside the modal

- **GIVEN** the picker is in text mode on a touch device with the keyboard shown
- **WHEN** the user needs to reach Done
- **THEN** the dialog SHALL provide a way to dismiss the keyboard from within the modal
- **AND** the commit actions SHALL remain reachable

### Requirement: An empty grind SHALL open a wide wheel when the grinder has observed numeric history
When the grind value the picker opens on is **empty** but the grinder has observed **numeric** settings in the user's history, the wheel SHALL synthesise the same wide, effectively-unbounded window a set value would. The window SHALL be anchored on the **median** of the grinder's observed numeric settings, computed over the numeric subset, and the wheel SHALL open centred on the anchor.

#### Scenario: New recipe with grinder history opens on a wide wheel

- **GIVEN** a new recipe whose grind is empty and whose selected grinder has observed numeric settings (e.g. a DF83V with logged settings `0.2, 3, 4 … 9`)
- **WHEN** the grind control is activated
- **THEN** the picker SHALL open on the wheels, centred on the median observed setting
- **AND** the user SHALL be able to spin hundreds of steps in each direction without closing and reopening the picker
- **AND** the wheel SHALL NOT be limited to the grinder's ~10 observed settings

#### Scenario: Habitual settings appear on the wide wheel

- **GIVEN** the wide, median-anchored wheel for an empty grind, whose step is derived from the grinder's observed history
- **WHEN** the user spins toward their usual settings
- **THEN** those on-lattice observed values SHALL appear as selectable rows within the window

#### Scenario: Done without spinning commits the median

- **GIVEN** the wheel opened centred on the median anchor for an empty grind value
- **WHEN** the user presses Done without moving the grind wheel
- **THEN** the median SHALL be committed as the grind
- **AND** an empty grind SHALL NOT be written from this state

#### Scenario: Unparseable value with history keeps its value and offers uncapped history

- **GIVEN** a grind value the stepper cannot parse AND observed history for the grinder
- **WHEN** the picker opens
- **THEN** the wheel SHALL keep that value as its centred, selectable row
- **AND** the offered observed settings SHALL NOT be truncated to a fixed count

### Requirement: The wheel step comes from the same observed history
The wheel's step SHALL be derived from the same observed history, so the user's habitual settings fall on the generated lattice and appear within the window.

#### Scenario: Habitual settings appear on the wide wheel
- **WHEN** the wide wheel opens for a grinder with observed numeric history
- **THEN** the habitual settings are on the lattice within the window

### Requirement: No numeric history means no anchor
A grinder with **no** observed numeric history, including one whose entire history is compound `a+b` notation, SHALL have no anchor. The picker SHALL open on the observed-history fallback, or in text mode when there is nothing to offer. The median anchor SHALL apply ONLY to an empty value. The observed-history fallback SHALL NOT be capped to a fixed number of rows.

#### Scenario: Compound-only history has no anchor
- **WHEN** a grinder's entire history is compound notation and the value is empty
- **THEN** there is no median anchor and the picker opens on the fallback or text mode

### Requirement: Done without spinning commits the anchor
Pressing Done without spinning SHALL commit the median anchor as the grind. An empty grind SHALL NOT be committed from this state.

#### Scenario: Done commits the anchor not an empty value
- **WHEN** the wide wheel opens on the median and Done is pressed without spinning
- **THEN** the median is committed as the grind

### Requirement: The wide-wheel behaviour is inherited by every surface
This behaviour SHALL be inherited from the shared control by every adopting surface. In practice only the empty-open case, a new recipe with no grind yet, changes, since the other surfaces edit a value that already exists.

#### Scenario: Other surfaces keep their existing value
- **WHEN** an existing grind value is edited on any adopting surface
- **THEN** the wide-wheel anchor is not applied

