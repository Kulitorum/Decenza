# QML Navigation System

How page navigation works in `main.qml` and the conventions every operation page (Steam, HotWater, Flush, Espresso) must follow.

## Page Navigation (main.qml)

- **StackView**: `pageStack` manages page navigation
- **Auto-navigation**: `MachineState.phaseChanged` signal triggers automatic page transitions
- **Operation pages**: SteamPage, HotWaterPage, FlushPage, EspressoPage
- **Completion flow**: When operations end, show 1.5-second completion overlay, then navigate to IdlePage

## Phase Change Handler Pattern

```qml
// In main.qml onPhaseChanged handler:
// 1. Check pageStack.busy ONLY for navigation calls, not completion handling
// 2. Navigation TO operation pages: check !pageStack.busy before replace()
// 3. Completion handling (Idle/Ready): NEVER skip - always show completion overlay
```

## Operation Page Structure

Each operation page (Steam, HotWater, Flush) has:

- `objectName`: Must be set for navigation detection (e.g., `objectName: "steamPage"`)
- `isOperating` property: Binds to `MachineState.phase === <phase>`
- **Live view**: Shown during operation (timer, progress, stop button)
- **Settings view**: Shown when idle (presets, configuration)
- **Stop button**: Only visible on headless machines (`DE1Device.isHeadless`)

## Common Bug Pattern

**Problem**: Early `return` in `onPhaseChanged` can skip completion handling.

**Solution**: Only check `pageStack.busy` before `replace()` calls, not at handler start.
