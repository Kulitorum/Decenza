# Shot History & Comparison

Local storage, review, and comparison system for DE1 shots.

## Overview

Every extraction (espresso, americano, hot water, steam) is saved automatically on completion. Users can browse, filter, search, rate, annotate, compare, export, and upload past shots entirely offline.

## Storage backend

**SQLite** (WAL mode) — chosen for fast indexed queries on old Android tablets, efficient filtering without loading full samples, FTS5 search, and single-file backup.

Database path: under the app's writable directory as `shots.db`. Resolve at runtime via `ShotHistoryStorage::initialize()` rather than hard-coding — the path is platform-dependent and has changed between Android versions.

### Schema (current)

Source of truth: `src/history/shothistorystorage.cpp` (see the `CREATE TABLE` block around line 143). Key tables:

- **`shots`** — one row per shot. Columns: `id`, `uuid`, `timestamp`, `profile_name`, `profile_json`, `profile_kb_id`, `beverage_type`, `duration_seconds`, `final_weight`, `dose_weight`, `bean_brand`, `bean_type`, `bean_notes`, `roast_date`, `roast_level`, `grinder_brand`, `grinder_model`, `grinder_burrs`, `grinder_setting`, `drink_tds`, `drink_ey`, `enjoyment`, `espresso_notes`, `profile_notes`, `barista`, `visualizer_id`, `visualizer_url`, `debug_log`, `temperature_override`, `yield_override`, `created_at`, `updated_at`.
- **`shot_samples`** — one row per shot. `data_blob` is the zlib-compressed JSON time series (~30 KB raw → ~5–10 KB compressed). Contains pressure, flow, temperature, weight, pressure/flow/temperature goals, and derived series (resistance, conductance, etc.).
- **`shot_phases`** — phase markers (EspressoPreheating, Preinfusion, Pouring, Ending) with timestamps, frame numbers, and transition reasons (weight/pressure/flow/time).
- **`shots_fts`** — FTS5 virtual table over `espresso_notes`, `bean_brand`, `bean_type`, `profile_name`, `grinder_brand`, `grinder_model`, `grinder_burrs`. Kept in sync via triggers.

Indexes: `timestamp DESC`, `profile_name`, `(bean_brand, bean_type)`, `(grinder_brand, grinder_model)`, `enjoyment`, `profile_kb_id`, `shot_phases(shot_id)`.

Schema migrations are handled in-place at startup via a `schema_version` table.

## C++ architecture

### `ShotHistoryStorage` (`src/history/shothistorystorage.*`)

The main database interface, registered as a QML singleton. **Async-first**: most mutating and expensive read operations are `requestX()` methods that dispatch to a background thread (`withTempDb()` helper from `src/core/dbutils.h`) and deliver results via signals. This matches the CLAUDE.md rule that DB and disk I/O must not run on the main thread.

Primary APIs (see the header for the full surface):

- **Reads** — `requestShotsFiltered(filter, offset, limit)` → `shotsFilteredReady`, `requestShot(shotId)` → `shotReady`, `requestMostRecentShotId()` → `mostRecentShotIdReady`, `requestRecentShotsByKbId(kbId, limit)` → `recentShotsByKbIdReady`.
- **Writes** — `requestUpdateShotMetadata(shotId, metadata)`, `requestUpdateVisualizerInfo(shotId, id, url)`, `requestDeleteShot(shotId)`, `deleteShots(ids)`.
- **Distinct value caches** (synchronous, hit in-memory cache) — `getDistinctBeanBrands()`, `getDistinctBaristas()`, `getDistinctBeanTypesForBrand(brand)`, `getDistinctGrinderBrands()`, `getDistinctGrinderModelsForBrand(brand)`, `getDistinctGrinderSettingsForGrinder(model)`. `requestDistinctCache()` refreshes the cache from the DB on a background thread.
- **Grouped reads for auto-favorites** — `requestAutoFavorites(groupBy, maxItems)`, `requestAutoFavoriteGroupDetails(groupBy, groupValue)`.
- **Backup/import** — `requestCreateBackup(destPath)`, `requestImportDatabase(filePath, merge)`. See `docs/CLAUDE_MD/DATA_MIGRATION.md` for the device-to-device transfer story.
- **Reanalysis** — `requestReanalyzeBadges(shotId)` recomputes channel/temperature/grind quality flags on legacy shots.

Filter keys for `requestShotsFiltered`: `profileName`, `beanBrand`, `beanType`, `grinderBrand`, `grinderModel`, `roastLevel`, `minEnjoyment`, `maxEnjoyment`, `dateFrom`, `dateTo`, `searchText`, `onlyWithVisualizer`, `beverageType`. Text fields are exact-match; `searchText` hits the FTS5 index.

### `ShotHistoryExporter` (`src/history/shothistoryexporter.*`)

Generates formatted shot exports (Markdown/plain text) for bug reports, Visualizer uploads, and AI-context summaries. Extracted from the storage class so formatting concerns don't live alongside SQL.

### `ShotDebugLogger` (`src/history/shotdebuglogger.*`)

Captures a per-shot diagnostic log during extraction. `startCapture()` is called from `MainController::onEspressoCycleStarted()`; `stopCapture()` runs on `shotEnded` and the captured text is saved into the shot's `debug_log` column.

Log entries are prefixed with elapsed seconds (`[12.345]`) and a severity/category tag (`DEBUG`, `BLE`, `ANOMALY`, `FRAME`, `WEIGHT`, `STATE`, `PHASE`, `WARN`, `ERROR`).

### `ShotDataModel` (`src/history/shotdatamodel.*`)

Live sample buffer during extraction, also used to feed historical shots into the graph QML components after a `requestShot()` load.

### `ShotComparisonModel` (`src/history/shotcomparisonmodel.*`)

Holds up to 3 shots side-by-side for the comparison page. Exposes `addShot(shotId)` / `removeShot(shotId)` / `clearAll()`, per-index getters for each time series, and a color assignment per slot (used by the legend and overlay graphs).

### `ShotImporter` (`src/history/shotimporter.*`)

Imports legacy `.shot` files from de1app and JSON files exported by other Decenza instances.

## QML surface

### Pages (`qml/pages/`)

- **`ShotHistoryPage.qml`** — main list with filter dropdowns, FTS search box, multi-select, grouped favorites. Entry point from `IdlePage`.
- **`ShotDetailPage.qml`** — single-shot detail (graph, metrics, bean/grinder/analysis/notes cards, debug log viewer, delete, Visualizer upload).
- **`ShotComparisonPage.qml`** — overlaid graphs and comparison tables for 2–3 shots.
- **`PostShotReviewPage.qml`** — immediately-after-extraction review flow. Prompts for enjoyment, notes, TDS/EY input, and (optionally) triggers `ShotAnalysisDialog`.

### Graph & comparison components (`qml/components/`)

- **`HistoryShotGraph.qml`** — static graph for a single historical shot. Binds to the `QVector<QPointF>` series returned via `shotReady`.
- **`ComparisonGraph.qml`** — multi-shot overlay. Takes a `ShotComparisonModel` and renders 2–3 color-coded series.
- **`ComparisonDataTable.qml`, `ComparisonShotTable.qml`, `ComparisonInspectBar.qml`** — side-by-side metrics tables and scrub-to-inspect UI.
- **`ShotAnalysisDialog.qml`** — AI-driven analysis triggered from the detail or post-shot pages. See `docs/AI_ADVISOR.md`.
- **`GraphInspectBar.qml`, `GraphLegend.qml`** — shared inspect/legend widgets.

## Write path (shot save)

1. `MainController::onEspressoCycleStarted()` → `ShotDebugLogger::startCapture()`.
2. Samples stream into `ShotDataModel` via `shotSampleReceived` (from `DE1Device`).
3. `ShotTimingController::shotProcessingReady` (not `MachineState::shotEnded` directly — the timing controller waits for any SAW settling) fires `MainController::onShotEnded`.
4. `onShotEnded` collects the debug log, builds a `ShotMetadata` struct from `Settings`, and calls `ShotHistoryStorage::saveShot(...)`. Save runs on the background thread; the main thread receives `shotSaved(shotId)` and navigates to `PostShotReviewPage` if the user's settings permit.
5. Visualizer upload (if enabled) is triggered post-save by `VisualizerUploader`, which calls `requestUpdateVisualizerInfo(shotId, id, url)` on success.

## Performance

- All expensive reads are background-threaded via `withTempDb()` (see `src/core/dbutils.h`).
- List page uses paginated summary reads (50 at a time). Full `ShotRecord` and the compressed sample blob are only fetched when a specific shot is opened.
- FTS5 search keeps notes queries sub-50 ms on old tablets at 1k+ shots.
- Distinct-value filter dropdowns hit an in-memory cache populated by `requestDistinctCache()`; the cache is invalidated on write.

## Data retention & backup

No automatic purge — user manages their own history. Backups are created via `requestCreateBackup(destPath)` and the daily auto-backup runs in `DatabaseBackupManager` (`src/core/databasebackupmanager.*`).

Device-to-device transfer uses `requestImportDatabase(filePath, merge)` with deduplication on `uuid`. See `docs/CLAUDE_MD/DATA_MIGRATION.md`.

## Related docs

- `docs/CLAUDE_MD/DATA_MIGRATION.md` — device-to-device transfer REST protocol and import options.
- `docs/CLAUDE_MD/VISUALIZER.md` — shot upload to visualizer.coffee and DYE metadata.
- `docs/CLAUDE_MD/TESTING.md` — how to test storage changes (including the `withTempDb` pattern).
- `docs/AI_ADVISOR.md` — AI assistant that reads shot history for dialing-in guidance.
