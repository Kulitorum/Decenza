## Visualizer Integration

### DYE (Describe Your Espresso) Metadata
- **Location**: `qml/pages/PostShotReviewPage.qml` and `qml/pages/BeanInfoPage.qml`
- **Settings**: `src/core/settings.h` - dye* properties (sticky between shots)
- **Feature toggle**: `visualizerExtendedMetadata` setting (no UI toggle — controlled via layout system and backup/restore)
- **Auto-show**: Settings → Visualizer → "Edit after shot"
- **Access**: BeansItem in layout system (`qml/components/layout/items/BeansItem.qml`), auto-show after shot, or shot history tap

Supported metadata fields:
- `bean_brand`, `bean_type`, `roast_date`, `roast_level`
- `grinder_model`, `grinder_setting`
- `drink_tds`, `drink_ey`, `espresso_enjoyment`
- `dyeShotNotes`, `barista`

### Shot Upload (VisualizerUploader)

- **Location**: `src/network/visualizeruploader.h/.cpp`
- **Endpoint**: `POST https://visualizer.coffee/api/shots/upload` (multipart form-data)
- **Auth**: HTTP Basic Auth (username:password base64)
- **Update**: `PATCH https://visualizer.coffee/api/shots/{id}` (JSON body)
- **Two paths**: `buildShotJson()` for live shots, `buildHistoryShotJson()` for history re-uploads

#### Result persistence (authoritative C++ path)

The returned Visualizer shot id is persisted to the originating local
row by **`MainController`**, not by any UI page. `uploadShot()` /
`uploadShotFromHistory()` carry the local `shots.id`; on success
`VisualizerUploader::uploadSucceededForShot(dbShotId, visualizerId,
url)` fires and `MainController` calls
`ShotHistoryStorage::requestUpdateVisualizerInfo(...)`. The shot-end
auto-upload is dispatched from the `shotSaved` callback (once the row
id is known) — never before save, so it cannot orphan. The
`PostShotReviewPage` / `ShotDetailPage` `onUploadSuccess` handlers do
**not** persist (they only refresh UI); do not reintroduce a
page-gated writeback — it silently lost links whenever the review page
was disabled, auto-closed, or navigated away before the ~1 s round
trip (OpenSpec `persist-visualizer-id-in-controller`).

#### One-time reconciliation backfill

`MainController::processVisualizerReconciliation()` runs once per
device (QSettings `visualizerBackfill/doneV1`), gated on credentials
(absent → skip without setting the flag, retried next boot). It lists
the user's shots (`GET /api/shots`, paged, bounded to 60 days),
relinks local rows whose `visualizer_id` is empty by matching
`shots.timestamp` to the cloud shot's `clock` within ±2 s — strict
1:1, no reuse of an id already on a row, ambiguous skipped
(`reconcileVisualizerLinksStatic`, unit-tested). Each linked row is
then queued onto the same serial drain as the migration-16 sync to
push the now-authoritative local rating up (cleared → JSON `null`).
Independent of and order-insensitive to the migration-16 back-sync.

#### Upload JSON Structure

The upload JSON matches de1app v2 format:

```
{
  "version": 2,
  "clock": <unix_timestamp>,
  "date": "<ISO 8601>",
  "timestamp": <unix_timestamp>,
  "elapsed": [<seconds>...],
  "pressure": { "pressure": [...], "goal": [...] },
  "flow": { "flow": [...], "goal": [...], "by_weight": [...], "by_weight_raw": [...] },
  "temperature": { "basket": [...], "mix": [...], "goal": [...] },
  "totals": { "weight": [...], "water_dispensed": [...] },
  "resistance": { "resistance": [...] },
  "state_change": [...],
  "meta": { "bean": {...}, "shot": {...}, "grinder": {...}, "in": N, "out": N, "time": N },
  "profile": { <full profile JSON> },
  "app": {
    "app_name": "Decenza",
    "app_version": "<version>",
    "data": {
      "settings": { <DYE metadata + profile TCL fields> },
      "machine_state": { "firmware_version": "...", "state": "...", ... }
    }
  }
}
```

#### app.data.settings — Critical for Visualizer Profile Extraction

The Visualizer's server-side `DecentJson` parser extracts profile fields from `app.data.settings` using a fixed list of TCL field names (`PROFILE_FIELDS`). De1app dumps its entire `::settings` array (hundreds of keys); Decenza sends a curated subset via `buildProfileSettings()`.

**DYE metadata fields**: `bean_brand`, `bean_type`, `roast_date`, `roast_level`, `grinder_model`, `grinder_setting`, `grinder_dose_weight`, `drink_weight`, `drink_tds`, `drink_ey`, `espresso_enjoyment`, `espresso_notes`, `barista`, `profile_title`

**Profile fields** (for Visualizer TCL reconstruction):
- `settings_profile_type` — `settings_2a`/`settings_2b`/`settings_2c`
- `espresso_temperature`, `espresso_temperature_0..3` — temperature presets (as strings)
- `maximum_pressure`, `maximum_flow`, `flow_profile_minimum_pressure`
- `tank_desired_water_temperature`, `maximum_flow_range_advanced`, `maximum_pressure_range_advanced`
- `final_desired_shot_weight`, `final_desired_shot_weight_advanced`
- `final_desired_shot_volume`, `final_desired_shot_volume_advanced`
- `final_desired_shot_volume_advanced_count_start` — preinfuse frame count
- `advanced_shot` — TCL list of all frames via `ProfileFrame::toTclList()`

**Simple profile params** (for settings_2a/2b reconstruction):
- `preinfusion_time`, `preinfusion_flow_rate`, `preinfusion_stop_pressure`
- `espresso_pressure`, `espresso_hold_time`, `espresso_decline_time`, `pressure_end`
- `flow_profile_hold`, `flow_profile_decline`
- `maximum_flow_range_default`, `maximum_pressure_range_default`

#### ProfileFrame::toTclList()

Inverse of `fromTclList()`. Serializes a frame to de1app TCL list format:
```
{name {preinfusion} temperature 93.00 sensor coffee pump pressure transition fast pressure 3.50 flow 2.00 seconds 5.00 volume 0.0 exit_if 1 exit_type {pressure_over} exit_pressure_over 9.00 ...}
```

#### state_change Array

Matches de1app format: a per-sample array where the value alternates between `10000000` and `-10000000` at each frame transition. Generated from `ShotDataModel::phaseMarkersList()` (live) or history phase markers. Used by Visualizer to draw vertical frame marker lines.

#### flow.by_weight_raw

Raw (pre-smoothing) weight flow rate from scale. `ShotDataModel::smoothWeightFlowRate()` saves a copy of the raw data before applying the centered moving average.

#### app.data.machine_state

Includes `firmware_version`, `state`, `substate`, and `headless` flag from `DE1Device`. De1app dumps its entire `::DE1` array; Decenza sends key fields only.

### Feature Parity with de1app

Decenza's upload is at feature parity with de1app for Visualizer's purposes. Key differences:

| Aspect | de1app | Decenza |
|--------|--------|---------|
| `app.data.settings` | Entire `::settings` array (hundreds of keys) | Curated subset (~40 keys) |
| `app.data.machine_state` | Entire `::DE1` array | Key fields only (firmware, state, headless) |
| `flow.by_weight_raw` | Raw scale flow rate | Raw scale flow rate |
| `state_change` | Per-sample alternating sign | Per-sample alternating sign (from phase markers) |
| `resistance.by_weight` | Resistance from weight flow | Not sent (minimal Visualizer impact) |
| `timers` | Timer reference points | Not sent (not used by Visualizer) |
| `scale` | Raw scale data (timestamps, raw weight) | Not sent (not used by Visualizer) |
| `meta.bean.notes` | Bean notes field | Not sent (Decenza doesn't store bean notes separately) |

### Profile Import (VisualizerImporter)
- **Location**: `src/network/visualizerimporter.cpp/.h`
- **QML Page**: `qml/pages/VisualizerBrowserPage.qml`
- **Input**: User enters a 4-character share code (no embedded browser)
- **API**: `GET https://visualizer.coffee/api/shots/{id}/profile?format=json`
- **Multi-import**: `qml/pages/VisualizerMultiImportPage.qml` — imports all profiles the user has shared

### Import Flow
1. User enters a 4-character share code from visualizer.coffee
2. VisualizerImporter resolves the share code to a shot ID via the API
3. VisualizerImporter fetches the profile JSON and converts to native format
4. If duplicate exists, shows overwrite/save-as-new/rename dialog

### Key Implementation Notes
- Duplicate handling: `saveOverwrite()`, `saveAsNew()`, `saveWithNewName(newTitle)`, `cancelPending()`
- Keyboard handling for Android: FocusScope + keyboardOffset pattern for text input

### Visualizer Profile Format
- Visualizer and de1app use the same JSON format with string-encoded numbers (Tcl huddle serialization)
- The unified `jsonToDouble()` helper and `ProfileFrame::fromJson()` handle string-to-double conversion and nested-to-flat field mapping transparently
- The Visualizer uploader (`buildVisualizerProfileJson()`) string-encodes numbers to match de1app convention

### Profile Import Architecture (ProfileSaveHelper)

Both `ProfileImporter` (file-system import from DE1 tablet) and `VisualizerImporter` (network import from visualizer.coffee) delegate save/compare/deduplicate logic to `ProfileSaveHelper` (`src/profile/profilesavehelper.h/.cpp`). Key methods:

- **`compareProfiles()`** — 6 profile-level fields + all frame fields (temperature, sensor, pump, transition, pressure, flow, seconds, volume, exit conditions, weight exit, limiter, popup)
- **`checkProfileStatus()`** — Checks ProfileStorage, downloaded folder, and built-in profiles
- **`saveProfile()`** — Save with duplicate detection (downloaded + built-in). Returns: 1=saved, 0=duplicate (emits `duplicateFound`), -1=failed. Callers must emit `importSuccess`/`importFailed` themselves.
- **`saveOverwrite()`/`saveAsNew()`/`saveWithNewName()`** — Duplicate resolution (emit signals directly)
- **`titleToFilename()`** — Delegates to `MainController::titleToFilename()`
- **`downloadedProfilesPath()`** — Static helper: `{AppDataLocation}/profiles/downloaded/`

### Filename Generation: Decenza vs de1app

**de1app** (`profile.tcl` `filename_from_title`): Preserves case and Unicode, replaces spaces→`_`, `/`→`__`, removes shell-unsafe special chars, truncates to 60 chars. Example: `"Café Leche"` → `"Café_Leche"`.

**Decenza** (`MainController::titleToFilename`): Lowercases, replaces accented chars with ASCII equivalents (é→e, ñ→n, etc.), replaces all non-alphanumeric→`_`, collapses double underscores, strips leading/trailing underscores. Example: `"Café Leche"` → `"cafe_leche"`.

This is an intentional divergence for cross-platform filesystem compatibility. Both approaches are internally consistent. de1app's approach can produce filenames with Unicode characters that may cause issues on some platforms.

### Profile Import: Decenza vs de1app

| Aspect | de1app | Decenza |
|--------|--------|---------|
| Duplicate detection | Filename existence only | Filename + content comparison |
| Duplicate resolution | Append `_YYYYMMDD_HHMMSS` timestamp | User dialog: overwrite/save-as-new/rename |
| `saveAsNew` naming | N/A (uses timestamp) | Smart: author → step count → numbered suffix |
| Visualizer category | Auto-prefixes `"Visualizer/"` to title | No category prefix |
| Comparison fields | DYE viewer: 5 textual lines per step | 6 profile fields + all frame fields |
| Profile comparison for imports | None (file existence only) | Full frame-by-frame comparison |
