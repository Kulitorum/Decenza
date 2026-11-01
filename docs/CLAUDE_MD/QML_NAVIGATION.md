# QML Navigation System

How page navigation works, and the conventions every page must follow.

## A page never touches `pageStack`

`SharedAppNavigation` owns each client's StackView. Only the application roots and that shared
shell may manipulate it. A page or widget states what it wants through the `AppShell` singleton;
the shared shell decides how to enter the destination. Native device/window lifecycle and browser
history remain in their respective roots. Each client keeps its own stack and drafts.

```qml
// In any page or widget:
AppShell.steamRequested()          // "take me to steam"
AppShell.backRequested()           // "go back"
AppShell.shotDetailRequested(id, ids)
AppShell.dismissRequested()        // "leave this page, however you can"
```

```qml
// In SharedAppNavigation.qml:
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

Adding a destination: declare a `Component` in `SharedAppNavigation.qml`, add one `goToX()` beside the others,
add a signal to `AppShell.qml` and one line to the `Connections` block. All four, or none.

## Operation pages are pushed, whoever started them

Espresso, steam, hot water, flush, descaling and transport go through `showOperationPage()`,
whether the user tapped a widget or the machine changed phase (group head, GHC, timer). Leaving
one is a pop back to wherever the user was: Stop and Back emit `AppShell.dismissRequested()`;
completion, disconnect, the shot-end timer and Space call `leaveOperationPage()`.

Which page a phase gets is `OperationNavigationPolicy.machineOperationPage(phase, phases, AppShell.drinkInCupView)`: an americano or long black with the cup view keeps HotWater on the Espresso page. Pass the third argument at every call site.

An operation started from another takes its place, the post-shot review replaces the shot page
and any review it was started from, and an operation the machine starts while the screensaver is
up replaces it rather than returning to it. The screensaver itself is pushed over the home screen,
so waking is a pop.
Pushing over a page closes its open popups (`PopupCloser`): Qt only closes a popup when its
parent is destroyed, not when it is covered.

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

`stopReason`, `userExitedFlush`, `debugLiveView`,
`scaleDialogDeferred`, `pendingBrewDialog` and `currentPage` are
`AppShell` properties with exactly one owner per client. The roots read and write them like any
other participant; the shared stack publishes `currentPage`.

Pages use `PreparationService.state` in native and Wasm; its source delegates to
the host's `MainController.preparation`.
Its espresso/steam/hotwater/flush mode is independent of page navigation. The native
`PreparationController` alone captures bean/milk weights and stores the measured
milk/time pair; pages observe its typed state and send explicit requests. Changing
mode updates preparation controls without leaving an editor or history view.

Ordinary destination decisions live in `OperationNavigationPolicy.js`. Reopening
the current Recipes, Beans, Equipment, Favorites or Community page retains that
instance. A Settings request without a valid explicit tab retains the current tab;
a History request with no filter retains the current search, while `{}` clears it.
Shell callbacks own the actual stack mutation and browser history entry.

## AppShell is not a junk drawer

Only the shell contract belongs there: navigation requests and state genuinely shared between
`main.qml` and pages. `cleanForSpeech()` went to `AccessibilityManager` because it is a
screen-reader text helper; the input method went to `Keyboard`. Putting everything global in one
object is the flat-`Settings` mistake the project already had to reverse (see `SETTINGS.md`).

## Browser application lifetime

Shared widgets request immediate application exit with `AppShell.immediateQuitRequested()`
or confirmation with `AppShell.quitRequested()`. The native shell handles both.
The Wasm shell ignores both, and its entry point disconnects QML quit/exit from
the application. Browser Quit actions must not stop the host or close the interface.


## Steam auto-flush belongs to the host

`MainController.steamAutoFlushCountdown` is read-only presentation state. The native
`SteamAutoFlushController` starts it on entering connected Steam/Puffing while the
machine phase is Steaming, using the configured duration at that moment. Leaving
that state or disconnecting cancels it. Expiry rechecks the machine before releasing
steam permission and requesting Idle. Shared SteamPage observes the countdown;
page navigation and browser lifetime do not own timers or stop commands.
