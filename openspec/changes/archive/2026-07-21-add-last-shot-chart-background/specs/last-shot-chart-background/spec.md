## ADDED Requirements

### Requirement: Two shot-chart background entries
The background chooser SHALL offer two entries that draw the most recent completed shot's chart
as the app background: **Last Shot**, which draws the basic curves, and **Last Shot (Advanced)**,
which additionally draws the curves the review page treats as advanced (resistance, conductance,
Darcy resistance, mix temperature and its goal). The two entries SHALL be mutually exclusive with
each other and with every other background.

The advanced split SHALL be a property of the chosen entry, NOT a mirror of
`shotReview/advancedMode`. Toggling Advanced on a shot review page SHALL NOT change the
background, because that toggle is used to inspect a single shot and must not repaint the app.

#### Scenario: Basic draws basic curves only
- **WHEN** Last Shot is the active background
- **THEN** the advanced curves are not drawn, whatever `shotReview/advancedMode` is set to

#### Scenario: Advanced draws the advanced curves
- **WHEN** Last Shot (Advanced) is the active background
- **THEN** the advanced curves are drawn, whatever `shotReview/advancedMode` is set to

#### Scenario: The review page's advanced toggle does not repaint the app
- **WHEN** a shot-chart background is active and the user toggles Advanced on a review page
- **THEN** the background is unchanged

### Requirement: Per-line visibility is inherited from the review page
The curves drawn SHALL follow the same persisted visibility toggles the shot review chart uses —
the `graph/show*` keys for pressure, flow, temperature, weight, weight flow, resistance,
conductance, conductance derivative, Darcy resistance, mix temperature and mix temperature goal.
A curve hidden on the review page SHALL be hidden on the background. This change SHALL NOT
introduce a second set of visibility settings.

#### Scenario: Hiding a curve hides it on the wallpaper
- **WHEN** the user turns a curve off in the review chart's legend
- **THEN** that curve is no longer drawn on the background

#### Scenario: A hidden advanced curve stays hidden under the advanced entry
- **WHEN** Last Shot (Advanced) is active and an advanced curve is toggled off
- **THEN** that curve is not drawn, because the entry governs the advanced *split*, not each line

#### Scenario: No new settings
- **WHEN** the feature ships
- **THEN** no additional visibility setting is added for the background

### Requirement: The background draws the most recent completed shot
The background SHALL draw the most recent shot in history, and SHALL refresh when that changes:
when a new shot completes, and when the drawn shot is deleted. It SHALL NOT animate during an
extraction — it is the last *completed* shot, not a live chart.

#### Scenario: A finished shot becomes the background
- **WHEN** a shot completes while a shot-chart background is active
- **THEN** the background updates to that shot

#### Scenario: Deleting the drawn shot refreshes
- **WHEN** the shot currently drawn is deleted from history
- **THEN** the background refreshes to the new most recent shot, or falls back if none remains

#### Scenario: Not a live chart
- **WHEN** an extraction is in progress
- **THEN** the background still shows the previous completed shot

### Requirement: The chart is rendered once and drawn as an image
Because the chart is static between shots, the app SHALL render it once into an image and draw
that image as the background, rather than keeping a live chart behind every page. A background
surface — instantiated for every page, for the chooser's tiles and for the chooser's preview —
SHALL NOT issue a database query, and SHALL NOT instantiate a chart.

The shot's series SHALL be fetched on a background thread; database I/O SHALL NOT run on the main
thread.

#### Scenario: One render, many surfaces
- **WHEN** several background surfaces exist at once
- **THEN** they draw the same already-rendered image; no surface renders a chart of its own

#### Scenario: Steady-state cost matches an image background
- **WHEN** a shot-chart background is active and the user moves between pages
- **THEN** no chart is re-rendered; the cost is that of drawing an image

#### Scenario: No main-thread database work
- **WHEN** the shot series are loaded
- **THEN** the query runs on a background thread and the result is delivered to the main thread

### Requirement: The cached render is invalidated by everything it depends on
The render depends on the shot, on the advanced split, on the theme's colours, on the per-line
visibility settings and on the size it was drawn at. The app SHALL re-render when ANY of these
changes, rather than drawing a stale image.

A stale render SHALL be treated as a defect of the same seriousness as a wrong colour: it does
not announce itself, and the visible symptom — a previous theme's curve colours behind the
current theme — is easy to miss.

#### Scenario: A theme change re-renders
- **WHEN** the user applies a different theme, or switches between light and dark
- **THEN** the background is re-rendered in the new theme's colours

#### Scenario: A visibility toggle re-renders
- **WHEN** a `graph/show*` toggle changes
- **THEN** the background is re-rendered with that curve added or removed

#### Scenario: Switching entry re-renders
- **WHEN** the user switches between Last Shot and Last Shot (Advanced)
- **THEN** the background is re-rendered with the other curve set

#### Scenario: A resize re-renders rather than stretching
- **WHEN** the window size or device pixel ratio changes materially
- **THEN** the background is re-rendered at the new size rather than scaled from the old raster

#### Scenario: Nothing else re-renders
- **WHEN** the user navigates between pages, or shot metadata unrelated to the curves changes
- **THEN** the cached image is reused

### Requirement: The background is inert
The background SHALL NOT accept any input. No crosshair, inspect bar, tooltip, tap-to-toggle or
hover behaviour SHALL be reachable on it, and every control on the page in front SHALL respond
exactly as it does with any other background.

#### Scenario: Touches reach the page
- **WHEN** a shot-chart background is active and the user taps an idle-screen button
- **THEN** the button responds; the background receives nothing

#### Scenario: No chart interaction surfaces
- **WHEN** the user taps or hovers anywhere over the background
- **THEN** no crosshair, inspect bar or tooltip appears

### Requirement: Legibility treatment
The chart SHALL be drawn dimmed — at a fixed wallpaper opacity over the theme's own background
colour, applied at draw time so changing it costs no re-render — because at full strength thin
bright curves under white text read worse than a photo does. The glass chrome SHALL be forced on
while a RENDER EXISTS, not merely while the source is selected: with nothing drawn (a fresh
install, an empty history) the chrome stays opaque over the flat colour, since scrimming chrome
over a flat page cancels its elevation for nothing. Foreground colours SHALL NOT be derived from
the chart's content: the chart's canvas is the theme's own background colour, and deriving from
the drawn pixels would shift the whole UI's text colour every time a shot finished.

#### Scenario: Glass is forced on when the chart is drawn
- **WHEN** a shot-chart background is active, a render exists, and the glass option is off
- **THEN** the chrome is translucent anyway, appearing in the same update as the picture

#### Scenario: No render, no glass
- **WHEN** a shot-chart background is selected but there is no shot to draw
- **THEN** the chrome stays opaque over the theme colour

#### Scenario: Text colour does not move when a shot completes
- **WHEN** a new shot becomes the background
- **THEN** the app's text, icon and border colours are unchanged

### Requirement: No shot means no chart
When there is no shot to draw — a fresh install, a cleared history, or series that failed to
load — the app SHALL fall back to the plain theme background. It SHALL NOT show empty axes, a
spinner, or an error.

#### Scenario: Fresh install
- **WHEN** a shot-chart background is selected and history is empty
- **THEN** the theme's own background colour is shown, with no chart furniture

#### Scenario: The selection survives the absence
- **WHEN** history is empty, a shot-chart background is selected, and a shot is then pulled
- **THEN** that shot is drawn, because the selection was retained rather than reset

#### Scenario: A degenerate shot still draws
- **WHEN** the most recent shot has very few samples or an unusual duration
- **THEN** it is drawn as it is; only *absent* data triggers the fallback
