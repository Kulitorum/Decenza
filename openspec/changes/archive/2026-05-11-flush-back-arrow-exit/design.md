## Context

The Flush Water page (`qml/pages/FlushPage.qml`) has two visual states driven by `isFlushing`:

1. **Settings view** (`!isFlushing`): preset pills, duration, flow inputs, and a bottom navigation bar with a back arrow + value chips.
2. **Flushing view** (`isFlushing`): live preset switcher, timer + progress bar, and (only on headless machines) an on-screen STOP button.

The settings view's `BottomBar` is gated by `visible: !isFlushing`, so during a flush the page has no on-screen navigation chrome. The flush ends one of two ways:

- The DE1 firmware reaches the configured flush duration → phase transitions to Idle/Ready → `main.qml:2630` calls `showCompletion(trFlushComplete.text, "flush")` → a full-screen completion overlay (`completionOverlay`, z:500) is shown for 1500ms before navigating to idle.
- A headless user taps the on-page STOP button → `DE1Device.stopOperation()` + `root.goToIdle()` → triggers the same Idle/Ready handler. To make STOP genuinely equivalent to the new back-arrow exit, this change also sets `root.userExitedFlush = true` in the STOP handlers so both paths suppress the 1.5s overlay identically.

GitHub issue #1115 reports both that the 1.5s overlay feels long for users who don't need it, and that there's no way to abort early on machines without a GHC button. The completion overlay code path is shared with steam and hot water completions (`main.qml:2626-2631`), so any change there has cross-page implications.

## Goals / Non-Goals

**Goals:**

- Provide a familiar on-screen escape during active flush, using the same back-arrow affordance the user already sees on every other page.
- Make the exit *immediate* when the user explicitly invokes it: stop the flush and bypass the 1.5s "Flush Complete" overlay.
- Reuse existing components and signal flow — no new BLE commands, no new settings, no new completion-overlay variants.

**Non-Goals:**

- Adding a user-configurable overlay duration setting (explicitly declined per Jeff's preference for good defaults over knobs).
- Changing the completion overlay's behavior for *natural* flush completion (timer-driven), or for steam / hot water completion. Those paths are untouched.
- Removing or repositioning the existing headless-only on-page STOP button. It stays; both the STOP button and the new back arrow set `userExitedFlush` so headless users get two equivalent exits.
- Extending the same back-arrow pattern to `SteamPage` and `HotWaterPage`. The pattern exists there too but is out of scope for this change.

## Decisions

### Decision: Show the existing BottomBar during flush, hide its value chips

Drop the `visible: !isFlushing` gate so the bar is rendered in both views. Inside the bar, hide the two `Text` chips that display `secondsInput.value` and `flowInput.value` (`FlushPage.qml:606-616`) when `isFlushing` is true.

**Why:** The live timer above already shows the relevant state during a flush (elapsed/target seconds, progress bar). Repeating preset config in the bottom bar would be visual noise without new information. Hiding the chips is a one-attribute change (`visible: !isFlushing` on each chip) and keeps the bar consistent in height/layout.

**Alternative considered:** A separate "flush mode" BottomBar component. Rejected — overkill for two `Text` items and a divider.

### Decision: Suppress completion overlay via a root flag, not a parameter to `showCompletion()`

Add a `userExitedFlush: false` property on `main.qml`'s root. The FlushPage's back handler sets it `true` before calling `DE1Device.stopOperation()` and navigating. The existing `Phase.Idle/Ready` handler in `main.qml` checks the flag before calling `showCompletion("flush")`; if set, it skips the call and clears the flag.

**Why:** The phase change is what triggers `showCompletion()`, and that happens asynchronously after `stopOperation()`. The back handler doesn't have a synchronous hook to suppress the overlay at call time. A root-level flag is the simplest event-based mechanism — set by the user's action, consumed by the next Idle transition.

This matches the project rule "no timers as guards" (CLAUDE.md): the flag is event-cleared (by the Idle transition), not time-cleared.

**Alternative considered:** Pass a parameter through `showCompletion(suppress=true)`. Rejected — the caller (`Phase.Idle/Ready` handler) wouldn't know the suppression intent without consulting a flag anyway.

**Alternative considered:** Use the existing `returnToPageName` / `returnToShotId` mechanism. Rejected — that system handles *where* to return after the overlay, not whether to skip it.

### Decision: Flag is single-shot and unconditionally cleared on the next Idle/Ready transition

The flag is cleared after the per-page completion decision runs, **regardless of which page is current**. Why unconditional: `root.goBack()` / `root.goToIdle()` from the back handler mutate `currentPage` synchronously, before the BLE-driven phase change arrives, so by the time the Idle/Ready handler runs `currentPage` is typically no longer `"flushPage"`. If we only cleared inside the flushPage branch, the flag would strand and silently suppress the next legitimate flush completion. Clearing unconditionally at the end of the Idle/Ready handler guarantees:

- A natural flush completion right after a user-initiated exit (racey but possible) still shows its overlay normally on a subsequent flush.
- A stale flag from a prior exit cannot survive past the very next Idle/Ready transition — and that transition is exactly the one caused by `stopOperation()` from the back handler, so the lifetime is one phase event.

The flag is only ever *consumed for suppression* inside the flushPage branch, so unconditional clearing is safe — it cannot accidentally suppress a steam or hot-water completion.

### Decision: Back handler stops the operation, then navigates

```
onBackClicked: {
    if (isFlushing) {
        root.userExitedFlush = true
        DE1Device.stopOperation()
        if (pageStack.depth > 1) root.goBack()
        else root.goToIdle()
    } else {
        MainController.applyFlushSettings()
        if (pageStack.depth > 1) root.goBack()
        else root.goToIdle()
    }
}
```

The order (set flag → stop → navigate) matters: the flag must be set before the phase transition fires, and navigation must follow so the user perceives an instant response. Navigation does not block on the phase change; the page transitions immediately while `stopOperation()` propagates to the device.

**Why split the handler this way:** the settings-view path needs `applyFlushSettings()` to commit any in-progress preset edits; the flush-view path does not (the running flush has already received its settings). Keeping the two branches explicit avoids accidentally calling `applyFlushSettings()` mid-flush, which could re-send a BLE write the device is mid-acting on.

## Risks / Trade-offs

- **[Risk]** Race: user taps back at the exact moment the firmware-driven flush completes on its own. → Both paths converge on `Phase.Idle/Ready`; the flag suppresses the overlay either way. Acceptable outcome — the user wanted to leave, and they leave.
- **[Risk]** Race: user taps back, then before the next phase transition, the user navigates to another operation page (e.g., espresso). → The flag is only ever *checked* inside the flushPage branch of the Idle/Ready handler, so it cannot suppress steam/hot-water completion overlays. The flag is also cleared unconditionally at the end of every Idle/Ready handler invocation, so it cannot survive past one phase event.
- **[Risk]** Hidden value chips during flush could be missed by users who relied on the bottom bar to see preset config. → The full settings view re-appears the moment the flush ends, and the preset name remains visible in the bar's title. Low impact.
- **[Trade-off]** Two ways to exit on headless machines (STOP button + back arrow). → Intentional. Removing the STOP button would be a behavioral regression for headless users who learned that gesture; back arrow is additive.
- **[Trade-off]** Skipping the completion overlay means the user loses the "12.4s" final-time confirmation on explicit exit. → Acceptable: the timer was visible up to the moment of exit, and the user explicitly chose to leave. Natural completions still show it.
