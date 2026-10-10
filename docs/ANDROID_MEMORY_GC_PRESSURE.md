# Android Memory & GC Pressure Investigation

## Problem Statement

Slow Android devices (confirmed: Samsung Galaxy Tab A7 Lite) experience escalating SAW
(Stop-at-Weight) latency over long sessions, eventually causing shot stopping failures.
Observed in debug logs: SAW-Latency grows from 1 ms early in a session to 18,305 ms after
~8 hours of continuous use.

Root cause: Android's stop-the-world garbage collector pauses **all threads** — including
Qt's main thread and the BLE callback thread. When pauses become long enough, BLE weight
notifications queue in the main thread's event loop and are processed too late for SAW to
stop the shot on time.

Issue: https://github.com/Kulitorum/Decenza/issues/292

---

## Evidence from Logs

### A7 Lite log (build 3143, pre native-heap fix)

```
[SAW-Latency] dispatch=    1 ms, bleAck=    3 ms, total=    4 ms   ← early in session
[SAW-Latency] dispatch= 6825 ms, bleAck=11480 ms, total=18305 ms   ← 8h later
```

`dispatch` = time from WeightProcessor worker signal to DE1 BLE write being queued.
`bleAck` = time from BLE write to DE1 acknowledgement.

The escalating `dispatch` value means the main thread was stalled — i.e., a GC pause
occurred after the worker emitted its signal but before the main thread processed it.

RSS showed `0.0 MB` in that log because `8cda92b` (native heap fix) hadn't landed yet.

### Contrast: 26-hour Bookoo user session (current build)

Same SAW infrastructure, different hardware. SAW-Worker → SAW propagation: **4 ms** even
at 24+ hours uptime. No memory pressure. Their problem was Bookoo scale tare behavior
(see issue #341), not GC.

---

## Architecture of the Problem

```
Android BLE thread
  → onCharacteristicChanged() [Java, creates byte[] + callback objects]
  → JNI
  → Qt BLE layer
  → characteristicChanged signal → Qt::QueuedConnection
  → main thread event loop  ← GC pause stalls delivery here
  → WeightProcessor::processWeight()  [worker thread]
  → SAW check
  → QCoreApplication::postEvent(DE1Device, high-priority)
  → main thread  ← GC pause stalls delivery here too
  → DE1Device BLE write
```

Two stall points: BLE notification delivery to the main thread, and the SAW stop command
delivery to DE1Device. Both are blocked by GC pauses.

---

## What MemoryMonitor Currently Tracks

`MemoryMonitor` samples every 60 seconds and logs:

```
[Memory] RSS: 82.3 MB, QObjects: 847, peak: 95.1 MB
[Memory] QObject deltas: +12 QQuickItem, -3 QQmlContext, ...
```

- **Native heap**: `android.os.Debug.getNativeHeapAllocatedSize()` — C++ malloc'd memory
- **QObject count**: full tree walk of QApplication + QQmlEngine + root objects
- **Per-class deltas**: top-5 classes with changed counts since last sample
- **Full breakdown**: available via web UI at `/api/memory`

### Critical gap: Java heap is NOT tracked

Android's stop-the-world GC manages the **Java heap**, not the native heap. Every BLE
notification creates Java objects (`byte[]`, `BluetoothGattCharacteristic`, callback
objects) that must be collected. At 5–10 Hz during shots over 8 hours, that is 144k–288k
BLE callback objects cycling through the Java heap.

`getNativeHeapAllocatedSize()` does not see these. We have zero visibility into whether
the Java heap is growing across a long session.

**Fix implemented**: Java heap logging added to `MemoryMonitor::takeSample()` on Android:

```
[Memory] RSS: 82.3 MB  Java heap: 45.2 / 256.0 MB  QObjects: 847  peak: 95.1 MB
```

See the "Changes Made" section below.

---

## Known Memory Consumers

### High-frequency allocations during shots

| Source | Rate | Notes |
|--------|------|-------|
| BLE notifications (DE1) | ~5 Hz | Java `byte[]` + callback per notification |
| BLE notifications (scale) | ~5 Hz | Same Java object cycle |
| FlowScale Compare logging | 5 Hz × 5 strings | **Eliminated** — see PR fixing #292 |
| ShotDebugLogger capture | ~10–15 lines/s | QStringList grows unbounded during shot |
| WebDebugLogger ring buffer | continuous | 1000 strings in RAM, constant churn |

### Persistent allocations

| Source | Size | Notes |
|--------|------|-------|
| WebDebugLogger ring buffer | ~100 KB | 1000 lines × ~100 chars each |
| AsyncLogger ring buffer | ~4096 LogEntry structs | Each holds a QString |
| Translation strings | 1080 strings | Loaded at startup, constant |
| QML engine JS heap | low | Profiled; JS heap flat across shots. resolvedText optimized. |
| ShotDataModel vectors | ~105 KB per shot | Pre-alloc 600 pts × 11 series; freed on clear() |
| MemoryMonitor sample history | ~70 KB | 1440 MemorySample structs (24h at 60s) |

### MemoryMonitor QObject scan overhead

`findChildren<QObject*>()` on the full QML tree (hundreds of items) runs every 60 seconds
on the main thread. This allocates a large temporary `QList`. On a complex page like
SettingsPage, this list could hold 500+ pointers. Minor but worth noting.

---

## Recommendations

### ✅ Done — QML memory leaks fixed

The largest known QObject leak (StackView retaining full page trees) and the
PipesScreensaver instance leak are both fixed in the current build. The resolvedText
binding overhead is reduced 49%. Buffer sizes are reduced. Memory snapshots are now
included in debug log downloads.

### Priority 1 — Get diagnostic data (requires user action)

Ask the A7 Lite user to submit a new log **after a 2–4 hour session** using the current
build (which now includes native heap tracking, Java heap logging, the StackView fix, and
the resolvedText optimization).

From that log, check three things:

1. Does `[Memory] Java heap` grow over the session?
   - Yes → Java heap leak; investigate BLE callback objects, Qt JNI caching
   - No → Java heap is fine; look elsewhere

2. Does `QObjects` count grow over the session?
   - Yes → further QML leak (fix #6 should have addressed the main one)
   - No → QML tree is stable

3. Does native `RSS` grow over the session?
   - Yes → C++ memory leak; QObject delta log will show which class is leaking
   - No → Native allocations are stable

### Priority 2 — ✅ Done (buffer size reductions)

All three buffer reductions implemented:
- WebDebugLogger: 1000 → 500 lines
- AsyncLogger: 4096 → 512 on Android/iOS
- ShotDebugLogger: 2000-line cap added

### Priority 3 — Reduce QObject scan frequency

Lower `MemoryMonitor`'s QObject tree walk from every 60 seconds to every 5 minutes on
mobile, or skip it entirely on devices with < 2 GB RAM. The per-class breakdown is
diagnostic only and doesn't need 60-second resolution.

### Priority 4 — ✅ Done (QML Profiler investigation)

Ran QML Profiler; found and fixed the `resolvedText` binding overhead. JS heap is flat
across shots (confirmed via Memory track). No further QML engine issues identified.

### Priority 5 — ✅ Done (BLE CONNECTION_PRIORITY_HIGH)

`BluetoothGatt.requestConnectionPriority(CONNECTION_PRIORITY_HIGH)` implemented via a
Java reflection helper (`BleHelper.java`) called from `onControllerConnected()` in both
`BleTransport` (DE1) and `QtScaleBleTransport` (scale). Reduces the post-GC-pause BLE
delivery window from ~50 ms to ~7.5 ms on both connections. See issue #342.

---

## Changes Made

### 1. Java heap logging in MemoryMonitor (this session)

Added Java heap tracking via `java.lang.Runtime` JNI call in the Android branch of
`MemoryMonitor::takeSample()`. Log line now reads:

```
[Memory] RSS: 82.3 MB  Java heap: 45.2 / 256.0 MB  QObjects: 847  peak: 95.1 MB
```

`Java heap: USED / MAX MB` — used is `totalMemory() - freeMemory()`, max is `maxMemory()`.

### 2. FlowScale Compare logging disabled when BT scale connected (this session)

The FlowScale Compare block in `MainController::onScaleWeightChanged()` was running at
5 Hz × 5 `QString::number()` allocations = 25 native string allocations per second during
shots, for debug output no user ever sees. This contributed both to native heap churn and
to ShotDebugLogger line accumulation.

Fix: added `!btScaleConnected` guard and changed `useFlowScale` default to `false`,
with an explicit `setUseFlowScale(false)` when a physical scale connects.

### 3. Shadow FlowScale feeding removed (this session)

The DE1 sample handler was feeding flow data into FlowScale even when a physical BLE scale
was connected (shadow mode for comparison logging). Removed the feeding block; the
comparison logging guard already prevents output, but the integration itself was still
running at 5 Hz.

### 4. CustomItem resolvedText binding optimized (QML Profiler finding)

QML Profiler showed `resolvedText` in `CustomItem.qml` re-evaluating at 5 Hz for **every**
layout item on screen (21,149 calls across 5 shots), even items displaying static values
like `%PROFILE%` or `%TARGET_TEMP%` that never change during a shot.

Root cause: the binding used unconditional `void()` references to all live properties
(DE1Device.temperature, MachineState.scaleWeight, etc.), causing QML to track all of them
as dependencies for every instance.

Fix: added precomputed `_needsMachineData`, `_needsScaleData`, `_needsControllerData`,
`_needsScaleDevice`, `_needsSettingsData` boolean flags that evaluate once from `content`
at layout load. The `resolvedText` binding now only subscribes to the live properties the
item's template actually uses. Call count dropped from 21,149 → 10,763 (49% reduction).
JS heap usage remains flat across shots (confirmed via QML Profiler Memory track).

### 5. Buffer size reductions (Priority 2)

- **WebDebugLogger**: ring buffer 1000 → 500 lines (~50 KB constant RAM saved)
- **AsyncLogger**: ring buffer 4096 → 512 entries on Android/iOS (desktop unchanged)
- **ShotDebugLogger**: added 2000-line cap in `appendLog()` to prevent unbounded growth
  during very long shots. A 30s shot generates ~300–450 lines; cap covers long shots while
  bounding worst-case RAM use.

### 6. StackView replace(null) — primary QObject leak fix

**Confirmed via live `/api/memory` data on Android**: 8,531 QObjects after 21h of use vs.
baseline 1,301 at startup. All QObject classes grew uniformly ~65–75%, the signature of
whole pages being retained rather than a single leaky class.

**Root cause**: `pageStack.replace(page)` replaces only the **top** item in the StackView.
When the user navigates deep (e.g. Settings → ProfileEditor → PressureEditor) and a phase
change fires `replace(espressoPage)`, only PressureEditor is replaced — Settings and
ProfileEditor remain on the stack forever.

**Fix**: Changed all root-level `pageStack.replace(page)` calls to `pageStack.replace(null, page)`,
which clears the entire stack before pushing the new page. 18 call sites updated in
`qml/main.qml` across the phase change handler, `goToIdle()`, `goToEspresso()`,
`goToSteam()`, `goToHotWater()`, `goToShotMetadata()`, screensaver enter/exit, and the
post-shot completion timer.

Intentionally **not** changed: `switchToDFlowEditor()` and `switchToAdvancedEditor()` —
these are deliberate sibling swaps at the same stack level, not root navigations.

**Superseded (#1976).** Operation pages are now pushed and popped (`QML_NAVIGATION.md`), so a
phase change no longer clears the stack. The leak above does not come back: what stays under an
operation page is the stack the user built and returns to with Back, not pages nothing can reach.
Leaving an operation page pops to where it was entered, and a post-shot review replaces the
review a shot was started from, so repeated shots do not grow it.

### 7. PipesScreensaver null-parent instance cleanup

`PipesScreensaver.qml` builds the pipes scene using `InstanceListEntry` objects created
via `createObject(null, ...)` — null parent, outside the QObject tree. When the component
was destroyed, those objects were never freed.

**Fix**: Added `Component.onDestruction` to explicitly `destroy()` all entries in
`cylinderEntries` and `sphereEntries`. Also cleared the lists in `initializePipes()` before
destroying entries to avoid dangling references from `cylinderInstanceList.instances`.

### 8. Memory snapshot appended to debug log download

Added `MemoryMonitor::toSummaryString()` which produces a plain-text snapshot including
uptime, RSS (current/peak/startup), QObject count, top-20 QObject classes with delta vs
startup, and the last 20 60-second samples.

This is now appended to the content returned by `/api/debug/file` (the "Download Log"
button on the debug page). Every debug log download automatically includes the current
memory snapshot, giving developers full context without a separate `/api/memory` request.

---

## macOS Profiling with Qt Creator

Qt Creator on macOS wraps Xcode and provides direct access to Instruments and
AddressSanitizer without any command-line setup. This is the best way to find C++ heap
growth and true memory leaks before getting a new Android log.

**What macOS profiling finds:** C++ heap leaks and growth in Qt BLE/QML/network
internals — the same code that runs on Android. Any leak found here is a leak on Android.

**What it won't find:** Java heap pressure (Android-only). But if native heap grows
significantly across shots on macOS, the same growth feeds Android's GC pressure.

**Simulator mode is on by default in Debug builds.** No DE1 hardware needed.

---

### Code review findings (no hardware profiling needed)

A full static review of `src/` found no application-level memory leaks:
- All `QNetworkReply` instances call `deleteLater()` as the first line of their handler
- All `QThread::create()` threads are either self-cleaning or tracked with `deleteLater()`
- All lambda captures of `this` are guarded with `std::shared_ptr<bool> m_destroyed`
- No orphaned `new` allocations, no unbounded static containers

If leaks exist, they are in **Qt framework internals** (BLE stack, QML engine, network
layer) that are only visible at runtime. The three tools below target exactly that.

---

### Option A — AddressSanitizer (easiest, best leak detail)

Catches true leaks and memory errors. Reports every leaked allocation with a full stack
trace when the app exits. Recommended starting point.

**Setup in Qt Creator:**

1. **Projects** (wrench icon, left sidebar) → select your macOS Debug kit → **Build Settings**
2. Under the **CMake** section, find **Additional CMake arguments** (or expand **CMake Build Step**)
3. Add both flags:
   ```
   -DCMAKE_CXX_FLAGS=-fsanitize=address -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address
   ```
4. Click **Apply** — Qt Creator will re-run CMake automatically
5. **Build** (Cmd+B)
6. **Run** normally (Cmd+R) — not Debug, just Run

**In the running app:**

1. Run 10–15 simulated shots (simulator is on in Debug builds by default)
2. Quit the app

**What to look for:**

ASan output appears in the **Application Output** pane. Each leak looks like:
```
Direct leak of 1024 byte(s) in 1 object(s) allocated from:
    #0 operator new(unsigned long) ...
    #1 QLowEnergyControllerPrivateDarwin::...
    #2 ...
```

Since our application code is clean, any report entries point at Qt BLE/QML/network
internals. These are actionable: a `QLowEnergyController` leak means we need to call
`disconnectFromDevice()` + `deleteLater()` at the right time; a `QQmlContext` leak means
a QML component isn't being destroyed when its page is popped.

**Remove ASan when done:** delete the CMake flags and rebuild — ASan adds ~2× memory and
slowdown, unsuitable for normal development.

---

### Option B — Instruments Allocations (finds what grows, not just what leaks)

Use this when something grows over many shots but isn't technically a "leak" — for
example a Qt internal cache that never shrinks, or a QML item that's retained when it
should be released.

**Run from Qt Creator:**

1. Build with the macOS Debug kit (no extra flags needed)
2. Menu: **Analyze → Instruments**
3. Qt Creator shows a template picker — select **Allocations**
4. Click **Profile** — Qt Creator launches the app under Instruments

**In the running app + Instruments:**

1. Wait ~5 seconds for startup to settle
2. Click **Mark Generation** (camera icon in Instruments toolbar) — this is your baseline
3. Run 10 shots in the simulator
4. Click **Mark Generation** again (Generation B)
5. Run 10 more shots
6. Click **Mark Generation** again (Generation C)
7. Quit the app

**What to look for:**

In the **Allocations** track, switch to **Generations** view and select Generation C.
Sort by **Bytes** descending. This shows everything that was allocated during the last 10
shots and wasn't freed.

Key suspects:

| Object type | What it means |
|-------------|---------------|
| `QLowEnergyCharacteristic` / `QLowEnergyService` | Qt BLE not releasing service objects after disconnect |
| `QQmlContext` | QML pages retaining context after being popped from StackView |
| `QSGTexture` / `QImage` | Qt Quick texture cache not evicting (emoji, graph images) |
| `QString::Data` | Log buffer churn — normal, but large count = reduce buffer sizes |
| `QNetworkReply` | Reply not deleted — would also show in Leaks |

If Generation C shows more objects than Generation B by a consistent per-shot amount,
that's a leak rate. 10 extra `QQmlContext` objects per 10 shots = 1 per shot = real leak.

---

### Option C — Instruments Leaks (for true leaks only)

Simpler than Allocations but only catches objects with no live references — misses
growing-but-referenced containers. Use as a quick sanity check.

**Run from Qt Creator:**

1. Menu: **Analyze → Instruments**
2. Select **Leaks** template
3. Click **Profile**
4. Run 10–15 shots, quit the app
5. Instruments highlights leaked allocations in red in the timeline

---

### Option D — QML Profiler (for QML/JS heap and binding frequency)

Qt Creator's built-in QML Profiler instruments the JavaScript engine directly — the one
memory pool not visible in any of the above tools.

**Run from Qt Creator:**

1. Menu: **Analyze → QML Profiler**
2. Run several shots in the simulator
3. Stop profiling

**What to look for:**

- **Bindings** tab: which QML bindings fire most often. On `EspressoPage`, bindings for
  weight, pressure, flow, and time all fire at 5 Hz. Each firing creates/destroys JS
  values. High-frequency bindings that do string formatting (`"Weight: " + weight.toFixed(1)`)
  are especially expensive.
- **Memory** tab: JS heap size over time. Should be flat between shots.

---

### Recommended sequence

1. **Start with ASan (Option A)** — zero extra steps once flag is set, full stack traces.
   If it reports Qt BLE or QML leaks, fix those first; they affect all platforms.

2. **Follow with Allocations Generation Analysis (Option B)** — run after fixing any ASan
   findings. This catches the growing-but-not-leaked category.

3. **QML Profiler (Option D)** — run last, specifically targeting EspressoPage binding
   overhead at 5 Hz.

4. **Get a new Android log** from the A7 Lite user (current build, 2–4 hour session).
   With Java heap now logged, the three questions from the Recommendations section above
   can be answered definitively.

---

## How to Reproduce / Confirm

1. Enable "Shot debug log" in Settings → Shot History
2. Run 4–6 shots over a 3–4 hour session on the A7 Lite (don't restart the app)
3. Export the debug log from the last shot (it captures the full session log)
4. Check `[Memory]` lines for Java heap and native heap trends
5. Check `[SAW-Latency]` lines for dispatch time growth

The app must be kept in the foreground (or at least not killed) throughout the session.
Background suspend clears GC pressure — latency issues only manifest in continuous use.
