# Tasks: plan-widget-review-fixes

## 1. Shot plan toggles + content (ShotPlanText)

- [x] 1.1 Restore toggle semantics in `qml/components/ShotPlanText.qml`: `_roasterStr` = roaster brand only; new `_coffeeStr` = coffee name gated on new `showCoffee` flag; `_grindStr` = grinder setting + `dyeGrinderRpm` ("… · 90 rpm") when > 0, gated on `showGrind`; wire all segments through `_build()` in the documented order (dose, roaster, coffee, grind, roast date)
- [x] 1.2 Restore the yield-override arrow in `_yieldStr`: `"{profileYield} → {target}g"` when `Settings.brew.hasBrewYieldOverride && profileYield > 0 && |targetWeight − profileYield| > 0.1` (highlight stays temperature-only); update the header comment
- [x] 1.3 Add `showCoffee` pass-through (modelData key `shotPlanShowCoffee`, default ON via `!== false`) to `qml/components/layout/items/ShotPlanItem.qml` and `qml/components/layout/items/PlanItem.qml`

## 2. Option plumbing (editors + C++ + tests)

- [x] 2.1 In-app editor `qml/components/layout/ScreensaverEditorPopup.qml`: relabel toggles to "Coffee" and "Grind", add the `shotPlanShowCoffee` toggle (load/save/default), keep the existing five keys working
- [x] 2.2 Web editor `src/network/shotserver_layout.cpp`: add the Coffee toggle to the shot-plan/plan options panel (checkbox id, load, save), relabel Grind
- [x] 2.3 ~no code needed~ Whitelist `shotPlanShowCoffee` in `src/core/settings_network.cpp` and extend `tests/tst_settings.cpp` coverage for the new key
- [x] 2.4 Retitle the `plan` widget's settings dialog ("Plan Settings") and add a one-line note that the toggles apply to the shot-plan display (in-app popup + web editor)

## 3. Steam plan (SteamPlanText)

- [x] 3.1 Pitcher-name dedupe: when the preset name contains "pitcher" (case-insensitive), use a second template without the word ("Steam %1 of milk, using the %2 for %3"), in both `text` and `_rich`
- [x] 3.2 Add `Q_INVOKABLE int SettingsBrew::effectiveSteamDurationSec(int pitcherIndex, double milkG)` — returns `scaledSteamTime(pitcherIndex, milkG)` when > 0, else that preset's base `duration` — so the scaled-or-base resolution lives in one place per the weight-timed-steaming spec's single-source rule
- [x] 3.3 `SteamPlanText` displays `effectiveSteamDurationSec(selectedIndex, targetMilk)` (its existing session/calibration/last-milk fallback chain feeds `milkG`) instead of reading `Settings.brew.steamTimeout`; rewire IdlePage's steam `onPresetSelected` to use the same helper for the `steamTimeout` write

## 4. IdlePage fixes

- [x] 4.1 Restore `ScaleDevice && ` null guards at the three unguarded sites (`idlePitcherDetect.rawWeight`, `idlePitcherDetect.active`, steam `onPresetSelected` milk read)
- [x] 4.2 Make the pitcher prompt's hint line conditional on `Settings.brew.doseCaptureSoundEnabled`: beep wording when on, "hold still until the weight registers" wording (new translation key) when off

## 5. Page-state consolidation

- [x] 5.1 Add `property string currentOperationMode: ""` to `qml/Theme.qml` next to `currentPageObjectName`; IdlePage's `_publishOperationMode()` writes `Theme.currentOperationMode`
- [x] 5.2 `PlanItem.qml`: bind `_onSteamPage` directly to `Theme.currentPageObjectName` / `Theme.currentOperationMode`; delete `winRoot`, `_pageName`, `_opMode`, `_refreshPage()`, and the `Connections` block
- [x] 5.3 Remove the now-unused `currentPageObjectName` / `currentOperationMode` properties from `qml/main.qml` (verify no other consumers first)

## 6. Small leftovers

- [x] 6.1 `qml/components/layout/items/ShotPlanItem.qml`: translate the hardcoded `Accessible.name` using the existing `plan.a11y.shotPlan` / `plan.a11y.shotPlanEmpty` keys
- [x] 6.2 `qml/components/BeanSummary.qml`: fix the header comment so the `[· Def Md]`/`[Frozen]` suffix is documented on the history-only template line too

## 7. Separator visibility sweep (styled bold dot in remaining data rows)

- [x] 7.1 GATE: verify elide + StyledText works in Qt 6.11 — feed `BeanSummary` an overlong bean name on a narrow window and confirm the ellipsis appears (not clipping/overflow). If elide is broken, STOP this group and flag BeanSummary itself for a rethink
- [x] 7.2 Convert `qml/components/BagCard.qml` (attrLine + metaLine) to `Theme.joinWithBullet` + `textFormat: Text.StyledText`; keep plain joins for any accessibility strings
- [x] 7.3 Convert `qml/components/EquipmentSummary.qml` (puck-prep line + lastDialLine) the same way — note `equipment.card.puckPrep`/`lastGrind` use `.arg()` templates, so escape the translated prefix and join only the parts
- [x] 7.4 Convert `qml/components/BeanBaseDetailsRow.qml` (origin · variety · process line)
- [x] 7.5 Convert `qml/components/ChangeBeansDialog.qml` (beanBaseAttr line; leave the source-label line plain)
- [x] 7.6 Leave incidental captions as plain `·` (FlowCalibrationPage axis title, SettingsAITab MCP status, ShotHistoryPage filter caption, WeatherItem provider credit) — confirm no other data-row candidates were missed (`grep -rn '·' qml/`)
- [x] 7.7 Check each converted line with `&`/`<` in user data (e.g. roaster "A&B") renders literally, and on a narrow (mobile-width) window

## 8. Verify

- [x] 8.1 Build via Qt Creator MCP (quick compile check); run `tst_settings`
- [ ] 8.2 Manual pass in the app: all six toggles change the sentence as labeled; saved `showGrind: false` config keeps grind hidden; yield arrow appears only on a dialed override; "Large Pitcher" preset renders without duplication; prompt wording matches the sound setting; no `ScaleDevice` TypeErrors in the launch log (clear-warnings rule)
- [x] 8.3 Update the PR with a comment summarizing the fixes against the review's numbered findings

## 9. Consolidate: fold steam plan into the Shot Plan widget (remove `plan`/`steamPlan` types)

- [x] 9.1 `ShotPlanItem.qml`: absorb PlanItem's page-aware logic behind new `showSteamPlan` (`shotPlanShowSteamPlan !== false`): steam context via `Theme.currentOperationMode`/`currentPageObjectName`/`MachineState.phase`, SteamPlanText instances in both modes, role/name switching (Button ↔ StaticText), press action gated off in steam mode
- [x] 9.2 Delete `PlanItem.qml` and `SteamPlanItem.qml`; remove both from `CMakeLists.txt`
- [x] 9.3 De-register `plan`/`steamPlan`: `LayoutItemDelegate.qml` cases, `LayoutEditorZone.qml` palette + chip map, `LayoutCenterZone.qml` isAutoSized (keep `shotPlan`), `shotserver_layout.cpp` widget list + chip labels + configurable list + plan-specific load/save/title/note branches
- [x] 9.4 `settings_network.cpp`: drop `"plan"` from `typeHasOptions`; `tst_settings.cpp`: update allowlist test (both removed types assert non-configurable)
- [x] 9.5 Editors: add the "Steam plan" toggle (`shotPlanShowSteamPlan`, default ON) to the Shot Plan settings in `ScreensaverEditorPopup.qml` and the web editor; remove the plan-only title/note special-casing
- [x] 9.6 Update specs (`plan-widgets`, `layout-widget-instance-config`) for the consolidated single-widget model
- [x] 9.7 Rebuild + rerun `tst_settings`; push and note the consolidation on the PR
