## 1. Settings

> The two new keys were first added to the generic store, matching the 13 `graph/*` siblings.
> That was then superseded: the whole set moved into a new `SettingsGraph` domain (see the
> proposal's included-refactor section), because adding to the generic store would have
> extended the stale-read defect class this feature depends on not having. The key is named
> `flowMultiplier`, not `flowScale`, because `Settings::useFlowScale()` already means "derive
> flow from the connected scale".

- [x] 1.1 Create `SettingsGraph` (`src/core/settings_graph.{h,cpp}`) and complete all 8 steps of the domain checklist in `docs/CLAUDE_MD/SETTINGS.md`, including the `QML_FOREIGN` registration in `settings_qml.h`
- [x] 1.2 Move the 11 series-visibility keys and `shotReview/advancedMode` onto it as `Q_PROPERTY`s, preserving every storage key
- [x] 1.3 Add `graph/flowMultiplier` (int, default 2) and `graph/rightAxisMode` (string, default `weight`) with static pure resolvers
- [x] 1.4 Resolver rules: non-numeric/out-of-range multiplier → 2; unrecognised mode → migrate `graph/showWeightAxis` (`true → weight`, `false → temperature`), else `weight`. Read-time only, no write-back
- [x] 1.5 Centralise the mode vocabulary and cycle order (`kRightAxis*`, `nextRightAxisMode()`, `cycleRightAxisMode()`) so the three graphs don't each re-type them

## 2. Scaling in the graphs

- [x] 2.1 `ShotGraph.qml`: apply the multiplier to the flow, weight flow rate and flow goal series; leave the source arrays unmutated
- [x] 2.2 `ShotGraph.qml`: left axis `titleText` becomes pressure-unit-only when the scale is not 1x
- [x] 2.3 `HistoryShotGraph.qml`: same series scaling and same title rule
- [x] 2.4 `HistoryShotGraph.qml`: make `pressureAxisMax` (`:334`) walk **unscaled** flow, weight flow rate and flow goal — the axis must be byte-identical at 1x and 3x for the same shot
- [x] 2.5 `ComparisonGraph.qml`: same series scaling and title rule
- [x] 2.6 Verify no non-flow series (pressure, goals, temperature, weight, resistance, conductance, Darcy, dC/dt) picks up the multiplier

## 3. Flow right-axis mode

- [x] 3.1 Add a `flowAxis` value-holder `QtObject` alongside `tempAxis`/`weightAxis`, with `max = sharedAxisMax / flowScale`
- [x] 3.2 Replace the `showWeightAxis` boolean with the three-state mode across `ShotGraph.qml`, `HistoryShotGraph.qml` and `ComparisonGraph.qml`
- [x] 3.3 Turn `toggleRightAxis()` into a three-way cycle (weight → temperature → flow → weight) that persists the new mode
- [x] 3.4 Extend the manual right-axis label column (`HistoryShotGraph.qml:724-750`) with the flow case: labels from the flow axis, flow unit suffix, its own theme colour
- [x] 3.5 Update the label column's `Accessible.name` for three states
- [x] 3.6 Update every `toggleRightAxis()` call site: `ShotDetailPage.qml:676`, `AutoFavoriteInfoPage.qml:230`, `PostShotReviewPage.qml:1419`

## 4. Readouts un-scale

- [x] 4.1 Audit every consumer of flow and weight-flow-rate values: `GraphInspectBar.qml`, `ComparisonInspectBar.qml`, crosshair/marker labels, `PhaseSummaryPanel.qml`, stat tiles and layout readout widgets
- [x] 4.2 Make each read the unmutated source array, or un-scale at the single point where plotted values enter the readout path — not per call site
- [x] 4.3 Confirm the layout/home-screen readout widgets that show live flow are unaffected (they do not read the graph's plotted values)

## 5. Graph options menu

- [x] 5.1 Create the menu component following `ExtractionViewSelector.qml`'s option-card pattern, theming and dismissal behaviour; add it to the `qt_add_qml_module` file list in `CMakeLists.txt`
- [x] 5.2 Move the advanced curves toggle into the menu, writing the same `shotReview/advancedMode` setting
- [x] 5.3 Add the 1x/2x/3x flow scale selector, showing which value is active
- [x] 5.4 `ShotDetailPage.qml:517-547`: the Advanced button becomes the menu opener and no longer toggles on activation
- [x] 5.5 `PostShotReviewPage.qml:1279+`: same
- [x] 5.6 Add the flow scale selector to `ExtractionViewSelector.qml`, gated on chart mode like the advanced toggle at `:317`
- [x] 5.7 Accessibility pass on the menu: roles, names, checked/selected state, `Accessible.onPressAction`, keyboard focus order; fix any pre-existing violations in the files touched

## 6. Widget and settings plumbing

- [x] 6.1 Add `graph/flowScale` and `graph/rightAxisMode` to `LastShotChartSource.qml:102`'s watch-list, keeping `graph/showWeightAxis` until the migration retires
- [x] 6.2 Check `LastShotChartRenderer.qml` and the snapshot path honour the scale, or deliberately do not — and say which in a comment

## 7. Internationalisation

- [x] 7.1 Add translation keys for the menu title, the flow scale label and its 1x/2x/3x options, and the flow right-axis accessible name; reuse existing common keys where they fit
- [x] 7.2 Confirm every new string goes through `TranslationManager.translate` or `Tr`, with no `translationVersion` boilerplate

## 8. Tests

> 8.2-8.4 are unticked deliberately. Each is currently guaranteed by CONSTRUCTION rather than
> by a test: the multiplier is applied to the mapped range (live, comparison) or at append
> time (history), and the source arrays are never mutated — so `pressureAxisMax` and every
> readout necessarily see true values. That is a stronger guarantee than a test, but it is
> not a *regression* guarantee: someone could later "simplify" by rewriting the arrays, and
> nothing would catch it. These need a QML-level harness, which this suite does not have.

- [x] 8.1 Settings resolver: default 2x; out-of-range and non-numeric `flowScale` → 2x; `showWeightAxis` true/false migration; unrecognised mode → weight
- [ ] 8.2 Auto-range invariant: `pressureAxisMax` identical at 1x, 2x and 3x for the same shot — the test that catches the feature-does-nothing bug
- [ ] 8.3 Series scaling: flow, weight flow rate and flow goal scale together; no other series scales
- [ ] 8.4 Readouts report true mL/s and g/s at 3x (the Decaid defect shape)
- [x] 8.5 Right-axis cycle order (persistence not separately asserted — it is a plain setting write)
- [x] 8.6 Add these as new test functions in existing `tst_*` files wherever possible — a new test FILE costs ~1.4 s of build forever, a new slot costs milliseconds (`docs/CLAUDE_MD/TESTING.md`)
- [x] 8.7 Break the code each new test covers and watch it go red before keeping it — done for the GraphSeries completeness test (removed the Weight entry, confirmed the exact failure message, restored)

## 9. Verification

- [x] 9.1 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) — ask before invoking, per the shared Qt Creator window
- [x] 9.2 Run `qmllint_check`; the tree is at zero and must stay there
- [ ] 9.3 Open the live espresso, shot detail, post-shot review and comparison screens in the running app — delegates and menus are not covered by the compiler, qmllint or the suite
- [ ] 9.4 Verify at 1x, 2x and 3x that the axis title, the right-axis flow labels and the inspect readouts all agree with each other

## 10. Documentation

- [ ] 10.1 Update the wiki manual shot-graph page: the new menu, the flow scale, the third right-axis mode, and a note that a screenshot taken at 2x/3x shows flow above its true height
- [ ] 10.2 Update `docs/CLAUDE_MD/` where the graph's axis behaviour is described, if any document asserts the current single-scale behaviour
- [ ] 10.3 Archive this change with `openspec archive add-graph-flow-scale-menu` as the last commit on the branch, before merge
