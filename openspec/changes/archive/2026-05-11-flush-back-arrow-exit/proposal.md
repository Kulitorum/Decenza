## Why

The Flush Water screen offers no way to exit early: once a flush starts, the user must wait for the configured duration to elapse and then sit through a fixed 1.5s "Flush Complete" overlay before navigation returns to the previous page. GitHub issue [#1115](https://github.com/Kulitorum/Decenza/issues/1115) calls this out — users with shorter information needs find the post-flush wait too long, and on non-headless machines there is no on-screen way to abort the flush at all. The existing BottomBar back arrow already appears on the page's settings view; extending it to the active-flush view gives users the familiar one-tap escape they already expect from other pages.

## What Changes

- Show the existing `BottomBar` on `FlushPage.qml` during an active flush (currently hidden via `visible: !isFlushing`). The bar's content collapses to back arrow + title only during flush — the seconds/flow value chips are hidden because the live timer above already shows the relevant state.
- Tapping back during an active flush stops the flush via `DE1Device.stopOperation()` and navigates back to the previous page (`goBack()` / `goToIdle()`).
- An explicit user-initiated exit suppresses the "Flush Complete" completion overlay: a `userExitedFlush` flag on `main.qml`'s root is set by the back handler and checked in the `Phase.Idle/Ready` handler before calling `showCompletion()`. The flag is cleared after the phase transition.
- The existing headless-only on-screen STOP button (`FlushPage.qml:198`) is left in place. Headless users get two equivalent exits; this is intentional.
- Out of scope: the issue's "(a) configurable delay" request is deliberately declined. Decenza prefers good defaults over user-tunable timing knobs, and the 1.5s overlay was already shortened from 3s based on prior user feedback. The skip-on-explicit-exit behavior in this change addresses the same impatience without adding a setting.
- Out of scope: the same hide-bottom-bar-during-operation pattern exists on `SteamPage` and `HotWaterPage`. This change does not touch them; if desired, a follow-up can extend the pattern symmetrically.

## Capabilities

### New Capabilities

- `flush-page-navigation`: User-facing navigation and exit behavior for the Flush Water page, including the always-visible back arrow, the explicit-exit completion-overlay suppression, and the relationship between the page's two views (settings vs. flushing).

### Modified Capabilities

<!-- None. The completion overlay logic in main.qml is touched, but no existing spec covers it. -->

## Impact

- **QML**: `qml/pages/FlushPage.qml` (BottomBar visibility + back handler), `qml/main.qml` (`userExitedFlush` root property + Phase.Idle/Ready guard around `showCompletion("flush")`).
- **BLE / device layer**: none. Exit reuses existing `DE1Device.stopOperation()`.
- **Settings**: none. No new setting is introduced.
- **Translations**: no new user-visible strings — the BottomBar's back arrow and existing flush page title are already translated.
- **Accessibility**: the back arrow inherits `BottomBar`'s existing accessibility wiring (focusable, Accessible.name, key handlers); no new accessibility surface to design.
- **Tests**: no new automated tests — the change is QML navigation behavior with no logic suitable for the Qt Test harness. Manual verification on simulator and a real DE1 (both headless and GHC-equipped) covers the change.
- **Closes**: [Kulitorum/Decenza#1115](https://github.com/Kulitorum/Decenza/issues/1115) — the (b) "exit option" portion. The (a) "configurable delay" portion is explicitly declined.
