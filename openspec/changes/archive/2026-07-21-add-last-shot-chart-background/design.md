## Context

The background is currently one of three things, all of them decoration, and all of them cheap
to draw: a flat catalogue colour, that colour with a tiled SVG over it, or a screensaver photo.
`BackgroundSurface.qml` is the single renderer for all of it, and both the chooser's tiles and
the chooser's preview instantiate the same component as the page does — that is what makes the
chooser's promise ("this is what you will get") true by construction rather than by three
renderings happening to agree.

The last shot's chart is a fourth kind, and it is different in ways that matter:

- It is **live data**, not an asset. It has to be fetched from SQLite, it changes when a shot
  finishes, and it can be absent entirely on a fresh install.
- It is **expensive to draw, but only once**. `HistoryShotGraph` is a `GraphsView` with up to
  eleven series plus dashed overlays, marker labels and a right-axis label column — far heavier
  than a JPEG. But it is also *static between shots*: nothing on it moves while the machine sits
  idle. Rendered once to an image and blitted thereafter, its steady-state cost is a texture,
  identical to the photo background. What it costs instead is **cache invalidation**.
- Its legibility profile is unlike either existing case. A preset colour is a **known** value, so
  the foreground is computed from it exactly. A photo is **unknowable**, so the app scrims and
  forces the glass on. A chart is a hybrid: its canvas is a known theme colour, but thin
  saturated curves cross it in unpredictable places.

The per-line visibility toggles (`graph/showPressure`, `graph/showFlow`, …) and the Basic /
Advanced split (`shotReview/advancedMode`) already exist and are read by `EspressoPage`,
`ShotDetailPage` and `ShotComparisonPage`. The chart is fed by plain array properties
(`pressureData`, `flowData`, …), so it does not care where its data came from.

## Goals / Non-Goals

**Goals:**

- Two chooser entries, **Last Shot** and **Last Shot (Advanced)**, that draw the most recent
  shot behind every page.
- Per-line visibility inherited from the review page's existing toggles, with no new settings.
- One renderer and one data load, shared by the page, the chooser tiles and the preview.
- A background that stays *readable as a chart* while text stays readable on top of it.
- Silent, graceful degradation to the plain theme background when there is no shot.

**Non-Goals:**

- **Not a live chart.** The background shows the last *completed* shot; it does not animate
  during extraction. The Extraction View already does that job on the espresso page.
- **Not interactive.** No crosshair, no inspect bar, no tap-to-toggle. It is wallpaper.
- **Not a new visibility model.** If someone wants different curves on the wallpaper than on the
  review page, that is a second set of toggles and this change does not add one.
- **Not per-shot selection.** "The last shot", not "a shot you pick". Pinning a favourite shot as
  wallpaper is a plausible follow-up and is deliberately excluded.

## Decisions

### 1. A background *source*, not a third mutually-clearing key

`backgroundPreset` and `backgroundImagePath` currently encode the choice implicitly: each setter
clears the other, and "which one is set" is the answer. A third kind makes that pairwise clearing
quadratic and easy to get wrong — the failure mode being two sources set at once, where whichever
`BackgroundSurface` happens to test first wins.

Introduce an explicit `backgroundSource` (`"none" | "colour" | "image" | "shot"`) as the single
answer to "what is the background", with the existing keys retained as its parameters. Setting a
preset sets the source to `colour`; setting an image sets it to `image`; the two shot entries set
it to `shot` and record which of the two.

*Alternative considered:* a sentinel in `backgroundPreset` such as `"__lastshot"`. Rejected — the
preset id is looked up in a catalogue that returns an empty entry for anything unknown, so every
lookup site would need a special case, and the spec requirement "a preset is a colour, not an
image" would become a lie in a second way.

*Migration:* an install with neither key set is `none`; with a preset set, `colour`; with an image
path, `image`. The derivation is unambiguous, so no stored-value rewriting is needed — consistent
with the project rule against retro-rewriting user-set data.

### 2. Render the chart ONCE to an image; the background is that image

The chart is static between shots. Nothing about it moves while you sit at the idle screen, so
there is no reason for a live `GraphsView` to exist behind every page — it should be rendered
once, to an image, and blitted from then on.

So: at the moment a shot's data becomes available, instantiate `HistoryShotGraph` offscreen at
background size, `grabToImage()` it, cache the result, and destroy the chart. `BackgroundSurface`
then draws an ordinary `Image`, which is **the path the photo background already takes**. Scrim,
glass chrome, fade-in and the stale-source fallback all come for free because it is the same
code.

This collapses most of the difficulty. It also makes several requirements trivially true rather
than carefully engineered:

- **Inertness** stops being something to verify — an `Image` cannot accept input, so there is no
  handler to omit and no `enabled: false` to get right.
- **The chooser tiles** stop being a question. Everything downstream of the grab is an image, so
  a tile is that image scaled, not a second live chart.
- **Steady-state cost** equals the photo background exactly: one texture.

*On piggybacking the review render:* tempting, and it is the obvious place to grab from since the
chart is already on screen there. Two wrinkles make it the wrong sole source. The review chart is
sized and styled for the review page — different aspect, labels and axes we do not want on
wallpaper — and, more importantly, **the user may never open review**. Pull a shot, walk away,
and the background would never update. So the grab is its own offscreen render at background
size, triggered by shot data becoming available rather than by a page being visited. Whether it
can *share* work with the review render is an optimisation to look at after it works.

*Where the cost actually is now:* one offscreen render per shot, on the same event that already
writes the shot to the database. That is a moment when the app is doing work anyway and the user
is not interacting.

### 3. The hard part is invalidation, not rendering

A cached image is only correct until something it was rendered from changes. The chart takes its
colours from `Theme`, its curves from the visibility settings, and its size from the window — so
the cache MUST be discarded and re-rendered when any of these change:

| Trigger | Why |
|---|---|
| A shot completes | The point of the feature |
| The drawn shot is deleted | It no longer exists |
| Theme applied, or light/dark switched | Curve and axis colours come from `Theme` |
| A `graph/show*` toggle changes | A curve appeared or disappeared |
| Basic ↔ Advanced entry switched | Different curve set |
| Window size or device pixel ratio changes materially | A stretched raster looks it |

The failure this replaces is worth naming: without theme invalidation you get **last week's theme
colours painted behind this week's theme** — a stale render is a subtler and longer-lived bug than
a slow one, because nothing about it looks broken until you notice the curves are the wrong blue.

The cache is therefore keyed on (shot id, advanced flag, theme identity, visibility set, size),
and a key mismatch triggers a re-render rather than a redraw of stale pixels.

*Persistence:* the render is held in memory. Writing it to disk would survive a restart, but a
re-render at startup costs one grab on an event the user is already waiting through, and a
persisted image is another thing that can go stale against the theme. Start in-memory; revisit
only if the startup flash is visible on the tablet.

### 4. Load the shot series once, off the main thread

The series are needed only at render time now, not continuously, but the rule stands: the fetch
uses the existing async path — `requestMostRecentShotId()`, then the canonical `requestShot()`
background-thread pattern — because database I/O never runs on the main thread.

`BackgroundSurface` never touches storage. It binds to the cached image.

### 5. Scrim it like a photo, and force the glass on

The chart is drawn full-page and dimmed, and `glassChrome` is forced on while it is active, for
the same reason an image forces it: opaque cards on top of a chart read as slabs sitting on data.

This makes `Theme.glassChrome` stop meaning "an image is set" — it already stopped meaning that
once the option was added, and the shared predicate is precisely the thing that makes adding a
third source a one-line change instead of a seventy-site sweep.

*Foreground derivation:* the chart's canvas is the theme's own background colour, so the existing
non-preset path is correct and nothing needs deriving. The curves are bright but sparse — the
same argument the pattern axis already makes, where what matters is opacity weighted by coverage.
The scrim is what buys the margin, and the scrim alpha is the one already tuned for photos.

*Alternative considered:* deriving the foreground from the chart's mean luminance. Rejected —
that mean moves every time a shot finishes, so the entire UI's text colour would shift when you
pulled a shot.

### 6. The pattern axis is disabled, not ignored

A tiled pattern over a chart is texture on top of data, and every combination of the six patterns
with a chart is worse than the chart alone. The Pattern row is **disabled with its reason
visible** while a shot background is selected, rather than silently having no effect — a control
that does nothing when you press it is a bug report waiting to happen.

The stored `backgroundPattern` value is left alone, so switching back to a colour restores the
pattern the user had.

### 7. Basic / Advanced is a property of the background, not a mirror of the review page

The two entries carry their own `advancedMode`. Per-line toggles are inherited live; the
advanced *split* is not.

The reason is that the alternative has a surprising failure: if the wallpaper mirrored
`shotReview/advancedMode`, then toggling Advanced on the review page — a thing you do to inspect
one shot — would silently repaint the whole app. Two entries make the wallpaper's own level an
explicit choice, and it is also the reading that matches "two more backgrounds".

## Risks / Trade-offs

- **A stale cached render** → The risk that replaced render cost, and the more dangerous of the
  two because it does not look like a bug. A theme change with no invalidation leaves the
  previous theme's curve colours painted behind the new theme, and nothing about that reads as
  broken until you notice the blue is wrong. Mitigated by keying the cache on everything the
  render depends on (shot, advanced flag, theme, visibility set, size) rather than by remembering
  to invalidate at each call site.
- **Render cost at shot end** → One offscreen grab per shot, on an event that already writes to
  the database and where the user is not interacting. Steady state is a single texture, the same
  as the photo background. Still worth timing the grab on the tablet, because a long stall right
  as a shot finishes lands at a bad moment — but this is no longer a per-page concern.
- **`grabToImage` on an offscreen item** → It needs a window and a live scene graph; grabbing an
  item that was never rendered can yield an empty result. This is the mechanism most likely to
  need a second attempt, and it is the reason the offscreen render is a task of its own rather
  than a line in another one.
- **A busy chart under text is still a chart** → The scrim is the mitigation, but nine of the
  nineteen catalogue colours taught us that contrast intentions need measuring. The legibility
  claim here is weaker than the preset case because the curve positions are data-dependent; this
  is why the treatment is "scrimmed like a photo" (an already-tuned, already-shipped alpha)
  rather than a new number.
- **A shot with a pathological series** — one sample, a 20-minute flush, all-zero pressure →
  renders as a degenerate chart rather than failing. Treated as valid: it is a true picture of a
  strange shot. Only *absent* data falls back.
- **Refresh timing** → The background changing the instant a shot ends is the point, but it must
  not repaint mid-shot or during the post-shot review flow in a way that flickers.
- **Three-way exclusivity is a settings-shape change** → The migration is derivable from existing
  values, and the new source is written by the same setters that already clear each other, so
  there is one place to get it right rather than three.

## Open Questions

- **Should the cached render be persisted to disk?** In-memory means one re-render at startup.
  Persisting avoids that but adds a file that can go stale against the theme. Start in-memory and
  revisit only if the startup flash is visible on the tablet.
- **Can the offscreen render share work with the post-shot review render?** They want different
  sizes and different labelling, and review may never be opened, so it cannot be the *only*
  trigger — but it may still be worth reusing once the standalone path works.
- **Should the background refresh when the drawn shot's *metadata* changes** (an espresso gets
  re-rated, notes added)? The curves do not change, so probably not — but a deleted shot must
  refresh, and those two paths share a signal.
