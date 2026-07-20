## ADDED Requirements

### Requirement: A new bag SHALL NOT start with an empty equipment package

The bag creation form SHALL default its equipment package so the grind control's
grinder context is never empty (an empty identity trips the unknown-grinder
"assume RPM-capable" fallback and offers the wrong picker): a re-buy prefill
SHALL keep the SOURCE bag's package — past beans keep the equipment they were
actually ground on — and every other create path SHALL default to the currently
active package. The user can still switch before saving. Applies to the in-app
dialog and the `/beans` web form alike; editing an existing bag keeps that bag's
own link, including "none".

#### Scenario: Manual new coffee defaults to the active package

- **GIVEN** an active equipment package and a manual "add a new coffee" entry
- **WHEN** the creation form opens (app or `/beans`)
- **THEN** the equipment row SHALL show the active package
- **AND** the grind picker SHALL resolve RPM capability against it, not the unknown-grinder fallback

#### Scenario: Re-buy keeps the source bag's package

- **GIVEN** a past bag linked to package A while package B is active
- **WHEN** the user re-buys that bag
- **THEN** the creation form SHALL open with package A
