## 1. Implementation

- [x] 1.1 `Profile::beverageGroup` (espresso / filter / tea / maintenance; trimmed, case-insensitive; empty or unknown = espresso).
- [x] 1.2 `SettingsBrew::clearProfileScopedBrewOverrides(bool keepRatioAnchor)`.
- [x] 1.3 `ProfileManager::resetBrewOverridesForLoadedProfile` keeps a ratio only within the previous profile's group, logs the clear at INFO, and bumps `brewLoadGeneration`.
- [x] 1.4 `MainController::restoreYieldAnchorAfterProfileLoad`: after a load that left no anchor, arm the recipe's, else the bean's, saved yield (`BrewBaseline::anchorToRestore`), not on maintenance profiles; skipped during recipe activation, which seeds the brew itself.
- [x] 1.5 Brew Settings: picking a profile in the dialog keeps a dialed ratio within its group and otherwise re-seeds from the session.
- [x] 1.6 Maintenance profiles clear nothing and keep the drink's group; `targetWeight()` and the upload ignore the overrides on them.
- [x] 1.7 Shot replay clears the yield anchor when the shot had no override.
- [x] 1.8 A restore skipped before the recipe row loads runs when `recipeReady` delivers it.
- [x] 1.9 Reloading the drink profile from before a maintenance run keeps every override, temperature included. Live: D-Flow/Q at 1:2.5 and 90 °C, Cleaning/Forward flush brewed at its own 85 °C and 36 g, back on D-Flow/Q at 90 °C and 45 g.

## 2. Tests

- [x] 2.1 `profileSwitchKeepsRatioClearsAbsolute`: espresso → tea clears, a ratio dialed on tea carries to another tea profile, tea → pourover clears.
- [x] 2.2 `Profile::beverageGroup` normalization and grouping.
- [x] 2.3 `BrewBaseline::anchorToRestore` cases in `tst_mcptools_write::brewBaselineLadderPicksOneRung`.
- [x] 2.4 Break the fix and watch the new assertions go red.
- [x] 2.5 Full suite green via Qt Creator.
- [x] 2.6 Live check over the `decenza` MCP: espresso → pourover cleared a dialed 1:2.5; 1:3 dialed on filter cleared on the way back to espresso and the bean's 1:2 was restored (36 g); INFO lines logged for both.

## 3. Docs

- [x] 3.1 `docs/CLAUDE_MD/RECIPES.md` yield paragraph.
- [x] 3.2 Wiki manual and FAQ: a ratio survives a switch to a profile of the same kind.
- [ ] 3.3 After archive, update the `yield-anchor` Purpose line ("a ratio survives a profile change") to "survives a change within a beverage group"; a delta cannot change a Purpose.
