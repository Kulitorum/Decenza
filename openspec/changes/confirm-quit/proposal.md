## Why

Holding the Sleep widget quits the app, and so does a single tap on the Quit widget or on a custom "Quit App" action. Wiping the screen or resting a wet finger on Sleep is enough. On Android the app runs immersive, so an accidental quit drops the tablet to its launcher with the machine still on.

Turning long-press-to-quit off by default is not the fix. No built-in layout has a Quit widget, so for those users Sleep's long-press is the only in-app exit.

## What Changes

- Every in-app quit raises `AppShell.quitRequested()`. This covers the Quit widget, Sleep's long-press, and the `command:quit` layout action that custom widgets and the compiled centre-zone tiles use.
- The shell answers with a single "Quit Decenza?" confirmation. Cancel is the default focus.
- While a firmware flash is running, the request opens the existing firmware-flash exit warning instead.
- Nothing else changes: `allowQuit` and its default stay as they are, and the window's own close path is untouched.

## Capabilities

### New Capabilities
- `quit-confirmation`: in-app quits ask once.

### Modified Capabilities
- `layout-widget-instance-config`: with `allowQuit` on, Sleep's long-press offers to quit rather than quitting at once.

## Impact

- **QML:**
  - `qml/AppShell.qml`: the new signal.
  - `QuitItem.qml`, `SleepItem.qml`, `LayoutActions.qml`: now raise `quitRequested()`.
  - `qml/main.qml`: the confirmation dialog and its handler.
- **Tests:** `tst_customwidgethtml::everyInAppQuitAsksFirst`.
- **Docs:** one sentence in the wiki manual's Sleep and Quit entries.
