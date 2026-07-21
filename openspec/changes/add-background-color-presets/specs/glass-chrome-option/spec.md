## ADDED Requirements

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

### Requirement: Translucent chrome is gated on one shared predicate
The app SHALL gate every translucent-chrome fill — cards, dialogs, bars, inset controls, action
tiles, action buttons and the layout item widgets — on a single predicate, true when the glass
option is on **or** a background image is set. Individual call sites SHALL NOT test the
background image path directly.

#### Scenario: A background image forces it on
- **WHEN** a background image is set and the glass option is off
- **THEN** the chrome is translucent, because opaque chrome over a photo reads as a slab

#### Scenario: Neither leaves everything unchanged
- **WHEN** no background image is set and the glass option is off
- **THEN** every chrome colour resolves to the opaque fill it used before this change

#### Scenario: Uniform across surfaces
- **WHEN** the predicate is true
- **THEN** no surface is left on its opaque no-background fill
