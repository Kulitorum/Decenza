## ADDED Requirements

### Requirement: Glass is a built-in theme, not a third theme mode
The app SHALL ship a built-in theme named "Glass" that appears in `themeNames()` alongside
"Default Dark" and "Default Light" and any user-saved themes. Glass SHALL be selectable wherever
a theme name is selectable — the Dark theme picker, the Light theme picker, and the theme
gallery. `themeMode` SHALL remain `system` | `light` | `dark`, and `isDarkMode` SHALL remain a
boolean; Glass SHALL NOT introduce a third mode or a third palette polarity.

#### Scenario: Glass is offered in both pickers
- **WHEN** the user opens the Dark theme or Light theme picker in Machine → Theme Mode
- **THEN** "Glass" is listed among the available themes

#### Scenario: Glass carries both polarities
- **WHEN** Glass is applied to the dark slot, and separately to the light slot
- **THEN** each slot receives the corresponding Glass palette, the same way a user-saved theme
  supplies `colorsDark` and `colorsLight`

#### Scenario: Follow-system still works
- **WHEN** Glass is assigned to one slot, a different theme to the other, and "Follow system
  theme" is on
- **THEN** the app switches between the two slots on the OS signal exactly as it does today

#### Scenario: Glass is marked as built in
- **WHEN** the theme gallery lists available themes
- **THEN** Glass is reported with the same built-in marker "Default Dark" and "Default Light"
  carry

### Requirement: Glass turns on the translucent chrome without requiring a background
The translucent chrome predicate SHALL be true whenever the active palette is a Glass palette,
whether or not a background preset or image is selected, in addition to being true whenever a
background preset or image is selected under any theme. Glassiness SHALL be carried by an explicit
entry in the palette rather than by the theme's name, so that it survives a palette edit (which
forks the theme to "Custom") and survives being saved as a user theme.

#### Scenario: Glass with no background
- **WHEN** Glass is the active theme and neither a preset nor a background image is set
- **THEN** cards, dialogs, bars, inset controls and action tiles take their translucent fills over
  the theme's own background colour

#### Scenario: Glass with a background
- **WHEN** Glass is the active theme and a preset or background image is set
- **THEN** the chrome is translucent over that background — theme and background are independent
  choices

#### Scenario: Another theme with a background
- **WHEN** a non-Glass theme is active and a preset or background image is set
- **THEN** the chrome is translucent, as it is today

#### Scenario: Another theme with no background
- **WHEN** a non-Glass theme is active and no preset or background image is set
- **THEN** the chrome is opaque, exactly as before this change

#### Scenario: Editing a colour keeps the glass look
- **WHEN** Glass is active and the user changes one palette colour, which forks the slot to
  "Custom"
- **THEN** the chrome stays translucent, and the built-in Glass entry is unchanged

#### Scenario: A saved copy stays glass
- **WHEN** the user saves that customised palette as a new user theme and later re-applies it
- **THEN** the chrome is translucent under that theme too

### Requirement: Glass is not editable
Glass SHALL be read-only in its first release: the app SHALL refuse to overwrite or delete it by
name, as it already does for "Default Dark" and "Default Light", so the tuned original is always
recoverable. Editing a colour while Glass is active SHALL fork the edit onto a "Custom" palette
seeded from Glass — the behaviour the theme editors already have — leaving the built-in entry
untouched.

#### Scenario: Saving over Glass is refused
- **WHEN** a save is attempted with the name "Glass"
- **THEN** the save is refused and the built-in Glass palette is unchanged

#### Scenario: Deleting Glass is refused
- **WHEN** a delete is attempted for "Glass"
- **THEN** the delete is refused and Glass remains in `themeNames()`

#### Scenario: Editing forks rather than modifying Glass
- **WHEN** Glass is the active theme and the user changes a colour in either theme editor
- **THEN** the edit lands on a "Custom" palette seeded from Glass, and the built-in Glass entry is
  unchanged and still listed

#### Scenario: A saved copy is an ordinary theme
- **WHEN** the user saves that customised palette under a new name
- **THEN** the copy is an ordinary user theme, fully editable and deletable, and the built-in
  Glass entry still exists

### Requirement: Glass palettes are legible
Both Glass palettes SHALL meet the same contrast floor the background presets meet, measured
against the colours text actually lands on under translucent chrome.

#### Scenario: Glass text contrast
- **WHEN** the Glass dark and light palettes are measured — `textColor` and `textSecondaryColor`
  against the palette's own background colour, and against the translucent card colour composited
  over it
- **THEN** every resulting WCAG contrast ratio is at least 4.5:1

#### Scenario: Glass over every preset
- **WHEN** Glass is combined with each background preset in turn, in both polarities
- **THEN** every text-on-card and text-on-background contrast ratio is at least 4.5:1
