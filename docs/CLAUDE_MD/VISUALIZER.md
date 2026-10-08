## Visualizer Integration

### DYE (Describe Your Espresso) Metadata
- **Location**: `qml/pages/PostShotReviewPage.qml` and the bag editor (`qml/components/ChangeBeansDialog.qml`)
- **Settings**: `SettingsDye` (`src/core/settings_dye.h`) — sticky between shots
- **Auto-show**: Settings → Machine → App Behavior → "Edit after shot"

Fields that reach Visualizer: bean brand/type, roast date/level, grinder (from the
equipment package) and setting (+ rpm), dose, yield, TDS, EY, enjoyment, notes,
barista, taste taps (as CVA scores), and the canonical bean link.

### Shot Upload (VisualizerUploader)

- **Location**: `src/network/visualizeruploader.h/.cpp`
- **Endpoint**: `POST https://visualizer.coffee/api/shots/upload` (multipart form-data)
- **Auth**: HTTP Basic Auth (username:password base64)
- **Update**: `PATCH https://visualizer.coffee/api/shots/{id}` (JSON body)
- **One builder**: `buildHistoryShotJson()`, from the saved row, for every upload. A live builder
  (`buildShotJson()`) existed until add-decent-shot-upload Stage 2 and drifted from it (the history
  copy once dropped `temperature.mix`). What only it sent now comes from the row (`by_weight_raw`,
  stored in the sample blob) or the device at upload time (`machine_state`).
- **When**: decided by `ShotUploads` (`src/network/shotuploads.h`), shared with the Decent account.
  `VisualizerUploader` is a `ShotUploadDestination`: `sendSavedShot()` PATCHes a shot that has a
  `visualizer_id` and uploads it otherwise, so a shot is never uploaded twice; a PATCH answered 404
  (deleted on visualizer.coffee) clears the link and uploads afresh. Requests are one at a time; do
  not call the upload or PATCH paths around it. The one exception is the migration-16 back-sync,
  which PATCHes the rating directly; a job never mistakes that PATCH for its own.
- **Which fields a PATCH sends** (`buildShotUpdateBody(shot, fields)`): an automatic update sends
  only the fields edited here since the last send (`shots.visualizer_dirty`, below); the Upload
  button sends all of them. A sent field the user cleared goes as JSON `null`.
- **Errors**: `apiErrorMessage()` shows Visualizer's own JSON `error` when it sends one — that is
  how a disabled account (403), the rate limit (429) and a validation failure (422) explain
  themselves.

#### Notes are rich text on Visualizer, plain text here

Since 2026-08-02 Visualizer stores notes as HTML, and each route reads them differently
(`src/network/visualizernotes.h`):

| Route | Visualizer reads the value as | Decenza sends |
|---|---|---|
| Upload (`app.data.settings.espresso_notes`) | Markdown (GFM, hard wraps) | `escapeMarkdown()` — `*`, `1.` etc. stay literal |
| Shot / bag PATCH | HTML (a bare newline is whitespace) | `plainToHtml()` — `<p>`, `<br>` |
| Every read (shot, bag, recovery) | returns HTML | `htmlToPlain()` on the way in |

Notes uploaded before the escaping came back rendered ("1. finer" as a list). `sameNotes()`
ignores Markdown syntax and list numbering, so a pull does not mistake that for an edit.

#### Barista is `my_name`

Visualizer's parser reads the barista from `app.data.settings.my_name` only (de1app's key,
`Parsers::Base#build_shot`). A `barista` key there, or at the root, is ignored — which is how
every uploaded Decenza shot lost its barista until 2026-10.

### Two-way sync (VisualizerShotSync)

Edits made on visualizer.coffee — notably in its Journal table, which bulk-edits up to 100
shots — come back to Decenza. `src/network/visualizershotsync.{h,cpp}`, rules in
`src/network/visualizersync.h` (pure, unit-tested).

- **Runs** at startup, every 30 min and on account connect, only while Visualizer is on and
  **automatic update** is on (the same switch that sends edits). Requests are paced by
  `VisualizerUploader::kApiRequestIntervalMs` (the rate-limit budget is shared with uploads).
- **Shots**: `GET /api/shots?updated_after=<cursor>&sort=updated_at`, then
  `GET /api/shots/:id?essentials=1` for each one linked to a local shot. The cursor
  (`visualizer/pullCursor`, per account) advances only over a complete pass; the first pass on an
  account looks back 14 days. Our own uploads and PATCHes come back too and write nothing. The
  list is paged by offset over a moving sort: a list that shrinks mid-pass (a shot deleted there)
  could hide a row, so the pass ends without advancing the cursor. Past 50 pages the cursor
  re-baselines to the 14-day window rather than failing forever. A shot that will not read is
  skipped with a WARN; an account-wide failure (offline, 401/403/429/5xx) or a pulled write
  that did not save ends the pass, so the cursor stays. A pass that outlives its account (signed
  in elsewhere meanwhile) drops its cursor.
- **What a pull writes** (`shotPullChanges`): a field whose remote value differs from local, unless
  it is dirty here. A missing or null remote value never clears a local one (CVA needs Premium;
  barista never reached Visualizer before the `my_name` fix). Grinder identity and the canonical
  link are not pulled — locally they are an equipment package and a Bean Base snapshot. Nor are
  the bean fields: Visualizer rewrites a bag-linked shot's `bean_brand`/`bean_type`/`roast_date`/
  `roast_level` from its coffee bag (`Shot#refresh_coffee_bag_fields`, `roast_date` in the
  user's display format), so they are not edits. The rpm comes back off the `"2.4 1400rpm"` suffix.
- **Pull signals**: a pulled shot emits `ShotHistoryStorage::shotPulledFromVisualizer(id, previous,
  written)`, a pulled bag `CoffeeBagStorage::bagPulledFromVisualizer(id)` — never
  `shotMetadataUpdated`/`bagUpdated`, which the MCP and web handlers, AIManager, SettingsDye's
  self-write tokens and the review page's held saves all read as the result of their own write.
  ShotUploads forwards a pulled shot to the other destinations (Decent); the exporter rewrites it.
- **A push overtakes a read**: `VisualizerUploader::shotPushGeneration`/`bagPushGeneration` count
  sends per item. The pull snapshots the count before each GET (bags: before the list) and drops
  the read if it moved — the read may predate our own change, which comes back next pass anyway.
- **Dirty tracking** (migration 43): `updateShotMetadataStatic` sets a `VisualizerSync::Field` bit
  in `shots.visualizer_dirty` for each field whose value an edit changes (compared in SQL, NULL and
  "" equal) and, only then, bumps `visualizer_dirty_seq`. A successful send clears only the bits it carried, and
  only if the seq is unchanged — an edit that landed while the request was out keeps its bits. A
  pull does not mark anything. Both columns travel with backups.
- **Coffee bags**:
  - **Archive, both ways** (API since visualizer `0668577`, our
    [miharekar/visualizer#262](https://github.com/miharekar/visualizer/issues/262)): `archived_at` is a key of
    `visualizer_seen` (below). The pull reads every bag's `archived_at` from the paged bag list
    (not one read per bag) and acts only on a change — an archive there marks the bag finished
    here, a restore puts it back (`bagArchivePullChanges`). Finished bags are listed under "Show finished" on the Beans page (app and web), where Restock opens the new-bag form prefilled from one. First sight records the state without
    acting: a difference that predates sync is not an archive. A bag push carries `archived_at` only when the bag's
    inventory state here disagrees with that value (`bagArchiveForPush`: now, or null to
    restore), and records what the reply says; so an edit to anything else never moves an
    archive the pull has not applied yet. `inInventory` is therefore a Visualizer-pushed field.
  - **Fields, both ways**: `coffee_bags.visualizer_seen` is a JSON object of each attribute's
    value as Visualizer was last known to hold it. A side *changed* a field when its value
    differs from that. A push (`bagPushBody`) sends only fields changed here — a clear as
    `null` — and records them as seen once accepted. A pull (`bagFieldPullChanges`, each bag
    still in inventory, `GET /api/coffee_bags/:id`) takes a field changed there and not here;
    changed on both sides, the local edit stays and goes out next. A field never seen (a bag
    synced before this existed) is pushed only when set here and pulled only into a blank.
    Name and the canonical link are pushed but never pulled, and never sent as `null`.
  - **Photo**: whichever side lacks one gets the other's. Visualizer's signed `image_url`
    (expires in 5 min) goes into the bag photo cache under `BeanBaseClient::imageKeyFor()`; a
    cached photo is uploaded as `coffee_bag[image]` multipart. Neither side's photo is replaced.
    A 403 stops photo uploads for the session.
  - Pulled values are decided and written on the bag worker against the row as it stands
    (`CoffeeBagStorage::requestApplyVisualizerPull`), so a local edit queued first wins; it emits
    `bagsChanged`, `bagPulledFromVisualizer` and the finished/restocked lifecycle signals, and
    commits the fields with the `visualizer_seen` merge.
- **Failures** are logged once at WARN per distinct message, repeats counted; the recovery is an
  INFO carrying the count.

### Fresh when viewed

A screen showing synced data reads it from Visualizer when it opens, instead of waiting for the
next pass: the review and detail pages call `VisualizerShotSync::refreshShot`, the bag editor
`refreshBag`, the bean inventory `refreshBags` (also the web `/shot/<id>` and `/beans` pages;
skipped within 3 min of the last bag pass, which costs a request per bag in use).
Editors never write back what they did not change: the review page, the bag editor (the detail
blob key by key, `beanBaseDataPatch`) and the web shot editor save only the fields edited there,
and while open they take a pulled change into any field not yet touched. The review page also
moves its undo frames, so Undo cannot write the old value back. All background requests share one pacer
(`VisualizerUploader::paceApiRequest`), so concurrent passes keep to the rate budget together.

### Bag edits reach Visualizer without a shot upload

A bag edit (editor, AI fill, MCP `bag_update`) is pushed at once unless Coffee Management is
known to be off. While CM is unconfirmed a roaster rename re-points only to an existing roaster;
none is created. It used to wait for a shot upload to confirm CM, so on a device that never uploads
a shot an edit never arrived. A parked (failed) push is retried after each upload and at the end of
each sync pass.


**Optional series are omitted, never zero-filled.** `interpolateGoalData()`
returns an array of zeros for an empty input vector, so an unguarded
`temperature["mix_goal"] = interpolateGoalData(...)` would upload a flat 0 °C
goal line for every shot that predates the series — and for every shot imported
from a de1app `.shot` file, since de1app has no `espresso_temperature_mix_goal`
vector. Guard each optional series with `!isEmpty()`; Visualizer reads a missing
key as legacy data and draws nothing.

#### Result persistence (authoritative C++ path)

The returned Visualizer shot id is persisted to the originating local
row by **`MainController`**, not by any UI page. The upload carries the
local `shots.id`; on success
`VisualizerUploader::uploadSucceededForShot(dbShotId, visualizerId,
url)` fires and `MainController` calls
`ShotHistoryStorage::requestUpdateVisualizerInfo(...)`. The shot-end
auto-upload is dispatched from the `shotSaved` callback through
`ShotUploads::shotSaved` (once the row id is known) — never before save, so it cannot orphan. The
`PostShotReviewPage`'s `onUploadSucceededForShot` handler does
**not** persist (it only refreshes the UI); do not reintroduce a
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
  "temperature": { "basket": [...], "mix": [...], "goal": [...], "mix_goal": [...] },
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

#### The two temperature goals

The DE1 shot sample carries two setpoints, and Visualizer plots both:

| JSON key | DE1 field | Visualizer label | Source |
|---|---|---|---|
| `temperature.goal` | `SetHeadTemp` | Basket Temperature Goal | `ShotSample::setTempGoal` |
| `temperature.mix_goal` | `SetMixTemp` | Mix Temperature Goal | `ShotSample::setMixTempGoal` |

`temperature.goal` is `SetHeadTemp` — matching de1app, whose
`espresso_temperature_goal` vector is fed from `SetHeadTemp`. **Do not "fix"
this to the mix target**: it would silently relabel every Decenza shot already
on Visualizer. (Decaid had this one wrong and corrected it in
tadelv/reaprime#472; Decenza never did.)

`mix_goal` is newer than the rest of the temperature block — Visualizer added it
in `0bba67e`, and it has no de1app counterpart, so imported `.shot` files never
carry one.

#### app.data.settings — Critical for Visualizer Profile Extraction

The Visualizer's server-side `DecentJson` parser extracts profile fields from `app.data.settings` using a fixed list of TCL field names (`PROFILE_FIELDS`). De1app dumps its entire `::settings` array (hundreds of keys); Decenza sends a curated subset via `buildProfileSettings()`.

**DYE metadata fields**: `bean_brand`, `bean_type`, `roast_date`, `roast_level`, `grinder_model`, `grinder_setting`, `grinder_dose_weight`, `drink_weight`, `drink_tds`, `drink_ey`, `espresso_enjoyment`, `espresso_notes`, `my_name` (barista), `profile_title`

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
| `bean_notes` | Bean notes | Not sent |

### Profile Import (VisualizerImporter)
- **Location**: `src/network/visualizerimporter.cpp/.h`
- `GET /api/shots/shared?code=` with an EMPTY code and credentials lists the user's shared
  shots (multi-import relies on it); an unknown code answers 404 (visualizer `39d2219`).
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
- **The uploaded profile is the canonical format — there is only one.** `buildVisualizerProfileJson()` **delegates to `Profile::toJsonObject()`** and must not re-serialize any field itself. It previously hand-built its own copy of the payload, and the two writers drifted (the Visualizer path gained `tank_temperature` / `target_volume_count_start` / the tablet metadata while the on-disk writer never did, leaving Decenza's exported profiles unreadable by Decaid). Add new profile fields to the canonical serializer only. See "JSON Format (canonical)" in `RECIPE_PROFILES.md`.
- **`buildHistoryShotJson()` uploads the stored snapshot VERBATIM — do not "fix" it to re-serialize.** `Profile::fromJson` is not a pure decoder: it fills non-zero defaults for absent keys (`target_weight` 36.0, `maximum_pressure` 12.0) and rewrites `espresso_temperature` via the leaked-default repair. Round-tripping a historical shot through it would make that shot claim values it never ran — Visualizer-imported profiles omit `espresso_temperature` entirely, so they are the concrete victim. The snapshot is a *record*, not a profile we own. New shots are already stored canonically (`shothistorystorage.cpp` writes `profile->toJson()`), so nothing is lost by leaving old ones alone.

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
