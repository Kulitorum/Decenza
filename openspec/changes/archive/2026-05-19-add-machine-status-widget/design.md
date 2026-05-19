## Context

Decenza already knows the DE1's phase, temperature, target temperature, connection state, and most-recent shot — the same fields the MCP `machine_get_state` tool serializes (`MachineState::phase()/phaseString()`, `DE1Device::temperature()/steamTemperature()/goalTemperature()`, connection status; shot-history "most recent" path). None of this is reachable from a Home Screen widget today.

Widgets render in a process the Qt runtime does not control:
- **iOS** WidgetKit widgets are a separate SwiftUI app-extension target; they read a snapshot and render on WidgetKit's timeline. They cannot call into Qt.
- **Android** AppWidgets are an `AppWidgetProvider` (a `BroadcastReceiver`) drawing `RemoteViews`; the layout subset is restricted (image + text, no custom drawing).

The app retains a backgrounded BLE connection on both platforms (Android `BleConnectionService` foreground service, declared at `android/AndroidManifest.xml.in:127`; iOS `bluetooth-central` background mode at `ios/Info.plist:68`). So the limiting factor for live data is the **BLE connection**, not foreground/background — but the OS can still reclaim the process under memory pressure or when BLE is genuinely gone, which is why staleness handling is a first-class requirement, not polish.

## Goals / Non-Goals

**Goals:**
- A read-only status widget on iOS and Android showing machine phase, temperature-vs-target, last-shot summary, and an honest connection/staleness state.
- Tap anywhere → launch the Decenza app (normal entry point).
- A single Qt-side snapshot writer that publishes status to platform-shared storage on state changes, off the main thread, reusing existing accessors.
- The widget never presents stale data as if it were live.

**Non-Goals:**
- Waking or otherwise controlling the machine from the widget (no interactive App Intents / `PendingIntent` actions beyond launching the app).
- Smart/contextual deep-linking (tap always opens the app's normal entry; no routing to live-shot view).
- User-configurable widget content or multiple distinct widget kinds.
- Real-time/streaming updates independent of the app — the widget shows the last snapshot the app published plus the OS refresh cadence.
- Desktop (Windows/macOS/Linux) widgets — mobile only.

## Decisions

### D1: One Qt-side snapshot writer, observing existing signals
A single `MachineStatusSnapshot` service subscribes to `MachineState::phaseChanged`, `DE1Device::shotSampleReceived`, connection-state changes, and shot-history "most recent" updates. On any change it assembles a compact record and writes it to platform-shared storage. It reads only existing accessors — no new BLE traffic, no change to the signal graph.

- **Rationale:** The MCP layer already proves these accessors fully describe machine status; reusing them keeps one source of truth and zero protocol risk.
- **Alternatives considered:** (a) Have the widget query an in-app local server — rejected: the widget process can't rely on the app being alive/listening. (b) Reuse MCP `machine_get_state` directly — rejected: MCP is request/response over its own transport, not a push-to-shared-storage mechanism; the snapshot must persist for when the app is dead.

### D2: Coalesced, debounced writes off the main thread
`shotSampleReceived` fires at ~5 Hz; writing shared storage that often is wasteful and risks main-thread I/O. The writer coalesces: it keeps the latest in-memory snapshot and flushes to shared storage on **meaningful change** (phase change, connection change, new shot, or temperature crossing a small threshold/round value), with at most one flush per ~1–2 s during continuous temperature drift. The flush runs off the main thread per the project's `QThread::create()` + `QueuedConnection` rule.

- **Constraint compliance:** "No timers as guards" — coalescing is driven by change events and a last-flush comparison, not a guard timer. Periodic flush is acceptable only as the genuinely-periodic temperature-drift case (same category as polling/animation), but the preferred trigger is event-based (threshold crossing).
- **Alternatives considered:** Write on every sample — rejected: I/O churn and battery cost for no glance value (the widget can't render at 5 Hz anyway).

### D3: Platform-shared storage
- **iOS:** a new **App Group** (`group.<bundleid>`); the app writes shared `UserDefaults` (or a file in the group container) via a thin Objective-C++ bridge. App Group entitlement added to `ios/Decenza.entitlements` and the new extension's entitlements.
- **Android:** the widget runs in the same app sandbox, so the app writes a small store the `AppWidgetProvider` reads — `SharedPreferences` (or a single small JSON file in app-internal storage) via a QJniObject bridge, consistent with existing Java bridges under `android/src/io/github/kulitorum/decenza_de1/`.

- **Rationale:** These are the only sanctioned cross-process channels on each platform; both are tiny payloads (a handful of fields), so format choice is not performance-sensitive. A shared compact JSON schema is reused across platforms to keep the writer single-sourced.
- **Alternatives considered:** SQLite/shared DB — rejected: heavyweight for a few fields and adds a second writer to the history DB path.

### D4: Native widget UI per platform (no shared UI)
- **iOS:** new WidgetKit extension target (SwiftUI), wired into the Qt CMake → Xcode generation; its `TimelineProvider` reads the App Group snapshot.
- **Android:** `AppWidgetProvider` + `RemoteViews` XML layout under `android/src/...`; `<receiver>` + `appwidget-provider` metadata added to `android/AndroidManifest.xml.in` (generated from template by CMake).

There is intentionally no shared widget UI — the platforms share only the JSON snapshot schema and the field semantics.

### D5: Staleness / connection model (the correctness mechanism)
Every snapshot carries: `phase`, `connected` (bool), `temperatureC`, `targetTemperatureC`, `steamTemperatureC`, last-shot fields, and a `capturedAt` ISO-8601 timestamp (project MCP convention: ISO-8601 with offset, units in field names). The widget derives its display state:
- `connected == false` → render `Disconnected — Tap to open`; do **not** show a temperature as if current.
- `connected == true` but `now - capturedAt` exceeds a freshness window → append `updated N min ago`.
- Fresh + connected → render phase + temp-vs-target + last shot normally.

The widget never infers "live"; it only trusts what the snapshot asserts plus its own age check. This makes "the OS killed the process" and "BLE dropped" degrade honestly instead of lying.

### D6: Temperature-vs-target rendering
The widget formats temperature from snapshot fields, not the app:
- Heating phase → `Heating <cur> → <target> °C`.
- At-temperature / Ready → `Ready` indicator + current °C.
- Steaming → steam temperature.
This keeps the formatting logic in each native widget (small, presentational) while the app only publishes raw numbers + phase.

### D7: Tap target
Both platforms use the simplest launch primitive (iOS `widgetURL`/default tap → app; Android root `PendingIntent` → launcher activity `DecenzaActivity`). No URL routing or scheme handling is added.

## Risks / Trade-offs

- **[iOS app-extension target complicates the build]** Qt's CMake → Xcode generation does not create extension targets; the App Store CI archive flow must include and sign the extension. → Mitigation: add the extension wiring as an explicit, documented CMake/Xcode step; verify a clean `qt-cmake -G Xcode` archive locally before touching CI; update `docs/CLAUDE_MD/PLATFORM_BUILD.md`.
- **[Stale data presented as live]** Core risk for any read-only widget. → Mitigation: D5 makes connection + capture-age first-class; the widget renders disconnected/stale states explicitly and is reviewed as a primary acceptance scenario.
- **[Main-thread I/O regression]** Naive snapshot writes could hit disk on the main thread. → Mitigation: D2 mandates off-main-thread flush via the project's standard pattern; coalescing bounds write frequency.
- **[Widget refresh budget]** iOS throttles WidgetKit timeline reloads; Android `updatePeriodMillis` has a 30-min floor and is Doze-throttled. → Mitigation: rely on app-pushed reloads while connected (the common case); treat OS-cadence refresh as the stale-fallback path, which the staleness indicator already covers.
- **[App Group / entitlement misconfig]** Wrong group id or missing entitlement silently yields an empty snapshot (widget always "disconnected"). → Mitigation: single shared constant for the group id; an explicit "snapshot present and parsable" acceptance scenario.
- **[Two new native surfaces to maintain]** Swift + Java widget code outside the Qt codebase. → Mitigation: keep both widgets strictly presentational over a versioned snapshot schema; no business logic in widget code.

## Migration Plan

Purely additive. No data migration, no behavior change to existing features, no settings/profile/BLE changes. Rollout is gated by app release on each store. Rollback = remove the widget extension/receiver and the snapshot writer; no persisted user data depends on it. The snapshot file/`UserDefaults` keys are disposable cache.

## Open Questions

- Final widget size families per platform (iOS systemSmall vs systemMedium; Android cell span) — presentational, decided during implementation against the chosen field set.
- Exact freshness window for the `updated N min ago` threshold (proposed: a small fixed window, tuned against observed background-refresh cadence).
- Whether the iOS extension ships in the same CI App Store workflow run or needs a separate provisioning profile entry (resolve before CI changes; verify with a local archive first).
