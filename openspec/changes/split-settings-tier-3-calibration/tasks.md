# Tasks: Extract SAW + flow-cal learning into SettingsCalibration domain sub-object (Tier 3)

The architectural pattern is documented in `docs/CLAUDE_MD/SETTINGS.md` (read it first — it captures every gotcha from PR #852 and PR #855). The 8-step domain checklist applies. Tasks below add the calibration-domain-specific work on top.

## Pre-work

- [x] Read `docs/CLAUDE_MD/SETTINGS.md` end to end; re-read the "Adding a new domain sub-object class" 8-step checklist
- [x] Read `docs/CLAUDE_MD/SAW_LEARNING.md` and `docs/CLAUDE_MD/AUTO_FLOW_CALIBRATION.md` — confirm storage-key and behaviour invariants before any rewrites
- [x] Capture baseline: `wc -l src/core/settings.h src/core/settings.cpp` and direct/transitive `settings.h` includer counts in `src/` and `tests/`

## Build the new domain class

- [x] Create `src/core/settings_calibration.{h,cpp}`. `SettingsCalibration : public QObject` with its own `mutable QSettings m_settings("DecentEspresso", "DE1Qt")`
- [x] Move the auto flow calibration surface verbatim (keep storage keys byte-identical):
  - `flowCalibrationMultiplier` getter/setter (`calibration/flowMultiplier`) + `flowCalibrationMultiplierChanged` signal
  - `autoFlowCalibration` getter/setter (`calibration/autoFlowCalibration`) + `autoFlowCalibrationChanged` signal
  - `profileFlowCalibration`, `setProfileFlowCalibration`, `clearProfileFlowCalibration`, `effectiveFlowCalibration`, `hasProfileFlowCalibration`, `allProfileFlowCalibrations` (key `calibration/perProfileFlow`)
  - `perProfileFlowCalVersion` (the int counter + `Q_PROPERTY`) + `perProfileFlowCalibrationChanged` signal
  - `flowCalPendingIdeals`, `appendFlowCalPendingIdeal`, `clearFlowCalPendingIdeals` (key `calibration/flowCalBatch`) + the file-scope `parseFlowCalBatch` helper
  - The `m_perProfileFlowCalCache` / `m_perProfileFlowCalCacheValid` cache pair + `savePerProfileFlowCalMap` invariant
- [x] Move the SAW learning surface verbatim (keep storage keys byte-identical):
  - Global pool: `sawLearnedLag`, `getExpectedDrip`, `sawLearningEntries`, `addSawLearningPoint` (legacy path), `resetSawLearning`, `isSawConverged`, `ensureSawCacheLoaded`, the `m_sawHistoryCache` / `m_sawHistoryCacheDirty` / `m_sawConvergedCache` / `m_sawConvergedScaleType` mutables (key `saw/learningHistory`)
  - Per-pair: `sawLearnedLagFor`, `getExpectedDripFor`, `sawLearningEntriesFor`, `sawModelSource`, `resetSawLearningForProfile`, `perProfileSawHistory`, `allPerProfileSawHistory`, `sawPendingBatch`, the `m_perProfileSawHistoryCache` / `m_perProfileSawBatchCache` cache pair, `loadPerProfileSawHistoryMap` / `savePerProfileSawHistoryMap` / `loadPerProfileSawBatchMap` / `savePerProfileSawBatchMap`, `addSawPerPairEntry`, `recomputeGlobalSawBootstrap`, the static `sawPairKey` helper (keys `saw/perProfileHistory`, `saw/perProfileBatch`, `saw/globalBootstrapLag/<scale>`)
  - `globalSawBootstrapLag` / `setGlobalSawBootstrapLag` accessors
  - `sawLearnedLagChanged` signal
- [x] Move `Settings::sensorLag(scaleType)` to `SettingsCalibration::sensorLag(scaleType)` (still `static`) and update the only external caller (`WeightProcessor`) to call `SettingsCalibration::sensorLag(...)`
- [x] Move the file-scope constants to `SettingsCalibration` private statics (or anonymous namespace inside `settings_calibration.cpp`): `kSawMinMediansForGraduation`, `kBatchSize`, `kMaxPairHistory`, `kBatchMaxIqr`, `kBatchMaxDeviation`
- [x] `SettingsCalibration` holds a non-owning `Settings* m_owner` (set in constructor) so `sawLearnedLag()` and `getExpectedDrip()` can read `m_owner->scaleType()` without changing their public API (QML callers pass no args today; preserving that avoids a QML-side migration). The owner pointer is dereferenced ONLY for `scaleType()` lookups — no other `Settings` surface is allowed. (`settings_calibration.cpp` may `#include "settings.h"`; `settings_calibration.h` only forward-declares.)

## Wire into `Settings`

- [x] Add `class SettingsCalibration;` forward declaration at the top of `settings.h` (NEVER `#include "settings_calibration.h"` from `settings.h` — that re-pollutes the includer tree)
- [x] Add `Q_PROPERTY(QObject* calibration READ calibrationQObject CONSTANT)` to `Settings`
- [x] Add typed inline `SettingsCalibration* calibration() const { return m_calibration; }` accessor in the header
- [x] Add out-of-line `QObject* Settings::calibrationQObject() const { return m_calibration; }` upcast in `settings.cpp`
- [x] Construct `m_calibration(new SettingsCalibration(this))` in the `Settings::Settings()` member-init list (after the dependency-targets it needs); add `#include "settings_calibration.h"` in `settings.cpp` only
- [x] Re-wire the `resetSawLearning() → m_brew->setHotWaterSawOffset/setHotWaterSawSampleCount` cross-call via `connect()` in `Settings::Settings()`. Emit a new `SettingsCalibration::sawLearningResetRequested(scaleType)` signal from inside `resetSawLearning` and forward to `SettingsBrew` from the wiring lambda.
- [x] Strip the migrated declarations from `settings.h` (every `Q_PROPERTY`, getter/setter, `Q_INVOKABLE`, signal, mutable cache, and helper that moved). `settings.h` should land at ≤ 200 lines.
- [x] Strip the migrated definitions (~900 lines, roughly 525–1417) from `settings.cpp`.

## Register, build, verify

- [x] `qmlRegisterUncreatableType<SettingsCalibration>("Decenza", 1, 0, "SettingsCalibrationType", "SettingsCalibration is created in C++")` in `main.cpp`
- [x] Add `src/core/settings_calibration.cpp` and `src/core/settings_calibration.h` to `CMakeLists.txt` (both SOURCES and HEADERS), and `tests/CMakeLists.txt` (`CORE_SOURCES`)
- [x] Build clean (`cmake --build` Release) — fix any compile errors from missed call sites; do not paper over them with `using` aliases or wrapper getters on `Settings`

## Migrate QML readers

- [x] `qml/pages/settings/SettingsCalibrationTab.qml` — every `Settings.sawLearned*`, `Settings.profileFlowCalibration`, `Settings.autoFlowCalibration`, `Settings.flowCalibrationMultiplier`, `Settings.allProfileFlowCalibrations`, `Settings.perProfileFlowCalVersion` becomes `Settings.calibration.*`
- [x] `qml/pages/ProfileInfoPage.qml` — same migration for any matching reader
- [x] Audit the rest of `qml/` for the patterns above (`grep -rn "Settings\.\(sawLearned\|getExpectedDrip\|profileFlowCalibration\|autoFlowCalibration\|flowCalibrationMultiplier\|sawModelSource\|sawLearningEntries\|effectiveFlowCalibration\|hasProfileFlowCalibration\|allProfileFlowCalibrations\|perProfileFlowCalVersion\|isSawConverged\|globalSawBootstrapLag\|resetSawLearning\|sawPendingBatch\|allPerProfileSawHistory\|perProfileSawHistory\)" qml/`)
- [x] Re-target any `Connections { target: Settings }` listening for `flowCalibrationMultiplierChanged`, `autoFlowCalibrationChanged`, `perProfileFlowCalibrationChanged`, or `sawLearnedLagChanged` to `Connections { target: Settings.calibration }`. Silent failure mode if missed.

## Migrate C++ consumers

- [x] `src/controllers/maincontroller.cpp` — every `m_settings->sawLearned*`, `m_settings->profileFlowCalibration*`, `m_settings->autoFlowCalibration*`, `m_settings->setFlowCalibrationMultiplier`, `m_settings->addSawLearningPoint`, `m_settings->isSawConverged`, `m_settings->effectiveFlowCalibration`, `m_settings->appendFlowCalPendingIdeal`, `m_settings->clearFlowCalPendingIdeals` becomes `m_settings->calibration()->*`. MainController stays a wide consumer.
- [x] `src/machine/weightprocessor.{h,cpp}` — narrow to `SettingsCalibration*` (constructor takes `SettingsCalibration*` instead of `Settings*`); `Settings::sensorLag` callsites become `SettingsCalibration::sensorLag`
- [x] `src/controllers/profilemanager.cpp` — re-route the migrated calls
- [x] `src/models/flowcalibrationmodel.{h,cpp}` — narrow to `SettingsCalibration*` (constructor change + header-include swap); update `main.cpp` callsite to pass `settings.calibration()`
- [x] `src/mcp/mcptools_control.cpp` — `reset_saw_learning`, `reset_saw_learning_for_profile`, `clear_flow_calibration` re-routed
- [x] `src/mcp/mcptools_settings.cpp`, `src/mcp/mcptools_write.cpp`, `src/mcp/mcptools_dialing.cpp` — re-route any matched callers
- [x] `src/network/shotserver_settings.cpp`, `src/network/shotserver_ai.cpp` — re-route any matched callers
- [x] `src/core/settingsserializer.cpp` — JSON keys preserved exactly (mirror PR #855's pattern); only the C++ accessors change
- [x] Any remaining hits from `grep -rn "settings->\(<surface>\)\|m_settings->\(<surface>\)\|m_settingsCalibration\b" src/ tests/`

## Cross-domain wiring

- [x] In `Settings::Settings()`, wire `connect(m_calibration, &SettingsCalibration::sawLearningResetRequested, this, [this]{ m_brew->setHotWaterSawOffset(2.0); m_brew->setHotWaterSawSampleCount(0); });` (or the equivalent — match the existing reset semantics exactly)
- [x] Confirm no other cross-domain calls exist: search `m_brew->`, `m_dye->`, etc. inside the calibration-related code paths

## Tests

- [x] `tests/tst_saw.cpp`, `tests/tst_saw_settings.cpp`, `tests/tst_autoflowcal.cpp`, `tests/tst_sawprediction.cpp` — every `Settings::` call on a migrated method becomes `Settings::calibration()->`. **No test logic changes.**
- [x] If any test uses `friend class Settings;` access, add the equivalent `friend class <Test>;` to `SettingsCalibration` behind `#ifdef DECENZA_TESTING`
- [x] Add a Tier-3-specific test: instantiate `Settings`, write a flow-cal multiplier through `Settings.calibration`, sync, re-open with the legacy key path, confirm value reads back identically (storage-key stability regression test)
- [x] Run `ctest` — target: 0 regressions vs. the pre-change baseline

## Final cleanup

- [x] `wc -l src/core/settings.h` — target: ≤ 200 lines
- [x] No `#include "settings_calibration.h"` in `settings.h` (only forward decl); confirm with `grep`
- [x] Re-measure transitive `settings.h` includer count — should drop once `WeightProcessor` and `FlowCalibrationModel` are narrowed to `SettingsCalibration*`
- [x] All ctest suites pass
- [x] App boots; SettingsCalibrationTab loads; flow cal + SAW learning visible state matches a backup of `~/.config/DecentEspresso/DE1Qt.conf` from a build immediately before this change (storage-key stability check on a real device)
- [x] `openspec validate split-settings-tier-3-calibration --strict --no-interactive` passes

## PR

- [x] Open PR with before/after measurements (settings.h line count, transitive includer count) in description
- [x] Reference issue #860 and the prior tier PRs (#852, #855, #855's Tier-2 follow-up)
- [x] After merge, follow the OpenSpec archive flow (`openspec archive split-settings-tier-3-calibration --yes`)
