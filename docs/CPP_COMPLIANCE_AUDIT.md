# CLAUDE.md C++ Compliance Audit

Last updated: 2026-03-05 (originally audited 2026-03-01, cleanup pass 2026-03-05)

This document tracks C++ code violations of the conventions defined in CLAUDE.md.
Work through each category and check off items as they are fixed.

---

## 1. Naming Conventions

### 1a. Slot naming — must follow `onEventName()` pattern

CLAUDE.md: "Slots: `onEventName()`"

Slots declared in `public slots:` or `private slots:` sections that do not follow the `on*` naming convention:

- [x] `src/core/accessibilitymanager.h` — `updateTtsLocale()` → `onLanguageChanged()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/machine/machinestate.h` — `updateShotTimer()` → `onShotTimerTick()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/models/shotdatamodel.h` — `flushToChart()` → `onFlushTimerTick()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/controllers/shottimingcontroller.h` — `updateDisplayTimer()` → `onDisplayTimerTick()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/screensaver/strangeattractorrenderer.h` — `iterate()` → `onRenderTick()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/network/mqttclient.h` — `publishTelemetry()` → `onPublishTimerTick()`, `attemptReconnect()` → `onReconnectTimerTick()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/usb/usbmanager.h` — `pollPorts()` → `onPollTimerTick()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/usb/usbscalemanager.h` — `pollPorts()` → `onPollTimerTick()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/core/memorymonitor.h` — `takeSample()` → `onSampleTimerTick()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/simulator/de1simulator.h` — `simulationTick()` → `onSimulationTimerTick()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/network/shotserver.h` — `cleanupStaleConnections()` → `onCleanupTimerTick()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/profile/profileconverter.h` — `processNextFile()` → `onProcessNextFile()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/profile/profileimporter.h` — `processNextScan()` → `onProcessNextScan()`, `processNextImport()` → `onProcessNextImport()` *(fixed in cpp-compliance-cleanup)*
- [x] `src/history/shotimporter.h` — `processNextFile()` → `onProcessNextFile()` *(fixed in cpp-compliance-cleanup)*

**Accepted as-is** (public API / setter-style / data-model slots where `on*` reads poorly):

- `src/core/batterymanager.h` — `setChargingMode()` (setter), `checkBattery()` (mixed: timer handler + public API)
- `src/models/shotdatamodel.h` — `clear()`, `clearWeightData()`, `addSample()`, `addWeightSample()`, `markExtractionStart()`, `markStopAt()`, `smoothWeightFlowRate()`, `addPhaseMarker()` (data ingestion API)
- `src/network/mqttclient.h` — `publishState()` (event-driven publish, not timer handler)
- `src/ble/blemanager.h` — `stopScan()`, `clearDevices()` (public API called from main.cpp)
- `src/ble/bletransport.h` — `processCommandQueue()` (mixed: timer handler + called from write/error paths)

### 1b. Class naming — PascalCase

CLAUDE.md: "Classes: `PascalCase`"

- Accepted: `src/usb/usbmanager.h` — `USBManager` keeps all-caps `USB` abbreviation (widely recognized acronym; Qt examples vary: `QUrl` vs `QIODevice`)

### 1c. Member variable missing `m_` prefix

CLAUDE.md: "Members: `m_` prefix"

- [x] `src/ble/transport/corebluetooth/corebluetoothscalebletransport.h:48` — `Impl* d` → `m_impl` (PIMPL pointer; ObjC delegate callbacks use local `auto* d = self.impl` unchanged) *(fixed in cpp-compliance-cleanup)*

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
| `getDistinctBeanBrands()` | `BrewDialog.qml`, `PostShotReviewPage.qml`, `BeanInfoPage.qml` | **Fixed** — cache-only, returns `{}` on miss and triggers async `requestDistinctValueAsync()` |
| `getDistinctBeanTypesForBrand()` | `BrewDialog.qml`, `PostShotReviewPage.qml`, `BeanInfoPage.qml` | **Fixed** — cache-only with async miss handler |
| `getDistinctGrinderBrands()` | `BrewDialog.qml`, `PostShotReviewPage.qml`, `BeanInfoPage.qml` | **Fixed** — pre-warmed by `requestDistinctCache()`, cache-only on access |
| `getDistinctGrinderModelsForBrand()` | `BrewDialog.qml`, `PostShotReviewPage.qml`, `BeanInfoPage.qml` | **Fixed** — cache-only with async miss handler |
| `getDistinctGrinderBurrsForModel()` | `BrewDialog.qml`, `PostShotReviewPage.qml`, `BeanInfoPage.qml` | **Fixed** — cache-only with async miss handler |
| `getDistinctGrinderSettingsForGrinder()` | `BrewDialog.qml`, `PostShotReviewPage.qml`, `BeanInfoPage.qml` | **Fixed** — cache-only with async miss handler |
| `getDistinctBaristas()` | `PostShotReviewPage.qml`, `BeanInfoPage.qml` | **Fixed** — cache-only, returns `{}` on miss and triggers async fetch |
| `updateVisualizerInfo()` | `PostShotReviewPage.qml`, `ShotDetailPage.qml` | **Fixed** (PR #362) — uses `requestUpdateVisualizerInfo()` |

**Dead synchronous methods removed (zero callers):**

- [x] `getShots()`, `getShotsFiltered()`, `getShotTimestamp()`, `getShot()`, `getShotRecord()`, `getShotsForComparison()` — removed (async equivalents exist)
- [x] `updateShotMetadata()`, `updateVisualizerInfo()` — removed (async equivalents exist)
- [x] `getDistinctProfiles()`, `getDistinctRoastLevels()` — removed (no callers)
- [x] `getDistinctProfilesFiltered()`, `getDistinctBeanBrandsFiltered()`, `getDistinctBeanTypesFiltered()`, `getDistinctValuesFiltered()` — removed (no callers)
- [x] `getFilteredShotCount()` — removed (no callers)
- [x] `getAutoFavorites()`, `getAutoFavoriteGroupDetails()` — removed (async equivalents exist)
- [x] `exportShotData()` — removed (no callers)
- [x] `createBackup()`, `importDatabase()` — removed (async equivalents exist)
- [x] `getRecentShotsByKbId()` — removed (no callers)

**Other fixes:**

- [x] `updateTotalShots()` — converted to async (uses `getShotCountStatic()` on background thread); sync count only during `initialize()` (before UI)
- [x] `grinder_brand` added to `requestDistinctCache()` pre-warm list (was missing, caused cache miss on first access)
- [x] `getDistinctValues()` — converted to cache-only (returns `{}` on miss, triggers `requestDistinctValueAsync()`)

**Remaining known violation:**

- [ ] `importShotRecord()` — called synchronously on main thread from `ShotImporter::onProcessNextFile()` via `QTimer::singleShot` batching. Needs refactor to background-thread batch processing (see TODO in `shotimporter.cpp`).
- [ ] `deleteShot()` — called from `importShotRecord()` (internal, same main-thread constraint)

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

**Fixed (cpp-compliance-cleanup):** `src/ble/scales/decentscale.cpp` now uses shared `scalelogging.h` macros (`DECENT_LOG`/`DECENT_WARN`). Direct `qWarning()` calls replaced with `DECENT_WARN`, and "service not found" path now logs via `DECENT_WARN` before `emit errorOccurred()`.

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

- [x] `src/ble/bletransport.cpp:242` — Commented-out `clearQueue` log removed, replaced with `Q_UNUSED(cleared)` *(fixed in cpp-compliance-cleanup)*

---

## 6. Dead Code / Commented-Out Code

These are not rule violations but reduce code maintainability.

- [x] `src/screensaver/screensavervideomanager.cpp` — 72 commented-out `qDebug()`/`qWarning()` lines removed *(fixed in cpp-compliance-cleanup)*
- [x] `src/ble/bletransport.cpp:242` — Commented-out clearQueue log removed *(fixed in cpp-compliance-cleanup)*
- [x] `src/core/translationmanager.cpp:2231-2232` — Commented-out Gemini/Ollama providers replaced with explanatory comment *(fixed in cpp-compliance-cleanup)*

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
| **Medium** | Main-thread I/O (QML sync callers) | 8 confirmed callers | 3a | **Fixed** — all cache-only (no sync DB), 21 dead methods removed |
| **Medium** | Timer guards (platform constraints) | 5+1 instances | 2 | Accepted / 1 **Fixed** (queueDrained) |
| **Medium** | Main-thread I/O (ShotServer) | 10 call sites | 3b | **Fixed** |
| **Medium** | Scale LOG macros route errors to `qDebug()` | 10 files | 5a | **Fixed** |
| **Medium** | Scale connection timeout uses `qDebug` | 1 | 5c | **Fixed** |
| **Low** | ShotServer JS fetch missing `.catch()` | 7 | 4a | **Fixed** |
| **Low** | ShotServer JS fetch missing `r.ok` check | 21 | 4b | **Fixed** |
| **Low** | BLE write timeout logging level | 7 paths | 5b | **Fixed** |
| **Low** | Slot naming convention | ~27 slots across 18 files | 1a | **Fixed** (16 renamed, 11 accepted as API slots) |
| **Low** | Class naming (USBManager) | 1 | 1b | **Accepted** (all-caps abbreviation OK) |
| **Low** | Member variable missing `m_` prefix | 1 | 1c | **Fixed** → `m_impl` |
| **Low** | Class/filename spelling inconsistency | 1 | 1d | **Fixed** |
| **Low** | Dead / commented-out code | 3 areas | 6 | **Fixed** |
| **Low** | Commented-out log statement | 1 | 5d | **Fixed** |
| **Low** | DecentScale missing `DECENT_WARN` macro | 1 file | 5a | **Fixed** |

### Priority rationale

- **High = directly affects primary touch UI.** Sections 3c/3d/3e blocked the QML UI thread during user interactions (loading shots, flow calibration, AI queries) — all now fixed.
- **Medium = affects secondary interfaces, mitigated, or developer experience.** Section 3a: all 8 QML callers now cache-only (return `{}` on miss, trigger async fetch via `requestDistinctValueAsync()`). 21 dead synchronous methods removed. Only remaining violation: `importShotRecord()` runs on main thread via timer batching (flagged as TODO). Timer platform constraints (§2) have no event-based alternative — accepted. ShotServer async (§3b) only stalls the web UI. Scale log macros (§5a) and connection timeout (§5c) now use `qWarning()` for errors.
- **Low = correctness improvements with minimal user impact.** JS fetch fixes protect against edge cases on a localhost server. BLE log levels, naming conventions, and dead code removal are hygiene.

### Status

All audit items are now either **Fixed** or **Accepted**, with one remaining known violation: `importShotRecord()` runs on the main thread via `QTimer::singleShot` batching in `ShotImporter` (flagged as TODO for future background-thread refactor).
