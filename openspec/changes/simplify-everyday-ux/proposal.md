## Why

Decenza can do more than any other DE1 controller. The cost shows up at the machine, where someone with wet hands wants a coffee. A code read of v2.0.8's daily path found:

- the tap that starts a shot looks the same as the tap that selects;
- a start the app refuses is silent;
- dialogs can open over a running shot;
- the review after every shot shows about 35 controls;
- the light theme's graphs are unreadable.

Users report the same in issues: #1547, #1609, #1610, #1668, #1793, #1799, #1972 and #1993.

It is also heavy.
- The v2.0.8 APK is **219 MB for a single ABI**. About 60–70 MB of it is libraries the app does not use or that serve features most users leave off: unused Quick Controls styles, Qt Widgets, FFmpeg, Tailscale, Quick3D, and QML debug plugins.
- Owners of older Decent tablets report lag and taps landing on the wrong thing (#1976), slow wake (#694) and freezes (#580).
- The newest crash report (#2030) dies inside the JS garbage collector on the main thread mid-shot.
- The code shows why: pages and dialogs are built synchronously all at once, analysis and compression run on the main thread at shot end, and the log writes to disk line by line on the calling thread.

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
  - Ask before any in-app quit, and gate the fake-shot gesture to debug or simulation builds.
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
- **Phase 5: lighter and smoother** (measure first, on the reference tablet)
  - Extend `PERFORMANCE_BASELINE.md` with cold start, page-open time and a main-thread lag probe, and record a baseline.
  - Trim the package: unused styles, Widgets, debug plugins, image formats, unreferenced splash images and rcc compression (about 25–30 MB, small changes). Then Tailscale, FFmpeg and Quick3D, which need maintainer calls (about 30–45 MB more).
  - Build hidden views and rare dialogs only when they are shown.
  - Hold the navigation guard until the new page has drawn.
  - Move shot-end analysis and compression off the main thread, and measure the log file writes before changing them.
  - Stop per-sample rebuilds (goal curves, custom widgets), start TTS lazily, and slow MemoryMonitor down.
  - Check the ART heap tuning against GC logs.

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
  - A package that carries only what it uses.
  - Optional features that cost nothing until switched on.
  - No avoidable main-thread work while the machine is running.

### Modified Capabilities
None in this change. A phase that is taken up adds its own deltas to the specs it touches. design.md lists them: `idle-default-layout`, `settings-ui`, `shot-page`, `recipe-activation`, `layout-machine-status-widget`, `ble-error-surfacing`, `profile-picker`, `charting`, `theme-font-size-defaults`.

## Impact

- **QML**: mostly `qml/main.qml`, `qml/pages/IdlePage.qml`, `qml/components/layout/items/*`, `qml/pages/PostShotReviewPage.qml`, `qml/components/ValueInput.qml`, `qml/pages/settings/*`, `qml/components/graphs/*` and `qml/Theme.qml`.
- **C++**: default layout and gesture tables (`src/core/settings_network.cpp`), default themes (`src/core/settings_theme.cpp`), and a toast signal for recipe deactivation (`src/controllers/maincontroller.cpp`). Phase 5 adds `src/main.cpp` (`QGuiApplication`), `src/history/shothistorystorage.cpp` (moving work to the DB thread), `src/network/webdebuglogger.cpp`, `src/core/accessibilitymanager.cpp`, `src/core/memorymonitor.cpp` and `src/core/translationmanager.cpp`. There are no BLE protocol, profile-engine or schema changes.
- **Packaging/CI**: `android/build.gradle` (packaging excludes), `CMakeLists.txt` and `cmake/tsnet.cmake`, `resources/resources.qrc`, `android/.../BleHelper.java`. A CI step lists the APK contents so that excluded libraries cannot silently return.
- **Docs**: wiki manual entries per phase, which should get shorter as gestures stop needing explanation.
- **Not in scope**:
  - renaming "bag" (#1993);
  - collapsing the multi-page recipe wizard (#1610);
  - removing any feature;
  - copying Meticulous's visual design.
