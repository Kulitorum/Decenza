## 1. Catalogue (C++)

- [x] 1.1 Add `src/core/backgroundpresets.{h,cpp}` with two tables — colours (`id`, `nameKey`,
      `nameFallback`, `value`) and patterns (`asset`, `opacity`, `tile`, measured ink `coverage`)
      — plus lookups that return an empty entry for an unknown id, and `contrastShift()` for the
      coverage-weighted figure the contrast test needs
- [x] 1.2 Add six tileable pattern assets under `resources/backgrounds/` — grain, linen, twill,
      pinstripe, dots, weave — as small monochrome tiles tinted at runtime, and register them in
      `resources/resources.qrc`
- [x] 1.3 Add both sources to `CMakeLists.txt` (app, tests and `saw_parity`)

## 2. Settings

- [x] 2.1 Add `backgroundPreset` and `backgroundPattern` to `SettingsTheme`, persisted, with an
      id absent from its table reading back as `""`
- [x] 2.2 Expose both tables and the resolved active entries as properties, so QML bindings
      re-run on a change
- [x] 2.3 Make colour and image mutually exclusive: setting either clears the other
- [x] 2.4 Clear the colour in `applyDarkTheme()` / `applyLightTheme()` / `applyPresetTheme()` and
      when the theme editor changes the background colour — a later explicit choice wins. A
      light/dark **mode** switch does not clear it
- [x] 2.5 Round-trip `backgroundPreset` and `glassChrome` through `settingsserializer.cpp`; the
      image path stays unexported, being device-local

## 3. Deriving a readable foreground

- [x] 3.1 Derive `backgroundColor`, `surfaceColor`, `textColor`, `textSecondaryColor`,
      `iconColor`, `borderColor` and the inset fill from the chosen colour, so any colour works
      under any theme
- [x] 3.2 Make the card lift a fixed step in **L\*** (`_liftFrom`, by bisection) rather than a
      fixed RGB fraction — the fixed fraction is what made the whole mid-light range unusable
- [x] 3.3 Give action tiles their own larger lift (12 L\*) so they read as pressable, and derive
      their content colour from the fill via `contentColorOn()`, which only overrides when the
      palette colour actually fails on that fill
- [x] 3.4 Add `chromeFill()`: scrim over an image, opaque over a flat colour. A scrim over a flat
      colour is not translucency, it just cancels the elevation
- [x] 3.5 Give tiles a hairline border on a flat colour — fill contrast alone cannot define an
      edge against a lifted shade of the same colour

## 4. Rendering

- [x] 4.1 Add `qml/components/BackgroundSurface.qml` — the one place a background is drawn
      (colour, pattern or image), used by the page, the chooser tiles and the preview
- [x] 4.2 Tint the pattern from the surface it sits on, not the global text colour
- [x] 4.3 Delegate `ThemedPageBackground.qml` to it, keeping the flat-colour-until-decoded
      fallback for the image case
- [x] 4.4 Route `LayoutPreview.qml` through it, and key its bars on `Theme.glassChrome` so the
      preview cannot disagree with the page

## 5. Glass chrome option

- [x] 5.1 Add `Settings.theme.glassChrome` and a switch in Machine → Theme Mode. An option, not
      a theme: a theme occupies one polarity slot and could only ever be half-applied
- [x] 5.2 Gate every translucent fill on `Theme.glassChrome`, and sweep the ~70 hand-rolled
      `backgroundImagePath.length > 0` reads onto it
- [x] 5.3 Keep look-fills (`actionTileColor`, `actionButtonFill()`) on the switch and
      legibility-colours on `hasBackgroundPreset`, so the switch turns off everything it turns on
- [x] 5.4 Fix `StyledSwitch`'s implicit width, which ignored padding and overhung its card

## 6. Chooser

- [x] 6.1 Rebuild `BackgroundPickerDialog.qml`: a colour section, a pattern row beside the live
      preview, then images
- [x] 6.2 Track one candidate across colour and image, plus an independent pattern, committed
      only on Apply
- [x] 6.3 Render every tile with `BackgroundSurface`, deriving each caption from its own tile
      colour, and name preset tiles for screen readers
- [x] 6.4 Scope the "caching is disabled" explanation to the Images section
- [x] 6.5 Reflect a colour choice in the Background row's button label

## 7. Translations

- [x] 7.1 Translation keys for the colour and pattern names and the section headers

## 8. Tests

- [x] 8.1 `tests/tst_backgroundpresets.cpp`: table shape, unique ids, parseable colours, pattern
      assets present in the compiled resources, opacity and coverage in range
- [x] 8.2 Contrast floors at 4.5:1 for every colour — derived text and secondary text against
      the page, the page under the densest pattern, the card, and the action tile
- [x] 8.3 A perceptual floor (2 L\*) so no chrome fill dissolves into the page, and a guard that
      the catalogue keeps spanning the range instead of collapsing to two clusters
- [x] 8.4 Settings tests: defaults, persistence, unknown ids, colour↔image exclusion, the
      clear-on-explicit-choice rules, mode-independence, and that glass is not a theme
- [x] 8.5 Register the target and run the full suite locally — the only gate, there is no PR CI

## 9. Verification and docs

- [x] 9.1 Verified in the running app across light, mid and dark colours: the chooser, the live
      preview, colour/image exclusion, and the derived foreground following the background
- [x] 9.2 Verified the glass option composes with any theme, survives a mode switch, and turns
      every surface back off when switched off
- [x] 9.3 Verified the contrast fixes on device: secondary text on a light colour, the recessed
      inset fill, and tile definition at both ends of the ramp
- [x] 9.4 Update the wiki manual: colours, patterns, the derived-foreground consequence, and the
      glass option
- [~] 9.5 DEFERRED to the beta (agreed with Jeff): check the six patterns on a real TABLET for moiré or shimmer at the device's scale
      factor. Desktop looks right, but the tiles are 8-32 px and fractional scaling is exactly
      what makes them shimmer, so the desktop pass cannot stand in for this
- [x] 9.6 Run `/opsx:archive` as the final commit on the feature branch, before merge
