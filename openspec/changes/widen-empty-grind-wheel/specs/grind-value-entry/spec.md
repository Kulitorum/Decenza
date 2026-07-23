## MODIFIED Requirements

### Requirement: The picker SHALL open in text mode when the wheel cannot express the value

When the dialog opens and the grind wheel has no lattice to spin AND the grinder
has **no numeric basis**, it SHALL open with the text fields already shown and
focused rather than displaying an empty or unusable wheel. This applies exactly
when both a lattice and a numeric basis are absent: either there is no current
value and no observed history, or the current value cannot be parsed by the
stepper AND there is no observed history to anchor on.

An empty or unparseable current value WHEN the grinder **has** a numeric basis
SHALL stay on the wheels — but as a wide, median-anchored wheel, not a short
observed-history list (see the wide-wheel requirement below). Text mode is
reserved for the case where the wheel genuinely has nothing numeric to offer.

The RPM half SHALL NOT decide the mode (its rows always generate, seeded from
the neutral anchor when unset): the grind half is the trigger, and both halves
SHALL switch together, matching the single toggle.

#### Scenario: New bag with no history opens ready to type

- **GIVEN** a new bag with no grind value and no observed history for the grinder
- **WHEN** the grind control is activated
- **THEN** the picker SHALL open in text mode with the grind field focused
- **AND** for an RPM-capable grinder the RPM half SHALL be in text mode too — its always-generatable anchor rows SHALL NOT force wheel mode
- **AND** it SHALL NOT display a message directing the user to set a grind elsewhere

#### Scenario: Unparseable current value with no history opens in text mode

- **GIVEN** a recorded grind value the stepper cannot parse AND no observed history for the grinder
- **WHEN** the picker opens
- **THEN** it SHALL open in text mode showing that value, editable

#### Scenario: Typed first value populates the wheel

- **GIVEN** the picker opened in text mode with no prior value
- **WHEN** the user types a value and switches to the wheels
- **THEN** the wheels SHALL generate candidates centred on the typed value

## ADDED Requirements

### Requirement: An empty or unparseable grind SHALL open a wide wheel when the grinder has a numeric basis

When the grind value the picker opens on is empty or cannot be parsed into a
lattice, but the grinder has a **numeric basis** — it has observed numeric
settings in the user's history — the wheel SHALL synthesise the same wide,
effectively-unbounded window a set value would (hundreds of steps in each
direction), rather than a short list of observed settings.

The window SHALL be anchored on a numeric **anchor value**: the **median** of the
grinder's observed numeric settings (computed over the numeric subset, so a stray
text setting cannot skew it). Because the wheel's step is itself derived from the
same observed history, the user's habitual settings fall on the generated lattice
and appear naturally within the window; the window's width guarantees any value
is reachable by spinning. When the grinder has **no** observed numeric history
there is no numeric basis and the picker opens in text mode instead (see the
text-mode requirement above). The observed-history fallback that remains (an
unparseable value with history, on a grinder with no numeric anchor) SHALL NOT be
capped to a fixed number of rows.

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
