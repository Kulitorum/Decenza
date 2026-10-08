# Design

## Context

Both shot pages load the same `ShotProjection` through `ShotHistoryStorage::requestShot` →
`shotReady`. The difference between them is behaviour, not data.

- **`ShotDetailPage.qml`** (1,737 lines) is read-only. Only it has:
  - list navigation: swipe, Newer/Older buttons and the slide animation
  - Delete and its confirmation
  - the Debug Log dialog
  - Create recipe
  - the Visualizer and Decent status cards
  - the five-cell metrics row
- **`PostShotReviewPage.qml`** (2,699 lines) is the editor. It has:
  - autosave with coalesced undo, the `_fieldSpecs` diff and sticky sync to `Settings.dye`
  - upload hold and release
  - the refractometer hunt and Read TDS
  - milk weighing
  - the auto-close timer
  - the bean, equipment and taste editors

About 400 lines are copied between the two files: header, plan-line block, graph card and resize
handle, recipe text helpers, and the AI, Discuss and Email buttons.

The comparison work left reusable pieces:
- **C++.**
  - `ShotComparison::metricsFor` / `metricDefs` / `badgesFor` / `compare(shots, baseIndex)`.
  - `ShotComparisonText`: id → {key, label}, exposed to QML and embedded in web pages.
  - `ProfileDialInText`.
- **QML**, all currently written for the comparison page:
  - the inline `Chip` component and the curve and phase entry lists in `ShotComparisonPage.qml`
  - `ComparisonReadout.qml`
  - the summary and value formatters in `ComparisonShotTable.qml` (`summaryFor`, `metricText`,
    `metricDelta`, `inputText`, `ratingText`)
- **Web.** The `/compare/` page has the same formatters in JS (`txt`, `metricText`,
  `summaryFor`, `diffRowText`), plus `.chip` / `.readout` / sticky `.graph-col` CSS, all inline
  in `generateComparisonPage`.

## Goals / Non-Goals

**Goals:**
- Each block of shot-page UI exists once.
- Each comparison formatter exists once per language: one QML copy and one web JS copy, both
  driven by the C++ wording table.
- The shot page's comparison numbers come from the same `ShotComparison` call as the compare
  page and MCP.

**Non-Goals:**
- Overlaying the previous shot on the shot page's graph. The comparison is one tap away and does
  that fully, with re-basing and alignment.
- A web Undo across page loads, and the refractometer and milk capture on the web: both
  need the user at the machine.
- Changing the MCP surface. `shots_compare` already serves pair comparisons.
- Replacing `HistoryShotGraph` with `ComparisonGraph`.

## Decisions

### D1. Keep `PostShotReviewPage.qml`; delete `ShotDetailPage.qml`

The editor is the larger and riskier half: autosave, undo, upload hold, the refractometer and
milk capture. Moving Detail's few read-only features into it is much smaller than moving the
editor into Detail.

The file keeps its name. Renaming it would ripple through specs (`refractometer-review-page-
discovery` names it), `main.qml` and settings keys, with no user-visible gain.

In `main.qml`, `goToShotDetail(shotId, shotIds)` pushes the review page with
`{editShotId, shotIds, autoClose: false}`. The `shotDetailRequested` signal stays as the route
into it, so callers do not change.

### D2. Stepping between shots reuses the shot-switch path the page already has

`goToShotMetadata` already replaces the page's shot. Stepping works as follows:
1. `finalizeEdit` + `saveEditedShot()`, the same flush `StackView.onDeactivating` runs.
2. Clear the undo stack.
3. `shotUploads.releaseUpdates(old)` and `holdUpdates(new)`.
4. Set `editShotId`, which runs `loadShotForEditing()`.

The same-id re-delivery guard in `onShotReady` stays.

The swipe lives only on the graph's `SwipeableArea`, as on Detail, so it cannot fire from a text
field or the rating slider. The auto-close timer runs only when the page was opened with
`autoClose: true`. Stepping cancels it, because stepping is the user taking over.

### D3. One worker request delivers the shot's outcome and its "since last shot" comparison

Add `ShotHistoryStorage::requestShotOutcome(shotId)`, which emits `shotOutcomeReady(shotId,
QVariantMap)`. On its worker thread (the `requestShot` pattern with `withTempDb`), it:
1. Loads the shot.
2. Resolves the previous shot on the same profile using the query `requestPreviousShot` uses,
   moved into a shared static helper so both callers use one query.
3. Loads that shot.
4. Returns:
   - `metrics`: `ShotComparison::metricsFor`, `badgesFor` and `stoppedBy`.
   - `previousShotId`.
   - `comparison`: `ShotComparison::compare({prev, cur}, 0)`, the exact shape the compare page
     and MCP consume, so the summary facts, input diff, profile diff and metric Δs need no new
     format.

The page requests the outcome on load and again on `shotMetadataUpdated` for its shot, so an
edited grind updates the card.

**Alternative: compute in `convertShotRecord` on `shotReady`.** Rejected for two reasons. It
runs on the main thread, and it would make every `requestShot` caller pay for a second shot load
it does not need.

**Alternative: reuse `ShotComparisonModel`.** Rejected because it is a QML-facing singleton
holding the compare page's selection. Driving it from the shot page would overwrite the
comparison the user built.

### D4. Comparison formatters move into one QML singleton

`summaryFor`, `metricText`, `metricDelta`, `signed`, `unitLabel`, `inputText`, `inputDelta`,
`ratingText` and `txt` move from `ComparisonShotTable.qml` into `ComparisonText.qml`, a
`pragma Singleton` registered with `QT_QML_SINGLETON_TYPE` in `CMakeLists.txt`. The functions
take a comparison or metric row as an argument rather than reading a model, so the shot page's
plain `QVariantMap` and the compare page's model rows both work. `ComparisonShotTable.qml` calls
the singleton.

`ComparePreviousButton.qml` is deleted. The card replaces it on the shot page, and the shot page
was its only user.

### D5. Graph chips and readout become shared components

- **`GraphChip.qml`** is the extracted `Chip`.
- **`GraphChipRow.qml`**: curve chips bound to `Settings.graph.*`, "+N", the phase chips, and an
  optional align chip shown only by the comparison. The curve and phase entry lists move into it
  from `ShotComparisonPage.qml`. Curve visibility is the global `Settings.graph` toggles, so a
  curve hidden on one page is hidden on the other, as the options menu already behaves.
- **`HistoryShotGraph`** gains `hiddenPhaseLabels` and `togglePhaseLabel()`, with the
  comparison graph's semantics.
- **`ComparisonReadout`** is generalised from "rows = visible shots" to "rows = a list of
  {swatch, values}". `ComparisonGraph` and `HistoryShotGraph` each expose `readoutCurves` and
  the inspected values in that shape.
- **`GraphInspectBar`** stays for `AutoFavoriteInfoPage`. The shot page stops using it.

### D6. Layout: one column, graph at full width

The page is a single `ColumnLayout` at every width: header and plan line, the graph with
its readout and chips, Newer/Older, then rating, taste, notes and measurements, then "Shot
results" (which also carries the comparison with the previous shot), the phase summary,
barista, the recipe/bean/equipment cards, uploads and actions. The rating stays directly under the graph because it is the
first thing filled in after a pull; an order with the outcome first pushed it below the
fold and was reverted.

A two-column layout at tablet-landscape width was built and tried first. Like the
comparison page's earlier attempt, it halved the plot until the curves were hard to read,
so it was dropped. The web page keeps its side-by-side layout from 1300 px, where a browser
window leaves the chart wide enough; that is the same threshold `/compare/` uses.

The graph height becomes one key, `shotPage/graphHeight`, read once with a fallback to the
old `postShotReview/graphHeight` value, so existing users keep their size. The Detail key is
abandoned.

### D7. Web: shared CSS and JS templates; the shot page reuses `/compare/` pieces

- **New `webtemplates/comparison_text_js.h` (`WEB_JS_COMPARISON_TEXT`):** `txt`, `metricText`,
  `metricDelta`, `summaryFor`, `diffRowText`, `puckLabels`. These are lifted from
  `generateComparisonPage`.
- **New `webtemplates/shot_graph.h`:** `WEB_CSS_SHOT_GRAPH` (chips, readout, sticky graph
  column) and `WEB_JS_SHOT_GRAPH` (crosshair plugin, `renderReadout`, `renderChips`, phase
  toggles).
- The compare page includes both instead of its inline copies.
- `generateShotDetailPage` takes the D3 payload, computed on the route's worker thread in the
  same `QThread::create` lambda that loads the record. It is embedded by concatenation with the
  `<` escape, as `/compare/` does.

The Edit/Save mode is gone: every field POSTs its change through the existing
`/api/shot/<id>/metadata` as it is made, with an in-page Undo stack of the values it replaced.
Beans and equipment are picked from `/api/bags` and `/api/equipment` and written with the same
metadata keys the app's dialogs use (`bagId` and the bean snapshot; `equipmentId`). Two small
routes are new: `GET /api/shot/<id>/outcome` (shotOutcomeStatic as JSON, so "Shot results"
refreshes after a save) and `POST /api/shot/<id>/upload` (`ShotUploads::uploadNow`). Delete
POSTs to the existing `/api/shots/delete` and moves to the next shot.

The `Beans (%13)` header bug is fixed by dropping the placeholder.

### D8. The refractometer hunt is unchanged

The hunt is scoped to `PostShotReviewPage` being active (`refractometer-review-page-discovery`).
Opening a shot from history therefore now hunts, as tapping Edit on Detail always did. The hunt
only runs when a refractometer is saved and not connected, so users without one see no
difference. Restricting it to the newest shot was considered and rejected: it would add a rule
to a spec that today has none, to save a scan that only refractometer owners pay for.

## Risks / Trade-offs

- **Browsing history now happens on an editable page, so a stray tap could change a rating.**
  → Edits need deliberate controls: the slider, chips and fields. The swipe is confined to the
  graph, and Undo is in the bottom bar. A read-only toggle was considered and rejected, because
  it is the second page again under another name.
- **One more worker query per shot open (the previous shot plus `compare`).**
  → It is on a worker, and the page renders before it arrives. The card and Δs fill in when
  `shotOutcomeReady` lands. Two shots' metrics are the same work the compare page does per pair.
- **The page gets longer.** → "Show more" keeps "Shot results" to six rows by default, the
  comparison with the previous shot is a sentence plus the changed inputs inside the same
  card, and everything after the outcome is where it was on the review page.
- **Moving formatters out of `ComparisonShotTable` can regress the compare page.**
  → The compare page is re-verified in the app and on the web as part of the change. The
  formatters move unchanged.
- **The settings-key fallback for the graph height.** → It is read-once code with no migration.
  The old key is simply no longer written.

## Migration Plan

No data migration. The old `shotDetail/graphHeight` setting is left unread. Rollback is a revert:
no schema or persisted format changes.
