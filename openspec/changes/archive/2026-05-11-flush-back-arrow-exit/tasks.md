## 1. Root flag plumbing in main.qml

- [x] 1.1 Add `property bool userExitedFlush: false` on `main.qml`'s root (near other navigation state like `returnToPageName` / `returnToShotId`)
- [x] 1.2 In the Idle/Ready phase handler around `main.qml:2630`, wrap the `showCompletion(trFlushComplete.text, "flush")` call so that when `currentPage === "flushPage"` and `root.userExitedFlush` is true, the call is skipped
- [x] 1.3 Clear `root.userExitedFlush = false` immediately after the check in 1.2, only inside the `currentPage === "flushPage"` branch (to avoid consuming the flag during an unrelated page's Idle transition)
- [x] 1.4 Add a `console.log` line at the suppression site mirroring the surrounding diagnostic style (helps when verifying on-device)

## 2. FlushPage BottomBar visibility and chip hiding

- [x] 2.1 In `qml/pages/FlushPage.qml`, remove `visible: !isFlushing` from the BottomBar (line 594) so it is always visible
- [x] 2.2 Add `visible: !isFlushing` to each of the two value-chip `Text` elements (`secondsInput.value` chip at lines 606-610 and `flowInput.value` chip at lines 612-616) and to the divider `Rectangle` between them (line 611)
- [x] 2.3 Confirm the BottomBar's `title` binding (`getCurrentPresetName() || pageTitle`) still resolves correctly during a flush — `getCurrentPresetName()` reads from `Settings.brew.flushPresets`, which is unchanged during a flush, so no change needed

## 3. FlushPage back-arrow exit behavior

- [x] 3.1 Replace the single `onBackClicked` body with a branch on `isFlushing`:
  - If `isFlushing`: set `root.userExitedFlush = true`, call `DE1Device.stopOperation()`, then navigate (`root.goBack()` if `pageStack.depth > 1` else `root.goToIdle()`)
  - Else: existing behavior unchanged (`MainController.applyFlushSettings()` then navigate)
- [x] 3.2 Verify keyboard accessibility paths inherited from `BottomBar` (Enter / Space / Escape all route to `backClicked()`) work in both branches

## 4. Manual verification

- [ ] 4.1 Build and run in Qt Creator (Jeff drives the build, not Claude — see CLAUDE.md "Building")
- [ ] 4.2 Verify on simulator: start a flush, wait for natural completion, confirm "Flush Complete" overlay still shows for 1.5s and returns to idle
- [ ] 4.3 Verify on simulator: start a flush, tap back arrow mid-flush, confirm flush stops immediately and navigation returns to previous page with NO completion overlay
- [ ] 4.4 Verify on simulator: navigate FlushPage → some other operation page after a back-exit, confirm any subsequent natural Idle transition on that other page is unaffected (overlay shows normally if applicable)
- [ ] 4.5 Verify on a real DE1 (non-headless): on-page STOP button is correctly hidden during flush, back arrow is the sole on-screen exit, and exiting via back arrow does not leave the machine in a flushing state
- [ ] 4.6 Verify on a real DE1 (headless if available): both STOP button and back arrow are visible during flush, both exit cleanly, neither shows the completion overlay
- [ ] 4.7 Verify TalkBack/VoiceOver focus order includes the back arrow during flush (BottomBar already wires this)

## 5. Wrap-up

- [ ] 5.1 Open a PR referencing GitHub issue #1115 in the description; note that the configurable-delay portion (a) is explicitly declined and that the exit option (b) is delivered
- [ ] 5.2 Confirm release-notes line in the PR body mentions only the user-visible change (one-line back-arrow escape during flush) — no mention of internal flag mechanics
