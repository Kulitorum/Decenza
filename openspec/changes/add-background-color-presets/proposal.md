## Why

The app background chooser (Machine → Theme Mode → Background) offers exactly two kinds of
answer today: a photo from the screensaver media library, or nothing. Every photo is a
full-bleed image chosen to look good as a *screensaver* — high contrast, busy, attention-seeking
— which is a fine thing to look at across the room and a lot to work in front of. Users who want
their machine to look like something other than the stock dark blue, but who do not want a
photograph behind the buttons, have no option at all.

The translucent chrome those photos bring with them is not the problem — it is the app's best
look, and this change keeps it. What is missing is a calm surface to put behind it. A short,
curated set of solid and near-solid backgrounds fills that gap without asking anyone to upload or
download anything.

## What Changes

- Add a curated catalogue of **10 built-in background presets** — 5 solid colours and 5 very
  subtle patterns — selectable from the existing Background chooser. All ten are meant to be
  backgrounds someone works in front of all day, not demonstrations of the mechanism.
  - Solids: **Graphite**, **Slate**, **Espresso**, **Forest**, **Plum**
  - Patterns, each on a different solid's colour pair: **Grain** (warm paper noise),
    **Linen** (woven crosshatch), **Twill** (45° hairlines), **Pinstripe** (vertical hairlines),
    **Dot Grid** (6 px lattice)
- Each preset is **mode-aware**: it carries a dark value and a light value and resolves against
  `Settings.theme.isDarkMode`, so a preset can never leave light text on a light page.
- A preset sets the app's flat background colour rather than posing as an image, but it drives
  **the same translucent chrome** a screensaver image does — scrimmed cards, bars, dialogs and
  action tiles. The glass look is the point: presets change the colour behind it, not the look.
- Add a built-in **Glass** theme — a peer of "Default Dark" and "Default Light" in the same
  theme list, offered in the Machine → Theme Mode Dark and Light pickers exactly like a
  user-saved theme. Glass turns the translucent chrome on by itself, so theme and background
  become independent choices: pick Glass, Default Dark or any saved theme, *and* the background
  of your choice. Glass ships both a `colorsDark` and a `colorsLight` palette, so it works in
  either slot, and **is read-only in this first release** — it cannot be overwritten or deleted,
  and editing while it is active offers to duplicate it under a new name.
  `themeMode` stays `system` | `light` | `dark` and `isDarkMode` stays a boolean: Glass is a
  theme, not a third mode.
- The gate for that chrome becomes a single predicate, `Theme.glassChrome` — true when a
  background image or preset is set, **or** when the active theme is Glass — replacing the ~70
  hand-rolled `backgroundImagePath.length > 0` reads scattered across `Theme.qml` and the
  components.
- Two contrast weaknesses the photo backgrounds masked are fixed in the same bindings, because
  presets make both load-bearing:
  - `textSecondaryColor` brightens whenever a background is active, which on a **light** preset
    pushes secondary text *toward* the page. It now moves away from the background instead —
    lighter in dark mode, darker in light mode.
  - `insetBackgroundColor` scrims the background colour over itself, which against a photo reads
    as a dimmed patch but against a solid preset is invisible — text fields and switch tracks
    sitting directly on the page disappear. Under a preset it scrims toward the contrast
    direction instead, keeping the recessed step.
- The chooser becomes **sectioned**: "Colours & patterns" (None + the 10 presets) above
  "Images" (personal uploads + cached stock). The existing live preview panel previews presets
  as well, rendered by the same component that draws the real page background.
- Preset and image are one selection: choosing either clears the other.
- New setting `Settings.theme.backgroundPreset` (empty string = no preset). Selecting a named
  theme, or editing the theme's own background colour in the web theme editor, clears the
  preset — the later explicit choice wins rather than being silently ignored.
- Wiki manual page for Theme Mode gains the preset list and the preset-vs-image rule.

No breaking changes: with no preset set, every existing code path resolves exactly as it does
today.

## Capabilities

### New Capabilities
- `background-color-presets`: the built-in preset catalogue, its mode-aware colour resolution,
  how a preset interacts with the image background and with theme selection, and the sectioned
  chooser that presents both.
- `glass-theme`: the built-in read-only Glass theme, its place in the existing theme list rather
  than in `themeMode`, and its role as the second trigger for the translucent chrome.

### Modified Capabilities
<!-- None. `custom-background` (the image chooser) is still an unarchived change and has no
     entry in openspec/specs/; this change adds a sibling capability alongside it and does not
     alter any requirement it states. -->

## Impact

**New**
- `src/core/backgroundpresets.{h,cpp}` — the catalogue (id, name key, dark/light colour, overlay
  kind + asset + opacity), single source of truth, unit-testable.
- `resources/backgrounds/*.svg` — 5 tiny tileable pattern assets (grain, linen, twill, pinstripe,
  dots); the solids need no asset.
- `qml/components/BackgroundSurface.qml` — shared renderer used by the real background, the
  chooser tiles, and the preview.
- `tests/tst_backgroundpresets.cpp` — id uniqueness, colour parsing, asset existence, and a WCAG
  contrast floor for every preset in both modes, measured against the raw preset colour *and*
  against the scrimmed card colour text actually sits on.

**Modified**
- `src/core/settings_theme.{h,cpp}` — `backgroundPreset` property, `backgroundPresets` catalogue
  property, resolved-colour accessor, and the two clear-on-explicit-choice rules. Glass added to
  `themeNames()`, `getPresetThemes()` (with `isBuiltIn`), `applyDarkTheme()`, `applyLightTheme()`
  and `applyPresetTheme()`, plus `glassDarkDefaults()`/`glassLightDefaults()`; "Glass" added to
  the existing built-in-name guards in `saveCurrentTheme()` and the delete path.
- `qml/Theme.qml` — new `glassChrome` predicate; `backgroundColor` resolves through the
  active preset; every scrim/tint binding gated on the predicate; the `textSecondaryColor` and
  `insetBackgroundColor` fixes.
- ~70 call sites across `qml/components/` and `qml/components/layout/items/` — swept from
  `Settings.theme.backgroundImagePath.length > 0` onto `Theme.glassChrome`.
- `qml/components/ThemedPageBackground.qml`, `qml/components/BackgroundPickerDialog.qml`,
  `qml/components/layout/LayoutPreview.qml` — render/choose/preview presets.
- `qml/pages/settings/SettingsMachineTab.qml` — Background row label reflects a preset choice;
  the Dark/Light theme pickers pick up Glass for free from `themeNames()`.
- The in-app theme editor and the web theme editor (`src/network/shotserver_settings.cpp`) —
  read-only presentation and a "duplicate to edit" affordance while Glass is active.
- `CMakeLists.txt` — new QML file, new C++ sources, new `.qrc` entries, new test target.
- Translations: preset names and the two section headers.

**Unaffected (deliberately out of scope)**
- The web theme editor and the MCP `apply_theme` tool expose no background surface today, so
  neither gains one here; the C++ catalogue is placed so they can, later, without a second
  source of truth.
- The screensaver itself, its media library, and its caching behaviour.
