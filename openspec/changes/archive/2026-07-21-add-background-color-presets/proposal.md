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

- Add a curated catalogue of **19 built-in background colours**, drawn from coffee and the
  things around it rather than invented greys, spanning near-black to near-white and **all
  offered under every theme, all the time**.
  - Roast: **French Roast**, **Cold Brew**, **Espresso**, **Green Bean**, **Ristretto**,
    **Cast Iron**, **Walnut**, **Barista**
  - Machine: **Machine Steel**, **Denim Apron**
  - Milk and cup: **Brushed Steel**, **Cortado**, **Latte**, **Oat Milk**, **Crema**,
    **Cappuccino**, **Steam**, **Flat White**, **Porcelain**
- Add **6 patterns** as an independent second axis — **Grain**, **Dot Grid**, **Pinstripe**,
  **Twill**, **Weave**, **Linen** — so any pattern can sit on any colour. Baking a pattern
  into each colour instead produced a catalogue where half the entries were near-invisible
  variants of the other half.
- Presets are **not tied to light/dark mode**. Instead, the readable foreground — text,
  secondary text, icons, borders, card and inset fills — is **derived from the chosen colour**,
  so a pale background is legible under a dark theme and vice versa. This is only possible
  because a preset is a known colour; over a photo none of it is computable.
  - **Consequence:** while a preset is active, a custom text colour from the theme is
    overridden. No stored preference can be right across a ramp this wide. Accents, chart
    colours and everything else still come from the user's theme.
- Add a **Glass chrome option** — a switch in Machine → Theme Mode that makes cards, bars and
  dialogs translucent, working with *any* theme including the user's own. It is an option, not
  a theme: translucency is orthogonal to light/dark, so a theme (which occupies one polarity
  slot) can only ever apply it by halves.
- The gate for that chrome becomes a single predicate, `Theme.glassChrome` — the option, or a
  background image (where opaque chrome would read as a slab on the photo) — replacing the ~70
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
- The chooser becomes **sectioned**: "Colours & patterns" (None + the 19 colours), then
  "Pattern" (None + the 6 patterns, each previewed on the colour actually selected), then
  "Images" (personal uploads + cached stock). The existing live preview panel previews the
  whole combination, rendered by the same component that draws the real page background.
- Preset and image are one selection: choosing either clears the other.
- New setting `Settings.theme.backgroundPreset` (empty string = no preset). Selecting a named
  theme, or editing the theme's own background colour in the web theme editor, clears the
  preset — the later explicit choice wins rather than being silently ignored.
- Wiki manual page for Theme Mode gains the preset list and the preset-vs-image rule.

No breaking changes: with no preset set, every existing code path resolves exactly as it does
today.

## Capabilities

### New Capabilities
- `background-color-presets`: the built-in preset catalogue, the derivation of readable
  foreground colours from the chosen background, how a preset interacts with the image
  background and with theme selection, and the sectioned chooser that presents both.
- `glass-chrome-option`: the translucent-chrome switch, why it is an option rather than a theme
  or a mode, and how it composes with any theme and any background.

### Modified Capabilities
<!-- None. `custom-background` (the image chooser) is still an unarchived change and has no
     entry in openspec/specs/; this change adds a sibling capability alongside it and does not
     alter any requirement it states. -->

## Impact

**New**
- `src/core/backgroundpresets.{h,cpp}` — two tables (colours; patterns with asset, opacity, tile
  and measured ink coverage), single source of truth, unit-testable.
- `resources/backgrounds/*.svg` — 6 tiny tileable pattern assets (grain, linen, twill, pinstripe,
  dots, weave); the colours need no asset.
- `qml/components/BackgroundSurface.qml` — shared renderer used by the real background, the
  chooser tiles, and the preview.
- `tests/tst_backgroundpresets.cpp` — id uniqueness, colour parsing, asset existence, a WCAG
  contrast floor for every colour against the page, the densest pattern, the card and the glass
  card, a perceptual card-separation floor, and a guard that the catalogue keeps spanning the
  range rather than collapsing back to two clusters.

**Modified**
- `src/core/settings_theme.{h,cpp}` — `backgroundPreset`, `backgroundPattern` and `glassChrome`
  properties, the two catalogue accessors, and the clear-on-explicit-choice rules.
- `qml/Theme.qml` — new `glassChrome` predicate; `backgroundColor` resolves through the
  active preset; every scrim/tint binding gated on the predicate; the `textSecondaryColor` and
  `insetBackgroundColor` fixes.
- ~70 call sites across `qml/components/` and `qml/components/layout/items/` — swept from
  `Settings.theme.backgroundImagePath.length > 0` onto `Theme.glassChrome`.
- `qml/components/ThemedPageBackground.qml`, `qml/components/BackgroundPickerDialog.qml`,
  `qml/components/layout/LayoutPreview.qml` — render/choose/preview presets.
- `qml/pages/settings/SettingsMachineTab.qml` — the Glass chrome switch, and a Background row
  label that reflects a preset choice as well as an image.
- `CMakeLists.txt` — new QML file, new C++ sources, new `.qrc` entries, new test target.
- Translations: preset names and the two section headers.

**Unaffected (deliberately out of scope)**
- The web theme editor and the MCP `apply_theme` tool expose no background surface today, so
  neither gains one here; the C++ catalogue is placed so they can, later, without a second
  source of truth.
- The screensaver itself, its media library, and its caching behaviour.
