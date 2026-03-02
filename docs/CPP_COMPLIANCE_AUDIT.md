# CLAUDE.md C++ Compliance Audit

Audit date: 2026-03-01

This document tracks C++ code violations of the conventions defined in CLAUDE.md.
Work through each category and check off items as they are fixed.

---

## 1. Naming Conventions

### 1a. Slot naming — must follow `onEventName()` pattern

CLAUDE.md: "Slots: `onEventName()`"

Slots declared in `public slots:` or `private slots:` sections that do not follow the `on*` naming convention:

- [ ] `src/core/accessibilitymanager.h:72` — `updateTtsLocale()` (public slot)
- [ ] `src/core/batterymanager.h:44` — `setChargingMode(int mode)` (public slot)
- [ ] `src/core/batterymanager.h:45` — `checkBattery()` (public slot)
- [ ] `src/machine/machinestate.h:121` — `updateShotTimer()` (private slot)
- [ ] `src/models/shotdatamodel.h:70-83,96` — `clear()`, `clearWeightData()`, `addSample()`, `addWeightSample()` (x2), `markExtractionStart()`, `markStopAt()`, `smoothWeightFlowRate()`, `addPhaseMarker()`, `flushToChart()` (public + private slots)
- [ ] `src/controllers/shottimingcontroller.h:107` — `updateDisplayTimer()` (private slot)
- [ ] `src/screensaver/strangeattractorrenderer.h:71` — `iterate()` (private slot)
- [ ] `src/network/mqttclient.h:76-80` — `publishTelemetry()`, `publishState()`, `attemptReconnect()` (private slots)
- [ ] `src/ble/blemanager.h:89-90` — `stopScan()`, `clearDevices()` (public slots)
- [ ] `src/usb/usbmanager.h:56` — `pollPorts()` (private slot)
- [ ] `src/usb/usbscalemanager.h:47` — `pollPorts()` (private slot)

**Note:** Some are setter-style slots (`setChargingMode`) or data-model API slots (`addSample`, `clear`) where the `on*` convention reads poorly. Consider renaming only where the slot is clearly a signal handler (e.g. `updateShotTimer` → `onShotTimerTimeout`, `publishTelemetry` → `onTelemetryTimerTimeout`).

### 1b. Class naming — PascalCase

CLAUDE.md: "Classes: `PascalCase`"

- [ ] `src/usb/usbmanager.h:27` — `USBManager` uses all-caps abbreviation; should be `UsbManager`

### 1c. Member variable missing `m_` prefix

CLAUDE.md: "Members: `m_` prefix"

- [ ] `src/ble/transport/corebluetooth/corebluetoothscalebletransport.h:48` — `Impl* d = nullptr` should be `m_impl` (PIMPL pointer with public access for ObjC delegate)

### 1d. Class/filename spelling inconsistency

- [ ] `src/ble/scales/solobaristascale.h:6` — Class is `SoloBarristaScale` (double-r "Barrista"), but filename is `solobaristascale.h` (single-r "barista"). The correct English spelling is "barista". Rename class to `SoloBarista Scale` and update all references.

---

## 2. Timer as Guard/Workaround (7 violations)

CLAUDE.md: "Never use timers as guards/workarounds. Timers are fragile heuristics that break on slow devices and hide the real problem. Use event-based flags and conditions instead."

### BLE stack not-ready-after-wake delays

- [ ] `src/main.cpp:411` — `QTimer::singleShot(500, ...)` to reconnect DE1 after auto-wake. Comment: "Delay slightly to let BLE stack initialize after wake"
- [ ] `src/main.cpp:417` — `QTimer::singleShot(500, ...)` to reconnect scale after auto-wake. Same rationale.

**Fix:** Hook into a BLE-stack-ready signal or callback instead of a 500ms delay.

### BLE write flush before suspend/exit

- [ ] `src/main.cpp:1023` — `QTimer::singleShot(500, &waitLoop, &QEventLoop::quit)` blocks main thread 500ms before app suspend. Comment: "Give BLE write time to complete before app suspends / de1app waits 1 second, we use 500ms"
- [ ] `src/main.cpp:1145` — `QTimer::singleShot(2000, &waitLoop, &QEventLoop::quit)` blocks main thread 2s before exit. Comment: "Wait for BLE writes to complete before exiting"

**Fix:** Wait on a command-queue-empty signal from `BleTransport` instead of arbitrary delays.

### BLE stack not-ready-after-resume delays

- [ ] `src/main.cpp:1057` — `QTimer::singleShot(500, ...)` to reconnect DE1 after app resume. Comment: "delay to let BLE stack initialize after resume"
- [ ] `src/main.cpp:1065` — `QTimer::singleShot(500, ...)` to reconnect scale after app resume. Same pattern.

### Initialization ordering hacks

- [ ] `src/controllers/maincontroller.cpp:97` — `m_settingsTimer` (1000ms) started on `initialSettingsComplete` signal. Comment: "The initial settings from DE1Device use hardcoded values; we need to send user settings quickly to set the correct steam temperature." The event trigger is correct but the 1s delay is an arbitrary buffer.
- [ ] `src/weather/weathermanager.cpp:73` — `QTimer::singleShot(2000, ...)` on `setLocationProvider()`. Comment: "Delay slightly to let other init finish"

### Borderline (event-loop deferral)

- [ ] `src/machine/machinestate.cpp:402` — `QTimer::singleShot(0, ...)` to defer `phaseChanged`/`shotStarted`/`shotEnded` signals. Comment: "Defer other signal emissions to allow pending BLE notifications to process first." Classic event-loop deferral hack — fragile if BLE notifications take more than one event-loop turn.

---

## 3. Main-Thread I/O (6 call sites, 30+ methods)

CLAUDE.md: "Never run database or disk I/O on the main thread. Use `QThread::create()` with a `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` callback."

Many async equivalents already exist (`requestShotsFiltered()`, `requestShot()`, etc.) but the synchronous versions are still called from the main thread.

### 3a. ShotHistoryStorage synchronous Q_INVOKABLE methods

All declared in `src/history/shothistorystorage.h` and callable from QML on the main thread:

- [ ] Line 189 — `getShots(int offset, int limit)` — blocking SQL SELECT with pagination
- [ ] Line 190 — `getShotsFiltered(...)` — blocking SQL SELECT with full filter/FTS evaluation
- [ ] Line 196 — `getShotTimestamp(qint64 shotId)` — blocking SQL SELECT
- [ ] Line 199 — `getShot(qint64 shotId)` — blocking full record load + blob decompression
- [ ] Line 200 — `getShotRecord(qint64 shotId)` — same (C++ return type, not Q_INVOKABLE)
- [ ] Line 206 — `getShotsForComparison(...)` — blocking batch full-record load loop
- [ ] Line 216 — `deleteShot(qint64 shotId)` — blocking SQL DELETE
- [ ] Line 223 — `updateShotMetadata(...)` — blocking SQL UPDATE
- [ ] Lines 229–244 — `getDistinct*()` methods (12 total) — each executes `SELECT DISTINCT`
- [ ] Line 247 — `getFilteredShotCount(...)` — blocking SQL SELECT COUNT
- [ ] Line 251 — `getAutoFavorites(...)` — blocking SQL aggregate
- [ ] Lines 258–263 — `getAutoFavoriteGroupDetails(...)` — blocking SQL aggregate
- [ ] Line 274 — `exportShotData(...)` — blocking full record load + JSON serialization
- [ ] Line 277 — `createBackup(...)` — blocking WAL checkpoint + file copy
- [ ] Line 283 — `importDatabase(...)` — blocking full DB import

### 3b. ShotServer synchronous DB calls in HTTP handlers

`ShotServer` lives on the main thread (created in `MainController` constructor). All request handlers block the main thread:

- [ ] `src/network/shotserver.cpp:887` — `getShot()` in profile.json download handler
- [ ] `src/network/shotserver.cpp:916` — `getShots(0, 1000)` in `/api/shots` handler
- [ ] `src/network/shotserver.cpp:945` — `updateShotMetadata()` in POST handler
- [ ] `src/network/shotserver.cpp:955` — `getShot()` in GET `/api/shot/:id` handler
- [ ] `src/network/shotserver.cpp:973` — `deleteShot()` in a loop in batch delete handler
- [ ] `src/network/shotserver.cpp:980` — `checkpoint()` + `sendFile()` blocks while reading entire DB file
- [ ] `src/network/shotserver.cpp:1355` — `createBackup()` + `sendFile()` in backup download handler
- [ ] `src/network/shotserver_shots.cpp:48` — `getShots(0, 1000)` in `generateShotListPage()`
- [ ] `src/network/shotserver_shots.cpp:1048` — `getShot()` in `generateShotDetailPage()`

**Note:** `shotserver_backup.cpp:479` correctly uses `QThread::create()` for its WAL checkpoint — not a violation.

### 3c. MainController loadShotWithMetadata

- [ ] `src/controllers/maincontroller.cpp:1081` — `loadShotWithMetadata()` is Q_INVOKABLE, called from QML, calls `getShotRecord()` synchronously. Async `requestShot()` / `shotReady()` exists but is not used here.

### 3d. FlowCalibrationModel loadRecentShots

- [ ] `src/models/flowcalibrationmodel.cpp:41` — `getShots(0, 50)` on main thread
- [ ] `src/models/flowcalibrationmodel.cpp:45` — `getShotRecord()` called up to 50 times in a loop on main thread
- [ ] `src/models/flowcalibrationmodel.cpp:104` — `getShotRecord()` in `loadCurrentShot()` on main thread

### 3e. AIManager getRecentShotContext

- [ ] `src/ai/aimanager.cpp:315` — `getShotTimestamp()` — blocking SQL
- [ ] `src/ai/aimanager.cpp:328` — `getShotsFiltered()` — blocking SQL
- [ ] `src/ai/aimanager.cpp:357` — `getShot()` in a loop (up to 3 iterations) — blocking full record loads

This Q_INVOKABLE method (called from QML) performs up to 5 sequential blocking DB queries.

---

## 4. ShotServer JavaScript Fetch Conventions

### 4a. fetch() missing .catch() handler

CLAUDE.md: "Every `fetch()` must have a `.catch()` handler. Never leave a fetch chain without error handling."

All in `src/network/shotserver_shots.cpp`:

- [ ] Line 1026 — `togglePower()` (shot list page) — no `.catch()`
- [ ] Line 2112 — `togglePower()` (shot detail page) — no `.catch()` (duplicate)
- [ ] Line 3071 — `togglePower()` (comparison page) — no `.catch()` (duplicate)
- [ ] Line 3248 — `fetchLogs()` — no `.catch()`
- [ ] Line 3275 — `clearLog()` — no `.catch()`
- [ ] Line 3284 — `clearAll()` — no `.catch()`
- [ ] Line 3310 — `loadPersistedLog()` — no `.catch()`

### 4b. Missing `r.ok` check before `.json()`

CLAUDE.md: "Check `r.ok` before `r.json()` in fetch chains. Non-2xx responses with non-JSON bodies will throw on `.json()` and produce a misleading error."

**In `src/network/shotserver_shots.cpp`:**

- [ ] Line 705 — `deleteSelected()` — `resp.json()` without `r.ok` check
- [ ] Line 845 — `loadSavedSearches()` — `r.json()` without `r.ok` check
- [ ] Line 858 — `saveSearch()` — `r.json()` without `r.ok` check
- [ ] Line 873 — `deleteSavedSearch()` — `r.json()` without `r.ok` check
- [ ] Line 1018 — `fetchPowerState()` (shot list) — `r.json()` without `r.ok` check
- [ ] Line 1026 — `togglePower()` (shot list) — `r.json()` without `r.ok` check
- [ ] Line 1766 — metadata save (shot detail) — `r.json()` without `r.ok` check
- [ ] Line 2105 — `fetchPowerState()` (shot detail) — `r.json()` without `r.ok` check (duplicate)
- [ ] Line 2112 — `togglePower()` (shot detail) — `r.json()` without `r.ok` check (duplicate)
- [ ] Line 3064 — `fetchPowerState()` (comparison) — `r.json()` without `r.ok` check (duplicate)
- [ ] Line 3071 — `togglePower()` (comparison) — `r.json()` without `r.ok` check (duplicate)
- [ ] Line 3248 — `fetchLogs()` — `r.json()` without `r.ok` check
- [ ] Line 3310 — `loadPersistedLog()` — `r.json()` without `r.ok` check

**In `src/network/shotserver_settings.cpp`:**

- [ ] Line 559 — `loadSettings()` — `await resp.json()` without `resp.ok` check (has try/catch but no HTTP status check)
- [ ] Line 656 — `saveVisualizer()` — `await resp.json()` without `resp.ok` check
- [ ] Line 674 — `testVisualizer()` — `await resp.json()` without `resp.ok` check
- [ ] Line 693 — `saveAi()` — `await resp.json()` without `resp.ok` check
- [ ] Line 717 — `testAi()` — `await resp.json()` without `resp.ok` check
- [ ] Line 742 — `saveMqtt()` — `await resp.json()` without `resp.ok` check
- [ ] Line 789 — `connectMqtt()` — `await resp.json()` without `resp.ok` check
- [ ] Line 810 — `publishDiscovery()` — `resp.ok` checked after `.json()`, not before (non-JSON error body would still throw)

**Compliant examples (for reference):**
- `shotserver_shots.cpp:3293` — `downloadLog()` — correctly checks `r.ok` before `r.json()`
- `shotserver_settings.cpp:821` — `pollMqttStatus()` — correctly checks `resp.ok` before `.json()`

---

## 5. BLE Error Logging (4 violation areas)

### 5a. Scale `*_LOG` macros route errors to `qDebug()`

CLAUDE.md: "BLE errors are automatically captured (use `qWarning()` for errors)."

All scale implementations define a single LOG macro that maps everything (including error messages) to `qDebug()`. Error conditions like "Transport error", "No compatible service found" are logged at debug level, not warning level.

| File | Macro | Maps to |
|------|-------|---------|
| `src/ble/scales/acaiascale.cpp:9` | `ACAIA_LOG` | `qDebug()` |
| `src/ble/scales/bookooscale.cpp:9` | `BOOKOO_LOG` | `qDebug()` |
| `src/ble/scales/difluidscale.cpp:9` | `DIFLUID_LOG` | `qDebug()` |
| `src/ble/scales/eurekaprecisascale.cpp:9` | `EUREKA_LOG` | `qDebug()` |
| `src/ble/scales/hiroiascale.cpp:9` | `HIROIA_LOG` | `qDebug()` |
| `src/ble/scales/skalescale.cpp:9` | `SKALE_LOG` | `qDebug()` |
| `src/ble/scales/smartchefscale.cpp:9` | `SMARTCHEF_LOG` | `qDebug()` |
| `src/ble/scales/variaakuscale.cpp:8` | `VARIA_LOG` | `qDebug()` |
| `src/ble/scales/atomhearteclairscale.cpp:9` | `ECLAIR_LOG` | `qDebug()` |
| `src/ble/scales/felicitascale.cpp:9` | `FELICITA_LOG` | `qDebug()` |

**Compliant exception:** `src/ble/scales/decentscale.cpp` uses `DECENT_LOG` → `qDebug()` for informational messages, but correctly uses direct `qWarning()` calls for error conditions (lines 66, 74, 202).

**Fix:** Add a `*_WARN` macro mapping to `qWarning()` for each scale, or use dual macros (`*_LOG` / `*_ERR`).

### 5b. BLE write timeout has zero logging (`bletransport.cpp`)

CLAUDE.md documents these specific log messages:
```
DE1Device: BLE write TIMEOUT after 5000 ms - uuid: 0000a00f data: 0102...
DE1Device: Retrying after timeout (1/3)
DE1Device: Write FAILED (timeout) after 3 retries - uuid: 0000a00f data: 0102...
```

**None of these messages exist in the code.** The timeout lambda (`src/ble/bletransport.cpp:92-108`) and errorOccurred handler (`src/ble/bletransport.cpp:303-335`) contain no `qDebug()` or `qWarning()` calls at all.

- [ ] `src/ble/bletransport.cpp:92-108` — Timeout lambda: no logging on timeout, retry, or max-retry-failure
- [ ] `src/ble/bletransport.cpp:303-335` — `onControllerError()`: emits signal but no `qWarning()` log

### 5c. Scale connection timeout uses `qDebug` not `qWarning`

- [ ] `src/ble/blemanager.cpp:399` — `qDebug() << "BLEManager: Scale connection timeout - not found"` — this is an error condition, should use `qWarning()`

### 5d. Commented-out log statement

- [ ] `src/ble/bletransport.cpp:235` — `clearQueue` log is commented out: `// qDebug() << "BleTransport::clearQueue: Cleared" << cleared << "pending commands"`

---

## 6. Dead Code / Commented-Out Code

These are not rule violations but reduce code maintainability.

- [ ] `src/screensaver/screensavervideomanager.cpp` — ~80 commented-out `qDebug()`/`qWarning()` lines throughout the file (e.g., lines 46, 97, 124, 253, 504, 715)
- [ ] `src/ble/bletransport.cpp:235` — Commented-out clearQueue log (also listed in 5d)
- [ ] `src/core/translationmanager.cpp:2231-2232` — Two AI providers disabled via comments: Gemini ("aggressive rate limiting") and Ollama

---

## No Violations Found

- **Q_PROPERTY without NOTIFY** — All non-CONSTANT `Q_PROPERTY` declarations have `NOTIFY` signals. Checked: `machinestate.h`, `maincontroller.h`, `settings.h`, `de1device.h`.
- **Profile system (weight exit independence)** — Weight exit is correctly independent of `exitIf` in `weightprocessor.cpp`.
- **BLE write retry pattern** — Timeout (5s) and retry count (3) match CLAUDE.md spec. Missing log messages are covered in Category 5.

---

## Priority Guide

| Priority | Category | Count | Section | Status |
|----------|----------|-------|---------|--------|
| **High** | Main-thread I/O | 30+ methods, 6 call sites | 3 | Open |
| **High** | ShotServer JS fetch missing `.catch()` | 7 | 4a | Open |
| **High** | ShotServer JS fetch missing `r.ok` check | 21 | 4b | Open |
| **High** | BLE write timeout has zero logging | 2 handlers | 5b | Open |
| **Medium** | Timer as guard/workaround | 9 instances | 2 | Open |
| **Medium** | Scale LOG macros route errors to `qDebug()` | 10 files | 5a | Open |
| **Medium** | Scale connection timeout uses `qDebug` | 1 | 5c | Open |
| **Low** | Slot naming convention | ~20 slots across 11 files | 1a | Open |
| **Low** | Class naming (USBManager) | 1 | 1b | Open |
| **Low** | Member variable missing `m_` prefix | 1 | 1c | Open |
| **Low** | Class/filename spelling inconsistency | 1 | 1d | Open |
| **Low** | Dead / commented-out code | 3 areas | 6 | Open |
| **Low** | Commented-out log statement | 1 | 5d | Open |
