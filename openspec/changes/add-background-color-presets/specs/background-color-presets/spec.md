## ADDED Requirements

### Requirement: Built-in background preset catalogue
The app SHALL ship a fixed, curated catalogue of built-in background presets as its single
source of truth for preset identity, appearance and naming. The catalogue SHALL contain ten
presets — the five solids `graphite`, `slate`, `espresso`, `forest` and `plum`, and the five
patterns `grain`, `linen`, `twill`, `pinstripe` and `dots` — and each entry SHALL carry a stable
id, a translation key with an English fallback name, a dark-mode colour, a light-mode colour, an
overlay kind (`none` or `tile`), an overlay asset where the kind requires one, and an overlay
opacity.

#### Scenario: Catalogue is well-formed
- **WHEN** the catalogue is read
- **THEN** every entry has a unique id, both colours parse as valid colours, and every entry with
  overlay kind `tile` names an asset that exists in the compiled resources

#### Scenario: Every preset keeps text legible
- **WHEN** each catalogue entry's dark colour is measured against the default dark theme's
  `textColor` and `textSecondaryColor`, and its light colour against the default light theme's
- **THEN** every resulting WCAG contrast ratio is at least 4.5:1

#### Scenario: Text stays legible on the scrimmed chrome too
- **WHEN** the card colour is composited — `surfaceColor` at `backgroundScrimAlpha` over each
  preset's resolved colour — and text colours are measured against that result
- **THEN** every resulting WCAG contrast ratio is at least 4.5:1

#### Scenario: Catalogue is reachable from QML
- **WHEN** QML reads `Settings.theme.backgroundPresets`
- **THEN** it receives the catalogue entries in catalogue order, the five solids before the five
  patterns

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

### Requirement: Mode-aware colour resolution
An active preset SHALL supply the app's flat background colour, resolving to its dark colour
when `Settings.theme.isDarkMode` is true and its light colour otherwise. The resolved colour
SHALL take precedence over the active theme's own background colour, and SHALL follow a
light/dark mode switch without the user reselecting anything.

#### Scenario: Preset overrides the theme background colour
- **WHEN** a preset is active
- **THEN** `Theme.backgroundColor` resolves to the preset's colour for the current mode, and
  every page drawn on it uses that colour

#### Scenario: Mode switch flips the preset value
- **WHEN** the app switches between light and dark mode while a preset is active
- **THEN** the background changes to that preset's other colour and text stays legible

#### Scenario: Clearing restores the theme colour
- **WHEN** the user selects "None"
- **THEN** `Theme.backgroundColor` resolves to the active theme's background colour again

### Requirement: A preset drives the same translucent chrome as a background image
A preset SHALL produce the same translucent chrome a background image produces — scrimmed cards,
dialogs, bars, inset controls and action tiles, and the adjusted secondary text colour. The app
SHALL gate that chrome on a single shared predicate rather than on a background image alone. That
predicate is true when a background image or a preset is active, and — see the `glass-theme`
capability — also when the Glass theme is active. A preset SHALL NOT be stored as a background
image: while a preset is active and no image is set, `Settings.theme.backgroundImagePath` SHALL
remain empty.

#### Scenario: Cards are scrimmed under a preset
- **WHEN** a preset is active and no background image is set
- **THEN** `backgroundImagePath` is empty, the custom-background predicate is true, and
  `Theme.cardBackgroundColor` resolves to the scrimmed `surfaceColor` — the same value it takes
  under a background image

#### Scenario: Chrome is uniform across every surface
- **WHEN** a preset is active
- **THEN** dialogs, bars, action tiles, action buttons and the layout item widgets all take their
  background-active fills, with no surface left on its opaque no-background fill

#### Scenario: No background and no Glass leaves everything unchanged
- **WHEN** neither a preset nor a background image is active, and the active theme is not Glass
- **THEN** the predicate is false and every chrome colour resolves to the opaque fill it used
  before presets existed

### Requirement: Secondary text moves away from the background, not always lighter
When the custom-background chrome is active, the app SHALL adjust `textSecondaryColor` away from
the page background — lighter in dark mode, darker in light mode — rather than always lighter.

#### Scenario: Light mode darkens instead of lightening
- **WHEN** the custom-background chrome is active in light mode
- **THEN** `textSecondaryColor` is darker than its unadjusted value, increasing its contrast
  against the light page rather than reducing it

#### Scenario: Dark mode is unchanged
- **WHEN** the custom-background chrome is active in dark mode
- **THEN** `textSecondaryColor` is lightened as it was before

### Requirement: Inset controls stay visible on a solid background
`insetBackgroundColor` scrims the background colour, which over a flat preset colour composites to
that same colour and leaves inset controls invisible on the page. While a preset is active, the
inset fill SHALL instead be scrimmed toward the contrast direction — toward black in light mode,
toward white in dark mode — so it keeps a visible step from the page.

#### Scenario: A text field on the page is distinguishable
- **WHEN** a preset is active and an inset control is drawn directly on the page background
- **THEN** its fill differs visibly from the page background

#### Scenario: The image path is unaffected
- **WHEN** a background image is active
- **THEN** `insetBackgroundColor` keeps the scrim-toward-background behaviour it has today

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
