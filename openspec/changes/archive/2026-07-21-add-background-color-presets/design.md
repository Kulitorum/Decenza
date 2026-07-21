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

The change also adds a **glass chrome option** that turns that same chrome on without requiring a
background at all, so theme, glass and background are three independent choices (Decision 11).

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
- A preset selector in the web theme editor or the MCP `apply_theme` tool. Neither exposes any
  background surface today; the catalogue is placed in C++ so they can gain one later without a
  second definition.
- Any change to the screensaver, its media library, or its caching.
- A third `themeMode` value, or a Glass theme. Glass is an option (Decision 11).
- Per-preset text-colour customisation. While a preset is active the foreground is derived
  (Decision 3); a user who wants their own text colours should not set a preset.

## Decisions

### Decision 1: A preset sets the background *colour*, not a background *image*

`Settings.theme.backgroundPreset` is a new, separate string property. When it is set,
`Theme.backgroundColor` resolves through it; `backgroundImagePath` stays empty.

- **Why:** a preset carried as an image would mean shipping full-screen bitmaps, at several
  densities, for nineteen colours, to express what one hex value expresses — and a solid colour
  scales, crops and re-themes for free where a bitmap does none of those.
- **Rejected — reuse `backgroundImagePath` with `qrc:` values:** it forces a URL-scheme branch
  into `ThemedPageBackground` and, worse, into the deletion bookkeeping in
  `ScreensaverVideoManager`, which compares that setting against real file paths and clears it
  when the backing file is deleted. A preset has no backing file to delete. Keeping the two
  properties separate keeps that bookkeeping honest.
- **Consequence:** preset and image are mutually exclusive, and each setter clears the other. That
  is a requirement, not an emergent property — see the spec.
- **Note:** this decision is about *storage*, not about *appearance*. The chrome is Decision 2.

### Decision 2: One predicate gates the translucent chrome

Everything gated on "a background is set" — `cardBackgroundColor`, `dialogBackgroundColor`,
`insetBackgroundColor`, `actionTileColor`, `actionButtonFill()` and the per-widget scrims in the
layout items — is gated on one predicate, `Theme.glassChrome`, true when the glass option is on
or a background image is set. The ~70 hand-rolled
`Settings.theme.backgroundImagePath.length > 0` reads are swept onto it.

- **Why one predicate rather than extending the test in place:** `imagePath.length > 0 ||
  option` copied to seventy sites is seventy chances for the next trigger to be missed at one of
  them. One named predicate is also the honest name for what those sites were always asking —
  none of them cares that it is an *image*, only that the chrome should go translucent.
- **`backgroundScrimAlpha` is not retuned.** 0.4 was chosen against busy photos; changing it
  would move every existing photo user's UI to fix a problem they do not have.
- **Presets do not force it on.** Over a flat colour a translucent card and an opaque one are
  the same pixels, so the option's real effect there is the *size of the step* between card and
  page: with glass on the card composites at `backgroundScrimAlpha`, landing at 40% of the lift
  Decision 3a applies.

### Decision 3: One colour per preset, with the foreground derived from it

A preset is a single colour. While one is active, `textColor`, `textSecondaryColor`,
`iconColor`, `borderColor`, the card/dialog fill and the inset fill are all computed from it.

- **This replaces a mode-paired design that shipped in an earlier draft** — every preset carried
  a dark and a light value and resolved against `isDarkMode`. That guaranteed legibility, and it
  was wrong for the user: in dark mode the chooser offered ten near-blacks and nothing else. A
  catalogue whose light half is invisible whenever you might want it is not a catalogue.
- **Why derivation works here and not for photos:** a preset is a *known* hex value, so the
  colour text lands on is exact arithmetic. `Theme.contrastColorFor()` already existed for
  precisely this. Over a photo none of it is computable, which is why the image path keeps its
  eye-tuned scrim.
- **The cost, stated plainly:** a custom text colour is overridden while a preset is active. No
  stored preference can be right across a ramp from French Roast to Porcelain. Accents, chart
  series and status colours still come from the user's theme.
- **Rejected — let a light preset switch the app to light mode:** it silently changes a setting
  the user did not touch, and it re-couples the two things this decision separates.
- **Rejected — filter the catalogue by mode:** that is the mode-paired design in another costume;
  the grid would change contents on a mode switch and the light options would still be
  unreachable from dark mode.

### Decision 3a: The card lift is a fixed step in L*, not a fixed RGB fraction

`_liftFrom(base, deltaL)` bisects the mix fraction until the card sits a fixed perceptual
distance from the page.

- **The first version moved a fixed RGB fraction toward white**, which is a large perceptual
  step from near-black and almost nothing at L* 70. Every candidate between about L* 60 and 88
  therefore failed the card-separation test, and the catalogue had to cluster at the two
  extremes — which is exactly what "all the lights are too close in colour" was describing. The
  constraint was in the formula, not in the colours.
- **With a perceptual step the whole mid-light range is usable**, which is what makes a real
  ramp possible.
- **One genuinely dead band remains: L\* 30-58.** There neither black nor white text clears
  4.5:1 once secondary text is softened at all. That is a property of mid-greys under a
  monochrome-text UI, not a tuning failure, and the contrast test rejects anything placed there.
- Cost: a 20-iteration bisection per evaluation. It runs on a colour-binding change, not per
  frame.

### Decision 3b: Patterns are a second axis

`Settings.theme.backgroundPattern` is independent of the colour. Six patterns, any of which
can sit on any colour.

- **Baked-in patterns made a bad catalogue.** Half the entries were "colour X with texture Y",
  visually near-identical to plain colour X, so the chooser looked twice as long and offered
  half as much.
- **Two short rows beat one long one**: 19 colours x 7 pattern choices from ~26 tiles.
- **Opacity had to go up 3-4x** (from 4-6% to 14-18%) before a pattern read as a pattern at all.
  That is safe because a pattern is sparse: legibility depends on opacity WEIGHTED BY COVERAGE,
  and the densest tile shifts the page by about 4%. Each entry carries its measured ink coverage
  and the contrast test uses the weighted figure rather than the raw opacity.
- **A pattern is never drawn over an image** — it would fight the photograph rather than texture
  a surface.

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

### Decision 8: The set — coffee, not invented greys

| Band | Colours |
|------|---------|
| Roast (L\* 7-14) | French Roast, Cold Brew, Espresso, Green Bean, Ristretto, Cast Iron, Walnut, Barista |
| Machine (L\* 20-22) | Machine Steel, Denim Apron |
| Milk and cup (L\* 66-95) | Brushed Steel, Cortado, Latte, Oat Milk, Crema, Cappuccino, Steam, Flat White, Porcelain |

Patterns: Grain, Dot Grid, Pinstripe, Twill, Weave, Linen.

- **The first set was neutral by default and read as "eight variations on charcoal, seven on
  off-white".** Nothing in it was a colour anyone would choose on purpose. Naming and sourcing
  them from roast levels, milk drinks, the machine and the cup gives each one a reason to exist
  and makes the range legible at a glance.
- Worst measured contrast across the set is 4.87:1, including the densest pattern over the
  tightest colour; card separation is at least 2.4 L* everywhere.
- The gap between L\* 22 and 66 is Decision 3a's text dead band.

### Decision 9: Two contrast weaknesses the photos masked, fixed in the same bindings

*(Both still apply to the image path and to the glass option over a theme palette. While a
preset is active they are moot — Decision 3 derives those values from the background instead.)*

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

### Decision 11: Glass chrome is an option, not a theme

A boolean setting, `Settings.theme.glassChrome`, shown as a switch in Theme Mode. There is no
Glass theme.

- **It shipped as a theme first, and that was the wrong shape.** A theme occupies one polarity
  slot, so Glass could only ever be half-applied: setting it as the dark theme did nothing in
  light mode, and the two halves looked like different features. That is exactly what the user
  reported.
- **Translucency is orthogonal to light/dark.** Any theme can be glass; expressing an orthogonal
  property as a value of a non-orthogonal enum is the category error underneath the bug.
- **As a theme it also could not compose.** You could have Glass, or your own colours, never
  both. As an option it applies to any theme, including a user's own.
- **It also cost a whole 47-colour palette** to carry what is really one boolean plus two tweaked
  colours — and needed read-only guards, editor special-casing and a palette flag to survive
  editing. All of that disappears.
- **A background image still forces it on**, independent of the switch: opaque chrome over a
  photo reads as a slab, which is the original reason the translucent path exists.

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
- **A preset overrides the user's own text colour** → stated in Decision 3 and in the manual. The
  alternative is either an unreadable page or a catalogue that hides half its options; a user who
  wants full colour control should use the theme editor and leave the preset on None.
- **The mid band is empty, which may read as a gap** → it is measured, not arbitrary (Decision
  3a), and the tests reject anything placed there. If a mid-tone background is ever genuinely
  wanted, it needs a second text colour role for page-level text, which is a change of its own.

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
