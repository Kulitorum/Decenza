## Context

`Settings.theme.backgroundImagePath` is the app's only background control. It is a filesystem
path, it is read at roughly seventy call sites — almost all of them as
`backgroundImagePath.length > 0` — and that single boolean drives a whole second visual mode:
`Theme.cardBackgroundColor`, `dialogBackgroundColor`, `insetBackgroundColor`, `actionTileColor`,
`actionButtonFill()`, a brightened `textSecondaryColor`, and per-widget scrims across the layout
items. All of it exists because a screensaver photo is busy and bright, and opaque chrome on top
of one looks like a slab while translucent chrome does not.

That translucent chrome is not merely compensation, though — it is the look the app wears when a
background is set, and it is the look this change is asked to keep. A preset changes the colour
*behind* the glass; it does not opt out of the glass. So the design splits the photo path in two:
presets are a background **colour** rather than a background **image** (Decision 1), and they
drive the **same** chrome the image path drives (Decision 2), through one predicate rather than
seventy copies of an image-path test.

`ThemedPageBackground.qml` already falls back to a flat `Theme.backgroundColor` whenever no image
is set, so the rendering hook a preset needs is the one place that already draws the flat case.

The change also adds **Glass**, a built-in theme that turns that same chrome on without requiring
a background at all — making theme and background independent choices (Decisions 11–13). Themes
here are named palette blobs in `themeNames()` that `applyDarkTheme()`/`applyLightTheme()` copy
into the active bucket, so Glass needs no new storage concept and no third theme *mode*.

## Goals / Non-Goals

**Goals:**
- A short curated set of calm backgrounds, available offline, with no upload and no download.
- The same translucent chrome the screensaver images produce — one look, two sources of colour.
- Legible in both light and dark mode, by construction rather than by user care.
- One chooser, one selection: presets and images live together and cannot both be active.
- Zero visual change for every user who does not pick one.
- One source of truth for the catalogue, testable without a running UI.
- Theme and background as independent choices: any theme crossed with any background.

**Non-Goals:**
- User-defined custom colours or a colour picker. That is the theme editor's job, and adding a
  second way to set a background colour is how the two get out of sync.
- Separate "colour" and "pattern" settings. One flat list of named presets; no second axis.
- A preset selector in the web theme editor or the MCP `apply_theme` tool. Neither exposes any
  background surface today; the catalogue is placed in C++ so they can gain one later without a
  second definition.
- Any change to the screensaver, its media library, or its caching.
- An editable Glass theme. Read-only in this release; duplicate-to-edit covers the user who wants
  a variant (Decision 13).
- A third `themeMode` value. Glass is a theme, not a mode (Decision 11).

## Decisions

### Decision 1: A preset sets the background *colour*, not a background *image*

`Settings.theme.backgroundPreset` is a new, separate string property. When it is set,
`Theme.backgroundColor` resolves through it; `backgroundImagePath` stays empty.

- **Why:** a preset carried as an image would mean shipping full-screen bitmaps, at several
  densities, for ten presets, to express what one hex value expresses — and a solid colour
  scales, crops and re-themes for free where a bitmap does none of those.
- **Rejected — reuse `backgroundImagePath` with `qrc:` values:** it forces a URL-scheme branch
  into `ThemedPageBackground` and, worse, into the deletion bookkeeping in
  `ScreensaverVideoManager`, which compares that setting against real file paths and clears it
  when the backing file is deleted. A preset has no backing file to delete. Keeping the two
  properties separate keeps that bookkeeping honest.
- **Consequence:** preset and image are mutually exclusive, and each setter clears the other. That
  is a requirement, not an emergent property — see the spec.
- **Note:** this decision is about *storage*, not about *appearance*. The chrome is Decision 2.

### Decision 2: A preset drives the same translucent chrome an image does

Everything gated on "a background is set" — `cardBackgroundColor`, `dialogBackgroundColor`,
`insetBackgroundColor`, `actionTileColor`, `actionButtonFill()`, the adjusted
`textSecondaryColor`, and the per-widget scrims in the layout items — is gated on a preset too.
The gate becomes one predicate, `Theme.glassChrome`, true when either an image or a
preset is active, and the ~70 hand-rolled `Settings.theme.backgroundImagePath.length > 0` reads
are swept onto it.

- **Why:** the glass chrome is what a Decenza with a background *looks like*. A preset that kept
  opaque cards would not read as "the same app with a calmer background", it would read as a
  third, unrelated visual mode. The user's choice here is the colour behind the glass.
- **Why one predicate rather than extending the test in place:** `imagePath.length > 0 ||
  preset.length > 0` copied to seventy sites is seventy chances for the next background source to
  be missed at one of them. One named predicate is also the honest name for what those sites were
  always asking — none of them cares that it is an *image*, only that the page is not flat.
- **Composition is now computable.** Over a photo, what sits behind a 40%-alpha card is unknowable,
  which is why `backgroundScrimAlpha` was tuned by eye against worst-case bright regions. Over a
  preset the base is a known hex value, so the resulting card colour is exact arithmetic — which
  is what lets the contrast floor in Decision 9 be a real test rather than an intention.
- **`backgroundScrimAlpha` is not retuned.** 0.4 was chosen against busy photos; against a flat
  colour it is, if anything, generous. Changing it would move every existing photo user's UI to
  fix a problem presets do not have.

### Decision 3: Every preset is a dark/light pair, resolved by `isDarkMode`

Each catalogue entry carries `darkColor` and `lightColor`. `Theme.backgroundColor` picks by
`Settings.theme.isDarkMode`.

- **Why:** the alternative is a flat list of twenty colours where ten of them make the app
  unreadable depending on the current mode, because text colour comes from the theme and would not
  follow. Pairing removes the footgun entirely instead of guarding it with a warning, a filter, or
  a second setting — and it means switching light/dark mode keeps the user's choice of *character*
  ("Espresso") while changing its *value*.
- **Rejected — derive the light value from the dark one:** a computed inverse of a near-black is a
  muddy grey, not the pale warm cream that actually pairs with it. Both values are hand-picked.
- **Rejected — show only the presets valid for the current mode:** the grid would change contents
  when the user switches mode, and the same preset would appear to be two different things.

### Decision 4: The catalogue lives in C++, not in QML

`src/core/backgroundpresets.{h,cpp}` returns the catalogue as a `QVariantList`; `SettingsTheme`
exposes it as `backgroundPresets` and resolves the active colour.

- **Why:** it is testable headlessly, which is the point — the contrast floor in the spec is a real
  unit test over real values, not a design intention. It also follows the precedent set by the
  widget catalogue table in `settings_network.cpp`, where one C++ table feeds the in-app palette
  and the web editor, and it leaves the door open for the web/MCP surfaces named as non-goals.
- **Rejected — a `BackgroundPresets.qml` singleton:** slightly less code, untestable, and would
  have to be duplicated the day the web theme editor grows a background section.

### Decision 5: Patterns are tiny tileable SVGs tinted with the theme text colour

Two overlay kinds only: `none` (the solids) and `tile` (a small tileable SVG drawn at low opacity,
tinted with `Theme.textColor`).

- **Why tinted rather than baked:** one monochrome asset serves both modes — dark-on-light and
  light-on-dark come out of the tint, not out of two files. Assets stay a few hundred bytes.
- **Opacity lives in the catalogue** (4–6%), so tuning a pattern is a data edit, not a QML edit.
- **Rejected — a `gradient` kind:** an earlier draft carried a soft vertical fade as a third kind,
  rendered with `Rectangle.gradient` and no asset. It is a nice background but it is not a
  *pattern*, and keeping it meant a third branch in `BackgroundSurface` and a third shape in the
  catalogue for one entry. Two kinds cover ten presets.
- **Rejected — a radial vignette:** needs `QtQuick.Shapes` or a shader for a barely visible
  effect; not worth the render cost on a tablet.

### Decision 6: One renderer for the page, the tiles, and the preview

`qml/components/BackgroundSurface.qml` takes a preset id (or an image path) and draws it.
`ThemedPageBackground`, the chooser's tiles and `LayoutPreview` all use it.

- **Why:** the chooser's promise is "this is what you will get". Three separate renderings of the
  same preset is three chances to drift, and the drift shows up exactly where it costs most — in
  the preview that the user made the decision from.

### Decision 7: An explicit later choice of background colour clears the preset

`applyLightTheme()` / `applyDarkTheme()` and a theme-editor edit of `customThemeColors`'
background colour both clear `backgroundPreset`.

- **Why:** the preset overrides precisely the value those two actions set. Without the clear, a
  user picks a new theme, or drags a colour in the web editor, and *nothing happens* — the classic
  shape of a bug report that takes an hour to reproduce.
- **Not cleared by a light/dark mode switch**, which selects no colour of its own; the preset just
  resolves to its other value. This is the common case and must not lose the user's choice.

### Decision 8: The recommended set — 5 solids, 5 patterns

Neutral-leaning and desaturated on purpose: these are the option for people who found the photos
too much, so a saturated background would miss the request. Every one of the ten is meant to be a
background someone actually works in front of all day — none is a demo of what the mechanism can
do. Values are the starting point; the contrast test is the gate.

| # | Preset | Kind | Dark | Light | Notes |
|---|--------|------|------|-------|-------|
| 1 | Graphite | solid | `#14161a` | `#f2f3f5` | True neutral, the quietest option in the set |
| 2 | Slate | solid | `#181f2b` | `#eceff4` | Cool blue-grey, nearest neighbour to today's default |
| 3 | Espresso | solid | `#1e1815` | `#f5efe8` | Warm brown / cream; the on-theme one |
| 4 | Forest | solid | `#131e19` | `#eaf1ec` | Muted green |
| 5 | Plum | solid | `#1c1620` | `#f2edf4` | Muted purple |
| 6 | Grain | tile 5% | Espresso pair | Espresso pair | Fine noise; reads as paper, not as texture |
| 7 | Linen | tile 5% | Graphite pair | Graphite pair | Fine woven crosshatch |
| 8 | Twill | tile 5% | Slate pair | Slate pair | 45° hairlines |
| 9 | Pinstripe | tile 4% | Forest pair | Forest pair | Fine vertical hairlines |
| 10 | Dot Grid | tile 4% | Plum pair | Plum pair | 6 px dot lattice |

Each pattern sits on a different solid's colour pair, so the ten entries are ten distinct
backgrounds rather than five colours shown twice. A pattern inherits its base pair wholesale — it
is one catalogue row, not a colour crossed with a texture, because a colour × pattern grid is the
second axis Non-Goals rules out.

Graphite dark measures 18:1 against `textColor` and 8.3:1 against `textSecondaryColor`; the test
enforces 4.5:1 for all ten in both modes, against both the raw colour and the scrimmed card
colour, which is the floor, not the target.

### Decision 9: Two contrast weaknesses the photos masked, fixed in the same bindings

Decision 2 points the glass chrome at a flat colour for the first time. Two of those bindings only
ever made sense against a photo, and presets promote both from "rare annoyance" to "visibly
broken". Both are pre-existing, both live in lines this change already rewrites, and both are
fixed here rather than left for a follow-up.

**`textSecondaryColor` brightens in the wrong direction in light mode.** Today it is
`Qt.lighter(base, 1.4)` whenever a background is active — correct against a dark page, backwards
against a light one, where lightening pushes secondary text *toward* the background. Any light
theme with a photo has this today; presets ship a full light set, so it stops being rare. The fix
is to move away from the background rather than always lighter: lighten in dark mode, darken in
light mode.

- **Rejected — special-case presets and leave the photo path alone:** the bug is the same bug in
  both, and splitting it would leave light-mode photo users broken for no reason.

**`insetBackgroundColor` disappears against a solid.** It is `scrimColor(backgroundColor)` — 40%
of the background colour drawn over the background. Against a photo that reads as a dimmed patch,
because the photo shows through at different brightness. Against a flat preset it is 40% of a
colour composited over *itself*, which is that colour exactly: text fields, switch tracks and
unselected pills sitting directly on the page vanish. Under a preset, the inset scrim goes toward
the contrast direction (toward black in light mode, toward white in dark) at the same alpha, so
the recessed step survives.

- **Scoped to the preset case** because that is where the arithmetic degenerates; the photo path
  keeps the behaviour it was tuned for. One binding in `Theme.qml`, no call-site sweep.
- **Most inset controls sit on a card**, where the scrim composites over `surfaceColor` and reads
  fine either way — this fixes the minority that sit on the page. That is also why it went
  unnoticed.

### Decision 10: The chooser becomes two sections, presets first

`GridView` has no section support, so the dialog body becomes a `ScrollView` containing a
labelled preset `Flow`/`Grid` above the existing image grid.

- **Why presets first:** they always exist. Today a user with caching off and no uploads opens the
  chooser and finds one "None" tile and an explanation — the feature reads as broken. With presets
  first there is always something to choose, and the caching explanation narrows to the section it
  actually describes.
- Preset tiles carry real names, so their `Accessible.name` is the translated preset name rather
  than the positional "Background image 3 of 12" the photo tiles are stuck with.

### Decision 11: Glass is a built-in theme, not a third theme mode

Glass joins `themeNames()` next to "Default Dark" and "Default Light", carries a `colorsDark` and
a `colorsLight` palette like any user-saved theme, and is selectable in the existing Dark theme
and Light theme pickers. `themeMode` stays `system` | `light` | `dark`; `isDarkMode` stays a bool.

- **Why:** the first reading of "a peer to light and dark" was a third *mode* — and that is the
  expensive, wrong one. `m_isDarkMode` is a boolean that fans out to the palette storage keys
  (`customColorsDark`/`customColorsLight`), `activeThemeName()`, the editing-palette check, the
  iOS status-bar style and this change's preset resolution; a tri-state would touch every one of
  them, and the OS light/dark signal could never select the third value anyway.
- **The app already has the right slot.** Themes are named palette blobs; `applyDarkTheme(name)`
  copies a blob into the active bucket. Glass is one more blob. It needs no new storage, no new
  mode, and no change to follow-system — a user can run Glass as their dark theme and Default
  Light as their light theme, and the OS switch keeps working.
- **This is what makes theme and background orthogonal**, which is the actual request: any of the
  three (or any saved theme) crossed with any background.
- **Rejected — a third `themeMode` value:** described above. Also user-visible nonsense: "Follow
  system" and "Glass" would be mutually exclusive states of one control for no reason.
- **Rejected — Glass as a background preset:** it is not a backdrop, it is a chrome treatment plus
  a palette, and it must compose *with* a background rather than occupy the same slot.

### Decision 12: Glass is the second trigger for the translucent chrome

`Theme.glassChrome` (Decision 2) becomes true when a background image or preset is set **or** the
active theme is Glass, detected by `activeThemeName()`.

- **Why the union:** an existing user with a photo background and Default Dark must keep exactly
  what they have today, and a Glass user with no background must still get glass. Neither implies
  the other, so the predicate is an OR and not a migration.
- **Glass with no background is a real state, not a degenerate one:** cards resolve to
  `surfaceColor` at `backgroundScrimAlpha` over the palette's own `backgroundColor`, and since
  Glass's palette sets those two apart deliberately, that reads as a frosted plane. This is
  precisely why Decision 9's `insetBackgroundColor` fix matters — that binding scrims
  `backgroundColor` over itself and degenerates on any flat page, preset or Glass alike.
- **An explicit palette entry is the marker, not the theme name.** The Glass palettes carry
  `glassChrome: true`, and `Theme.glassChrome` reads that. This was written as a name check, and
  implementation proved it wrong: `setEditingPaletteColor()` renames the edited slot to "Custom"
  on any colour change, so a name check turned the chrome opaque the instant a user nudged one
  colour — with no way to have a glass look on a customised palette. The flag survives that fork
  and survives Save-as-user-theme, so a duplicated Glass stays glassy. The original wording's own
  reason ("comparing colours to guess would break the moment a user duplicated it") argues for
  this; the *name* is the thing that breaks on duplication.
- **The flag is inert in both editors**, which iterate curated colour lists (`colorDefinitions` in
  `SettingsThemesTab.qml`, `COLOR_DEFS` in `theme_js.h`) rather than the palette's keys, so a
  non-colour entry never renders as a broken swatch.
- **The name is still the marker for read-only**, which is what names are for: `saveCurrentTheme()`
  and `deleteUserTheme()` refuse "Glass" the way they already refuse the two defaults.

### Decision 13: Glass is read-only in this release

"Glass" is added to the built-in-name guards that already protect "Default Dark" and "Default
Light" in `saveCurrentTheme()` and the delete path, and both theme editors present it as
non-editable with a "duplicate to edit" affordance.

- **Why read-only to start:** Glass's value is that it is a known-good tuned pairing of palette
  and translucency. Shipping it editable on day one means the first support question is someone
  who edited it into illegibility and cannot get back.
- **The app already implements duplicate-to-edit, and no new UI is needed.** An earlier draft of
  this decision claimed the opposite — that editing after applying a theme left the name lying
  about the palette — and that was simply wrong about the code. `setEditingPaletteColor()` renames
  the edited slot to "Custom", so the first colour change on Glass forks to a Custom palette with
  Glass's colours as its starting point and leaves the built-in untouched. That IS the
  duplicate-to-edit flow, arrived at by editing rather than by a modal.
- **What "read-only" therefore means in code:** the two guards. Glass cannot be overwritten by
  `saveCurrentTheme()` or removed by `deleteUserTheme()`, so the entry a user returns to is always
  the tuned original.
- **The fork keeps the glass look** because glassiness rides in the palette (Decision 12), not in
  the name. A user who wants a warmer glass gets one, and it is still glass.

## Risks / Trade-offs

- **A preset colour clashes with the active theme's accent/surface colours** → presets are
  desaturated neutrals, which sit under any accent; and the contrast test covers text against the
  background. A user who wants a colour that matches their accent exactly still has the theme
  editor.
- **Two things now set the background colour (preset and theme)** → resolution order is stated
  once (preset > theme custom colour > built-in default) and Decision 7 clears the preset whenever
  the other is chosen explicitly, so the two cannot sit in silent disagreement.
- **A tiled overlay costs a texture and a blend on every page** → tiles are a few hundred bytes,
  drawn once behind static content, at 4–6% opacity with no animation. If a low-end tablet shows
  it, the five solids are the same feature without the overlay.
- **A 1 px pattern can moiré or shimmer on a scaled display** → the five patterns are the highest
  risk item in the set, and the one thing that cannot be judged from a design doc. Author the
  tiles at a size that survives `Theme.scaled()` rounding, set `sourceSize` from the device pixel
  ratio, and check each on a real tablet (task 7.1). A pattern that shimmers gets its tile
  redrawn, not its opacity lowered until the shimmer is merely faint.
- **Sweeping ~70 call sites onto `glassChrome` touches a lot of files at once** → the
  replacement is mechanical and each site is the identical expression; the risk is a *missed*
  site, which shows up as one widget staying opaque under a preset. Grep for the old expression
  returning nothing is the check, and task 7.1 walks the idle screen and settings pages.
- **`Theme.backgroundColor` becomes a slightly longer binding on a very hot property** → it is one
  extra ternary on a property that is already a `_c()`-wrapped binding over two settings reads.
- **The web theme editor's background swatch appears to do nothing while a preset is active** →
  Decision 7 clears the preset on that edit, so it does something: it wins.
- **Glass is detected by name, so a user who duplicates it loses the glass chrome** → intended:
  the copy is an ordinary theme with Glass's colours, and a background switches the chrome on for
  it like any other theme. Worth confirming it reads acceptably (task 8.3) rather than surprising.
- **Two editors must both learn Glass is read-only** → the in-app editor and the web editor are
  separate code; missing one leaves a path that silently edits the applied copy. Both are listed
  in Impact and tasked, and the `saveCurrentTheme()`/delete guards are the backstop that holds
  even if an editor's UI misses it.

## Migration Plan

No migration. The new setting defaults to empty, which is byte-for-byte today's behaviour; there
is no stored state to convert and nothing to roll back beyond reverting the change. A settings
file written by this version and read by an older one carries an unknown key, which is ignored.

## Open Questions

- Exact hex values for the five solids are a starting point and may be adjusted during
  implementation once seen on a real tablet in both modes; the contrast test is the constraint any
  adjustment must satisfy.
- Pattern opacities (4–6%) and tile sizes are guesses until seen on a real panel. Expect to tune
  them once; they are catalogue data precisely so that tuning is a one-line edit.
- Whether the pattern-to-base pairings in Decision 8 are the right five. The pairings are an
  aesthetic call, not a constraint — swapping which solid a pattern sits on is a catalogue edit
  and breaks nothing.
- The Glass palettes themselves. Starting point: dark `background #101319` / `surface #2a3140` /
  `text #ffffff`, light `background #eef1f6` / `surface #ffffff` / `text #16181d` — chosen so
  `surface` still separates from `background` at 40% alpha, which is the whole job of a glass
  palette. Expect tuning on device; the contrast test is the constraint.
- Whether Glass should ship a default background rather than starting on its own flat colour. It
  looks best over one, but pre-selecting a background for the user overrides a choice they may
  already have made. Starting with no implicit background.
