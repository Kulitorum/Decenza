## Why

The background chooser now offers a colour, a colour-plus-pattern, or a photograph. All three
are decoration: none of them tell you anything. A machine sitting idle on the counter has one
piece of information worth putting on a full screen — how the last shot actually went — and the
app already draws exactly that chart on the review page. Making it the wallpaper turns the idle
screen into a glance at your last extraction instead of a picture of a cup.

It also costs almost nothing to build honestly: the curves, the per-line visibility toggles and
the Basic/Advanced split all exist already and are shared by three pages. This change wires an
existing renderer to an existing chooser rather than inventing a fourth way to draw a shot.

## What Changes

- **Two new background entries**, in a new **Shot** section of the chooser: **Last Shot** and
  **Last Shot (Advanced)**. They are the Basic and Advanced options — the entry you pick decides
  whether the advanced curves are drawn, so the wallpaper does not silently change when you
  toggle Advanced on the review page.
- **Per-line visibility follows the review page.** The existing `graph/show*` keys — pressure,
  flow, temperature, weight, weight flow, resistance, conductance, Darcy resistance, mix temp —
  govern the background exactly as they govern the review chart. Turning a curve off in the
  legend turns it off on the wallpaper. Nothing new to configure.
- **Drawn scrimmed, like a photo**, so it stays recognisably your shot while sitting far enough
  back to work in front of. Glass chrome is forced on while it is active, for the same reason it
  is with an image: solid panels on top of a chart read as slabs.
- **The background axis becomes three-way.** Colour, image and shot chart are one choice;
  picking any of them clears the other two. **BREAKING** for the stored settings shape: the
  existing pair of `backgroundPreset` / `backgroundImagePath` cannot express a third kind, so a
  background *source* is introduced and the two existing keys keep their current meaning
  underneath it.
- **The pattern axis does not apply to a shot chart.** A pattern over a chart is noise on top of
  data. The Pattern row is disabled, not silently ignored, while a shot background is selected.
- **Degrades to the plain theme background when there is no shot to draw** — a fresh install, a
  cleared history, or a shot whose series failed to load. No empty axes, no spinner.

## Capabilities

### New Capabilities

- `last-shot-chart-background`: the last shot's chart as an app background — the two catalogue
  entries and what distinguishes them, which shot is drawn and when it refreshes, how per-line
  visibility is inherited from the review page, the scrim and legibility treatment, the
  non-interactive constraint, and the no-shot fallback.

### Modified Capabilities

- `background-color-presets`: the background is currently a two-way choice between a preset
  colour and an image, stated in *A preset is a colour, not an image*, *Preset and image are
  mutually exclusive* and *A later explicit background choice clears the preset*. All three
  become three-way. *Sectioned background chooser* gains the Shot section, and *A pattern is an
  independent second axis* gains the exception that it is not independent of a shot background.
- `glass-chrome-option`: *Translucent chrome is gated on one shared predicate* currently forces
  the glass on for a background image. A shot chart joins it, so the predicate stops meaning
  "an image is set".

## Impact

**Code**

- `src/core/settings_theme.{h,cpp}` — the background source, its three-way exclusivity, and the
  two catalogue entries.
- `qml/components/BackgroundSurface.qml` — the one place a background is drawn, and therefore
  the one place this is added. Keeps the chooser's promise that a tile shows what a page gets.
- `qml/components/BackgroundPickerDialog.qml` — the Shot section and the disabled Pattern row.
- `qml/Theme.qml` — `glassChrome` and `hasBackgroundImage`/`hasBackgroundPreset` gain a third
  case; the derived-foreground block needs a reference colour for a chart background.
- `qml/components/HistoryShotGraph.qml` (or a trimmed wrapper) — rendered non-interactively.
  Reused, not copied.
- `src/history/shothistorystorage.{h,cpp}` — already exposes `requestMostRecentShotId()` and the
  async `requestShot()`; a small holder is needed to keep the loaded series available app-wide.

**Behaviour and risk**

- **Cost.** A chart behind every page is heavier than a colour or a static image, and the target
  is a tablet. The series are loaded once and drawn once; this must not re-query per page.
- **Data loading must stay off the main thread**, per the project rule. The existing async
  accessors are the path.
- The chart must be **inert** — no tooltips, no inspect bar, no touch handling reaching it.
- **Refresh** after a shot completes, and after a shot is deleted if it was the one being drawn.

**Docs**

- Wiki manual (`Kulitorum/Decenza.wiki`, `Manual` page): the Backgrounds section gains the Shot
  entries and the note that per-line visibility is shared with the review page.
