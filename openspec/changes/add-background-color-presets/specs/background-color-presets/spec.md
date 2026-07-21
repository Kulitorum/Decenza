## ADDED Requirements

### Requirement: Built-in background preset catalogue
The app SHALL ship a fixed, curated catalogue of twenty built-in background presets as its
single source of truth for preset identity, appearance and naming. Each entry SHALL carry a
stable id, a translation key with an English fallback name, one colour, an overlay kind
(`none` or `tile`), an overlay asset where the kind requires one, an overlay opacity and an
authored tile size. Entries SHALL be ordered from darkest to lightest.

#### Scenario: Catalogue is well-formed
- **WHEN** the catalogue is read
- **THEN** every entry has a unique id, its colour parses, and every entry with overlay kind
  `tile` names an asset that exists in the compiled resources with an opacity in range

#### Scenario: Ordered dark to light
- **WHEN** the catalogue order is inspected
- **THEN** the first entry is darker than the last, so the chooser reads as a ramp

#### Scenario: The catalogue spans the usable range
- **WHEN** the lightness of every entry is measured
- **THEN** there is at least one deep option, at least one light option, and at least two
  between them — so that whatever theme is active, the chooser offers real choice rather than
  a single cluster

#### Scenario: Catalogue is reachable from QML
- **WHEN** QML reads `Settings.theme.backgroundPresets`
- **THEN** it receives every catalogue entry, in catalogue order

### Requirement: Preset selection and persistence
The app SHALL expose the selected preset as `Settings.theme.backgroundPreset`, a string holding
a catalogue id, where the empty string means no preset is selected. The value SHALL persist
across restarts, and an id that is not in the catalogue SHALL be treated as no preset.

#### Scenario: Default state
- **WHEN** the app starts with no background preset ever chosen
- **THEN** `backgroundPreset` is the empty string and every background-dependent colour resolves
  exactly as it did before presets existed

#### Scenario: Selection persists
- **WHEN** the user selects a preset and restarts the app
- **THEN** the same preset is still active

#### Scenario: Unknown id degrades gracefully
- **WHEN** the stored `backgroundPreset` names an id that is not in the catalogue
- **THEN** the app renders as though no preset were selected, rather than rendering a blank or
  undefined background

### Requirement: A preset supplies the background colour, independent of mode
An active preset SHALL supply the app's flat background colour, taking precedence over the
active theme's own background colour. The colour SHALL NOT depend on `isDarkMode`: every preset
is available under every theme, and a light/dark switch SHALL change neither the selection nor
the colour.

#### Scenario: Preset overrides the theme background colour
- **WHEN** a preset is active
- **THEN** `Theme.backgroundColor` resolves to the preset's colour, and every page drawn on it
  uses that colour

#### Scenario: A mode switch changes nothing about the preset
- **WHEN** the app switches between light and dark mode while a preset is active
- **THEN** the same preset is still selected and still renders the same colour

#### Scenario: Clearing restores the theme colour
- **WHEN** the user selects "None"
- **THEN** `Theme.backgroundColor` resolves to the active theme's background colour again

### Requirement: Foreground colours are derived from the preset
Because a preset is a known colour and may be anywhere from near-black to near-white, the app
SHALL derive the readable foreground from it rather than taking it from the palette. While a
preset is active, `textColor`, `textSecondaryColor`, `iconColor`, `borderColor`, the card and
dialog fills and the inset fill SHALL all be computed from the preset colour. All other palette
roles — accents, chart series, status colours — SHALL continue to come from the user's theme.

#### Scenario: Text follows the background, not the theme
- **WHEN** a light preset is active under a dark theme
- **THEN** page and card text render dark, not light, and remain legible

#### Scenario: Every preset is legible
- **WHEN** the derived text and secondary text are measured against the preset colour and
  against the derived card fill, for every preset in the catalogue
- **THEN** every WCAG contrast ratio is at least 4.5:1

#### Scenario: Every card is visible
- **WHEN** the derived card fill is compared with the preset colour for every preset, at both
  card strengths the app uses
- **THEN** the perceptual lightness difference is at least 2 L*, so a card never dissolves into
  the page

#### Scenario: A custom text colour is overridden
- **WHEN** the user has set a custom text colour and then selects a preset
- **THEN** the derived colour is used instead, because no single stored colour can be readable
  across the whole catalogue

### Requirement: A preset is a colour, not an image
A preset SHALL NOT be stored as a background image. While a preset is active and no image is
set, `Settings.theme.backgroundImagePath` SHALL remain empty, so the bookkeeping that clears
that setting when its backing file is deleted stays correct.

#### Scenario: The image path stays empty
- **WHEN** a preset is active and no background image is set
- **THEN** `backgroundImagePath` is empty

### Requirement: Preset and image are mutually exclusive
Selecting a preset SHALL clear any selected background image, and selecting a background image
SHALL clear any selected preset. At most one of the two SHALL be active at any time.

#### Scenario: Preset replaces an image
- **WHEN** a background image is active and the user applies a preset
- **THEN** the preset becomes active and `backgroundImagePath` becomes empty

#### Scenario: Image replaces a preset
- **WHEN** a preset is active and the user applies a background image
- **THEN** the image becomes active and `backgroundPreset` becomes empty

### Requirement: A later explicit background choice clears the preset
Because a preset overrides the theme's own background colour, an explicit later choice of that
colour SHALL win. Applying a named theme, or changing the theme's background colour through the
theme editor, SHALL clear the active preset.

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

### Requirement: Preset rendering
The app SHALL render an active preset as its resolved flat colour plus, where the catalogue entry
specifies one, a subtle pattern drawn above that colour: the named monochrome asset tiled at the
entry's opacity and tinted with the theme's text colour. The same rendering component SHALL be
used for the live page background, the chooser's tiles, and the chooser's preview panel.

#### Scenario: Solid preset
- **WHEN** a preset whose overlay kind is `none` is active
- **THEN** the page background is that preset's resolved colour with no overlay

#### Scenario: Patterned preset
- **WHEN** a preset whose overlay kind is `tile` is active
- **THEN** the page background is that preset's resolved colour with the named asset tiled above
  it at the catalogue opacity, tinted so it reads correctly in both light and dark mode

#### Scenario: Preview matches the result
- **WHEN** the user highlights a preset in the chooser
- **THEN** the preview panel shows the same rendering the page background will use once applied

### Requirement: Sectioned background chooser
The background chooser SHALL present its options in two labelled sections: "Colours & patterns"
containing the "None" tile followed by the catalogue presets, above "Images" containing personal
uploads and cached stock images. Highlighting any tile SHALL update the preview only; the
selection SHALL be committed only when the user applies it, and discarded on cancel. Each preset
tile SHALL be announced by its translated name.

#### Scenario: Presets appear first
- **WHEN** the user opens the background chooser
- **THEN** the "Colours & patterns" section is shown above the "Images" section, with "None"
  first among the presets

#### Scenario: Highlight previews without saving
- **WHEN** the user highlights a preset and then cancels
- **THEN** the previously active background is still in effect

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
