## 1. Shared snapshot schema

- [x] 1.1 Define the snapshot JSON schema (fields: `phase` string, `connected` bool, `temperatureC`, `targetTemperatureC`, `steamTemperatureC`, last-shot `yieldG`/`durationSec`/`qualityBadge`, `capturedAt` ISO-8601 with offset) and document it in the change folder
- [x] 1.2 Add a single shared constant for the iOS App Group id and the Android shared-store key/file name so the writer and both widgets agree

## 2. Qt-side snapshot writer

- [x] 2.1 Create `MachineStatusSnapshot` service class that observes `MachineState::phaseChanged`, connection-state change, `DE1Device::shotSampleReceived`, and the most-recent-shot update
- [x] 2.2 Assemble the snapshot record from existing accessors (`phase()/phaseString()`, `temperature()`, `goalTemperature()`, `steamTemperature()`, connection status, most-recent shot) — no new BLE traffic
- [x] 2.3 Implement event-based coalescing: flush on phase/connection/new-shot change immediately; for temperature drift flush only on threshold/round-value crossing at a bounded rate (no guard timers)
- [x] 2.4 Perform the shared-storage write off the main thread using the project's `QThread::create()` + `QueuedConnection` pattern
- [x] 2.5 Instantiate and wire the service where `DE1Device`/`MachineState` are created in `src/main.cpp`
- [x] 2.6 Write a fresh snapshot with `connected=false` on app shutdown / BLE teardown so a dead app degrades honestly

## 3. Platform shared-storage bridges

- [x] 3.1 Android: add a Java helper under `android/src/io/github/kulitorum/decenza_de1/` that writes the snapshot to SharedPreferences (or a small internal-storage JSON file); call it from C++ via QJniObject
- [x] 3.2 iOS: add an Objective-C++ bridge that writes the snapshot to the App Group shared `UserDefaults`/container; compile with the existing `-x objective-c++` path
- [ ] 3.3 Verify both bridges round-trip the schema (write from app, read raw value back) before building widget UI

## 4. Android widget

- [x] 4.1 Create `MachineStatusWidgetProvider` (`AppWidgetProvider`) under `android/src/io/github/kulitorum/decenza_de1/`
- [x] 4.2 Create the `RemoteViews` XML layout(s) (icon + phase label + temp-vs-target line + last-shot line + staleness/disconnected line)
- [x] 4.3 Implement reading the shared snapshot and mapping it to display state per the staleness/connection rules (disconnected, stale, fresh)
- [x] 4.4 Implement temp-vs-target / Ready / steam formatting from snapshot fields
- [x] 4.5 Set a root `PendingIntent` on the widget that launches `DecenzaActivity` (no control actions)
- [x] 4.6 Register `<receiver>` + `appwidget-provider` metadata in `android/AndroidManifest.xml.in` and add the widget metadata XML
- [x] 4.7 Trigger a widget update from the app when a new snapshot is written (broadcast / `AppWidgetManager`) so it refreshes promptly while connected

## 5. iOS widget

- [x] 5.1 Add a WidgetKit app-extension target to the Qt CMake → Xcode generation
- [x] 5.2 Add the App Group entitlement (`group.<bundleid>`) to `ios/Decenza.entitlements` and create the extension's entitlements with the same group
- [x] 5.3 Implement the SwiftUI widget view: icon + phase label + temp-vs-target/Ready/steam + last-shot summary + staleness/disconnected line
- [x] 5.4 Implement the `TimelineProvider` reading the App Group snapshot and applying the staleness/connection display rules
- [x] 5.5 Configure tap to launch the app (`widgetURL`/default) with no control actions
- [x] 5.6 Call `WidgetCenter.shared.reloadTimelines(...)` from the app (via the iOS bridge) when a new snapshot is written so it refreshes while connected/backgrounded

## 6. Build & CI

- [ ] 6.1 Verify a clean local iOS build/archive (`qt-cmake -G Xcode`) includes and signs the widget extension
- [x] 6.2 Update the iOS App Store CI workflow so the extension is built/signed without manual post-build steps; confirm provisioning for the App Group
- [ ] 6.3 Verify the Android APK build includes the widget receiver (no signing/flow change)
- [x] 6.4 Update `docs/CLAUDE_MD/PLATFORM_BUILD.md` with the iOS extension + App Group steps and the Android widget receiver

## 7. Documentation

- [x] 7.1 Update `docs/CLAUDE_MD/PLATFORM_BUILD.md` with the iOS extension + App Group steps and the Android widget receiver (also covered by 6.4 — keep consistent)
- [x] 7.2 Update `docs/CLAUDE_MD/` index/pointers if a new subsystem doc is warranted for the widget snapshot
- [ ] 7.3 Add a "Home Screen Widget" section to the end-user manual in the `Kulitorum/Decenza.wiki` repo (clone `Kulitorum/Decenza.wiki.git`; document what the widget shows, the connection/staleness behaviour, that it opens the app on tap, and how to add it on iOS and Android)

## 8. Verification (requires user hardware — not automatable here)

- [ ] 8.1 Verify all spec scenarios manually on Android: heating/ready/steam rendering, last-shot, disconnected, stale ("updated N min ago"), missing snapshot, tap-opens-app
- [ ] 8.2 Verify all spec scenarios manually on iOS, including widget refresh while the app is backgrounded with the DE1 connected
- [ ] 8.3 Confirm no main-thread I/O regression from snapshot writes (e.g. profiling / no jank during a shot at 5 Hz sampling)
- [ ] 8.4 Confirm the widget shows the disconnected/open-app state (not stale data) after the OS reclaims the app process

## 9. Land

- [x] 9.1 Open a PR for the change (feature branch `add-machine-status-widget`, do not push to main)
- [ ] 9.2 Run automated PR review (`/pr-review-toolkit:review-pr`) and address findings
- [ ] 9.3 Squash-merge + delete branch via the project `merge-pr` flow once approved
