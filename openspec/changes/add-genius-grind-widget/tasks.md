# Tasks

## 1. Starting shot in the calibration, and the reported source

- [ ] 1.1 Branch from `main` (not `try/genius-grind-mark`), and confirm with `git log` that the branch carries no fork commit yet
- [ ] 1.2 In `buildGrinderCalibrationBlock`: collect rated candidates before the badge and dialed-in filters, mark current-batch and earlier-bag membership with the existing lambdas, choose the starting shot (75+ → 50+ → latest good shot → earlier bag of an undated coffee; newest on a tie), and give each profile its own setting (75+ → 50+ → latest good shot → top-rated 50+ at the same UGS → earlier bag) before deriving from the starting shot; shots rated 50+ skip badges, the 15 g floor and weight but keep the beverage filter; shots rated 1–49 never count, not even as the latest good shot; verify with new `tst_dialing_blocks` functions for each `grinder-ugs-calibration` scenario, and confirm the first goes red with the rated step removed
- [ ] 1.2a Publish the history key with 10+ mined pairs whatever their spread (`kCalibMinUngatedPairs`); verify with the two new scenarios (58 failing pairs publish 0.125; 5 failing pairs publish nothing) and that the existing gate tests still pass
- [ ] 1.3 Emit the starting shot in the block (`coffeeAnchor.timestamp`, `enjoyment0to100`, `profiles[].source: "rated"`); verify with the payload delta's new scenario in `tst_dialing_blocks`, and that `check_mcp_tool_budget.py` passes without a `McpSurfaceVersion` bump
- [ ] 1.3a Report `ugsDelta` with `notCalibrated` and `outsideRange`, and the `noBean` / `noStart` reasons; verify with the "Uncalibrated move reported in UGS" scenario test
- [ ] 1.3b Report the reading not chosen (setting, source, rated shot, days between) when the rated and latest-good-shot readings differ; verify with the "alternative is reported" scenario test
- [ ] 1.4 Carry the starting shot and each profile's own setting on `GrindCalibrationStatus`; `grindSuggestionFor` returns the own setting with or without a key, else the derived one (`source`, `start*` incl. `startEarlierBag`, `reason`); verify with tests for the own-setting-without-key scenario, both prototype-coverage scenarios, and the `notCalibrated` and `outsideRange` reasons
- [ ] 1.5 Move the recipe wizard to the `start*` fields (no aliases) and add the starting shot's rating to its text (`RecipeWizardPage.qml`, new translation key); verify `git grep anchorProfileName` finds no suggestion reader, and open the wizard's grind step on a bag with a shot rated 75+
- [ ] 1.6 Delete the dead `AIManager` grind block: `requestGrindCalibration`, `requestMarkGrindAnchor`, `requestClearGrindAnchor`, `requestGrindSuggestion`, their five signals, and the `main.cpp` `connect` forwarding `grindCalibrationChanged`; verify `git grep` finds no reference and the build is green

## 2. Widget and popup

- [ ] 2.1 Take Mark's `GeniusGrindItem.qml` and `GeniusGrindDialog.qml` from `fredphoesh/Decenza-Descendant` at `97daea2e3`, drop their reference-shot, `requestGeniusSuggestion` and `*` parts, and note in the PR body that the squash commit must carry `Co-authored-by: fredphoesh <fredphoesh@users.noreply.github.com>`; verify `git grep` finds none of `genius_bean_references`, `markBeanReferenceShot`, `requestGeniusSuggestion`
- [ ] 2.2 Point both files at `EquipmentService.requestGrindSuggestion` / `grindSuggestionReady`, filtered by the echoed bag and profile, and refresh on profile, bag, equipment, `grindCalibrationChanged` and a changed `HistoryService.historyRevision` value; verify that switching bean and profile in the popup changes the number, and that rating a shot 85 on the post-shot page changes the widget with nothing else touched
- [ ] 2.3 Write the popup's source line from `source` and the start fields (profile, setting, local date and time, rating, and "earlier bag" / "same UGS" when so), and its no-number line with the UGS move; make it open the shot via `AppShell.shotDetailRequested` after closing the popup; map each `reason` to its message. Verify each message on screen with the matching data, and that the line opens the right shot
- [ ] 2.3a Show the alternative with its note when the two readings are at least one grind step apart, preselecting the suggestion; verify on screen with a bag whose old rated shot and recent shots disagree, and that a bag where they agree shows one number
- [ ] 2.4 Add `startRpm` to Apply's grind write when present; verify Apply on a variable-RPM package sets both grind and RPM, and that a failed bag activation reports and stops
- [ ] 2.5 Register the widget: `CMakeLists.txt`, `src/webclient/CMakeLists.txt`, `LayoutItemDelegate.qml`, `widgetCatalogTable()`, and the QML inventory in `openspec/changes/archive/2026-10-27-replace-web-interface-with-wasm/design.md`; verify a green Qt Creator build (QML gate passes) and that `tst_qmlinventory` and the layout catalog tests pass
- [ ] 2.6 Open the Equipment page's UGS guide button and confirm https://videoblurb.com/UGS/ loads (held from update-ugs-chart task 1.5). Check the widget in a bar and in the centre zone, natively and in the host-served browser client, with TalkBack/VoiceOver names on the tile, the bean/profile choices and Apply/Cancel
- [ ] 2.7 Wiki manual: a short "Genius Grind" entry (what it shows, that it starts from your best-rated shot of the bag, else your latest good shot, that shots rated under 50 are skipped, and what Apply changes); verify the page renders

## 3. Integration

- [ ] 3.0 Re-run the coverage replay (`audit3/replay3.py`, both event modes) against the implemented C++ on Mark's three packages and Jeff's tablet, and confirm 0 prototype-only cases other than those the user's rules exclude (non-coffee drinks, shots rated under 50, unnamed coffee); record the table in the PR
- [ ] 3.1 Run the full suite through `mcp__qtcreator__run_tests` (scope all) and confirm it passes

## Workflow follow-up

- Open the PR referencing Kulitorum/Decenza#2211. It credits Mark, lists what was taken from his fork and what was not (with the reasons), and gives the per-grinder figures measured on his database for each rule considered.
- Squash-merge with Mark's `Co-authored-by` trailer.
- Before merge: clean `audit-code` pass on the final commit, Codex threads resolved, then `openspec archive add-genius-grind-widget --yes` as the last commit.
