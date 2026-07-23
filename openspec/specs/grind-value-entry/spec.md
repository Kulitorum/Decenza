# grind-value-entry Specification

## Purpose
TBD - created by archiving change replace-grind-inputs-with-picker. Update Purpose after archive.
## Requirements
### Requirement: A single shared control SHALL accept grind and RPM on every surface

Every surface that accepts a grinder dial-in value SHALL use one shared control
rather than a per-site text field. The control SHALL offer two presentations:
`pill` (the brew bar's existing capsule) and `field` (a bordered control sized to
drop in where a text input sits today). Both presentations are **tap-to-open**:
the control displays the current value and activating it opens the picker.
Adopting hosts SHALL NOT retain inline grind/RPM text inputs — all typing happens
inside the picker's text mode. The value, its writer, **and the grinder
identity the value belongs to** SHALL all be supplied by the host; the control
SHALL NOT read or write `Settings.dye` directly, so the same control serves the
live dial-in, a past shot's recorded value, a bag's default, and a recipe's
pinned value.

The four QML surfaces are `BrewDialog`, `PostShotReviewPage`,
`ChangeBeansDialog`, and `RecipeWizardPage`.

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

### Requirement: Every grinder-derived behaviour SHALL resolve against the value's own grinder

The grinder is a property of the value being edited, not of the application. Each
surface SHALL resolve step size, observed-setting suggestions, notation, and
RPM-capability against the grinder that **owns the value**, never against the
globally-active grinder:

| Surface | Grinder context |
|---|---|
| Brew bar pill / `BrewDialog` | the active grinder — it edits the live dial-in |
| `PostShotReviewPage` | the grinder recorded on **that shot** |
| `ChangeBeansDialog` | the **bag's** linked equipment |
| `RecipeWizardPage` | the **package selected for that recipe** |

Notation makes this stricter than a preference: stepping a compound value such as
`"3+2"` with a plain-numeric grinder's rules produces a wrongly *formatted*
result, not merely a wrong increment.

RPM-capability SHALL be determined by `Settings.dye.grinderRpmCapable(brand, model)`
on every surface — one function, called with each surface's own context — rather
than by a per-package boolean flag that can drift from it.

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

### Requirement: The picker SHALL offer keyboard entry behind a visible toggle

`GrindPickerDialog` SHALL provide a single control in its header that switches
both wheels to text fields and back. The control SHALL be visible whenever the
dialog is open; the dialog SHALL NOT require a hidden gesture (double-tap,
long-press) to reach text entry. The icon SHALL indicate the destination — a
keyboard glyph while the wheels are shown, a picker glyph while the fields are
shown.

One toggle SHALL switch both halves together, and the grind and RPM fields SHALL
remain separate inputs: grind SHALL accept free text, RPM SHALL accept digits
only.

Text entry SHALL NOT change the dialog's commit contract: typing SHALL apply
nothing, and the existing Done action SHALL remain the only commit path.

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

### Requirement: The wheel SHALL NOT gate what a grind value can be

A typed value SHALL be accepted verbatim and stored unchanged. The control SHALL
NOT round, snap, clamp, or reject a typed grind value on the basis of the step
size, the candidate rows, a grinder's printed dial maximum, a grinder's printed
dial minimum, or a notation it cannot parse.

Row generation SHALL NOT refuse a candidate for being negative on a
plain-numeric grinder. A grinder whose zero is a user-set calibration reference
(a stepless collar such as the Niche Zero) can legitimately be dialled finer than
zero, and both the numeric and compound parsers already accept a leading `-`.

The wheel's window SHALL be wide enough that spinning is effectively unbounded:
the user SHALL never have to close and reopen the picker to keep spinning toward
a reachable value (hundreds of steps in each direction, anchored on the current
value — a Niche `9` → `-1` move is 40 steps at 0.25 and must be one continuous
spin). The window size is an implementation buffer, not a limit; the only real
limits are semantic and live in the stepper — grinders whose **registry notation
is Compound** floor at zero (keyed on the grinder's notation and NOT on the
current value's written form: a negative linear position has no meaning on
click-indexed hardware, so negative candidates SHALL continue to be skipped —
including when the current value is logged as a plain number, e.g. `"2.5"` on a
Mignon), and letter notations clamp at their alphabet.

On returning from text mode to the wheels, the wheels SHALL re-seed centred on
the typed value, generating candidates around it rather than returning to the
previous lattice.

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

### Requirement: The picker SHALL open in text mode when the wheel cannot express the value

When the dialog opens and the grind wheel has no lattice to spin AND the grinder
has **no numeric basis**, it SHALL open with the text fields already shown and
focused rather than displaying an empty or unusable wheel. This applies exactly
when both a lattice and a numeric basis are absent: either there is no current
value and no observed history, or the current value cannot be parsed by the
stepper AND there is no observed history to anchor on.

An **empty** current value WHEN the grinder **has** observed numeric history
SHALL stay on the wheels — but as a wide, median-anchored wheel, not a short
observed-history list (see the wide-wheel requirement below). A **non-empty but
unparseable** current value SHALL also stay on the wheels when observed history
exists, but SHALL keep the user's OWN value as the centred row (via the
observed-history fallback), never re-anchored on the median — re-centring on a
different value would let an untouched Done silently overwrite a value the user
actually set. Text mode is reserved for the case where the wheel genuinely has
nothing to offer: no lattice and no observed history.

Because the grind wheel's candidate history is loaded asynchronously, the mode
decision at open MAY resolve to text mode on a cold cache even when a wide wheel
will become available. When the history warms while the dialog is open, an
auto-entered text mode SHALL be promoted to the wheel (text → wheel only, never
the reverse), so the first open after app start is not left stranded in text
mode with the keyboard up. A user who has themselves switched to, or begun
typing in, text mode SHALL NOT be promoted.

The RPM half SHALL NOT decide the mode (its rows always generate, seeded from
the neutral anchor when unset): the grind half is the trigger, and both halves
SHALL switch together, matching the single toggle.

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

### Requirement: Grind and RPM entry semantics SHALL be uniform across surfaces

All surfaces accepting a dial-in SHALL agree on the following, by inheriting them
from the shared control rather than restating them per site:

- RPM "unset" SHALL be represented as `0`, not an empty string.
- Observed grind settings SHALL be offered as picker candidates (via the
  observed-history fallback), resolved against the surface's own grinder context
  (see the grinder-context requirement above).
- Soft-keyboard avoidance SHALL be owned once, by the picker — which contains the
  only text inputs — rather than by per-field registration on each host surface.

These are uniform in *mechanism*. They are deliberately NOT uniform in *result*:
two surfaces showing different suggestions because they own different grinders is
correct behaviour, not drift.

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

### Requirement: Committing an emptied grind SHALL clear the value where blank is meaningful

Committing Done with an emptied grind or RPM field SHALL clear the stored value
(grind to empty, RPM to `0`) on EVERY host, with no exceptions: the picker SHALL
treat an empty commit as explicit input, not as input to be ignored. Blank is
load-bearing — a recipe with no pinned grind adopts the linked bag's dial on
create, and a bag's or a shot's grind may simply be unset — and even where blank
carries no special meaning, emptying a field and pressing Done is a deliberate
act that SHALL NOT be silently discarded.

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

### Requirement: Web grind and RPM inputs SHALL offer the same stepped candidates

The ShotServer shot, bag, and recipe edit forms SHALL offer the stepped candidate
values through a native `<input list>` + `<datalist>` pair rather than a
free-standing text input. Free text SHALL remain accepted, so values the
candidate list does not contain — including non-numeric notations — SHALL still be
enterable and saved. The markup SHALL be produced by a shared helper rather than
inlined per page.

Candidates SHALL be computed server-side by the existing C++ stepping machinery,
resolved against **each record's own grinder** — the shot's, the bag's linked
package, the recipe's selected package — never the active one. The stepping
logic SHALL NOT be reimplemented in JavaScript. They are served by a dedicated
`GET /api/grind-candidates` endpoint taking the grinder identity as parameters,
rather than embedded per record in the list payloads: the bag and recipe dialogs
let the user switch equipment mid-edit, and embedded candidates would go stale —
the page re-requests when the selected equipment changes, so candidates follow
the grinder the value will actually be ground on.

The web surfaces SHALL NOT reproduce the wheel. This is a deliberate divergence
from the in-app control: the wheel is a touch-first affordance, while the web
forms are used with a keyboard where a text input with candidates is the better
control.

The web RPM input SHALL follow the same capability gate as the app forms,
sourced from the same function: the candidates endpoint fills RPM candidates
only when `grinderRpmCapable()` holds for the record's grinder, and an empty
RPM candidate list SHALL hide the RPM field's row (the input stays in the DOM,
loaded and saved, so a stale stored RPM is never silently cleared).

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

When the grind value the picker opens on is **empty**, but the grinder has
observed **numeric** settings in the user's history, the wheel SHALL synthesise
the same wide, effectively-unbounded window a set value would (hundreds of steps
in each direction), rather than a short list of observed settings.

The window SHALL be anchored on a numeric **anchor value**: the **median** of the
grinder's observed numeric settings (computed over the numeric subset, so a stray
text setting cannot skew it). Because the wheel's step is itself derived from the
same observed history, the user's habitual settings fall on the generated lattice
and appear naturally within the window; the window's width guarantees any value
is reachable by spinning. When the grinder has **no** observed numeric history
(including a grinder whose entire history is compound `a+b` notation) there is no
anchor and the picker opens on the observed-history fallback, or in text mode
when there is nothing to offer (see the text-mode requirement above).

The median anchor applies ONLY to an empty value; a non-empty unparseable value
keeps its own value via the observed-history fallback (see that requirement). The
observed-history fallback SHALL NOT be capped to a fixed number of rows.

The wheel SHALL open centred on the anchor. Because grind commits the centred
value on Done (grind has no neutral-anchor placeholder gate), pressing Done
without spinning SHALL commit the anchor (the median) as the grind — an empty
grind SHALL NOT be committed from this state.

This behaviour SHALL be inherited from the shared control by every adopting
surface; in practice only the empty-open case (a new recipe with no grind yet)
changes, since the other surfaces edit a value that already exists.

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

