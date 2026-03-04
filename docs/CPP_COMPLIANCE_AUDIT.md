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

## 2. Timer as Guard/Workaround

CLAUDE.md: "Never use timers as guards/workarounds. Timers are fragile heuristics that break on slow devices and hide the real problem. Use event-based flags and conditions instead."

### Platform constraints (no event-based alternative exists)

These use timers because Android/iOS BLE stacks provide no "ready after wake/resume" signal. The `BleTransport` retry mechanism (3 retries, 2s each) provides a safety net. These are accepted as unfixable.

- `src/main.cpp` — `QTimer::singleShot(500, ...)` to reconnect DE1 after auto-wake
- `src/main.cpp` — `QTimer::singleShot(500, ...)` to reconnect scale after auto-wake
- `src/main.cpp` — `QTimer::singleShot(500, ...)` to reconnect DE1 after app resume
- `src/main.cpp` — `QTimer::singleShot(500, &waitLoop, ...)` blocks 500ms before app suspend (OS provides no "OK to suspend" callback; de1app uses same pattern)
- `src/main.cpp` — Scale reconnect after resume uses `scaleReconnectTimer` with exponential backoff — this is a legitimate retry pattern, not a guard

### Platform constraint (improvable)

- [ ] `src/main.cpp` — `QTimer::singleShot(2000, &waitLoop, ...)` blocks 2s before exit for BLE writes. Could be improved by adding a `queueDrained` signal to `BleTransport` (emit when `m_commandQueue.isEmpty() && !m_writePending`), then quit the event loop on that signal or on a shorter timeout.

### Fixed (cpp-compliance-audit)

- [x] `src/controllers/maincontroller.cpp` — `m_settingsTimer` (1000ms) removed. `initialSettingsComplete` now connects directly to `applyAllSettings`. BleTransport's FIFO queue already guarantees ordering.
- [x] `src/weather/weathermanager.cpp` — `QTimer::singleShot(2000, ...)` replaced with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Init is already complete at the call site; the 2s delay served no purpose.
- [x] `src/machine/machinestate.cpp` — `QTimer::singleShot(0, ...)` replaced with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Semantically identical (deferred event dispatch), but the intent is now clear without the misleading "Timer" framing.

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

`ShotServer` lives on the main thread (created in `MainController` constructor). All request handlers were blocking the main thread.

**Fixed (cpp-compliance-audit):** Converted all route handlers to use `QThread::create()` + `QPointer<QTcpSocket>` + `QMetaObject::invokeMethod` pattern:

- [x] `src/network/shotserver.cpp` — `getShot()` in profile.json download handler → background thread with `loadShotRecordStatic()`
- [x] `src/network/shotserver.cpp` — `getShots(0, 1000)` in `/api/shots` handler → background thread with inline SQL
- [x] `src/network/shotserver.cpp` — `updateShotMetadata()` in POST handler → background thread with inline SQL
- [x] `src/network/shotserver.cpp` — `getShot()` in GET `/api/shot/:id` handler → background thread with `loadShotRecordStatic()`
- [x] `src/network/shotserver.cpp` — `deleteShot()` in batch delete handler → background thread with inline SQL
- [x] `src/network/shotserver.cpp` — `checkpoint()` + `sendFile()` in DB download → background thread
- [x] `src/network/shotserver.cpp` — `createBackup()` + `sendFile()` in backup download → background thread with `createBackupStatic()`
- [x] `src/network/shotserver.cpp` — Shot list page route → background thread for data fetch
- [x] `src/network/shotserver.cpp` — Shot detail page route → background thread with `loadShotRecordStatic()`
- [x] `src/network/shotserver.cpp` — Comparison page route → background thread with `loadShotRecordStatic()` loop

**Note:** `generateShotListPage()`, `generateShotDetailPage()`, and `generateComparisonPage()` now have overloads accepting pre-fetched data. The sync overloads remain for backward compatibility but route handlers use the async path.

**Note:** `shotserver_backup.cpp:479` correctly uses `QThread::create()` for its WAL checkpoint — not a violation.

### 3c. MainController loadShotWithMetadata

**Fixed (cpp-compliance-audit):** Converted to `QThread::create()` + `loadShotRecordStatic()` + `QMetaObject::invokeMethod` pattern. DB load runs on background thread; metadata application (profile load, DYE settings, BLE upload) happens on main thread via callback.

- [x] `src/controllers/maincontroller.cpp` — `loadShotWithMetadata()` now async, `applyLoadedShotMetadata()` handles main-thread work

### 3d. FlowCalibrationModel loadRecentShots

- [ ] `src/models/flowcalibrationmodel.cpp:41` — `getShots(0, 50)` on main thread
- [ ] `src/models/flowcalibrationmodel.cpp:45` — `getShotRecord()` called up to 50 times in a loop on main thread
- [ ] `src/models/flowcalibrationmodel.cpp:104` — `getShotRecord()` in `loadCurrentShot()` on main thread

### 3e. AIManager getRecentShotContext

**Fixed (cpp-compliance-audit):** Renamed to `requestRecentShotContext()`. All DB work (timestamp lookup, filtered query, full record loads, summary generation) runs on a background thread with its own DB connection. Emits `recentShotContextReady(QString)` on main thread. QML overlay opens immediately and receives context asynchronously via `Connections` handler.

- [x] `src/ai/aimanager.cpp` — `getRecentShotContext()` → `requestRecentShotContext()` with background thread
- [x] `qml/components/ConversationOverlay.qml` — Both callers updated to use async method + signal

---

## 4. ShotServer JavaScript Fetch Conventions

### 4a. fetch() missing .catch() handler

CLAUDE.md: "Every `fetch()` must have a `.catch()` handler. Never leave a fetch chain without error handling."

All in `src/network/shotserver_shots.cpp`:

- [x] Line 1026 — `togglePower()` (shot list page) — added `.catch()` *(fixed in cpp-compliance-audit)*
- [x] Line 2112 — `togglePower()` (shot detail page) — added `.catch()` *(fixed in cpp-compliance-audit)*
- [x] Line 3071 — `togglePower()` (comparison page) — added `.catch()` *(fixed in cpp-compliance-audit)*
- [x] Line 3584 — `fetchLogs()` — added `.catch()` *(fixed in cpp-compliance-audit)*
- [x] Line 3275 — `clearLog()` — already had `.catch()` (audit was stale)
- [x] Line 3284 — `clearAll()` — already had `.catch()` (audit was stale)
- [x] Line 3310 — `loadPersistedLog()` — already had `.catch()` (audit was stale)

### 4b. Missing `r.ok` check before `.json()`

CLAUDE.md: "Check `r.ok` before `r.json()` in fetch chains. Non-2xx responses with non-JSON bodies will throw on `.json()` and produce a misleading error."

**In `src/network/shotserver_shots.cpp`:** *(all fixed in cpp-compliance-audit)*

- [x] Line 705 — `deleteSelected()` — added `r.ok` check
- [x] Line 845 — `loadSavedSearches()` — added `r.ok` check
- [x] Line 858 — `saveSearch()` — added `r.ok` check
- [x] Line 873 — `deleteSavedSearch()` — added `r.ok` check
- [x] Line 1018 — `fetchPowerState()` (shot list) — added `r.ok` check
- [x] Line 1026 — `togglePower()` (shot list) — added `r.ok` check
- [x] Line 1766 — metadata save (shot detail) — added `r.ok` check
- [x] Line 2105 — `fetchPowerState()` (shot detail) — added `r.ok` check
- [x] Line 2112 — `togglePower()` (shot detail) — added `r.ok` check
- [x] Line 3064 — `fetchPowerState()` (comparison) — added `r.ok` check
- [x] Line 3071 — `togglePower()` (comparison) — added `r.ok` check
- [x] Line 3584 — `fetchLogs()` — added `r.ok` check
- [x] Line 3310 — `loadPersistedLog()` — already had `r.ok` check (audit was stale)

**In `src/network/shotserver_settings.cpp`:** *(all fixed in cpp-compliance-audit)*

- [x] Line 559 — `loadSettings()` — added `resp.ok` check
- [x] Line 656 — `saveVisualizer()` — added `resp.ok` check
- [x] Line 674 — `testVisualizer()` — added `resp.ok` check
- [x] Line 693 — `saveAi()` — added `resp.ok` check
- [x] Line 717 — `testAi()` — added `resp.ok` check
- [x] Line 742 — `saveMqtt()` — added `resp.ok` check
- [x] Line 789 — `connectMqtt()` — added `resp.ok` check
- [x] Line 810 — `publishDiscovery()` — moved `resp.ok` check before `.json()`

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

### 5b. BLE write timeout logging uses `qDebug` instead of `qWarning` (`bletransport.cpp`)

**Note:** The original audit was stale — logging IS present via `log()` calls (e.g., "Write timeout, retrying 1/3", "Write FAILED after 3 retries", "!!! CONTROLLER ERROR: ... !!!"). However, `log()` routed all messages through `qDebug()`.

**Fix applied (cpp-compliance-audit):** Added `warn()` method using `qWarning()` and switched all error paths to use it:

- [x] `src/ble/bletransport.cpp` — Write FAILED after retries: `log()` → `warn()`
- [x] `src/ble/bletransport.cpp` — `onControllerError()`: `log()` → `warn()`
- [x] `src/ble/bletransport.cpp` — CharacteristicWriteError FAILED: `log()` → `warn()`
- [x] `src/ble/bletransport.cpp` — createServiceObject null: `log()` → `warn()`
- [x] `src/ble/bletransport.cpp` — Failed to create BLE controller: `log()` → `warn()`
- [x] `src/ble/bletransport.cpp` — Retry abandoned: `log()` → `warn()`
- [x] `src/ble/bletransport.cpp` — subscribe FAILED (CCCD not found): `log()` → `warn()`

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
| **High** | Main-thread I/O (QML-facing): AI context | 1 caller | 3e | **Fixed** |
| **High** | Main-thread I/O (QML-facing): shot metadata | 1 caller | 3c | **Fixed** |
| **High** | Main-thread I/O (QML-facing): flow calibration | 1 caller | 3d | Open |
| **High** | Timer guards (fixable) | 3 instances | 2 | **Fixed** |
| **Medium** | Timer guards (platform constraints) | 5+1 instances | 2 | Accepted / 1 improvable |
| **Medium** | Main-thread I/O (ShotServer) | 10 call sites | 3b | **Fixed** |
| **Medium** | Main-thread I/O (sync Q_INVOKABLEs) | 30+ methods | 3a | Open (mostly mitigated by caching) |
| **Medium** | Scale LOG macros route errors to `qDebug()` | 10 files | 5a | Open |
| **Medium** | Scale connection timeout uses `qDebug` | 1 | 5c | Open |
| **Low** | ShotServer JS fetch missing `.catch()` | 7 | 4a | **Fixed** |
| **Low** | ShotServer JS fetch missing `r.ok` check | 21 | 4b | **Fixed** |
| **Low** | BLE write timeout logging level | 7 paths | 5b | **Fixed** |
| **Low** | Slot naming convention | ~20 slots across 11 files | 1a | Open |
| **Low** | Class naming (USBManager) | 1 | 1b | Open |
| **Low** | Member variable missing `m_` prefix | 1 | 1c | Open |
| **Low** | Class/filename spelling inconsistency | 1 | 1d | Open |
| **Low** | Dead / commented-out code | 3 areas | 6 | Open |
| **Low** | Commented-out log statement | 1 | 5d | Open |

### Priority rationale

- **High = directly affects primary touch UI.** Sections 3c/3e blocked the QML UI thread during user interactions (loading shots, AI queries) — now fixed. Section 3d (FlowCalibrationModel) can freeze up to 1s on page open but is an infrequent workflow requiring a DB schema change.
- **Medium = affects secondary interfaces, mitigated, or developer experience.** Timer platform constraints (§2) have no event-based alternative — accepted. ShotHistoryStorage sync Q_INVOKABLEs (§3a) are mostly mitigated by in-memory `m_distinctCache`; the remaining ones (`getShot()` in PostShotReview) are low-frequency. ShotServer async (§3b) only stalls the web UI. Scale log macros (§5a) affect debugging on real hardware.
- **Low = correctness improvements with minimal user impact.** JS fetch fixes protect against edge cases on a localhost server. BLE log levels and naming conventions are hygiene.
