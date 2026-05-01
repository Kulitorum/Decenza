# Change: Extract SAW + flow-cal learning into SettingsCalibration domain sub-object (Tier 3)

## Why

Tiers 1 and 2 of the domain split (PRs #852, #855, #855's Tier-2 follow-up) extracted 11 sub-objects from `Settings` and shrank `settings.h` from 1,170 → 298 lines. What remains on `Settings` itself is the **calibration / learning surface**: SAW (stop-at-weight) per-scale and per-(profile, scale) learning, plus auto flow calibration with a per-profile multiplier and batch accumulator. That's ~25 public methods, several mutable caches, and the `kSawMinMediansForGraduation` / `kBatchSize` / `kBatchMaxIqr` constants — roughly 900 lines of `settings.cpp` (≈ lines 525–1417) plus the related declarations in `settings.h`.

This pile predates the domain split for the legacy entry points (`sawLearnedLag`, `addSawLearningPoint`), but recent PRs have added meaningfully to it without splitting:
- #739 — auto flow calibration (per-profile flow cal storage, batch accumulator)
- #848 — per-(profile, scale) SAW learning (bootstrap, batch, IQR gate, fallback chain)
- #859 — graduation threshold tuning

Every change to either subsystem busts the rebuild blast across `settings.h`'s remaining 44 transitive includers — exactly the cost the domain split was built to eliminate. Tier 3 closes the gap so the calibration/learning code can iterate without dragging the rest of the app along.

## What Changes

- Add a new `SettingsCalibration` domain sub-object owning:
  - **Auto flow calibration**: `flowCalibrationMultiplier`, `autoFlowCalibration`, `profileFlowCalibration`, `setProfileFlowCalibration`, `clearProfileFlowCalibration`, `effectiveFlowCalibration`, `hasProfileFlowCalibration`, `allProfileFlowCalibrations`, `perProfileFlowCalVersion`, the `flowCalPendingIdeals` / `appendFlowCalPendingIdeal` / `clearFlowCalPendingIdeals` batch accumulator, the `m_perProfileFlowCalCache` and `savePerProfileFlowCalMap` helpers
  - **SAW learning (global pool + convergence)**: `sawLearnedLag`, `getExpectedDrip`, `sawLearningEntries`, `addSawLearningPoint`, `resetSawLearning`, `isSawConverged`, `ensureSawCacheLoaded`, `m_sawHistoryCache` / `m_sawConvergedCache` / `m_sawConvergedScaleType`
  - **SAW learning (per-pair)**: `sawLearnedLagFor`, `getExpectedDripFor`, `sawLearningEntriesFor`, `sawModelSource`, `resetSawLearningForProfile`, `perProfileSawHistory`, `allPerProfileSawHistory`, `sawPendingBatch`, `globalSawBootstrapLag`, `setGlobalSawBootstrapLag`, `addSawPerPairEntry`, `recomputeGlobalSawBootstrap`, the `m_perProfileSawHistoryCache` / `m_perProfileSawBatchCache` cache pair, `loadPerProfileSawHistoryMap` / `savePerProfileSawHistoryMap` / `loadPerProfileSawBatchMap` / `savePerProfileSawBatchMap`, the static `sawPairKey` helper
  - **Constants**: `kSawMinMediansForGraduation`, `kBatchSize`, `kMaxPairHistory`, `kBatchMaxIqr`, `kBatchMaxDeviation` (currently file-scope in `settings.cpp`)
  - **Static utility**: `Settings::sensorLag(scaleType)` — moves to `SettingsCalibration::sensorLag` (still a static method; called from `getExpectedDrip` / `getExpectedDripFor` inside the same class)
  - All matching NOTIFY signals: `flowCalibrationMultiplierChanged`, `autoFlowCalibrationChanged`, `perProfileFlowCalibrationChanged`, `sawLearnedLagChanged`
- `Settings` keeps only `m_calibration` + a typed `calibration()` accessor + a `Q_PROPERTY(QObject* calibration READ calibrationQObject CONSTANT)`. Per the architecture rule, `settings.h` MUST NOT `#include "settings_calibration.h"` — only forward-declare and dereference through the pointer.
- Cross-domain side effect: `resetSawLearning()` currently calls `m_brew->setHotWaterSawOffset(...)` and `m_brew->setHotWaterSawSampleCount(...)`. That cross-call moves into a `connect()` in `Settings::Settings()` that listens for a new `SettingsCalibration::sawLearningResetRequested` signal and forwards to `SettingsBrew`. (The setter on `SettingsCalibration` does not directly touch `SettingsBrew` — sub-objects don't know about each other.)
- QML access becomes `Settings.calibration.sawLearnedLag` / `Settings.calibration.expectedDrip(...)` / `Settings.calibration.profileFlowCalibration(...)` instead of the flat form. Needs `qmlRegisterUncreatableType<SettingsCalibration>` in `main.cpp`.
- **Storage keys are unchanged on disk**: `saw/learningHistory`, `saw/perProfileHistory`, `saw/perProfileBatch`, `saw/globalBootstrapLag/<scale>`, `calibration/flowMultiplier`, `calibration/autoFlowCalibration`, `calibration/perProfileFlow`, `calibration/flowCalBatch` all stay byte-identical (Storage Key Stability requirement).
- **BREAKING (internal API only):** removes the flat `Settings::X()` accessors for all migrated calibration/SAW properties; QML and C++ must use `Settings.calibration.X` / `settings->calibration()->X()`.
- Final state: `settings.h` shrinks from 298 → ≤ 200 lines. The remaining content on `Settings` is machine/scale/refractometer/USB-serial — a candidate for a future `SettingsHardware` extension or a Tier 4 `SettingsMachine` split, out of scope here.

## Impact

- **Affected specs:** `settings-architecture` (MODIFIED — `SettingsCalibration` joins the domain set; the existing "settings.h SHALL be under 200 lines" scenario becomes verifiable)
- **Affected code:**
  - `src/core/settings.{h,cpp}` — strip ~900 lines, add the new sub-object accessor and the `resetSawLearning → brew` cross-domain connect
  - 1 new file pair: `src/core/settings_calibration.{h,cpp}`
  - `src/main.cpp` — 1 new `qmlRegisterUncreatableType` call + sub-object pointer wiring at narrow consumer call sites
  - `CMakeLists.txt` + `tests/CMakeLists.txt` — add new sources to `SOURCES`/`HEADERS`/`CORE_SOURCES`
  - QML readers: `SettingsCalibrationTab.qml`, `IdlePage.qml`, `BrewDialog.qml`, calibration chart components, `SettingsMachineTab.qml`, any other touching `Settings.sawLearned*` / `Settings.profileFlowCalibration` / `Settings.autoFlowCalibration` / `Settings.flowCalibrationMultiplier`
  - C++ consumers: `MainController` (auto flow cal computation, SAW learning point recording on shot end), `WeightProcessor` (snapshots SAW learning entries at shot start), `mcptools_control.cpp` (`reset_saw_learning`, `reset_saw_learning_for_profile`, `clear_flow_calibration`), `mcptools_settings.cpp`, `mcptools_dialing.cpp`, `shotserver_settings.cpp`, `shotserver_ai.cpp`, `settingsserializer.cpp`
  - Tests: `tst_saw`, `tst_saw_settings`, `tst_autoflowcal` — switch every `Settings::` call on these methods to `Settings::calibration()->`. Test logic does not change.
- **Risk:** medium — the API surface is large (~25 methods + caches) but the migration is mechanical. The trickiest pieces are: (a) the `resetSawLearning → m_brew` cross-call must move to a connect-based wire; (b) the `m_perProfileFlowCalVersion` member must move with the property and stay coherent with QML rebinds; (c) the static `sensorLag()` helper has callers outside the class (e.g. `WeightProcessor`) that will need their callsite updated to `SettingsCalibration::sensorLag`. None of these are subtle, but they're the spots where missing one will silently break behaviour rather than error at compile time.

## Design decisions

- **Scale-type access:** `SettingsCalibration` holds a non-owning `Settings* m_owner` so `sawLearnedLag()` / `getExpectedDrip()` can read `m_owner->scaleType()` without changing their public API. Today QML calls these zero-arg, and adding a parameter would force every reader (`SettingsCalibrationTab`, `ProfileInfoPage`) to migrate. The owner pointer is reserved for this single call only — no other `Settings` surface is allowed via the back-pointer. `settings_calibration.cpp` may `#include "settings.h"`; `settings_calibration.h` only forward-declares.
- **Consumer scope:** `MainController` stays wide (touches many domains). `WeightProcessor` and `FlowCalibrationModel` narrow to `SettingsCalibration*` in this same PR (rolled-in follow-up) because they only touch calibration data — the recompile-blast win is precisely what the split is for.
