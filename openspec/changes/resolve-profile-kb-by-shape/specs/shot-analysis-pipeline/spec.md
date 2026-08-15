## ADDED Requirements

### Requirement: The Shot Summary affordance SHALL be reachable for every shot

The affordance that opens the Shot Summary dialog SHALL be present on the post-shot review page and the shot
detail page for every shot, regardless of whether the shot's profile resolved to a KB entry and regardless
of whether any quality badge fired. Its visibility SHALL NOT be conditioned on KB resolution.

This follows from what the dialog contains: its lines are computed from the shot's own captured curves and
do not depend on the KB. Hiding the affordance on an unresolved shot withholds the analysis precisely where
the analysis is weakest and where the user has least other information.

The existing badge chips SHALL be unaffected: the flag chips SHALL continue to appear only when their flag
fired, and the clean-extraction chip SHALL continue to appear only when no flag fired.

#### Scenario: Clean shot on an unresolved profile still offers the summary

- **GIVEN** a shot whose profile resolves to no KB entry and on which no quality-badge flag fired
- **WHEN** the post-shot review page or the shot detail page is shown
- **THEN** the Shot Summary affordance SHALL be present and SHALL open the dialog

#### Scenario: Badge chips keep their own conditions

- **GIVEN** any shot
- **WHEN** its badge row is shown
- **THEN** each flag chip SHALL appear only if its flag fired, and the clean-extraction chip SHALL appear
  only if no flag fired

#### Scenario: The affordance tint is unchanged

- **GIVEN** a shot whose recomputed verdict category is exactly `clean`
- **WHEN** the affordance is shown
- **THEN** it SHALL be untinted, per the existing affordance-tint requirement, which this requirement does
  not modify
