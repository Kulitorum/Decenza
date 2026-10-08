# Proposal

## Why

The app has two pages for one shot. **Shot Detail** is read-only, and its header has an Edit
button that opens **Shot Review**, which is the same shot again but editable. The two files are
largely copies of each other: header, plan line, graph card and resize handle, recipe card and
its text helpers, and the AI, Discuss and Email buttons are all duplicated. The copies have
already drifted. For example, the graph height is stored under two settings keys with two
defaults, and only Detail shows duration and ratio. A user who spots a wrong grind on the page
they are reading has to switch pages to fix it.

Neither page says much about how the shot went. Detail shows five numbers. Review shows none
outside the plan line. Neither page relates the shot to the one before it. The comparison work
(#2018) built exactly that in C++ and shared it across app, web and MCP: a full set of measured
metrics, why the shot stopped, which quality badges were raised, what changed since another
shot, and a one-line summary built by fixed rules.

## What Changes

- **One shot page.** Every way of opening a shot leads to the editable review page: the
  automatic post-shot path, Shot History, the last-shot widget, and the layout action. Shot
  Detail is deleted, along with its Edit button.
  - **BREAKING (UI):** there is no longer a read-only view of a shot.
- **Detail-only features move onto the shot page:**
  - stepping to the newer or older shot, by swipe or by buttons, when opened from a list
  - Delete Shot, with confirmation
  - View Debug Log
  - Create recipe from this shot
  - Visualizer and Decent upload state
  - going back when the shot is deleted elsewhere
- **"Shot results".** A metrics strip comes from `ShotComparison::metricsFor` / `metricDefs`
  and the `ShotComparisonText` wording, so the shot page and the comparison share one
  definition.
  - Shown by default: duration, yield against target, ratio, time to first drop, peak
    pressure and mean flow.
  - Under "Show more": the remaining comparison metrics.
  - It also shows why the shot stopped. It replaces Detail's five-cell row and adds metrics
    Review never had.
- **The previous shot, inside "Shot results".** When there is an earlier shot on the same
  profile, the card names it and compares with it in one place.
  - It shows the comparison's one-line summary, the inputs that changed (before → after), and
    the profile settings that changed when the profile was re-tuned.
  - Each metric carries its Δ against that shot.
  - A Compare control opens the full comparison. It replaces the bottom-bar Compare button.
  - A separate "Since your last shot" card was tried and dropped: two boxes comparing with
    the same shot read as one too many.
- **The compare page's graph improvements, shared rather than copied.**
  - The shot graph gets the comparison's chip row under the plot. Each curve is a toggle chip
    with an explanatory tip, rarely-used curves sit behind "+N", and each phase is a chip that
    shows or hides that phase's marker.
  - It also gets the crosshair readout under the plot in fixed columns, so it never covers a
    curve and a tap never moves the layout.
  - The chip and readout pieces move out of `ShotComparisonPage.qml` into shared components
    that both pages use. The labels and tips come from the same `ShotComparisonText` table.
- **The compare page's text conventions.**
  - Rating and taste share one compact line, for example "82% · sweet, thin", wherever the page
    shows them read-only.
  - Taste values are translated through the same table. Numbers use the comparison's display
    precision.
- **Better looking.**
  - One consistent card grammar, and a tighter header with actions grouped into pills.
  - One column at every width, with the graph at full width. Side by side was tried and,
    as on the comparison page, left the plot too small to read. The rating, notes and
    measurements follow the graph, as on the review page; "Shot results" and "Since your
    last shot" come after them, then the cards.
  - The graph height has one setting and one default.
- **Web `/shot/<id>` gets the same content and layout.**
  - The same "Shot results" metrics with "Show more" and stop reason, and the same "Since
    your last shot" summary, linking to `/compare/`.
  - The same curve and phase chips under the chart, and the same crosshair readout.
  - Editing in place as in the app: rating, taste, notes, measurements and barista save as
    they change, with Undo; beans and equipment are picked from the bags and packages.
  - The phase summary, upload state with Upload, Save as recipe, newer/older links and
    Delete.
  - The graph beside the details on browser windows 1300 px and wider, as `/compare/` does,
    built from the shared web CSS.
  - The summary and metric text helpers are extracted from the `/compare/` page into one shared
    web template rather than copied.
  - Also fixes the Beans card header, which shows the grind setting in parentheses.
- Updates the user manual: one shot page in place of two.

## Capabilities

### New Capabilities

- `shot-page`: the single page for viewing and editing one shot.
  - It covers how every entry point reaches it, stepping between shots, and the "Shot results"
    metrics.
  - It also covers the comparison with the previous shot inside "Shot results", the layout, the
    destructive and diagnostic actions it inherits from Shot Detail, and the web counterpart's
    parity.

### Modified Capabilities

- `shot-detail-metrics`: the five-metric row requirement is removed, since the page it governs
  no longer exists. "Shot results" in `shot-page` supersedes it.
- `post-shot-review-layout`: the field-grid ordering requirements are removed. The grid they
  describe is replaced by the `shot-page` layout. The rule that editing, autosave and undo
  survive a reorder is restated there for the new layout.
- `shot-plan-snapshot-line`: the requirement naming "the Shot Detail and Shot Review pages" and
  its swipe scenario is restated for the single shot page.

## Impact

- **QML**
  - `qml/pages/PostShotReviewPage.qml` (absorbs Detail's features, new layout and sections).
  - `qml/pages/ShotDetailPage.qml` is deleted. Its entry in `CMakeLists.txt` is removed.
  - Navigation in `main.qml` (`goToShotDetail`, `shotDetailRequested`), `AppShell.qml`,
    `ShotHistoryPage.qml` and `LastShotItem.qml`.
  - `ComparePreviousButton.qml` is replaced by the new card.
  - A new `ShotResultsCard` for the metrics and the comparison with the previous shot.
  - The summary and label helpers move out of `ComparisonShotTable.qml` into something both
    pages share.
- **C++**
  - `ShotHistoryStorage` computes the shot's metrics, badges and stop reason, plus the previous
    shot's comparison, on its worker thread.
  - It does this by extending `requestShot` / `requestPreviousShot`, or with one new request.
  - No schema change.
- **Web**
  - `src/network/shotserver_shots.cpp` (`generateShotDetailPage`).
  - A new shared JS template under `src/network/webtemplates/` for comparison text.
  - The `/compare/` page switches to that shared template.
- **Specs:** `shot-detail-metrics` and `post-shot-review-layout` lose their requirements.
  `shot-plan-snapshot-line` is reworded.
- **Unchanged:**
  - The refractometer hunt, which stays scoped to the review page and so covers the single page.
  - Autosave and undo semantics, upload holding, the MCP surface, and the comparison page.
- **Docs:** the wiki manual's Shot Detail / Shot Review sections.
