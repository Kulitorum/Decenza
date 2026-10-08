## Why

Decenza can do more than any other DE1 controller. The cost shows up at the machine, where someone with wet hands wants a coffee. A code read of v2.0.8's daily path found:

- the tap that starts a shot looks the same as the tap that selects;
- a start the app refuses is silent;
- dialogs can open over a running shot;
- the review after every shot shows about 35 controls;
- the light theme's graphs are unreadable.

Users report the same in issues: #1547, #1609, #1610, #1668, #1793, #1799, #1972 and #1993.

Meticulous's machine screen is a useful reference because it is deliberately narrow ([usage manual](https://meticuloushome.com/pages/usage-manual)). Its everyday surface has:

- one profile list and one explicit start gesture ("Hold to start");
- a live view of one pressure needle, four numbers and the current stage name;
- an end screen with the result and the next thing to do;
- error messages with a single action ("Tap to retry");
- a short menu with Advanced settings at the bottom.

Profile building and setup happen elsewhere.

Decenza does not need Meticulous's hardware or fewer features. It needs the same split: **the everyday path simple and obvious, everything else one deliberate step away.**

## What Changes

This change is a proposal and a plan; it changes no behaviour itself. Each phase lands as its own PRs, and each item is small enough to review alone.

- **Phase 1: make the everyday path safe and legible** (small, independent fixes)
  - Queue notices while an operation is active, not only while the screensaver is.
  - Give scale-missing dialogs real actions, and fix the "Shot Stopped" dialog's wrong "Settings → Bluetooth" path.
  - Show a start affordance on the selected pill, and say why a blocked start did nothing.
  - Remove the default double-tap that delays every home-tile tap.
  - Stop the default Sleep long-press from quitting the app, and gate the fake-shot gesture to debug builds.
  - Toast when a recipe is deactivated implicitly.
  - Fix the light theme's graph and button contrast.
  - Use error codes, not English substrings, for BLE permission errors.
  - Confirm or undo one-tap resets.
- **Phase 2: the daily loop**
  - A "quick rate" card at the top of the post-shot review, with everything else under Details.
  - Faster value entry: a visible keypad, hold acceleration, sensible step sizes and 44+ px buttons.
  - A first-run empty state on the Recipes tile.
  - A Stop button that says Stop.
  - Bottom-bar popups that stay open after selecting.
  - Machine status as words with a target ("Heating 88→93 °C"), tappable to reconnect.
- **Phase 3: structure** (needs maintainer decisions, see design.md)
  - One word per concept: Favorites vs Auto-Favorites, and "recipe" vs the profile editor.
  - Show where each brew value comes from (recipe / bag / profile).
  - Opening a profile to edit it no longer changes the machine, and the editor type is explicit.
  - Settings regrouped by task, with a "Show advanced settings" switch.
  - Settings search covering every setting.
  - A short skippable onboarding: language and units from the device locale, machine, scale, first drink.
- **Phase 4: visual system**
  - A hit-area floor that survives scaling.
  - Font sizes mapped onto Theme roles.
  - A control-border token that meets 3:1.
  - Phase-label packing on shot graphs.
  - Quality issues shown as text badges instead of colour-only dots.
  - Shot-history rows that open on tap.

## Capabilities

### New Capabilities
- `everyday-ux-principles`: testable rules for everyday surfaces.
  - Nothing modal over a running operation.
  - A visible start, and a blocked start that explains itself.
  - Every problem message has a way forward.
  - Implicit state changes are announced.
  - Destructive actions are deliberate.
  - One word, one meaning.
  - A hit-area floor.
  - Contrast in every shipped theme, and no colour-only state.

### Modified Capabilities
None in this change. A phase that is taken up adds its own deltas to the specs it touches. design.md lists them: `idle-default-layout`, `post-shot-review-layout`, `settings-ui`, `shot-page`, `recipe-activation`, `layout-machine-status-widget`, `ble-error-surfacing`, `profile-picker`, `charting`, `theme-font-size-defaults`.

## Impact

- **QML**: mostly `qml/main.qml`, `qml/pages/IdlePage.qml`, `qml/components/layout/items/*`, `qml/pages/PostShotReviewPage.qml`, `qml/components/ValueInput.qml`, `qml/pages/settings/*`, `qml/components/graphs/*` and `qml/Theme.qml`.
- **C++**: default layout and gesture tables (`src/core/settings_network.cpp`), default themes (`src/core/settings_theme.cpp`), and a toast signal for recipe deactivation (`src/controllers/maincontroller.cpp`). There are no BLE, profile-engine or database changes.
- **Docs**: wiki manual entries per phase, which should get shorter as gestures stop needing explanation.
- **Not in scope**:
  - renaming "bag" (#1993);
  - collapsing the multi-page recipe wizard (#1610);
  - removing any feature;
  - copying Meticulous's visual design.
