## MODIFIED Requirements

### Requirement: One dose field, whichever surface sets it

Every surface that offers a per-profile dose SHALL read and write the same profile field. No
surface may keep its own copy.

This is what the retired `recipe` block got wrong: it stored a second dose that the editors wrote
and nothing else read, so the value shown in an editor and the value the advisor saw could differ
without either being wrong. The Dose controls on the recipe editors, the advanced editor's
control, the parameter surface, the dialing context and the advisor now all resolve to
`recommended_dose` / `has_recommended_dose`.

**One field, one spelling.** The profile parameter edit surface SHALL accept exactly one name for
the per-profile dose — `dose`, which sets the value and enables the recommendation. The
`recommended_dose` and `has_recommended_dose` spellings SHALL NOT be accepted as edit inputs;
reporting still names both, because a reader needs the flag (see "A dose reported to a caller
carries its enabled flag").

This replaces the earlier rule that two spellings reaching one call must not both be discarded in
silence. That rule assumed both spellings would stay. Keeping them was the mistake: they had
different semantics — `dose` set-and-enable, `recommended_dose` set-only — so resolving a
collision toward the "canonical" spelling stored a dose with the recommendation left disabled, a
state no reader acts on. Removing the second spelling removes the collision rather than adjudicating
it.

An unrecognised spelling is reported as unrecognised by the surface's existing rule for unknown
fields, so a caller sending the old name is told, not silently ignored.

#### Scenario: An editor's dose control persists

- **WHEN** a dose is set from a recipe editor's Dose control and the profile is reloaded
- **THEN** the control shows the value that was set

#### Scenario: The same dose is visible to every reader

- **WHEN** a dose is set through any one surface
- **THEN** every other surface reporting a per-profile dose reports that same value

#### Scenario: The edit surface takes one name

- **WHEN** a caller sets `dose` on the profile parameter surface
- **THEN** the profile's recommended dose takes that value and its recommendation is enabled

#### Scenario: A retired spelling is reported, not silently applied

- **WHEN** a caller sends `recommended_dose` or `has_recommended_dose` to the profile parameter
  edit surface
- **THEN** the profile's dose is unchanged
- **AND** the response names the field as unrecognised

#### Scenario: An advanced profile takes a dose like any other

- **WHEN** a caller sets `dose` on a profile with no recipe editor type
- **THEN** the dose is applied and enabled, with no editor-type-specific handling
