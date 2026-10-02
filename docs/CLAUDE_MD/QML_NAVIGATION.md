# QML Navigation System

How page navigation works, and the conventions every page must follow.

## A page never touches `pageStack`

`pageStack` is `main.qml`'s StackView. **No other file may reference it**, and none currently
does. A page or widget states what it wants through the `AppShell` singleton; `main.qml` decides
how to do it.

```qml
// In any page or widget:
AppShell.steamRequested()          // "take me to steam"
AppShell.backRequested()           // "go back"
AppShell.shotDetailRequested(id, ids)
AppShell.dismissRequested()        // "leave this page, however you can"
```

```qml
// In main.qml, the only place that owns navigation:
Connections {
    target: AppShell
    function onSteamRequested() { root.goToSteam() }
    ...
}
```

**Why**, because the reasons are not obvious and were each paid for:

- Pages used to reach `main.qml`'s root by name — `root.goBack()`, `pageStack.push(...)` — which
  resolves only because StackView creates each page with `main.qml`'s context. 120 such call sites
  were invisible to qmllint, qmlcachegen and the language server. `ScreensaverPage` compared its
  RSS ceiling against `undefined` for months that way.
- Every direct `pageStack.push()`/`pop()` **bypassed the navigation guard.** `startNavigation()`
  refuses re-entry while a transition is in flight; a page calling `pop()` itself skipped it, so a
  double-tap could pop twice where `main.qml`'s own back could not.
- There were two ways to reach the same screen: `main.qml` pushed a declared `Component`, widgets
  pushed `Qt.resolvedUrl("../../../pages/SteamPage.qml")` — a *different* component instance, and
  a path that was spelled four different ways plus once by string concatenation.

Adding a destination: declare a `Component` in `main.qml`, add one `goToX()` beside the others,
add a signal to `AppShell.qml` and one line to the `Connections` block. All four, or none.

## Operation pages are pushed, whoever started them

Espresso, steam, hot water, flush, descaling and transport go through `showOperationPage()`,
whether the user tapped a widget or the machine changed phase (group head, GHC, timer). Leaving
one — Stop, Back, completion, disconnect — is `leaveOperationPage()`: a pop back to wherever the
user was. Two exceptions, both in `showOperationPage()`: an operation started from another takes
its place, and the screensaver is replaced rather than returned to.

This replaced a rule that the machine's phase change cleared the stack with `replace(null, ...)`.
That needed a return-to-page side channel to get back to the shot review, rebuilt the home screen
on every Back (~150 ms on a Galaxy Tab A9+, #1976), and made a GHC-started steam different from
a tapped one for no reason the user could see.

A page that wants to leave emits `AppShell.dismissRequested()`. It must not inspect
`pageStack.depth`; that is the shell's business. Landing on the home screen deliberately is
`showHome()`, which pops to the existing instance instead of rebuilding it.

## Phase Change Handler Pattern

```qml
// In main.qml onPhaseChanged handler:
// 1. Check pageStack.busy ONLY for navigation calls, not completion handling
// 2. Navigation TO operation pages: check !pageStack.busy before showOperationPage()
// 3. Completion handling (Idle/Ready): NEVER skip - always show completion overlay
```

**Common bug**: an early `return` in `onPhaseChanged` skips completion handling. Only check
`pageStack.busy` before navigating, never at the top of the handler.

## Operation Page Structure

Each operation page (Steam, HotWater, Flush, Espresso) has:

- `objectName`: must be set — navigation detection matches on it (e.g. `objectName: "steamPage"`)
- `isOperating` property bound to `MachineState.phase === <phase>`
- **Live view** during the operation (timer, progress, stop button)
- **Settings view** when idle (presets, configuration)
- **Stop button** only on headless machines (`DE1Device.isHeadless`)

## Shell state lives on AppShell, not on main.qml's root

`stopReason`, `userExitedFlush`, `steamAutoFlushCountdown`, `debugLiveView`,
`scaleDialogDeferred`, `pendingBrewDialog`, `sessionMeasuredMilkG` and `currentPage` are
`AppShell` properties with exactly one owner. `main.qml` reads and writes them like any other
participant.

Do not mirror one onto a page. `sessionMeasuredMilkG` used to be reached through
`Window.window`, and `SteamPlanText` had grown a local mirror, a manual refresh on two events, an
`ignoreUnknownSignals` Connections and a one-time `console.warn` — all of it scaffolding to make
a rename greppable. A rename now fails the build instead.

## AppShell is not a junk drawer

Only the shell contract belongs there: navigation requests and state genuinely shared between
`main.qml` and pages. `cleanForSpeech()` went to `AccessibilityManager` because it is a
screen-reader text helper; the input method went to `Keyboard`. Putting everything global in one
object is the flat-`Settings` mistake the project already had to reverse (see `SETTINGS.md`).
