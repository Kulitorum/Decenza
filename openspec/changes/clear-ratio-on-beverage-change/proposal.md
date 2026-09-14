## Why

A ratio yield anchor survived every profile switch (add-yield-ratio-anchor Decision 8). Selecting a tea profile after dialing 1:2.5 on espresso stopped the steep at 2.5 × the espresso dose (48.8 g on-device) instead of running the tea profile's own frames, whose `target_weight` is 0 (no weight stop, matching de1app) — [#1941](https://github.com/Kulitorum/Decenza/issues/1941). The same holds for filter: a ratio fits one kind of drink, and an espresso 1:2 is not a filter ratio.

## What Changes

- A runtime profile load keeps a dialed ratio only when the new profile is in the same beverage group as the previous one: espresso (including an empty or unrecognised `beverage_type`), filter (`filter`, `pourover`), tea (`tea`, `tea_portafilter`). Changing group clears it. A cleaning, descale or calibrate profile neither uses nor clears any override and is not a group change; reloading the profile from before it keeps every override, temperature included.
- Replaying a shot that had no yield override clears the anchor the profile load re-armed. A restore skipped because the active recipe's row had not loaded yet runs when it arrives.
- When the load leaves no yield anchor, the recipe's saved yield applies, else the bean's (`MainController::restoreYieldAnchorAfterProfileLoad`), except on a maintenance profile. A recipe's gram yield equal to the profile's own target is not re-armed.
- A ratio dialed after the load applies normally. A gram target and the temperature override still clear on every switch.
- Picking a profile inside Brew Settings follows what the load decided, so OK cannot undo it.

## Capabilities

### Modified Capabilities
- `yield-anchor`: a ratio anchor survives a profile change only within a beverage group; a load left with no anchor re-arms the recipe's, else the bean's, saved yield.
- `brew-overrides`: the profile-switch clearing rule follows the beverage group.

## Impact

- `src/profile/profile.h` — `Profile::beverageGroup`.
- `src/core/settings_brew.{h,cpp}` — `clearProfileScopedBrewOverrides(bool keepRatioAnchor)`.
- `src/controllers/profilemanager.{h,cpp}` — the load reset compares beverage groups; `brewLoadGeneration`, `currentProfileBeverageGroup`.
- `src/controllers/maincontroller.{h,cpp}` — the restore after a load; `activeRecipeIdChanged` relayed to `brewBaselineChanged`.
- `src/core/brewbaseline.h` — `anchorToRestore`.
- `qml/components/BrewDialog.qml` — profile pick re-seeds from the session.
- Tests: `tst_profilemanager`, `tst_mcptools_write` (the `BrewBaseline` ladder).
