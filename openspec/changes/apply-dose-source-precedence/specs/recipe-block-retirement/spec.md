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

A retired spelling is reported as RETIRED rather than merely unrecognised: it is not a typo, and
"unrecognised" alone sends the caller hunting for one instead of telling them what replaced it. The
report SHALL name the replacement, and SHALL appear in the response's human-readable message rather
than only in a sibling field — a client that reads the message and the success flag must not be
told a clean "updated" for a call that dropped an argument.

A call whose only inputs were retired spellings SHALL change nothing and SHALL NOT report success.
Reporting success there is not merely inaccurate: the surface's commit path marks the loaded profile
modified on the way out, so a fully rejected edit would dirty the profile and then invite the caller
to save the modification it never made.

**The dose input is validated, not coerced.** A value that cannot be read as a number SHALL be
rejected. `dose` is read directly as a number, and a failed read yields zero — which is the value
that CLEARS the recommendation, so silently coercing would delete a profile's dose on a malformed
call and report success. A value outside the accepted range is clamped, and the adjustment is
reported rather than the caller's number being echoed back as if stored.

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

- **WHEN** a caller sends `recommended_dose` or `has_recommended_dose` alongside other valid fields
- **THEN** the profile's dose is unchanged
- **AND** the response names the retired field and the replacement, in its message as well as in a
  dedicated field

#### Scenario: A call of nothing but retired spellings fails and changes nothing

- **WHEN** every field in a call is a retired spelling
- **THEN** the response does not report success
- **AND** the loaded profile is not marked modified

#### Scenario: A dose that is not a number is refused

- **WHEN** a caller sends a `dose` that cannot be read as a number
- **THEN** the call fails and the profile's recommended dose and enabled flag are both unchanged

#### Scenario: A dose outside the range is clamped and said so

- **WHEN** a caller sends a `dose` above the accepted maximum
- **THEN** the stored value is the maximum
- **AND** the response reports the adjustment

#### Scenario: An advanced profile takes a dose like any other

- **WHEN** a caller sets `dose` on a profile with no recipe editor type
- **THEN** the dose is applied and enabled, with no editor-type-specific handling
