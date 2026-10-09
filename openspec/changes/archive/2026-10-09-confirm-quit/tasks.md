## 1. Option and confirmation

- [x] 1.1 Add `confirmQuit` (default false) to `SettingsNetwork::sleepOptionDefaults()`.
- [x] 1.2 `LayoutActions.sleepLongPressAction()` returns `""`, `command:quit` or `command:quitConfirm`; the compiled tile and the compact SleepItem both use it. `quitConfirm` raises `AppShell.quitRequested()`.
- [x] 1.3 Handle it in `main.qml`. During a firmware flash, open the firmware-flash exit warning. Otherwise open "Quit Decenza?" with Cancel focused, a closed Tab loop and a FocusScope. The screensaver closes an unanswered dialog, and it counts as an open dialog for the notice queue, which drains when it closes.
- [x] 1.4 "Ask before quitting" row in the in-app Sleep editor (disabled while long-press-to-quit is off) and in the web editor.

## 2. Tests

- [x] 2.1 `tst_customwidgethtml::compiledSleepTileFollowsItsOptions`: each option combination yields the expected long-press, and dispatching it reaches `Qt.quit` or `AppShell.quitRequested`.

## 3. Docs

- [x] 3.1 Wiki manual, Sleep widget row: long-press quits; its options can turn that off or make it ask first.
