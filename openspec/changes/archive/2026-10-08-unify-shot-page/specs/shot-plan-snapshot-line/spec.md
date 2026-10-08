# Spec Delta

## MODIFIED Requirements

### Requirement: Shot pages show a Shot Plan snapshot line
The shot page SHALL display, directly beneath the title line (`<profile-or-recipe name> · <date>`), a single Shot Plan snapshot line rendering this shot's frozen dial-in as a sentence (e.g. `18.0g in · 42.0g · 88°C · Yemen West Haraz · grind 25 · 1400 rpm`), so the user can read a shot's key data at a glance without scrolling — including while stepping between shots to compare them.

#### Scenario: Shot with a full snapshot
- **WHEN** the user opens a shot whose record has dose, yield, temperature, bean, grind, and RPM
- **THEN** the snapshot line renders those values as a Shot Plan sentence beneath the title

#### Scenario: Comparing shots by swipe
- **WHEN** the user swipes from one shot to the next on the shot page
- **THEN** each shot's snapshot line updates to that shot's own values with the graph, requiring no scroll to read the key data
