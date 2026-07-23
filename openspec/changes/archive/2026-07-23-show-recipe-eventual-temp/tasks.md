## 1. Implementation

- [x] 1.1 In `qml/pages/RecipeWizardPage.qml`, inside the "Temp offset" `ColumnLayout` (~lines 3028–3061), add a read-only `Text` directly below the `ValueInput`, rendering `"→ " + ProfileManager.temperatureDisplayForSteps(fProfileStepTemps, fProfileTempC, Math.abs(fTempDeltaC) > 0.05, fProfileTempC + fTempDeltaC, fTempDeltaC)`.
- [x] 1.2 Add `void(Settings.app.temperatureUnit)` inside the `text` binding so the readout re-renders on a °C/°F switch (mirroring `BrewDialog.qml:954`).
- [x] 1.3 Gate it with `visible: wizardPage.fProfileTempC > 0`; colour `Theme.temperatureColor` when `Math.abs(wizardPage.fTempDeltaC) > 0.1` else `Theme.textSecondaryColor`; caption-sized italic to match BrewDialog's `tempSubtext`.
- [x] 1.4 Add accessibility: `Accessible.role: Accessible.StaticText` and `Accessible.name: text`.

## 2. Verification (manual — QML has no test harness)

- [ ] 2.1 Offset 0 on a single-temp profile shows `→ <profile temp>` with no tag.
- [ ] 2.2 Offset +N and −N show the shifted temperature with a signed tag (e.g. `→ 96°C +2°`).
- [ ] 2.3 A multi-temperature (ramp) profile collapses to `→ <first>…<last>`, matching brew settings for the same profile.
- [ ] 2.4 Switching °C ↔ °F re-renders the readout in the new unit.
- [ ] 2.5 An uninstalled/unresolvable profile hides the readout (and the stepper is disabled).
- [x] 2.6 `fProfileStepTemps` population confirmed on all entry paths — fresh-create (`selectProfile`), edit-existing/clone/promote (all via `applyRecipeMap` → `refreshProfileTemp`, which derives per-frame temps from the resolved profile). No empty-while-anchored state exists; per-frame form renders everywhere.
- [ ] 2.7 Confirm the tea and hot-water-tea paths are unchanged (no readout added there).

## 3. Documentation

- [ ] 3.1 Update the Decenza wiki `Manual` recipe-wizard section to describe the resulting-temperature readout beneath the Temp offset control (hold the push per the project's wiki-release convention unless told otherwise).

## 4. Pre-PR

- [x] 4.1 Run the full test suite via the Qt Creator MCP (`mcp__qtcreator__run_tests`, scope `all`) — no code paths change in C++, but confirm nothing regresses to build.
- [ ] 4.2 Open a PR for review (do not push to `main`); run the automated `/pr-review-toolkit:review-pr` before merge.
