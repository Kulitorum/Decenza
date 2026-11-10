# Tasks

Builds and tests run through the Qt Creator MCP only; ask before each build or test run.

## 1. Storage and calculation

- [x] 1.1 Add `seedSteamPitcherRates(rate)` (fills only presets with no `secondsPerGram`) and the one-time start-up copy from `steam/steamSecondsPerGram`, gated on `migration/steamRatePerPitcher`; verify in `tst_settings` that an upgrade gives every preset the old rate, a preset with its own rate keeps it, a second launch copies nothing, and an unreadable preset list leaves the flag unset
- [x] 1.2 Add `trySetSteamPitcherSecondsPerGram(index, expectedName, rate)` (clamped 0–2.0) via `updateSteamPitcherPresetFields`; verify in `tst_settings` that it changes one preset only and refuses Heater off
- [x] 1.3 Drop the `secondsPerGram` parameter from `SteamCalculations::steamScalingBlocker` / `scaledSteamTime` / `effectiveSteamDuration` so they read the preset, and update the callers in `SettingsBrew`, `PreparationController`, `SteamServiceSource` and `SteamService`; verify `tst_preparationcontroller` and `tst_steamservice` cover two presets with different rates giving different times for the same milk
- [x] 1.4 Give `tryAddSteamPitcherPreset` an optional rate and add the one "rate for a new preset" helper (selected preset's rate, 0 for Heater off); verify in `tst_settings`
- [x] 1.5 Remove `steamSecondsPerGram` / `setSteamSecondsPerGram` / `calibrateSteamFromReference` from `SettingsBrew`; verify by a clean build with no remaining references
- [x] 1.6 Update the fallback log line in `PreparationController` to name the pitcher and its rate, following `docs/CLAUDE_MD/LOGGING.md`; verify the message in the existing log assertion in `tst_preparationcontroller`

## 2. Last pour remembers its pitcher

- [x] 2.1 Add `steam/lastSteamPitcherName` to `SettingsBrew` and write it with the milk/time pair at session end (check both writers: `preparationcontroller.cpp:636` and `maincontroller.cpp:2332`); verify in `tst_preparationcontroller` that a weighed steam records the selected pitcher's name

## 3. Editor contract and remote state

- [x] 3.1 Replace `set-steam-seconds-per-gram` with `set-preset-seconds-per-gram {index, expectedName, value}` in `SteamEditRequest` and `SteamPageEditor`; verify field validation in `tst_steameditrequest` and the mutation in `tst_steampageeditor`
- [x] 3.2 Make `calibrate-from-last-steam` set the rate of the pitcher named in the last-pour record, refusing when that name is not the selected preset's or is empty, and still turning weight timing on; verify each branch in `tst_steampageeditor`
- [x] 3.3 Have `CreatePreset` pass the new-preset rate from 1.4; verify in `tst_steampageeditor` that a preset added with "Small" selected gets Small's rate
- [x] 3.4 Add `secondsPerGram` to the `SteamPitcherSnapshot` field list; remove `brewSteamSecondsPerGram` from `MachineUiSnapshot` and `steamSecondsPerGram` from `steamservice.rep`; add `lastSteamPitcherName` to `SteamState`; follow `docs/CLAUDE_MD/SHARED_UI.md`; verify `tst_steampitchersnapshot`, `tst_machineuisnapshot` and `tst_steamservice` pass and that a rate edit changes the editor revision
- [x] 3.5 Read `SHARED_UI.md` for whether a Wasm client can run against a host of a different build, and record the answer in design.md's first risk; verify the design note is updated

## 4. Backup export/import

- [x] 4.1 Export `secondsPerGram` per preset and stop exporting the top-level `steamSecondsPerGram`; on import, pass a preset's own rate through and, for an old backup with only the top-level value, call `seedSteamPitcherRates` after the preset loop; verify in `tst_settings` a round trip of two different rates, and an old-format backup giving every imported preset the old global rate

## 5. Steam page

- [x] 5.1 In the Steam rate section of `qml/pages/SteamPage.qml`, bind the value and the "calibrated" checks to the selected preset's `secondsPerGram`, name the pitcher in the value line, and send `set-preset-seconds-per-gram`; delete `steam.rate.note`; verify the build's QML lint gate passes
- [x] 5.2 Enable "Use last steam" only when `lastSteamPitcherName` matches the selected preset; otherwise show which pitcher the last steam was with; reword the uncalibrated text and the button's accessible name for "this pitcher"; verify the build passes the settings-search index check (the indexed title is unchanged, so `SettingsSearchEntries.js` did not need regenerating)
- [ ] 5.3 Ask Jeff to start the app with two pitchers staged at different rates and confirm: the rate box follows the selected pitcher, editing one leaves the other, the idle "Weight-timed: Ng → Ns" preview differs between them, and "Use last steam" is offered only for the pitcher last steamed

## 6. MCP

- [x] 6.1 Report `secPerGramMilk` in `steam_pitcher` `list` and accept it on `add`/`update` (default on `add` from 1.4); bump `McpSurfaceVersion`; verify in `tst_mcptools_presets` and that `scripts/check_mcp_tool_budget.py` passes
- [x] 6.2 Rewrite the rate paragraph in `resources/ai/tools/steam_pitcher.md` and the `steam_pitcher` row in `docs/CLAUDE_MD/MCP_SERVER.md`; verify neither still says the rate is global

## 7. Manual and integration

- [x] 7.1 Update the steam section of the wiki manual: the rate belongs to the pitcher, make a pitcher per fluid or preference; keep it to a few sentences, and ask Jeff whether to push now or hold for the release
- [x] 7.2 Run the full suite (`mcp__qtcreator__run_tests`, scope `all`) and confirm it is green with the QML diagnostics gate passed

## Workflow follow-up

- Open a PR, run `/pr-review-toolkit:review-pr`, and read the `text-invariants` run before merging.
- Archive with `openspec archive add-per-pitcher-steam-rate --yes` as the PR's final commit.
