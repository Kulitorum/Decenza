## 1. Request and confirmation

- [x] 1.1 Add `AppShell.quitRequested()`.
- [x] 1.2 Raise it from QuitItem (tap), SleepItem (long-press, both forms) and LayoutActions' `quit` arm.
- [x] 1.3 Handle it in `main.qml`. During a firmware flash, open the firmware-flash exit warning. Otherwise open "Quit Decenza?" with Cancel focused, a closed Tab loop and a FocusScope. Quit checks for a flash again, and the screensaver closes an unanswered dialog. It counts as an open dialog for the notice queue, which waits and drains when it closes.

## 2. Tests

- [x] 2.1 `tst_customwidgethtml::everyInAppQuitAsksFirst`: no QML or JS file under `qml/` except `main.qml` calls `Qt.quit()`, and the shell confirms the request.

## 3. Docs

- [ ] 3.1 Wiki manual, Sleep and Quit entries: "Quitting asks for confirmation."
