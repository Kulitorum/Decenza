# CLAUDE.md C++ Compliance Audit

Last updated: 2026-03-05 (originally audited 2026-03-01)

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

**Additional violations found 2026-03-04** (timer/signal-connected slots that clearly should use `on*`):

- [ ] `src/core/memorymonitor.h` — `takeSample()` (private slot, timer-connected → `onSampleTimer()`)
- [ ] `src/simulator/de1simulator.h` — `simulationTick()` (private slot, timer-connected → `onSimulationTick()`)
- [ ] `src/ble/bletransport.h` — `processCommandQueue()` (private slot → `onProcessCommandQueue()`)
- [ ] `src/network/shotserver.h` — `cleanupStaleConnections()` (private slot, timer-driven → `onCleanupTimer()`)
- [ ] `src/profile/profileconverter.h` — `processNextFile()` (private slot → `onProcessNextFile()`)
- [ ] `src/profile/profileimporter.h` — `processNextScan()`, `processNextImport()` (private slots → `onProcessNext*()`)
- [ ] `src/history/shotimporter.h` — `processNextFile()` (private slot → `onProcessNextFile()`)

**Note:** Some are setter-style slots (`setChargingMode`) or data-model API slots (`addSample`, `clear`) where the `on*` convention reads poorly. Consider renaming only where the slot is clearly a signal/timer handler. Public API slots in `maincontroller.h` (`loadProfile()`, `refreshProfiles()`, etc.) are imperative commands invoked from QML, not event handlers — these are acceptable as-is.

### 1b. Class naming — PascalCase

CLAUDE.md: "Classes: `PascalCase`"

- [ ] `src/usb/usbmanager.h:27` — `USBManager` uses all-caps abbreviation; should be `UsbManager`

### 1c. Member variable missing `m_` prefix

CLAUDE.md: "Members: `m_` prefix"

- [ ] `src/ble/transport/corebluetooth/corebluetoothscalebletransport.h:48` — `Impl* d = nullptr` should be `m_impl` (PIMPL pointer with public access for ObjC delegate)

### 1d. Class/filename spelling inconsistency

- [x] `src/ble/scales/solobaristascale.h:6` — Class was `SoloBarristaScale` (double-r "Barrista"), renamed to `SoloBaristaScale`. Updated header, cpp, and scalefactory.cpp. *(fixed in cpp-compliance-audit-continued)*

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

- [x] `src/main.cpp:1501` — `QTimer::singleShot(2000, &waitLoop, ...)` replaced with `DE1Transport::queueDrained` signal. Event loop quits when queue drains or on 2s safety timeout. Scale-only exit uses 500ms wait (no command queue). *(fixed in cpp-compliance-audit-3a)*

### Fixed (cpp-compliance-audit)

- [x] `src/controllers/maincontroller.cpp` — `m_settingsTimer` (1000ms) removed. `initialSettingsComplete` now connects directly to `applyAllSettings`. BleTransport's FIFO queue already guarantees ordering.
- [x] `src/weather/weathermanager.cpp` — `QTimer::singleShot(2000, ...)` replaced with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Init is already complete at the call site; the 2s delay served no purpose.
- [x] `src/machine/machinestate.cpp` — `QTimer::singleShot(0, ...)` replaced with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Semantically identical (deferred event dispatch), but the intent is now clear without the misleading "Timer" framing.

---

## 3. Main-Thread I/O (6 call sites, 30+ methods)

CLAUDE.md: "Never run database or disk I/O on the main thread. Use `QThread::create()` with a `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` callback."

Many async equivalents already exist (`requestShotsFiltered()`, `requestShot()`, etc.) but the synchronous versions are still called from the main thread.

### 3a. ShotHistoryStorage synchronous Q_INVOKABLE methods

All declared in `src/history/shothistorystorage.h` and callable from QML on the main thread.

**Confirmed called from QML on the main thread (2026-03-04 re-audit):**

| Method | QML Callers | Status |
|--------|-------------|--------|
| `getShots()` | `LastShotItem.qml` | **Fixed** (PR #362) — uses `requestMostRecentShotId()` |
| `getShot()` | `PostShotReviewPage.qml` | **Fixed** (PR #362) — uses `editShotData` + `Object.assign` |
| `getDistinctBeanBrands()` | `BrewDialog.qml`, `PostShotReviewPage.qml`, `BeanInfoPage.qml` | **Mitigated** (PR #362) — async cache pre-warm; sync fallback only on cold cache |
| `getDistinctBeanTypesForBrand()` | `BrewDialog.qml`, `PostShotReviewPage.qml`, `BeanInfoPage.qml` | **Mitigated** (PR #362) — async cache pre-warm; sync fallback on filtered cache miss |
| `getDistinctGrinders()` | `BrewDialog.qml`, `PostShotReviewPage.qml`, `BeanInfoPage.qml` | **Mitigated** (PR #362) — async cache pre-warm; sync fallback only on cold cache |
| `getDistinctGrinderSettingsForGrinder()` | `BrewDialog.qml`, `PostShotReviewPage.qml`, `BeanInfoPage.qml` | **Mitigated** (PR #362) — async cache pre-warm; sync fallback on filtered cache miss |
| `getDistinctBaristas()` | `PostShotReviewPage.qml`, `BeanInfoPage.qml` | **Mitigated** (PR #362) — async cache pre-warm; sync fallback only on cold cache |
| `updateVisualizerInfo()` | `PostShotReviewPage.qml`, `ShotDetailPage.qml` | **Fixed** (PR #362) — uses `requestUpdateVisualizerInfo()` |

**Declared synchronous but no direct QML callers found (called from C++ or unused):**

- [ ] `getShotsFiltered(...)` — has async counterpart `requestShotsFiltered()`
- [ ] `getShotTimestamp(qint64 shotId)` — blocking SQL SELECT
- [ ] `getShotRecord(qint64 shotId)` — C++ return type, not Q_INVOKABLE
- [ ] `getShotsForComparison(...)` — blocking batch full-record load loop
- [ ] `deleteShot(qint64 shotId)` — blocking SQL DELETE; batch `deleteShots()` is async
- [ ] `updateShotMetadata(...)` — has async counterpart `requestUpdateShotMetadata()`
- [ ] `getDistinctProfiles()`, `getDistinctBeanTypes()`, `getDistinctGrinderSettings()`, `getDistinctRoastLevels()` — synchronous with caching
- [ ] `getDistinctProfilesFiltered()`, `getDistinctBeanBrandsFiltered()`, `getDistinctBeanTypesFiltered()` — synchronous
- [ ] `getFilteredShotCount(...)` — blocking SQL SELECT COUNT
- [ ] `getAutoFavorites(...)` — has async counterpart `requestAutoFavorites()`
- [ ] `getAutoFavoriteGroupDetails(...)` — has async counterpart `requestAutoFavoriteGroupDetails()`
- [ ] `exportShotData(...)` — blocking full record load + JSON serialization
- [ ] `createBackup(...)` — blocking WAL checkpoint + file copy; has async `requestCreateBackup()`
- [ ] `importDatabase(...)` — blocking full DB import; has async `requestImportDatabase()`

**Mitigating factors:**
- The `getDistinct*()` methods have an in-memory cache (`m_distinctCache`) pre-warmed asynchronously at init and refreshed async on invalidation. Sync fallback only occurs on filtered cache miss (composite keys like `bean_type:<brand>`), which are fast indexed queries cached after first access.
- `getShots()`, `getShot()`, and `updateVisualizerInfo()` QML callers have been fully migrated to async counterparts.

**Remaining work:** The `getDistinct*()` filtered cache-miss sync fallback is the only remaining gap. Low priority — the queries are fast (single-column DISTINCT with WHERE on indexed column) and cached until the next invalidation.

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

**Fixed (cpp-compliance-audit-continued):** Both `loadRecentShots()` and `loadCurrentShot()` now use `QThread::create()` + `loadShotRecordStatic()` + `QMetaObject::invokeMethod` pattern. Added `loading` property for QML BusyIndicator and `m_destroyed` shared_ptr for safety. Nav buttons disabled while loading.

- [x] `src/models/flowcalibrationmodel.cpp` — `loadRecentShots()` runs SQL + record loads on background thread
- [x] `src/models/flowcalibrationmodel.cpp` — `loadCurrentShot()` runs `loadShotRecordStatic()` on background thread
- [x] `qml/pages/FlowCalibrationPage.qml` — Added BusyIndicator, disabled nav buttons while loading

### 3e. AIManager getRecentShotContext

**Fixed (cpp-compliance-audit):** Renamed to `requestRecentShotContext()`. All DB work (timestamp lookup, filtered query, full record loads, summary generation) runs on a background thread with its own DB connection. Emits `recentShotContextReady(QString)` on main thread. QML overlay opens immediately and receives context asynchronously via `Connections` handler.

- [x] `src/ai/aimanager.cpp` — `getRecentShotContext()` → `requestRecentShotContext()` with background thread
- [x] `qml/components/ConversationOverlay.qml` — Both callers updated to use async method + signal

---

## 4. ShotServer JavaScript Fetch Conventions

**Status (2026-03-04 re-audit): All 28 fetch() calls across shotserver_*.cpp are compliant. No new violations.**

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

---

## 5. BLE Error Logging (4 violation areas)

### 5a. Scale `*_LOG` macros route errors to `qDebug()`

CLAUDE.md: "BLE errors are automatically captured (use `qWarning()` for errors)."

**Fixed (cpp-compliance-audit-continued):** Added `*_WARN` macro (using `qWarning()`) to each scale file alongside existing `*_LOG` macro. Switched all error-condition calls (transport errors, service not found, init failures, checksum failures, watchdog timeouts) to use `*_WARN`.

| File | WARN Macro | Error calls switched |
|------|-----------|---------------------|
| `src/ble/scales/acaiascale.cpp` | `ACAIA_WARN` | Transport error, no compatible service, init failed after retries |
| `src/ble/scales/bookooscale.cpp` | `BOOKOO_WARN` | Transport error, service not found |
| `src/ble/scales/difluidscale.cpp` | `DIFLUID_WARN` | Transport error, service not found |
| `src/ble/scales/eurekaprecisascale.cpp` | `EUREKA_WARN` | Transport error, service not found |
| `src/ble/scales/hiroiascale.cpp` | `HIROIA_WARN` | Transport error, service not found |
| `src/ble/scales/skalescale.cpp` | `SKALE_WARN` | Transport error, service not found |
| `src/ble/scales/smartchefscale.cpp` | `SMARTCHEF_WARN` | Transport error, service not found |
| `src/ble/scales/variaakuscale.cpp` | `VARIA_WARN` | 9 error calls (transport, service, watchdog, tickle, notifications) |
| `src/ble/scales/atomhearteclairscale.cpp` | `ECLAIR_WARN` | Transport error, service not found, XOR checksum failed |
| `src/ble/scales/felicitascale.cpp` | `FELICITA_WARN` | Transport error, service not found |

**Minor inconsistency (2026-03-04):** `src/ble/scales/decentscale.cpp` uses its own `DECENT_LOG` macro instead of the shared `scalelogging.h` macros and has no `DECENT_WARN` macro. It uses direct `qWarning()` for transport errors (line 74) but the "service not found" path (line 86-88) only calls `emit errorOccurred()` without a warning log.

### 5b. BLE write timeout logging uses `qDebug` instead of `qWarning` (`bletransport.cpp`)

**Fix applied (cpp-compliance-audit):** Added `warn()` method using `qWarning()` and switched all error paths to use it:

- [x] `src/ble/bletransport.cpp` — Write FAILED after retries: `log()` → `warn()`
- [x] `src/ble/bletransport.cpp` — `onControllerError()`: `log()` → `warn()`
- [x] `src/ble/bletransport.cpp` — CharacteristicWriteError FAILED: `log()` → `warn()`
- [x] `src/ble/bletransport.cpp` — createServiceObject null: `log()` → `warn()`
- [x] `src/ble/bletransport.cpp` — Failed to create BLE controller: `log()` → `warn()`
- [x] `src/ble/bletransport.cpp` — Retry abandoned: `log()` → `warn()`
- [x] `src/ble/bletransport.cpp` — subscribe FAILED (CCCD not found): `log()` → `warn()`

### 5c. Scale connection timeout uses `qDebug` not `qWarning`

- [x] `src/ble/blemanager.cpp:399` — `qDebug()` → `qWarning()` for scale connection timeout *(fixed in cpp-compliance-audit-continued)*

### 5d. Commented-out log statement

- [ ] `src/ble/bletransport.cpp:242` — `clearQueue` log is commented out: `// qDebug() << "BleTransport::clearQueue: Cleared" << cleared << "pending commands"`

---

## 6. Dead Code / Commented-Out Code

These are not rule violations but reduce code maintainability.

- [ ] `src/screensaver/screensavervideomanager.cpp` — ~72 commented-out `qDebug()`/`qWarning()` lines throughout the file
- [ ] `src/ble/bletransport.cpp:242` — Commented-out clearQueue log (also listed in 5d)
- [ ] `src/core/translationmanager.cpp:2231-2232` — Gemini and Ollama disabled in auto-discovery provider list via comments. The actual implementation code is active and works when explicitly selected by the user — only auto-detection is disabled.

---

## No Violations Found

These areas were verified clean on 2026-03-04:

- **Q_PROPERTY without NOTIFY** — All non-CONSTANT `Q_PROPERTY` declarations have `NOTIFY` signals. Verified across all header files including recently added `mqttclient.h`, `flowcalibrationmodel.h`, `solobaristascale.h`.
- **Profile system (weight exit independence)** — Weight exit is correctly independent of `exitIf` in `weightprocessor.cpp` (line 179-184). Checks `exitWeight > 0 && weight >= exitWeight` with no coupling to machine-side exit flags.
- **BLE write retry pattern** — Timeout (5s) and retry count (3) match CLAUDE.md spec.
- **MQTT conventions** — `mqttclient.h/cpp` uses correct naming (`PascalCase` class, `camelCase` methods, `m_` prefixes, `on*` slots for signal handlers). Timers are used for genuinely periodic tasks (`m_publishTimer`) and reconnection with exponential backoff (`m_reconnectTimer`). Paho callbacks use `Qt::QueuedConnection` signals to marshal to the main thread.
- **SettingsSerializer thread safety** — `exportToJson()`/`importFromJson()` run on the main thread (correct — `Settings` wraps `QSettings` which uses platform APIs). Heavy I/O in backup workflows is properly offloaded to `QThread::create()`.
- **ShotServer JS fetch conventions** — All 28 `fetch()` calls across `shotserver_shots.cpp`, `shotserver_settings.cpp`, and `shotserver_layout.cpp` have `.catch()` handlers and check `r.ok`/`resp.ok` before `.json()`.
- **Scale error logging** — All scale files use `*_WARN` macros (via `qWarning()`) for error conditions. No regressions.
- **New files** — `src/core/dbutils.h`, `src/ble/scales/scalelogging.h`, `src/ble/scales/solobaristascale.h`, `src/models/flowcalibrationmodel.h` — all clean.

---

## Priority Guide

| Priority | Category | Count | Section | Status |
|----------|----------|-------|---------|--------|
| **High** | Main-thread I/O (QML-facing): AI context | 1 caller | 3e | **Fixed** |
| **High** | Main-thread I/O (QML-facing): shot metadata | 1 caller | 3c | **Fixed** |
| **High** | Main-thread I/O (QML-facing): flow calibration | 1 caller | 3d | **Fixed** |
| **High** | Timer guards (fixable) | 3 instances | 2 | **Fixed** |
| **Medium** | Main-thread I/O (QML sync callers) | 8 confirmed callers | 3a | **Mostly fixed** (PR #362) — 3 fully async, 5 mitigated via cache |
| **Medium** | Timer guards (platform constraints) | 5+1 instances | 2 | Accepted / 1 **Fixed** (queueDrained) |
| **Medium** | Main-thread I/O (ShotServer) | 10 call sites | 3b | **Fixed** |
| **Medium** | Scale LOG macros route errors to `qDebug()` | 10 files | 5a | **Fixed** |
| **Medium** | Scale connection timeout uses `qDebug` | 1 | 5c | **Fixed** |
| **Low** | ShotServer JS fetch missing `.catch()` | 7 | 4a | **Fixed** |
| **Low** | ShotServer JS fetch missing `r.ok` check | 21 | 4b | **Fixed** |
| **Low** | BLE write timeout logging level | 7 paths | 5b | **Fixed** |
| **Low** | Slot naming convention | ~27 slots across 18 files | 1a | Open |
| **Low** | Class naming (USBManager) | 1 | 1b | Open |
| **Low** | Member variable missing `m_` prefix | 1 | 1c | Open |
| **Low** | Class/filename spelling inconsistency | 1 | 1d | **Fixed** |
| **Low** | Dead / commented-out code | 3 areas | 6 | Open |
| **Low** | Commented-out log statement | 1 | 5d | Open |
| **Low** | DecentScale missing `DECENT_WARN` macro | 1 file | 5a | Open |

### Priority rationale

- **High = directly affects primary touch UI.** Sections 3c/3d/3e blocked the QML UI thread during user interactions (loading shots, flow calibration, AI queries) — all now fixed.
- **Medium = affects secondary interfaces, mitigated, or developer experience.** Section 3a had 8 confirmed QML callers — 3 fully migrated to async (PR #362), 5 `getDistinct*()` methods mitigated via async cache pre-warming with sync fallback only on filtered cache miss. Timer platform constraints (§2) have no event-based alternative — accepted. ShotServer async (§3b) only stalls the web UI. Scale log macros (§5a) and connection timeout (§5c) now use `qWarning()` for errors.
- **Low = correctness improvements with minimal user impact.** JS fetch fixes protect against edge cases on a localhost server. BLE log levels, naming conventions, and dead code removal are hygiene.

### Recommended next steps

1. **Slot naming cleanup** (§1a, Low): Rename timer/signal-connected slots to `on*` pattern. Skip public API slots where the convention reads poorly.
2. **Dead code cleanup** (§6, Low): Remove commented-out debug logs and disabled provider entries.
