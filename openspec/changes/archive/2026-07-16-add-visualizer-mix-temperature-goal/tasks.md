## 1. BLE decode

- [x] 1.1 Add `double setMixTempGoal = 0.0;` to `ShotSample` in `src/ble/de1device.h`, documenting that it is `SetMixTemp` and that `setTempGoal` is `SetHeadTemp`.
- [x] 1.2 Decode `SetMixTemp` in `src/ble/de1device.cpp`: bytes 11–12 in the 19-byte path (`:448`), bytes 8–9 in the 17-byte path (`:460`), both `decodeShortBE(...) / 256.0`.
- [x] 1.3 Populate `sample.setMixTempGoal` in `src/simulator/de1simulator.cpp:416` so simulated shots exercise the whole path.

## 2. Live model plumbing

- [x] 2.1 Add `m_temperatureMixGoalPoints` to `src/models/shotdatamodel.h` with a `temperatureMixGoalData()` accessor mirroring `temperatureGoalData()`.
- [x] 2.2 Add a `temperatureMixGoal` parameter to `ShotDataModel::addSample()` and append the point beside `m_temperatureGoalPoints` (`shotdatamodel.cpp:271`).
- [x] 2.3 Mirror `m_temperatureGoalPoints` at **every** other site: reserve (`:18`), clear (`:127`), trim-to-cutoff (`:448`), and the dirty-flag/flush handling. Grep for `m_temperatureGoalPoints` and confirm each hit has a mix-goal counterpart.
- [x] 2.4 Expose the series to QML alongside the existing goal series (`shotdatamodel.cpp:608` pattern).
- [x] 2.5 Update both `addSample()` call sites in `src/controllers/maincontroller.cpp` — the real one (`:3762`, pass `sample.setMixTempGoal`) and the replay/preview one (`:3407`).

## 3. Persistence

- [x] 3.1 Add `QVector<QPointF> temperatureMixGoal;` to `ShotRecord` in `src/history/shothistory_types.h` and `QVariantList temperatureMixGoal` (+ `Q_PROPERTY`, `toMap`/`coerce` entries) to `ShotProjection` in `src/history/shotprojection.{h,cpp}`.
- [x] 3.2 Write `root["temperatureMixGoal"]` in `compressSampleData()` (`shothistorystorage.cpp:1671` area) and read it back under an `if (root.contains("temperatureMixGoal"))` guard in `decompressSampleData()`. No DB migration and no schema-version bump — the blob is additive by design.
- [x] 3.3 Map the series in `src/history/shothistorystorage_serialize.cpp` beside `p.temperatureGoal` (`:108`).
- [x] 3.4 Confirm nothing in the export/import path (`shothistoryexporter.cpp`, `shotfileparser.cpp`) needs the field: de1app has no `espresso_temperature_mix_goal` vector, so imported shots legitimately have none.

## 4. Visualizer upload

- [x] 4.1 In the live builder (`visualizeruploader.cpp:~806`), emit `temperature["mix_goal"] = interpolateGoalData(...)` guarded by `!isEmpty()` — mirroring the existing `temperature["mix"]` guard. An unguarded call would upload a zero-filled line for every pre-change shot.
- [x] 4.2 In the history builder (`:~1403`), emit `mix_goal` under the same guard.
- [x] 4.3 Fix the pre-existing drift in the history builder: it never emits `temperature["mix"]`, so re-uploads silently drop the measured mix line the live path sends. Add it, guarded.

## 5. Graph series (advanced, default off)

- [x] 5.1 Add `temperatureMixGoalColor` to `qml/Theme.qml` beside `temperatureMixColor` (`:410`), routed through `_c()` + `Settings.theme.customThemeColors` like every other series color. Deliberately a DEEPER violet (#a678b8), not the washed-out twin the other goal colors are: seen rendered, two dashed pastels ~1°C apart read as one series. Visualizer separates the same pair the same way.
- [x] 5.2 `qml/components/ShotGraph.qml`: add `property bool showTemperatureMixGoal: Settings.boolValue("graph/showTemperatureMixGoal", false)` and a dashed series `visible: chart.showTemperatureMixGoal && chart.advancedMode` (follow the mix-temp series at `:315`).
- [x] 5.3 `qml/components/HistoryShotGraph.qml`: same property, series, and inspect entry (`:242`, `:461`).
- [x] 5.4 `qml/components/ComparisonGraph.qml`: add the series descriptor with `advanced: true, showFlag: "showTemperatureMixGoal"` (`:88`).
- [x] 5.5 `qml/components/GraphLegend.qml`: add the legend row with `advanced: true`, placed next to "Mix temp" (`:34`), using `TranslationManager.translate("graph.mixTempGoal", "Mix temp goal")`.
- [x] 5.6 `qml/components/GraphInspectBar.qml`: add the readout entry gated on `show: g.showTemperatureMixGoal && g.advancedMode`, and add the property to the change-tracking binding list (`:34`).
- [x] 5.7 `qml/components/ComparisonDataTable.qml`: add the column/settings-key mapping (`:71`, `:87`) — or drop it here and resolve design.md's open question about column budget.
- [x] 5.8 Verify shots with an empty mix goal series render no line rather than a line at zero, on all three graphs.

## 6. Tests

- [x] 6.1 Unit-test the BLE decode for **both** packet layouts from known byte buffers, asserting mix and head goals are distinct and not transposed — the likeliest silent bug in this change.
- [x] 6.2 Test the storage round-trip: save a shot with a mix goal series, load it, compare.
- [x] 6.3 Test that a blob with no `temperatureMixGoal` key loads with an empty series and no warnings.
- [x] 6.4 Test that upload JSON contains `mix_goal` of `elapsed` length when data exists, and **no `mix_goal` key at all** when the series is empty — for both builders.
- [x] 6.5 Update any test/stub broken by the `addSample()` signature change; build the `all` target so test stubs are compiled.
- [x] 6.6 Share the shot/history sources through the test libraries instead of relisting them per target. Added `ANALYSIS_SOURCES` (plain Qt Core) to `decenza_testlib`, and a new `decenza_shotlib` for the Quick/Graphs-dependent half (storage, shotdatamodel, fastlinerenderer, visualizeruploader). Removed the `HISTORY_SOURCES` variable and ~105 duplicated source lines across 20 targets.

## 7. Verification and docs

- [x] 7.1 Build via Qt Creator MCP and confirm no new warnings, including no `qrc:/…qml` TypeErrors.
- [x] 7.2 Verified on a simulated shot (macOS, shot 1062): 61 mix-goal samples in the blob, legend entry advanced-gated and off by default, line renders when enabled, and the upload stored `espresso_temperature_mix_goal` on visualizer.coffee. NOT yet verified on a real DE1 — the simulator emits ShotSample directly and never exercises parseShotSample(), so the BLE byte offsets remain covered only by unit tests.
- [x] 7.3 Verified on shot 38 (recorded 2025-01-01, blob has no `temperatureMixGoal` key): with the toggle explicitly ON the graph draws no line rather than a line at zero, and the re-upload carries no `espresso_temperature_mix_goal`. The same re-upload DID carry `espresso_temperature_mix`, confirming the history-builder drift fix (task 4.3) against the live service.
- [x] 7.4 Update `docs/CLAUDE_MD/VISUALIZER.md` with the `mix_goal` field and the two-builder rule.
- [x] 7.5 Update the wiki manual's graph/advanced-mode section to document the new line (required for user-visible features).
- [x] 7.6 Run `/opsx:archive` as the final commit on the feature branch, before merge.
