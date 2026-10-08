# Tasks

## 1. Shot outcome on the worker (C++)

- [x] 1.1 Move the same-profile previous-shot query out of `ShotHistoryStorage::requestPreviousShot` into one static helper that takes a connection, and make `requestPreviousShot` call it. Verify that the Compare button's behaviour is unchanged in the app before it is replaced in 4.4.
- [x] 1.2 Add `requestShotOutcome(shotId)` / `shotOutcomeReady(shotId, QVariantMap)` (design D3). On a `withTempDb` worker it loads the shot and the previous same-profile shot, and returns `metrics` (`metricsFor`, `badgesFor`, `stoppedBy`), `previousShotId` and `comparison` (`ShotComparison::compare({prev, cur}, 0)`). Verify with a new test function in an existing storage test file (no new `tst_*.cpp`) that seeds three shots on two profiles and asserts three things:
  - the previous shot comes from the same profile
  - a first-on-profile shot gets no comparison
  - the comparison's grind Δ matches the seeded change

  Break the profile filter and watch the test fail.
- [x] 1.3 Build through the Qt Creator MCP (ask first) and verify that it is green, including the QML gate.

## 2. Shared QML pieces from the compare page

- [x] 2.1 Create `qml/components/ComparisonText.qml` (`pragma Singleton`, `QT_QML_SINGLETON_TYPE` in `CMakeLists.txt`) holding `txt`, `summaryFor`, `metricText`, `metricDelta`, `signed`, `unitLabel`, `inputText`, `inputDelta`, `ratingText`. Each takes rows as arguments. Point `ComparisonShotTable.qml` at it, delete the old copies, and verify that the compare page reads identically in the app: summary, Δs, rating/taste and the "Nothing changed" note.
- [x] 2.2 Extract the `Chip` component and the curve, phase and "+N" chip logic from `ShotComparisonPage.qml` into `GraphChip.qml` and `GraphChipRow.qml`. The align chip appears only when asked for. Verify that the compare page's chips behave as before, including phase hiding and the align toggle.
- [x] 2.3 Generalise `ComparisonReadout.qml` to rows of {swatch, values}. Give `HistoryShotGraph` `readoutCurves`, the inspected values in that shape, and `hiddenPhaseLabels` / `togglePhaseLabel()`. Verify that the compare page's readout is unchanged and that a test page using `HistoryShotGraph` shows values under the plot.
- [x] 2.4 Build and confirm that the QML diagnostics gate passes with the new files clean.

## 3. Web shared templates

- [x] 3.1 Lift the compare page's text helpers into `src/network/webtemplates/comparison_text_js.h` (`WEB_JS_COMPARISON_TEXT`). Lift the chip, readout and sticky-graph CSS and JS into `webtemplates/shot_graph.h` (`WEB_CSS_SHOT_GRAPH`, `WEB_JS_SHOT_GRAPH`). Make `generateComparisonPage` include them in place of its inline copies. Verify `/compare/` in the built-in browser at desktop and phone widths: summary, chips, readout, phase hiding, sticky graph.

## 4. One shot page (app)

- [x] 4.1 Route every shot-opening path to `PostShotReviewPage`.
  - `goToShotDetail(shotId, shotIds)` pushes it with `{editShotId, shotIds, autoClose: false}`.
  - Remove `ShotDetailPage.qml`, its `CMakeLists.txt` entry, its `main.qml` component and its `qml-diagnostics-baseline.json` entry.

  Verify that Shot History, the last-shot widget, the layout action and the post-shot path all open the editable page.
- [x] 4.2 Add stepping between shots per design D2: the graph swipe, Newer/Older buttons with "i / N", and the slide animation. Each step flushes the save, clears undo, moves the upload hold and cancels auto-close. Verify in the app that a rating edit survives a swipe and Undo is empty on the new shot. Also verify there are no Newer/Older controls on the post-shot path.
- [x] 4.3 Move Detail's actions onto the page: Delete with confirmation, the Debug Log dialog (advanced mode), Create recipe (hidden when the shot has a recipe, as before), the Visualizer upload state beside the existing Decent status, and going back on `shotDeleted`. Verify each in the app, including cancelling a delete.
- [x] 4.4 Add the "Shot results" section from `shotOutcomeReady`. It shows the default metrics, "Show more", the stop reason and a Δ per metric when a previous shot exists, all through `ComparisonText`. Request the outcome again on `shotMetadataUpdated` for this shot. Verify in the app that values match the compare page for the same pair, and that a grind edit updates the Δs.
- [x] 4.5 Put the comparison with the previous shot inside "Shot results" (`ShotResultsCard`): the previous shot named by profile and time with a Compare control (opens the comparison with it as base), the summary sentence, changed inputs before → after, the profile settings diff (`ProfileDialInDiffBlock`), and a Δ on each metric. Show only the metrics when there is no previous shot. Delete `ComparePreviousButton.qml`. Verify these cases in the app:
  - a grind-change pair
  - an unchanged pair, which says the setup was the same
  - a profile re-tune
  - a first shot on a profile, which names no earlier shot and shows no Δs
- [x] 4.6 Replace the page's `GraphInspectBar` with `GraphChipRow` and the generalised readout under the plot. Verify that turning off a curve, hiding a phase and tapping a point (values appear and nothing moves) all work in the app.
- [x] 4.7 Lay the page out as one column (design D6): header and plan line, the graph at full width with readout and chips, Newer/Older, then rating, taste, notes and measurements, then "Shot results", phase summary and the cards. Tighten the header into one row of pills, and remove the effectively one-column `GridLayout`. Store the graph height in one `shotPage/graphHeight` key, falling back to `postShotReview/graphHeight` on read. Verify at desktop, tablet-landscape and phone widths that nothing clips and the graph spans the page.
- [x] 4.8 Collapse the remaining duplicates the merge leaves, such as the recipe text helpers and the AI/Discuss/Email buttons, into single copies. Fix any accessibility gaps in the touched components (`docs/CLAUDE_MD/ACCESSIBILITY.md`). Verify that the screen-reader order follows the visual order.
- [x] 4.9 Route new user-visible strings through `ShotComparisonText` (labels shared with the comparison) or `TranslationManager` (page-only text). Verify that the translation scan and source-drift tests pass in the full suite.

- [x] 4.10 Usability pass after the first live run: Shot History keeps one button per row (the E button opened the same page as >, so it is gone); the milk-weigh pill shows only on the most recently saved shot (`weight-timed-steaming` delta); the previous shot's time is one `ShotHistoryStorage::shortDateTime` label shared by the comparison model, the web pages and the card; the readout under the graph is blank rather than a row of dashes until the graph is tapped (app and web); the web page carries the app's plan line (dose → yield, target, ratio, grind, rpm) under its title. Verified in the app and on `/shot/<id>`.

## 5. Web shot page

- [x] 5.1 Compute the design D3 payload in the `/shot/<id>` route's worker lambda. Pass it to `generateShotDetailPage` by concatenation with the `<` escape. Render "Shot results" with "Show more", the stop reason and, when there is a previous shot, its name, the summary, changed inputs and a link to `/compare/<prev>,<cur>`. Verify in the built-in browser against the app for the same shot.
- [x] 5.2 Replace the shot chart's series toggles with `WEB_JS_SHOT_GRAPH`: chips, phase chips and readout under the chart. Use two columns with a sticky graph from 1300 px, as `/compare/` does. Use shared CSS throughout. Verify at desktop and phone widths that there is no horizontal page scroll.
- [x] 5.3 Add Newer/Older links, Delete with confirmation (existing `/api/shots/delete`, then back to the list), and taste balance/body chips in edit mode, saved through `/api/shot/<id>/metadata`. Fix the `Beans (%13)` header. Verify in the browser that a delete removes the shot, a taste edit round-trips into the app, and the Beans header shows no grind.

## 6. Docs and integration

- [x] 6.1 Update the developer docs for the merge:
  - `docs/SHOT_REVIEW.md` (the source of truth for the shot pages)
  - `docs/SHOT_HISTORY.md`
  - the stale `ShotDetailPage` mentions in `docs/CLAUDE_MD/` (`BEAN_BASE.md`, `VISUALIZER.md`, `PROJECT_STRUCTURE.md`, `MCP_SERVER.md`)
  - the QML comments that name it (`BeanBaseDetailsRow`, `ConversationOverlay`, `KbDerivedFromLabel`, `ShotPlanText`)

  Verify that `git grep ShotDetailPage` returns only archived OpenSpec and history.
- [x] 6.2 Rewrite the wiki manual's Shot Detail / Shot Review entries as one short "Shot page" section: what it shows, how to step between shots, and the comparison with the previous shot in "Shot results". Cut the draft in half before committing. Verify that the wiki page renders.
- [x] 6.3 Run the full suite through `mcp__qtcreator__run_tests` (scope `all`, ask first). Verify that it is green, and run `scripts/check_*` text-invariants locally.
- [x] 6.4 End-to-end in the running app and browser: rated the newest shot from history, stepped Older and Newer (the rating stayed with its shot; a stale slider found and fixed with `rebindInputs()`), opened the comparison from "Shot results", exercised the chips, readout, Debug Log and Delete dialogs (delete cancelled: no throwaway shot, so a completed delete is unverified live), and checked `/shot/<id>` and `/compare/` at desktop and phone widths. `openspec validate unify-shot-page --strict` passes.

## Workflow follow-up

- Archive with `openspec archive unify-shot-page --yes` as the PR's final commit, before merge.
