## 1. Search-syntax dialog layout tolerance

<!-- Ordered first deliberately: the bundled font covers only Latin/Greek/Cyrillic, so for every
     non-Latin locale these guards are the ONLY protection against a clipped dialog. Wider reach
     than the rename, and independently valuable if the collision theory turns out to be wrong. -->

- [x] 1.1 Give the description and example grid cells `Layout.fillWidth` + `elide: Text.ElideRight`
      so the grid has a legal narrow layout instead of an unshrinkable implicit width
- [x] 1.2 Fix the `ScrollView` so content exceeding the dialog scrolls rather than being clipped —
      verify against real content; the elide change alone may not fix it
- [ ] 1.3 Confirm the intro line wraps instead of overflowing and cutting mid-word, and that no
      column is silently clipped, at the smallest supported window size
- [ ] 1.4 Verify with a CJK or Arabic UI language, where every glyph comes from a fallback font —
      this is the case the guards exist for

## 2. Rename the bundled font family

- [x] 2.1 Rewrite name IDs 1, 4, 6 and 16 (where present) to `Decenza Sans` in all four
      `resources/fonts/Roboto-*.ttf`, leaving ID 2 (Subfamily) intact; commit the renamed files
- [x] 2.2 Rename the files to `DecenzaSans-{Light,Regular,Medium,Bold}.ttf` and update the resource
      paths in `src/main.cpp` and the qrc/CMake file list
- [x] 2.3 Record the rename procedure (tool + name IDs) in a comment beside the font block so the
      transformation is reproducible on the next font update
- [x] 2.4 Verify the family structure is preserved byte-for-byte apart from names, and that `OFL.txt`
      still accompanies the files (no Reserved Font Name clause, so renaming is permitted outright).
      NOTE: the original task premise was wrong — Regular/Bold are a RIBBI pair sharing ID1, while
      Light/Medium are *separate* families (ID1 "Decenza Sans Light"/"Medium") linked by typographic
      family ID16. That is how Google ships Roboto; the rename preserves it exactly, so weight
      selection behaves precisely as it did before. Changing that structure is a separate concern.

## 3. Single source of truth for font size defaults

- [x] 3.1 Declare the canonical defaults once in `SettingsTheme` (heading 32, title 24, subtitle 18,
      body 18, label 14, caption 12, value 48, timer 72)
- [x] 3.2 Add `Q_PROPERTY QVariantMap effectiveFontSizes` merging defaults with overrides, notifying
      on `customFontSizesChanged` — a property, not an invokable, or QML bindings will not
      re-evaluate when a slider moves
- [x] 3.3 Point the eight font roles in `qml/Theme.qml:478-485` at `effectiveFontSizes`, removing the
      inline `|| <default>` fallbacks
- [x] 3.4 Point `shotserver_theme.cpp:49`'s `fontDefaults` map at the canonical declaration, deleting
      the duplicate table
- [x] 3.5 Add an accessor returning only roles whose value differs from default, for startup logging

## 4. Startup font diagnostics

- [x] 4.1 Log host font families that could collide with the bundled family, before registration
- [x] 4.2 Log the resolved family and `QFontInfo::exactMatch()` after registration, distinguishing
      registration failure from resolution failure
- [x] 4.3 Log the probe metric: `horizontalAdvance("Extraction yield (%)")` at a fixed 14px — fixed,
      not the user's effective label size, so it is comparable between machines
- [x] 4.4 Log non-default font size overrides (role, current value, default) after `Settings` is
      constructed — not in the `[Font]` block, which runs before Settings exists
- [ ] 4.5 Confirm nothing is logged about font sizes when every role is at its default

## 5. Explicit family on theme roles

- [x] 5.1 Expose `Theme.fontFamily`, resolving to the registered family or empty string when
      registration failed (empty falls back to the application default)
- [x] 5.2 Add `family: Theme.fontFamily` to the eight font roles in `qml/Theme.qml`
- [ ] 5.3 Spot-check a CJK string still renders via platform fallback with no missing-glyph boxes

## 6. Web theme editor

- [ ] 6.1 Add `POST /api/theme/font/reset` calling the existing `resetFontSizesToDefault()`
- [ ] 6.2 Add a reset control beside the Font Sizes header in `theme_html.h` / `theme_js.h`
- [ ] 6.3 Reword the combined reset confirmation to name both theme colours and font sizes
- [ ] 6.4 Verify the fonts-only reset leaves customised colours untouched
- [ ] 6.5 Fix the `/themes` → `/theme` hint at `qml/pages/settings/SettingsThemesTab.qml:128`, which
      currently sends users to a 404

## 7. Correct the glyph-safety guidance

- [ ] 7.1 Remove `→` from CLAUDE.md's list of glyphs safe to use as literals — it is not in the
      bundled font's cmap and falls back to a system font, unlike `°` `·` `—` `×` which are covered.
      It ships in UI strings today (for example shot history's `18.0g → 41.6g`)
- [ ] 7.2 Note in the same guidance that the bundled font covers Latin/Greek/Cyrillic only, so
      non-Latin locales rely on platform fallback and on layout tolerance rather than metric
      determinism

## 8. Tests

- [ ] 8.1 Test that the bundled family registers under `Decenza Sans` with all four weights
- [ ] 8.2 Test the override-diff logic: stored-equal-to-default is not an override; changed values
      are reported with their defaults; all-default yields an empty result
- [ ] 8.3 Test that QML theme defaults and the web editor's reported defaults come from the same
      declaration and agree
- [ ] 8.4 Run the full suite with `-DBUILD_TESTS=ON` and clear all warnings

## 9. Documentation and release

- [ ] 9.1 Add a wiki manual entry for the fonts-only reset (user-visible feature)
- [ ] 9.2 Update the manual's theme editor section if it names the `/themes` path
- [ ] 9.3 Build via Qt Creator and have Jeff launch the app to verify rendering is unchanged on a
      known-good machine
- [ ] 9.4 Ask the reporter for a fresh debug log; confirm the new `[Font]` lines identify the
      resolved family and any collision
- [ ] 9.5 Archive this change with `/opsx:archive` as the last commit on the branch, before merge
- [ ] 9.6 Close #1537 and #1469 once the reporter's log confirms the fix
