## MODIFIED Requirements

### Requirement: A pattern is an independent second axis
The app SHALL store the pattern separately from the colour, so that any pattern can be applied
over any colour. A pattern SHALL be drawn over whatever flat colour is showing, and SHALL NOT be
drawn over a background image or a shot chart. Where a pattern cannot apply, the chooser SHALL
disable the pattern row with its reason visible rather than accept a selection that does nothing.
The stored pattern SHALL be retained while disabled, so returning to a colour restores it.

#### Scenario: Any pattern over any colour
- **WHEN** the user selects a colour and, separately, a pattern
- **THEN** the pattern is drawn over that colour, for any combination of the two

#### Scenario: A pattern without a colour
- **WHEN** a pattern is selected and no colour preset is
- **THEN** the pattern is drawn over the theme's own background colour

#### Scenario: No pattern over an image
- **WHEN** a background image is active
- **THEN** no pattern is drawn

#### Scenario: No pattern over a shot chart
- **WHEN** a shot-chart background is active
- **THEN** no pattern is drawn, because texture over data is noise on top of information

#### Scenario: The pattern row is disabled, not inert
- **WHEN** a shot-chart background is highlighted in the chooser
- **THEN** the pattern row is visibly disabled, rather than accepting taps that have no effect

#### Scenario: The pattern survives a detour through a shot background
- **WHEN** the user has a pattern set, switches to a shot-chart background, and later returns to
  a colour
- **THEN** the previously chosen pattern is applied again

#### Scenario: Patterns are visible but do not cost legibility
- **WHEN** the luminance shift each pattern imposes is measured as its opacity weighted by its
  ink coverage
- **THEN** derived text still clears 4.5:1 against every colour shifted by the densest pattern

### Requirement: A preset is a colour, not an image
A preset SHALL NOT be stored as a background image, and SHALL NOT be encoded as a sentinel value
standing for some other kind of background. While a preset is active and no image is set,
`Settings.theme.backgroundImagePath` SHALL remain empty, so the bookkeeping that clears that
setting when its backing file is deleted stays correct. Every value in `backgroundPreset` SHALL
resolve to a catalogue colour or to nothing.

#### Scenario: The image path stays empty
- **WHEN** a preset is active and no background image is set
- **THEN** `backgroundImagePath` is empty

#### Scenario: No sentinel presets
- **WHEN** a non-colour background such as a shot chart is active
- **THEN** it is not represented by a reserved id in `backgroundPreset`

### Requirement: The background sources are mutually exclusive
The app SHALL treat the background as ONE choice among a colour preset, a background image and a
shot chart, and SHALL record which of them is active explicitly rather than inferring it from
which keys happen to be populated. Selecting any source SHALL clear the others. At most one
SHALL be active at any time.

#### Scenario: Preset replaces an image
- **WHEN** a background image is active and the user applies a preset
- **THEN** the preset becomes active and `backgroundImagePath` becomes empty

#### Scenario: Image replaces a preset
- **WHEN** a preset is active and the user applies a background image
- **THEN** the image becomes active and `backgroundPreset` becomes empty

#### Scenario: A shot chart replaces either
- **WHEN** a preset or an image is active and the user applies a shot-chart background
- **THEN** the shot chart becomes active and both `backgroundPreset` and `backgroundImagePath`
  become empty

#### Scenario: Either replaces a shot chart
- **WHEN** a shot-chart background is active and the user applies a preset or an image
- **THEN** that becomes the active source and the shot chart is no longer drawn

#### Scenario: Two sources are never both active
- **WHEN** any background is applied
- **THEN** exactly one source is recorded as active, so no renderer has to choose between two

#### Scenario: An existing install keeps its background
- **WHEN** the app starts with a background chosen before this change
- **THEN** the active source is derived from the stored keys — a preset if one is set, an image
  if a path is set, otherwise none — and the user sees no change

### Requirement: A later explicit background choice clears the preset
Because a preset overrides the theme's own background colour, an explicit later choice of that
colour SHALL win. Applying a named theme, or changing the theme's background colour through the
theme editor, SHALL clear the active preset. A shot-chart background does not override the
theme's background colour in the same way, and SHALL NOT be cleared by a theme change.

#### Scenario: Applying a theme clears the preset
- **WHEN** a preset is active and the user applies a named light or dark theme
- **THEN** the preset is cleared and the newly applied theme's background colour is shown

#### Scenario: Editing the background colour clears the preset
- **WHEN** a preset is active and the theme's background colour is changed to a different value
  through the theme editor
- **THEN** the preset is cleared and the edited colour takes effect immediately

#### Scenario: A light/dark mode switch does not clear the preset
- **WHEN** a preset is active and the app switches between light and dark mode
- **THEN** the preset stays selected

#### Scenario: A theme change keeps a shot-chart background
- **WHEN** a shot-chart background is active and the user applies a different theme
- **THEN** the shot chart is still the background, drawn on the new theme's canvas

### Requirement: Sectioned background chooser
The background chooser SHALL present its options in labelled sections: "Colours & patterns"
containing the "None" tile followed by the catalogue presets, a "Shot" section containing the two
last-shot entries, and "Images" containing personal uploads and cached stock images. Highlighting
any tile SHALL update the preview only; the selection SHALL be committed only when the user
applies it, and discarded on cancel. Each tile SHALL be announced by its translated name.

#### Scenario: Presets appear first
- **WHEN** the user opens the background chooser
- **THEN** the "Colours & patterns" section is shown above the other sections, with "None"
  first among the presets

#### Scenario: The shot entries are offered together
- **WHEN** the user opens the background chooser
- **THEN** Last Shot and Last Shot (Advanced) appear together in their own labelled section

#### Scenario: Highlight previews without saving
- **WHEN** the user highlights a preset and then cancels
- **THEN** the previously active background is still in effect

#### Scenario: A highlighted shot entry previews as it will render
- **WHEN** the user highlights a shot-chart entry
- **THEN** the preview shows the page as it will look with that background, including the forced
  glass chrome

#### Scenario: Apply commits the highlighted tile
- **WHEN** the user highlights a preset and applies it
- **THEN** that preset becomes active app-wide

#### Scenario: Screen reader announces preset names
- **WHEN** a screen reader focuses a preset tile
- **THEN** it announces the preset's translated name, and whether it is currently selected

#### Scenario: Empty image library no longer empties the chooser
- **WHEN** the user has no personal uploads and no cached stock images
- **THEN** the chooser still offers the full "Colours & patterns" section, and the existing
  caching-disabled explanation applies only to the "Images" section

## RENAMED Requirements

- FROM: `### Requirement: Preset and image are mutually exclusive`
- TO: `### Requirement: The background sources are mutually exclusive`
