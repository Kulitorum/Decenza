## MODIFIED Requirements

### Requirement: Translucent chrome is gated on one shared predicate
The app SHALL gate every translucent-chrome fill — cards, dialogs, bars, inset controls, action
tiles, action buttons and the layout item widgets — on a single predicate, true when the glass
option is on **or** a background that requires it is active. A background image requires it; a shot-chart
background requires it while its render exists (with no shot to draw, the page is a flat colour
and scrimming over it would cancel the chrome's elevation for nothing). Individual call sites SHALL NOT test the background image path, or
any other individual background source, directly: adding a source SHALL be a change to the
predicate, not a sweep of its call sites.

#### Scenario: A background image forces it on
- **WHEN** a background image is set and the glass option is off
- **THEN** the chrome is translucent, because opaque chrome over a photo reads as a slab

#### Scenario: A shot-chart background forces it on
- **WHEN** a shot-chart background is active with a rendered chart, and the glass option is off
- **THEN** the chrome is translucent, because opaque chrome over a chart reads as a slab
  sitting on the data

#### Scenario: Neither leaves everything unchanged
- **WHEN** no background requiring it is set and the glass option is off
- **THEN** every chrome colour resolves to the opaque fill it used before this change

#### Scenario: Uniform across surfaces
- **WHEN** the predicate is true
- **THEN** no surface is left on its opaque no-background fill

#### Scenario: A new source costs one edit
- **WHEN** a background source that requires translucent chrome is added
- **THEN** only the shared predicate changes; no chrome call site is touched
