# Tasks

## 1. Shared comparison unit

- [x] 1.1 Create `src/history/shotcomparison.{h,cpp}` with the shot-variable input table (profile, temperature override, dose, effective target yield, grind, RPM, barista) alongside `ShotIdentity::fields()`; add to CMake as a narrow library linked by its consumers
- [x] 1.2 Move `sameGrinderSetting`, the grind/dose/yield tolerances and the RPM tolerance out of `dialing_blocks.cpp` into the unit; `dialing_blocks.cpp` uses them from there
- [x] 1.3 Implement `diffInputs(base, shot)`: per field `Same`/`Changed`/`OneSided`, numeric Δ, grind Δ suppressed unless grinder model and burrs are `Same`
- [x] 1.4 Implement `metricsFor(shot)` per design D4 (first drop, pour window from the detector results, peak pressure, pour-window peak/mean flow, mean weight flow, group temp, sag, resistance), reusing `ShotSummarizer::calculateMax`/`calculateAverage`
- [x] 1.5 Add preinfusion and pour times from the pour window, differing quality badges from the cached detector results, and `stoppedBy`
- [x] 1.6 Extract `ProfileManager::dialInDiffFor`'s row mapping into a shared function; implement `diffProfiles(base, shot)` via `Profile::fromJson` + `Profile::dialInDeltas`, names-only when titles differ
- [x] 1.6a Implement the structured summary per non-base shot: changed inputs, top three metrics by |Δ| ÷ |base| (zero-at-precision skipped), stop-reason difference, appeared/disappeared badges
- [x] 1.7 Implement `toJson(...)`: raw values plus `kind`/`unit` tokens, unit-bearing keys, unrated rating as null, a no-change flag per shot, and the unchanged-across-all set via `hoistSessionContext`
- [x] 1.8 Add `tests/tst_shotcomparison.cpp`: tolerances, notation equality, lettered grinds, one-sided values, grinder swap with no Δ, metrics on synthetic curves with exact expected values (corpus shots need the Visualizer parser library), profile encoding-noise vs a real setting change, unrated rating. Break each rule once and watch its test go red

## 2. Collapse the existing diffs onto the unit

- [x] 2.1 Rebuild `setupChangedFromPrior` on `diffInputs` (any of grind/RPM/dose/target yield `Changed`; `OneSided` is not a change); existing advisor tests stay green unchanged
- [x] 2.2 Replace `DialingHelpers::buildShotChangeDiff` in `changeFromPrev`/`changeFromBest` with the unit's JSON; update `resources/ai/tools/dialing_get_context.md` and every system-prompt reference to the old string form
- [x] 2.3 Stored advisor payloads carry the diff fields, so `AIConversation::changesFromPreviousShot` now builds projections from them and uses `pairChanges`
- [x] 2.4 Delete `buildShotChangeDiff` and `ShotDiffInputs` once nothing calls them

## 3. Comparison model

- [x] 3.1 Add `baseShotId` (default oldest, reset on `clearAll`/`addShots`) and `setBaseShot(id)`; visible columns = base + window of two over the rest in chronological order; previous/next pages the non-base shots
- [x] 3.2 Replace the `ComparisonShot` metadata copy with the loaded `ShotProjection` and its comparison JSON; the loader always loads base + window on the worker thread
- [x] 3.3 Expose the comparison JSON per visible shot and the visible count to QML

## 4. App page

- [x] 4.1 Rewrite `ComparisonShotTable.qml`: summary sentence per non-base shot from translated fragments; "What you changed" (differing inputs, marked cells, Δ, per-shot "nothing changed", "Unchanged" line, profile row via `ProfileDialInDiffBlock`); "What happened" default rows plus a collapsed "Show more"; stop reason and badges only when they differ; rating with taste in one row; notes as quotes; units in labels, tabular figures, Δ per design D8
- [x] 4.2 Shot header cards: tap re-bases and moves the shot left, base marked, eye toggle for graph visibility; accessible name and press action on both actions
- [x] 4.3 Remove `ComparisonDataTable.qml`; the crosshair readout becomes `ComparisonReadout`, a strip under the plot with one stable line per visible shot
- [x] 4.4 One wrapping chip row under the graph for curve and phase toggles, curves that are off collapsed to "+N"
- [x] 4.5 `ComparisonGraph.qml`: base drawn at `graphLineWidth + 1`; replace the hard-coded 3 here and in `ShotComparisonPage.qml` with the model's visible count
- [x] 4.6 Add the page-local "Align pours" chip, shifting each shot so its pour start meets the base's
- [x] 4.7 Graph panel fixed above a scrolling comparison, plot capped at about half the height (side by side tried and dropped)
- [x] 4.8 All new text through `TranslationManager.translate("comparison.*", …)`; translate the existing bare "TDS/EY" label
- [x] 4.9 Fix pre-existing accessibility gaps in every comparison QML file touched (raw `Rectangle`+`MouseArea` window arrows, phase pills)
- [x] 4.10 App-wide screen-reader toggle sweep (found reviewing the chips): `AccessibleMouseArea` and the hand-built CheckBox/RadioButton items handle `onToggleAction`, which VoiceOver sends for those roles on macOS; `StyledSwitch` and the firmware `Switch` drop their `toggle()` handlers, which skipped `onToggled` on every platform; rules added to `docs/CLAUDE_MD/ACCESSIBILITY.md`

## 5. Entry point

- [x] 5.1 Add a previous-shot query (same profile kb id and equipment package) via `withTempDb` on a worker thread
- [x] 5.2 Add "Compare with previous shot" to Shot Detail and Post-Shot Review, shown only when the query returns a shot, opening the comparison with it as base

## 6. Web page

- [x] 6.1 `/compare/` route: sort ids chronologically; `generateComparisonPage` embeds the unit's JSON instead of its own field list
- [x] 6.2 Port the summary sentence, both sections with "Show more", Δs, header-tap re-base, base line width and the alignment toggle to the page JS
- [x] 6.2a Web graph controls: replace the legend with the readout under the chart (one row per visible shot, only curves that are on), one chip row for curve and phase toggles, and eye toggles on shot header cards
- [x] 6.3 Side-by-side layout: graph left and sticky, sections right; single column below 1300 px; no horizontal scroll at phone width (labels on their own line, chip dates wrap)
- [x] 6.4 The shot list, shot detail, debug and compare pages use the shared `WEB_CSS_VARIABLES` / `WEB_CSS_HEADER` / `WEB_CSS_MENU` instead of hand-copied header, menu and colour CSS; each keeps only its own overrides
- [x] 6.5 Phone header: the injected status readout wraps to its own line below 600 px (shared, in `vital_stats.h`), so no page scrolls sideways
- [x] 6.6 Shot list on a phone: cards shrink to the screen (`minmax(0, 1fr)`), and the sort menu takes no width while closed and flips left only when right-aligned would leave the screen
- [x] 6.7 One shared `escapeHtml` (`webtemplates/escape_js.h`) for every web page, escaping both quotes; the compare page embeds its data by concatenation with `<` written as `\u003c`, builds it on the worker thread, and takes puck-prep order, profile-diff labels/decimals (`ProfileDialInText`, `ProfileFieldDelta::toVariantMap`) and its UI wording from the tables the app reads

## 7. MCP

- [x] 7.1 `shots_compare`: replace consecutive `changes[]` with the unit's base-relative JSON (base = oldest requested shot); update the description and `resources/ai/tools/` doc within the budget
- [x] 7.2 Bump `McpSurfaceVersion`; `scripts/check_mcp_tool_budget.py` passes

## 8. Verification and docs

- [x] 8.1 Build and run the full suite through Qt Creator MCP (scope `all`); QML lint gate passes
- [x] 8.2 Open the compare page in the running app with 2 and 5 shots, portrait and wide: re-base, paging, Show more, readout with all curves on, alignment toggle, unrated rating, a grinder swap, a re-tuned profile — the re-tuned profile (Blooming Espresso, Oct 1 → Oct 2, +0.5 °C on seven steps) was checked on `/compare/`, which renders the same `ShotComparison` JSON
- [x] 8.3 Open `/compare/` in Chrome at desktop and phone widths and check it against the app for the same shots and base
- [x] 8.4 Wiki manual: short Compare Shots entry (base shot, what the two sections mean, the one-tap entries) — written in the local wiki clone; pushed when this PR merges, since a wiki push is live at once

## Workflow follow-up

- Archive with `openspec archive compare-shots-what-changed --yes` as the PR's final commit.
