## 1. Catalogue (C++)

- [x] 1.1 Add `src/core/backgroundpresets.{h,cpp}` exposing `backgroundPresetCatalogue()` →
      `QVariantList` of `{ id, nameKey, nameFallback, darkColor, lightColor, overlayKind,
      overlayAsset, overlayOpacity }`, populated with the ten presets from design.md Decision 8,
      the five solids before the five patterns, plus a `backgroundPresetById(id)` lookup returning
      an empty entry for an unknown id
- [x] 1.2 Add the five tileable pattern assets under `resources/backgrounds/` — `grain.svg`,
      `linen.svg`, `twill.svg`, `pinstripe.svg`, `dots.svg` — as small monochrome tiles designed to
      be tinted at low opacity, authored at a tile size that survives `Theme.scaled()` rounding
      without moiré, and register them in `resources/resources.qrc`
- [x] 1.3 Add both sources to `CMakeLists.txt`

## 2. Settings

- [x] 2.1 Add `backgroundPreset` (QString, `""` = none) to `SettingsTheme` with getter/setter/NOTIFY,
      persisted under `theme/backgroundPreset`; an id absent from the catalogue reads back as `""`
- [x] 2.2 Add a read-only `backgroundPresets` property returning the catalogue, and a
      `resolvedBackgroundColor()`-style accessor that picks dark vs light by `isDarkMode` and emits
      on both `backgroundPresetChanged` and `isDarkModeChanged`
- [x] 2.3 Make `setBackgroundPreset()` clear `backgroundImagePath`, and `setBackgroundImagePath()`
      (non-empty) clear `backgroundPreset` — mutual exclusion, spec requirement
- [x] 2.4 Clear `backgroundPreset` in `applyLightTheme()` / `applyDarkTheme()` and when
      `setCustomThemeColors()` changes the background colour to a different value (design Decision 7);
      confirm a plain light/dark **mode** switch does not clear it
- [x] 2.5 Verify the new key round-trips through `settingsserializer.cpp` (backup/restore and
      device-to-device transfer) and add it if the serializer enumerates keys explicitly

## 3. Chrome predicate and contrast fixes

- [x] 3.1 Add `Theme.glassChrome` — true when a background image **or** a preset is active, **or**
      `Settings.theme.activeThemeName === "Glass"` — and gate `cardBackgroundColor`,
      `dialogBackgroundColor`, `insetBackgroundColor`, `actionTileColor`, `actionButtonFill()` and
      `textSecondaryColor` on it
- [x] 3.2 Sweep every remaining `Settings.theme.backgroundImagePath.length > 0` read in `qml/` onto
      `Theme.glassChrome` (~70 sites: `StatusBar`, `BottomBar`, `GrindField`,
      `PresetPillRow`, `CustomItem`, `MiniGHCItem` and the `layout/items/*` widgets); verify by
      grepping for the old expression and finding nothing outside `Theme.qml`
- [x] 3.3 Fix `textSecondaryColor` to move *away* from the background — lighten in dark mode, darken
      in light mode — instead of always lightening (design Decision 9); applies to the image path
      too, which has the same bug
- [x] 3.4 Fix `insetBackgroundColor` so that under a preset it scrims toward the contrast direction
      (black in light mode, white in dark) rather than toward the background colour it would
      composite into invisibility against; leave the image path as-is

## 4. Rendering

- [x] 4.1 Add `qml/components/BackgroundSurface.qml`: takes `presetId` and `imagePath`, draws the
      resolved flat colour, the tiled pattern (tinted with `Theme.textColor`, catalogue opacity),
      or the image — one component, both cases; register it in `CMakeLists.txt`'s
      `qt_add_qml_module` list
- [x] 4.2 Resolve `Theme.backgroundColor` through the active preset, keeping the `_c()` wrapper so
      the web theme editor's flash-to-identify still works; order = preset > custom theme colour >
      built-in default
- [x] 4.3 Rewrite `ThemedPageBackground.qml` to delegate to `BackgroundSurface`, preserving the
      existing flat-colour-until-image-decoded behaviour for the image case
- [x] 4.4 Add `backgroundPresetId` to `LayoutPreview.qml` alongside `backgroundImageSource` and route
      both through `BackgroundSurface`

## 5. Glass chrome option

- [x] 5.1 Add `glassDarkDefaults()` / `glassLightDefaults()` to `settings_theme.cpp`, tuned so
      `surfaceColor` still separates from `backgroundColor` at `backgroundScrimAlpha` (design Open
      Questions has the starting values)
- [x] 5.2 Add "Glass" to `themeNames()` and to `getPresetThemes()` with `isBuiltIn: true`
- [x] 5.3 Add "Glass" branches to `applyDarkTheme()`, `applyLightTheme()` and `applyPresetTheme()`,
      installing the matching palette the way the "Default Light"/"Default Dark" branches do
- [x] 5.4 Add "Glass" to the built-in-name guards in `saveCurrentTheme()` and the delete path, so it
      cannot be overwritten or deleted
- [x] 5.5 Confirm the in-app theme editor cannot modify Glass: `setEditingPaletteColor()` already
      forks the slot to "Custom" on any colour change, so the built-in is never written, and the
      glass look follows the fork because it rides in the palette (`glassChrome`) not the name
- [x] 5.6 Confirm the same for the web theme editor: it renders its list from `getPresetThemes()`
      and hides delete for `isBuiltIn` (which Glass now sets), and its save/delete endpoints go
      through the guarded `saveCurrentTheme()` / `deleteUserTheme()`
- [x] 5.7 Confirm the Machine → Theme Mode Dark/Light pickers list Glass with no change needed (they
      bind `Settings.theme.themeNames`), and that "Follow system theme" still switches slots
      normally with Glass in one of them

## 6. Chooser

- [x] 6.1 Restructure `BackgroundPickerDialog.qml` into two labelled sections inside a `ScrollView` —
      "Colours & patterns" (None + presets) above "Images" — replacing the single `GridView`
- [x] 6.2 Track a single candidate selection covering both sections (preset id or image path, never
      both), previewed live and committed only on Apply
- [x] 6.3 Render preset tiles with `BackgroundSurface` and the preset's translated name; give each an
      `Accessible.name` of that name plus selected state, and keep the positional naming for image
      tiles
- [x] 6.4 Scope the "caching is disabled" explanation to the Images section only, so an empty image
      library no longer reads as an empty chooser
- [x] 6.5 Update the Background row in `SettingsMachineTab.qml` so the button label reflects a preset
      selection as well as an image selection

## 7. Translations

- [x] 7.1 Add translation keys for the ten preset names and the two section headers, with English
      fallbacks, following the existing `backgroundPicker.*` key namespace

## 8. Tests

- [x] 8.1 Add `tests/tst_backgroundpresets.cpp`: ids unique and non-empty, both colours parse, every
      `tile` entry's asset exists in the compiled resources, overlay opacity in range
- [x] 8.2 Add the WCAG contrast test at a 4.5:1 floor, for all ten presets in both modes, measured
      **twice** — against the raw preset colour, and against the scrimmed card colour text actually
      sits on (`surfaceColor` at `backgroundScrimAlpha` composited over the preset colour)
- [x] 8.3 Add settings tests: default empty, round-trip persistence, unknown id reads back as `""`,
      preset↔image mutual exclusion both directions, cleared by theme apply and by a background
      colour edit, **not** cleared by a mode switch
- [x] 8.4 Add Glass tests: present in `themeNames()` and flagged built-in in `getPresetThemes()`;
      `applyDarkTheme("Glass")` / `applyLightTheme("Glass")` install the right palette;
      `saveCurrentTheme("Glass")` and deleting "Glass" are both refused; the glass-chrome predicate
      is true with Glass active and no background; and both Glass palettes clear the 4.5:1 floor —
      alone, and crossed with every preset
- [x] 8.5 Register the test target in `CMakeLists.txt` and run the full suite locally (there is no PR
      CI gate)

## 9. Verification and docs

- [ ] 9.1 Build via Qt Creator and have the user launch the app: check each of the ten presets in
      both light and dark mode; confirm the glass chrome matches what a screensaver image produces;
      confirm no widget was missed by the 3.2 sweep (walk the idle screen, settings tabs and an
      extraction page); confirm the preview matches the applied result; confirm picking an image
      clears the preset and vice versa
- [ ] 9.2 Check each of the five patterns on a real tablet for moiré or shimmer at the device's
      scale factor — redraw the tile if one shimmers rather than fading it out (design Risks)
- [ ] 9.3 Verify the two contrast fixes on device: secondary text on a light preset, and a text
      field sitting directly on the page background under a solid preset
- [ ] 9.4 Verify Glass on device: selectable in both pickers; glass chrome with no background set;
      Glass crossed with a preset and with a photo; follow-system switching with Glass in one slot;
      read-only in both editors with duplicate-to-edit producing an editable copy
- [x] 9.5 Update the wiki manual (Theme Mode / Background section) with the preset list, the
      derived-foreground behaviour, the preset-vs-image rule, and the Glass chrome option —
      that it is an option rather than a theme and works with any theme and any background
- [ ] 9.6 Run `/opsx:archive` as the final commit on the feature branch, before merge
