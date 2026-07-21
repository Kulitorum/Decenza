# glass-chrome-option Specification

## Purpose

Defines translucent "glass" chrome as a boolean option that composes with any theme rather than
as a theme or a `themeMode` value, the requirement that turning it off restores every surface to
its opaque fill, and the single shared predicate — the option on, or a background image set —
that every translucent-chrome fill is gated on instead of hand-rolled background-image tests at
individual call sites.
## Requirements
### Requirement: Glass chrome is an option, not a theme or a mode
The app SHALL expose translucent "glass" chrome as a boolean setting that composes with any
theme. It SHALL NOT be a theme in `themeNames()`, and SHALL NOT be a value of `themeMode`.
`themeMode` SHALL remain `system` | `light` | `dark` and `isDarkMode` SHALL remain a boolean.

#### Scenario: Offered as a switch
- **WHEN** the user opens Machine → Theme Mode
- **THEN** a "Glass chrome" switch is shown, independent of the Dark theme and Light theme
  pickers

#### Scenario: Not a theme
- **WHEN** the theme list is read
- **THEN** it contains no "Glass" entry

#### Scenario: Composes with any theme
- **WHEN** the option is on and the user applies any theme, including one of their own
- **THEN** the chrome is translucent under that theme, and the option is still on

#### Scenario: Survives a mode switch
- **WHEN** the option is on and the app switches between light and dark mode
- **THEN** the option is still on

#### Scenario: Survives a background change
- **WHEN** the option is on and the user picks or clears a background
- **THEN** the option is still on

#### Scenario: Persists
- **WHEN** the user turns the option on and restarts the app
- **THEN** it is still on

### Requirement: Turning the option off restores every surface
Everything the option turns on it SHALL turn back off. No surface SHALL be pinned to the
translucent or neutral look by anything other than the option itself and an active background
image. In particular, selecting a background colour SHALL NOT hold any surface in the glass
look once the option is off.

#### Scenario: Off restores the accent chrome
- **WHEN** the option is turned on, a background colour is selected, and the option is then
  turned off
- **THEN** every surface returns to the fill it has with the option off — action tiles and
  action buttons back to their accent colours, not only the bars

#### Scenario: The background colour survives the toggle
- **WHEN** the option is toggled off and on again with a background colour selected
- **THEN** the colour stays selected, and the colours derived from it for legibility — text,
  icons, borders, the recessed inset fill — apply in both states

#### Scenario: The preview agrees with the page
- **WHEN** the option is toggled
- **THEN** the layout preview changes with it, because it resolves the same predicate rather
  than testing for a background image

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

