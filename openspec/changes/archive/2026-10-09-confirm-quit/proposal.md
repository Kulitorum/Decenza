## Why

Holding the Sleep widget quits the app. A wiped screen or a wet finger resting on Sleep is enough, and on Android the app runs immersive, so an accidental quit drops the tablet to its launcher with the machine still on.

Some users want a safety net; others rely on the one-gesture exit. So the confirmation is an opt-in option on the Sleep widget, and nothing changes for anyone who does not turn it on.

## What Changes

- The Sleep widget gains a per-instance `confirmQuit` option ("Ask before quitting"), default off, next to `allowQuit` in both layout editors. It only has an effect while `allowQuit` is on.
- With it on, Sleep's long-press raises `AppShell.quitRequested()`, and the shell answers with a single "Quit Decenza?" confirmation, Cancel focused. During a firmware flash it opens the existing firmware-flash exit warning instead.
- With it off (the default), long-press quits at once, as today. The Quit widget and the "Quit App" custom action are unchanged.
- Both Sleep forms (compact item and centre-zone tile) take their long-press from one helper, `LayoutActions.sleepLongPressAction()`.

## Capabilities

### New Capabilities
- `quit-confirmation`: an opt-in confirmation before Sleep's long-press quits.

### Modified Capabilities
- `layout-widget-instance-config`: the Sleep widget's `confirmQuit` option.

## Impact

- **QML:** `AppShell.qml` (signal), `LayoutActions.qml` (`sleepLongPressAction`, `quitConfirm` arm), `SleepItem.qml`, `SleepEditorPopup.qml` (new row; switches now align), `main.qml` (dialog and handler).
- **C++:** `SettingsNetwork::sleepOptionDefaults()` (`confirmQuit: false`); the web layout editor's Sleep options.
- **Tests:** `tst_customwidgethtml::compiledSleepTileFollowsItsOptions` covers the option and dispatches each long-press.
- **Docs:** one sentence in the wiki manual's Sleep entry.
