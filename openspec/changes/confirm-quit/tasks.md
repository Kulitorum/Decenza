## 1. Request and confirmation

- [x] 1.1 Add `AppShell.quitRequested()`.
- [x] 1.2 Raise it from QuitItem (tap), SleepItem (long-press, both forms) and LayoutActions' `quit` arm.
- [x] 1.3 Handle it in `main.qml`. During a firmware flash, open the firmware-flash exit warning. Otherwise open "Quit Decenza?" with Cancel focused, a closed Tab loop and a FocusScope.

## 2. Tests

- [x] 2.1 `tst_customwidgethtml::everyInAppQuitAsksFirst`: no component calls `Qt.quit()`, and the shell confirms the request.

## 3. Docs

- [ ] 3.1 Wiki manual, Sleep and Quit entries: "Quitting asks for confirmation."
