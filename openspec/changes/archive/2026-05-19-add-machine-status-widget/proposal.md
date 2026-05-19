## Why

The only way to know the DE1's state (asleep, heating, ready) and current temperature is to unlock the device, find Decenza, open it, and wait for the UI. A Home Screen widget makes machine status glanceable and gives a one-tap path into the app. The app already maintains a backgrounded BLE connection (Android foreground BLE service; iOS `bluetooth-central` background mode), so a widget can show live status without the app being in the foreground.

## What Changes

- Add a Home Screen / lock-equivalent **status widget** on iOS (WidgetKit) and Android (AppWidget) that displays:
  - **Machine phase** as a human-readable label (Sleep, Idle, Heating, Ready, Espresso, Steam, Hot Water, Flush, …).
  - **Temperature**, rendered relative to target — `Heating 84 → 93 °C` while heating, a `Ready` indicator at temperature, steam temp while steaming.
  - **Last-shot summary** — most recent shot's yield, time, and quality badge.
  - **Connection / staleness state** — when BLE is down or the app process was reclaimed, the widget shows `Disconnected — Tap to open` or an `updated N min ago` marker instead of presenting stale data as live. This is the correctness mechanism, not optional polish.
- **Tap anywhere on the widget opens the Decenza app** (plain launch to the app's normal entry; no deep-link routing, no wake action).
- Add a small **machine-status snapshot writer** in the Qt/C++ app that publishes the above fields to platform-shared storage whenever machine phase, temperature, or shot history changes:
  - iOS: shared `UserDefaults` via a new **App Group** (`group.<bundleid>`).
  - Android: a small store readable by the widget process (SharedPreferences or a file in the shared app sandbox).
- Add native widget targets/registration:
  - iOS: a **WidgetKit app-extension target** plus App Group entitlement on both the app and the extension, wired into the Qt CMake/Xcode build.
  - Android: an `AppWidgetProvider` + `RemoteViews` layout in the existing `android/` source, with a `<receiver>` entry in `AndroidManifest.xml.in`.
- The widget refreshes from the snapshot when the app pushes an update (state change while connected, including backgrounded) and on the OS's normal widget refresh cadence otherwise.

Out of scope (explicitly): waking the machine from the widget, smart/contextual deep-linking, interactive widget buttons, configurable widget content.

## Capabilities

### New Capabilities
- `home-screen-widget`: A native iOS/Android Home Screen widget that displays DE1 machine phase, temperature-vs-target, last-shot summary, and connection/staleness state from a shared snapshot, and opens the app on tap. Includes the Qt-side snapshot writer that publishes machine status to platform-shared storage on state changes.

### Modified Capabilities
<!-- None. The widget reads existing machine state, temperature, and shot-history
     accessors without changing their behavior or any existing spec's requirements. -->

## Impact

- **New native code (no Qt/QML reuse possible — separate render layers):**
  - iOS: new WidgetKit extension target (Swift/SwiftUI); App Group entitlement added to `ios/Decenza.entitlements` and the new extension's entitlements; Info.plist / CMake / Xcode-project wiring for the extension and shared container.
  - Android: new `AppWidgetProvider` + `RemoteViews` XML layout under `android/src/io/github/kulitorum/decenza_de1/`; `<receiver>` + widget metadata registered in `android/AndroidManifest.xml.in` (generated from template by CMake).
- **New Qt/C++ code:** a machine-status snapshot service that observes existing signals (`MachineState::phaseChanged`, `DE1Device::shotSampleReceived`, shot-history updates) and writes a compact snapshot to platform-shared storage off the main thread; platform bridges (QJniObject on Android, Objective-C++ shared-`UserDefaults` write on iOS).
- **Existing accessors read (unchanged):** `MachineState::phase()/phaseString()`, `DE1Device::temperature()/steamTemperature()/goalTemperature()`, connection status, and the shot-history "most recent shot" path (same data the MCP `machine_get_state` tool already serializes).
- **Build/CI:** iOS build gains an app-extension target (affects local Xcode archive and the App Store CI workflow). Android APK gains a widget receiver (no signing/flow change). Both `ios/` and `android/` build configs and `docs/CLAUDE_MD/PLATFORM_BUILD.md` need updates.
- **No changes** to BLE protocol, profile pipeline, settings domains, or existing navigation.
