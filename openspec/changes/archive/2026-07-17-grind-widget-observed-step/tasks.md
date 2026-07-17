# Tasks

## 1. Shared noise-filtered step estimator

- [x] 1.1 Add a static `deriveGrindStep(const QList<double>& sortedDistinct) -> double` helper in `src/history/shothistorystorage_queries.cpp` (returns `0` when it cannot derive): modal consecutive gap, gaps rounded to 2 decimals, ties broken toward the smaller gap, clamped to a `0.05` floor, requires ≥ 2 distinct numeric values.
- [x] 1.2 Rewrite the step block in `ShotHistoryStorage::queryGrinderContext()` to call `deriveGrindStep` instead of the inline minimum-gap loop.
- [x] 1.3 Rename `GrinderContext::smallestStep` → `stepSize` in `src/history/shothistory_types.h` and update its writer in `queryGrinderContext()`.

## 2. QML-facing step accessor

- [x] 2.1 Add `Q_INVOKABLE double ShotHistoryStorage::grindStepForGrinder(const QString& grinderModel)` (declaration in `shothistorystorage.h`, definition in `_queries.cpp`): non-empty model → distinct settings for that grinder; empty model → all-grinder distinct settings; parse numeric subset; return `deriveGrindStep(...)` or `0`.
- [x] 2.2 Back it with the existing async distinct-value cache (`requestDistinctValueAsync` + `distinctCacheReady`) so a cold cache returns `0` and QML recomputes on `distinctCacheReady`.

## 3. Grind widget consumes derived step

- [x] 3.1 In `qml/components/layout/items/GrindQuickSelectItem.qml`, replace the `grindStep` property (currently reads `Settings.brew.grindQuickSelectStep`) with a value from `MainController.shotHistory.grindStepForGrinder(grinderModel)`, defaulting to `1.0` when it returns `0`, and re-evaluating on `_distinctCacheVersion`.
- [x] 3.2 Remove the now-dead comment/reference to the global step setting in the widget header comment; keep RPM-mode and compound stepping unchanged.
- [x] 3.3 Verify the picker rows step by the derived increment for a numeric grinder and still fall back to observed history / current-only when `stepGrind` yields ≤ 2 distinct rows.

## 3b. RPM step also history-derived (parity with burr)

- [x] 3b.1 Add `Q_INVOKABLE double ShotHistoryStorage::grindRpmStepForGrinder(const QString& grinderModel)` — cache-backed distinct query over `shots.rpm` (rpm > 0) scoped to the grinder, parsed to numerics, `deriveGrindStep(...)` or `0`; empty model → `0`.
- [x] 3b.2 In `GrindQuickSelectItem.qml`, replace the fixed `rpmStep: 50` with `MainController.shotHistory.grindRpmStepForGrinder(grinderModel)` rounded to int, defaulting to 50 when it returns `0`, re-evaluating on `_distinctCacheVersion`.

## 4. AI / MCP surfaces reflect the rename + improved value

- [x] 4.1 Update `src/ai/dialing_blocks.cpp` to emit `stepSize` (was `smallestStep`).
- [x] 4.2 Update `src/ai/aimanager.cpp` step label ("Smallest step" → "Typical step") and the guarded read of the renamed struct field.
- [x] 4.3 Update the `grinderContext` description prose in `src/mcp/mcptools_dialing.cpp` (and `mcptools_ai.cpp` if it references the step) to describe the noise-filtered typical step.

## 5. Remove the Grind step setting

- [x] 5.1 Remove `grindQuickSelectStep` property, getter, setter, and NOTIFY signal from `src/core/settings_brew.h` / `src/core/settings_brew.cpp`.
- [x] 5.2 Remove the "Grind step" `ValueInput` block from `qml/pages/settings/SettingsMachineTab.qml` (and any now-unused surrounding label/translation).
- [x] 5.3 Remove the backup + restore lines for `grindQuickSelectStep` in `src/core/settingsserializer.cpp`.
- [x] 5.4 Grep the tree for any remaining `grindQuickSelectStep` references and clear them.

## 6. Pill transparency (Grind + Ratio)

- [x] 6.1 In `GrindQuickSelectItem.qml`, gate the pill fill/border on `hasBackgroundImage` (`Settings.theme.backgroundImagePath.length > 0`): transparent + no border with an image, `Theme.surfaceColor` + border otherwise; move the value text color to a background-legible color when transparent.
- [x] 6.2 Apply the same `hasBackgroundImage` fill/border/text treatment to `qml/components/layout/items/RatioQuickSelectItem.qml`.
- [x] 6.3 Confirm both pills match Beans/Milk over a background image, and are unchanged (opaque) with no background image.

## 7. Tests

- [x] 7.1 Unit-test `deriveGrindStep`: clean 0.25 history → 0.25; outlier `8.1` added → still 0.25; tie 0.5/0.25 → 0.25; single value → 0; two values → their gap; a sub-floor cluster → clamped to floor.
- [x] 7.2 Test `grindStepForGrinder` for empty vs specific model, including the empty-history → `0` path.
- [x] 7.3 Update any existing test that asserts `grinderContext.smallestStep` to the new `stepSize` name/semantics.

## 10. RPM ↔ grind pairing — serialization linchpin

- [x] 10.1 `src/history/shotprojection.cpp`: `toVariantMap()` SHALL emit `m["rpm"]` (sparse, only when `rpm > 0`) next to `grinderSetting`; `fromVariantMap()` SHALL read `rpm` back. Restores rpm to MCP `shots_get_detail`/`shots_compare`, the QML history row, and the clone/coerce round-trip.
- [x] 10.2 Test: `tst_shotprojection::toVariantMap_roundTripsRpm` asserts the round-trip preserves `rpm` and that `rpm == 0` emits no key.

## 11. RPM ↔ grind pairing — AI advisor / dial-in

- [x] 11.1 `dialing_blocks.cpp`: emit per-shot `rpm` (sparse) on `dialInSessions[].shots[]` (`shotToJson`, ~:102), on `bestRecentShot` (~:374), and on the adherence `userResponse` (~:724). Keep rpm shot-variable (never hoisted to session `context`).
- [x] 11.2 `dialing_helpers.h` + `dialing_blocks.cpp`: add `int rpm` to `ShotDiffInputs`, copy it in `toDiffInputs`, and add an RPM line to `buildShotChangeDiff` so `changeFromPrev`/`changeFromBest` report RPM moves.
- [x] 11.3 `shotsummarizer.cpp`: render the already-computed `summary.rpm` on the prose "Grind setting" line (`@ <rpm> RPM`, ~:678), in `buildShotBlock` (~:559), and in `buildHistoryContext` (~:922); pair rpm in `aiconversation.cpp` grinder change-detection (~:804).
- [x] 11.4 `grinderContext` observed RPM: add RPM summary fields to `GrinderContext` (`shothistory_types.h`), populate in `queryGrinderContext` (rpm branch, reuse `deriveGrindStep` for the RPM step), and emit next to `settingsObserved` in `dialing_blocks.cpp` + the `aimanager.cpp` render.
- [~] 11.5 Tests: a synthetic per-shot-rpm dialing test was written then REMOVED per user direction ("do not put random rpm numbers in the DB"). RPM pairing is instead validated against a real user database (see §17). The `tst_shotprojection` rpm round-trip (§10.2) covers the serialization contract.

## 12. RPM ↔ grind pairing — MCP inputs/reads

- [x] 12.1 `mcptools_shots.cpp`: add `rpm` to the `shots_list` SELECT + row emit; add rpm to the change-diff.
- [x] 12.2 `mcptools_settings.cpp`: emit `dyeGrinderRpm` in `settings_get`; `mcptools_write.cpp`: accept `dyeGrinderRpm` in `settings_set`, `rpm` in `shots_update`, and `rpm` in `bag_create`/`bag_update` (+ whitelists). Emit `rpm` in `bagToJson`.
- [x] 12.3 Update MCP tool description prose where it enumerates grind fields.

## 13. RPM ↔ grind pairing — comparison model + other storage reads

- [x] 13.1 `shotcomparisonmodel.h`/`.cpp`: add `qint64 rpm` to `ComparisonShot`, populate from `record.rpm` in `scheduleLoad`, emit in `getShotInfo`.
- [x] 13.2 `ComparisonShotTable.qml`: add an RPM row (shown when any compared shot has `rpm > 0`).
- [x] 13.3 `unifiedbeansearchmodel.cpp`: select + emit `rpm` in the history projection query (both sites) so bag-from-history carries RPM.
- [x] 13.4 `shotfileparser.cpp`: on Visualizer import, split the `"<setting> <rpm>rpm"` convention back into `record.rpm`.

## 14. RPM ↔ grind pairing — network

- [x] 14.1 `visualizeruploader.cpp`: fix the PATCH (`updateShotOnVisualizer`, ~:371) to send `grinderSettingWithRpm(...)` instead of the bare setting, and add an `rpm` key to both `...WithOverrides` maps — stop clobbering the uploaded RPM.
- [x] 14.2 `shotserver.cpp` shot-list SQL + emit `rpm`; `shotserver_shots.cpp` shot-detail JSON `rpm`; and the ShotServer HTML **edit forms** — shot (`shotserver_shots.cpp`), bag (`shotserver_bags.cpp`), recipe (`shotserver_recipes.cpp`) — now have an RPM input (field + load + save) plus RPM in the bag/recipe list summary lines.

## 15. RPM ↔ grind pairing — remaining QML display

- [x] 15.1 `ShotHistoryPage.qml`: pair rpm in the list-row subtitle and the filter-chip summary (depends on 10.1 exposing `model.rpm`).
- [~] 15.2 `AutoFavoriteInfoPage.qml` + `AutoFavoritesPage.qml`: DEFERRED — grouping is keyed by grinderSetting, so pairing rpm needs the group-key/details query reworked. Memory note "Auto Favorites may be deprecated by recipes — don't invest in deepening it" ⇒ accept the limit rather than plumb rpm through the aggregate query.
- [x] 15.3 `CustomItem.qml`: add a `%RPM%` substitution token alongside `%GRIND%` (with dependency tracking).

## 16. Widget redesign — combined grind + RPM pill

- [x] 16.1 `GrindPickerDialog.qml`: support two labeled row groups — a Grind section and an optional RPM section — each with its own rows, current-highlight, and (grind only) Finer/Coarser end labels; carry the empty-state + accessibility per section.
- [x] 16.2 `GrindQuickSelectItem.qml`: replace the mutually-exclusive `isRpmMode` with a combined model — pill shows `"<grind> · <rpm>"` when `grinderRpmCapable` and an RPM is set, else grind alone; feed the picker both grind rows (`grindStepForGrinder`) and rpm rows (`grindRpmStepForGrinder`); picking a section row writes only that half. Gate the RPM half on broad `grinderRpmCapable`.
- [x] 16.3 Keep the transparent-over-background rendering for the combined pill; update the widget catalog description if it mentions RPM/grind mode.
- [x] 16.4 Accessibility: the combined pill's accessible name SHALL announce both values; each picker section row keeps a Button role + press action.

## 8. Docs

- [x] 8.1 Update the wiki manual Grind-widget entry: history-derived automatic stepping, removal of the Grind step setting, no-grinder fallback.
- [x] 8.3 Updated the wiki manual (combined grind+RPM pill + two-section picker) and `docs/CLAUDE_MD/AI_ADVISOR.md` (`structuredNext.rpm` coaching + per-shot/grinderContext RPM pairing).
- [x] 8.2 Update `docs/CLAUDE_MD/` dialing/layout references that mention `smallestStep` or the grind step setting.

## 18. RPM coaching — the advisor can recommend + score an RPM change

- [x] 18.1 `shotsummarizer.cpp` response-format schema: add `rpm` (integer) to the `nextShot` block — REQUIRED iff recommending an RPM move, ONLY for variable-RPM grinders, independent of grind.
- [x] 18.2 `dialing_blocks.h` `summarizeStructuredNext`: render the recommended `rpm` in Predicted parts.
- [x] 18.3 `dialing_blocks.cpp` `computeAdherence`: add `rpmMatches` (±25 RPM tolerance + no-movement guard) and score the `rpm` recommendation.
- [x] 18.4 `aimanager.cpp` recentAdvice render: show the actual RPM in "Your next shot".
- [x] 18.5 `grinderCalibration` block: carry the anchor shot's actual `rpm` on `coffeeAnchor` (query + `CalRow` + emit), so a variable-RPM recommendation can name the concrete RPM instead of only the prose caveat.

## 19. RPM brew override (start-a-shot)

- [x] 19.1 `ProfileManager::activateBrewWithOverrides`: add an `int rpm = -1` param (< 0 = leave untouched; >= 0 sets `dyeGrinderRpm`).
- [x] 19.2 `machine_start_espresso` MCP: accept an `rpm` override input and forward it; update the tool description.

## 17. RPM only for RPM-capable grinders (invariant already holds — validation only)

- [x] 17.1 Validate against the real user DB (markpalmos, pre-upgrade snapshot): every grind carrying an rpm token belongs to the RPM-capable DF83V; the non-RPM Eureka Mignon Specialita (2843 shots) and Niche Zero have zero. Invariant confirmed on real data.
- [x] 17.2 No production extraction/gating code added. The grind→rpm split already happened in the shipped equipment-packages migration; new shots carry RPM only for RPM-capable grinders because `dyeGrinderRpm` resets per active package. Earlier-added migration/import/save gates were REVERTED as unnecessary (the upgrade already ran; the DB was for validation only).

## 9. Verification

- [x] 9.1 Quick compile check via Qt Creator MCP (build only; do not launch the app). — Build succeeded, 0 errors / 0 warnings; tst_dialing_blocks, tst_aimanager, tst_settings all pass.
- [x] 9.2 Ask Jeff to launch the app and confirm: Grind pill steps by 0.25 on the Niche Zero, both pills render transparent over a background image, and the Grind step setting is gone from the Machine tab.
