## Context

The current `SettingsDye` bean model conflates three responsibilities: a live editable DYE state (what feeds the next shot), saved preset shortcuts (quick-fill from past coffees), and canonical Bean Base links. This produces persistent UX friction — a computed `beansModified` divergence check, save-to-preset prompts, and ambiguity about which state is authoritative. Users also have no model for bag lifecycle (freeze/defrost, portions, notes), which matters for the significant subset who freeze beans.

Bean presets live in QSettings as a JSON array under the `bean/presets` key, with the selection index in `bean/selectedPreset`. Shot records already snapshot bean fields at save time (`bean_brand`, `bean_type`, `roast_date`, `roast_level`, `beanbase_json` columns), so historical data is safe regardless of what presets do. The `add-bean-base-integration` change added `beanBaseId` and `beanBaseData` to presets; this change evolves that work into a first-class bag model.

DB migrations live in `src/history/shothistorystorage.cpp`; the current schema version is 18, so this change is migration 19.

## Goals / Non-Goals

**Goals:**
- Replace presets with bags — one concept, no live-state ambiguity
- Add lifecycle fields (frozenDate, defrostDate, notes, startWeightG) to bags
- Deliver a unified Change Beans search dialog (Bean Base + shot history, quality-ranked)
- Read-only bean summary in shot contexts — show less when more is known
- Visualizer Coffee Management sync for users with CM enabled
- Full backwards-compatible migration from existing presets + shot history

**Non-Goals:**
- "Next Shot" / Equipment profile abstraction — grinder fields stay on bags as "last used" for now; extraction to a separate Equipment concept is a follow-on change
- Upstream PR to miharekar/visualizer for `coffee_management_enabled` in `/api/me` — noted as future work
- Per-profile active bag — one global active bag for now
- Loffee Labs Bean Base API key requirement — canonical search remains keyless via Visualizer autocomplete
- Automatic Visualizer upload retry queue — Visualizer uploads remain fire-and-forget per upload cycle (only ShotServer has a queue); bag sync idempotency relies on persisted `visualizerBagId`, not a retry mechanism

## Decisions

### Decision: SQLite for bags, not QSettings

QSettings is a flat key/value store; bags need relational queries (history search, Visualizer sync state). The app already uses SQLite for shots via `withTempDb()`. Moving bags to DB gives us indexed queries for the history lane, consistent I/O patterns, and clean migration tooling. The `coffee_bags` table lives in the shot history database, and the storage class (`src/history/coffeebagstorage.{h,cpp}`) follows the `ShotHistoryStorage` background-thread pattern.

**Alternative considered:** Keep bags in QSettings as the preset array was. Rejected — the history search (SELECT DISTINCT from shots grouped by canonical id) cannot be efficiently expressed against QSettings.

### Decision: Migration 19 converts presets to bags on first launch

On startup, if the `bean/presets` QSettings key is present, convert each preset to a bag (inInventory = true, no frozenDate/defrostDate). Field mapping:
- `brand` → `roasterName`, `type` → `coffeeName`, `roastDate`/`roastLevel`/`beanBaseId`/`beanBaseData` map directly
- Grinder fields (`grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting`) map directly
- `name` (user-chosen preset label) → stored in the bag's `notes` when it differs from "{brand} {type}"; otherwise dropped
- `barista` → intentionally dropped (per-shot concept, already snapshot on every shot)
- `showOnIdle` → intentionally dropped (all inventory bags show on idle; inventory is naturally small because empties are removed)

`bean/selectedPreset` (index) maps to `activeBagId` (the DB id of the converted row). Clear both QSettings keys only after a successful DB transaction. Shots are untouched — they already snapshot bean fields.

**Re-appearing presets (device transfer / backup restore):** Device-to-device transfer and backup restore can reintroduce the `bean/presets` key on a device where `coffee_bags` is already populated. In that case, import presets that do not match an existing bag (case-insensitive roasterName + coffeeName + roastDate); skip matches; log the outcome; clear the key after commit. Never skip the import wholesale just because the table is non-empty — that silently loses data.

**Alternative considered:** Keep QSettings presets as a read-only legacy source alongside new bags. Rejected — dual sources would recreate the confusion this change is eliminating.

### Decision: Bean/grinder edits write through to the active bag; shot save stamps dose

The bag IS the state. Any pre-shot edit to grinder fields (brew dialog, bag card edit) writes directly to the active bag — there is no intermediate live-DYE copy that can diverge, which is precisely the failure mode this change eliminates. At shot save, the active bag's `doseWeightG` and `yieldTargetG` are additionally stamped from the actual shot values (dose may come from SAW/profile settings rather than a manual edit). No explicit "save to bag" action, no `beansModified` computed property.

**Alternative considered:** Only write to the bag at shot save (edits live in DYE state until then). Rejected — recreates the live-state-vs-saved divergence under a new name.

### Decision: Active bag is global, not per-profile

One `activeBagId` in `SettingsDye` (replacing `bean/selectedPreset`). Profiles do not remember which bag was last used.

**Alternative considered:** Per-profile active bag stored on the profile record. Deferred — the "Next Shot" follow-on (Bag × Equipment = Recipe per Profile) is the right home for that association. Implementing it now without the full abstraction would create a partial model.

### Decision: Context-dependent Change Beans semantics

The Change Beans dialog behaves differently depending on where it is opened:
- **Brew settings / Beans window / idle page**: selection sets `activeBagId` (affects the next shot)
- **Post-shot review**: selection sets `activeBagId` AND retroactively updates the just-saved shot's snapshot (bean fields, bagId, frozen/defrost dates) — this is the "I pulled the shot with the wrong bag selected" fix path
- **Historical shot detail**: selection updates only that shot's snapshot; `activeBagId` is untouched. This preserves the existing ability to fix bean metadata on past shots (which `shots_update` via MCP also retains).

### Decision: History lane via a real beanbase_id column on shots

The canonical UUID currently lives only inside the `beanbase_json` blob — there is no `beanbase_id` column. Migration 19 adds a nullable `beanbase_id` TEXT column to `shots`, backfilled via `json_extract(beanbase_json, '$.id')`, plus an index on `beanbase_id` (an index on `(bean_brand, bean_type)` already exists as `idx_shots_bean`). New shot saves populate the column directly. The history lane is then `SELECT bean_brand, bean_type, beanbase_id, MAX(timestamp) ... GROUP BY COALESCE(beanbase_id, bean_brand || '|' || bean_type)` — indexed, no JSON parsing at query time. This gives users their full coffee rolodex from day one — no "history starts after the upgrade" cliff.

**Alternative considered:** `json_extract()` at query time over all shots. Rejected — unindexable without a generated column, and a real column is simpler than a generated one given we control all write paths.

### Decision: Bags are editable in place

The Bag Details form has two modes. **Edit mode** (from a bag card's Edit action) modifies the existing row in place — typo fixes, adding a roast date later, updating notes — without touching `activeBagId` or any shot snapshots. **Create mode** is used by the Change Beans dialog (Tier 1–5). A per-card "New Bag" save-as shortcut existed briefly but was removed (it just added clutter); "bought the same coffee again" flows through Change Beans — finish the old bag, then the coffee surfaces as a history/canonical result that pre-fills the form.

### Decision: Roast date is optional — blank, never inferred, but skippable

The roast date field is always blank on bag creation and never pre-filled from any source (a new bag is a new roast date), but it is not required: supermarket beans and gifts have unknown roast dates, and today's field is optional. The summary silences an absent roast date rather than showing a placeholder, and edit mode allows adding it later. (Resolved from open questions — user decision.)

### Decision: Inventory bags are Tier 0 in the Change Beans dialog

The dialog's primary job is *switching* bags, not only creating them. Bags currently in inventory appear first (Tier 0, "In inventory" label), shown even with an empty query; selecting one applies immediately with no details form. A history/canonical result that corresponds to an existing inventory bag is absorbed into its Tier 0 entry — the dialog never offers to re-create a coffee already in inventory. Without this, the headline "wrong bag selected" fix path would force the user to duplicate a bag and re-type its roast date.

### Decision: Merge logic for Change Beans dialog results

A result present in both history and Bean Base canonical (matched on `beanBaseId` or case-insensitive roaster+name) is shown as a single Tier 1 entry with both source labels. The merged result pre-fills grinder/dose from history and canonical attributes from Bean Base. Within the history lane, the same coffee appearing both linked and unlinked (shots before/after canonical linking) is also merged into one entry.

**Alternative considered:** Show history and Bean Base as separate sections with dividers. Rejected by user during design exploration — unified ranked list with source labels is cleaner.

### Decision: Bag selection applies dose/yield to the next shot

`doseWeightG`/`yieldTargetG` on the bag are not passive metadata — selecting a bag applies them as the next shot's dose and target weight (the values that feed brew-by-ratio / SAW via `ProfileManager`, today sourced from `dyeBeanWeight`/`dyeDrinkWeight`). "The bag knows my last setting" must include the setting that actually drives the machine. A newly created bag with no dose yet inherits the current global values and adopts them on first edit or shot. The `dyeBeanWeight`/`dyeDrinkWeight` properties become read-through proxies of the active bag (falling back to the stored global values when no bag is active) so `ProfileManager` and existing QML consumers keep working during the transition.

### Decision: Bags survive device transfer and backup restore

The device-to-device transfer and backup/restore path (`ShotHistoryStorage::importDatabaseStatic`) copies an explicit column list of `shots` + `shot_samples` and remaps shot row ids — it would silently drop the `coffee_bags` table and the new shot columns. This change extends the importer to migrate `coffee_bags` rows and remap `shots.bag_id` accordingly (bag ids change on import just like shot ids). While touching that column list, the existing omission of `stopped_by` and `beanbase_json` (a pre-existing bug — restored shots silently lose their Bean Base link) is fixed too. `SettingsSerializer` currently exports/imports presets via the removed `SettingsDye` APIs (`settingsserializer.cpp:128–148, 523–545`); its import path is rewritten to translate a legacy `beans.presets` JSON section into bag rows at import time (the raw QSettings key never reappears via this path, so key-detection alone is insufficient), and `dye/activeBagId` is excluded from settings export — a DB row id is meaningless on another device.

### Decision: Idle-page bean widget shows inventory bags

`BeansItem.qml` (the idle-page layout widget) currently shows preset pills filtered by `showOnIdle`. It is reworked to show all inventory bags (`inInventory = true`) as pills; tapping a pill sets `activeBagId`. The `showOnIdle` flag is dropped — "in inventory" is the new visibility criterion, and marking a bag empty removes it from both the Beans window and the idle widget.

### Decision: Visualizer CM detection via single-field PATCH probe (spike-verified 2026-06-11)

**Spike findings (task 1.1, run against the live API with the user's premium / CM-disabled account + the open-source controllers):**

- **Bag CRUD is premium-gated, NOT CM-gated** (`check_premium!` in `api/coffee_bags_controller.rb`; `coffee_management_enabled` appears nowhere in the API layer). A premium user with CM off can list/create/update/delete bags freely.
- **GET /api/coffee_bags non-empty does NOT mean CM is on.** Toggling CM on makes the server auto-create roasters+bags from the user's whole shot history (`CoffeeManagementUpdateJob#enable_coffee_management`, find-or-create by brand/type/roast_date) and link the shots; toggling it off nulls every shot's `coffee_bag_id`/`canonical_coffee_bag_id` but keeps the bags. The user's own account: CM off, 95 bags.
- **`coffee_bag_id` on shot PATCH is permitted only when `coffee_management_enabled`** (`update_shot_params` in `shots/editing.rb`). When CM is off: a PATCH whose `shot{}` contains ONLY `coffee_bag_id` → **HTTP 400** (Rails `params.expect` finds nothing permitted); mixed with permitted fields → 200 with the key silently dropped. This 400-vs-200 is the deterministic CM probe.
- **GET /api/shots/{id} does NOT return `coffee_bag_id`** — but linking observably rewrites `bean_brand`/`bean_type`/`roast_date`/`roast_level` from the bag (`Shot#refresh_coffee_bag_fields`), and linking a bag also auto-sets `canonical_coffee_bag_id` from the bag.
- **Bag schema** (flat JSON, no wrapper; 201 echoes the object): `name`, `roaster_id` (**required** — 422 "Roaster must exist"), `roast_date`, `roast_level`, `country`, `region`, `farm`, `farmer`, `variety`, `elevation`, `processing`, `harvest_time`, `quality_score`, `tasting_notes`, `place_of_purchase`, `frozen_date`, **`defrosted_date`** (not defrost_date), `url`, `notes`, `canonical_coffee_bag_id`, `metadata`, `archived_at`. Roaster POST: `{name, website, canonical_roaster_id}` → 201.
- **The upload POST ignores embedded `coffee_bag_id`** → a post-upload PATCH is required. Shot PATCH needs `Accept: application/json` (Rails `request.format`, independent of Content-Type) and a `{"shot": {...}}` body.
- **DELETE exists** for bags, roasters, and shots (all `{success:true}`).
- `/api/me` exposes only id/name/public/avatar (upstream PR still worthwhile). Rate limits: 200 req/user/10 min — upload-time sync is well within budget.
- **Positive path verified after the user enabled CM on the same account (2026-06-11):** the enable-job auto-created 11 bags from shot history (95 → 106); the single-field probe PATCH on a throwaway shot returned **200** (vs 400 with CM off — both probe outcomes now observed on one account), and the server rewrote the shot's bean fields from the linked bag (`refresh_coffee_bag_fields` live-confirmed). Note: `shots.roast_date` comes back formatted in the user's display preference (e.g. "06.05.2026"), but the bag detail endpoint returns ISO `yyyy-MM-dd` — our find-or-create compares bag dates only, so ISO comparison holds.

**Resulting design:**

- Detection: at first upload after the shot POST succeeds, send a probe PATCH to OUR OWN just-uploaded shot with `shot{coffee_bag_id}` as the only key, using any existing bag id from `GET /api/coffee_bags` (the user's account already has bags in every CM-relevant scenario; if zero bags exist, create the real bag first — POST 403 = `NO_COFFEE_MANAGEMENT` i.e. not premium). Probe result: 200 → `COFFEE_MANAGEMENT_ACTIVE`; 400 → `PREMIUM_NO_CM`; 401/429/5xx/network → stays `UNKNOWN`, retry next cycle. Cache; reset to `UNKNOWN` on each connection test.
- When CM active: find-or-create roaster (`canonicalRoasterId` from the blob, else GET match by name, else POST) → find bag by roaster+name+roast_date in `GET /api/coffee_bags?roaster_id=` before POSTing (the server's own enable-job dedupes that way; the API create does not) → PATCH the shot with `coffee_bag_id` (+ `canonical_coffee_bag_id`, always safe).
- When `PREMIUM_NO_CM` or `NO_COFFEE_MANAGEMENT`: today's behavior exactly — `canonical_coffee_bag_id` only, and do NOT create bags (a CM-off user's bag list is dormant server state; adding to it is clutter they never see).
- Map our `defrostDate` → `defrosted_date` on the wire; `startWeightG` stays local (no server field — `metadata` exists but is the user's own free-form space, leave it alone).

### Decision: canonical_roaster_id added to beanBaseData blob

`parseCanonicalPayload` in `beanbaseclient.cpp` gains a `canonicalRoasterId` key, stored in `beanBaseData` alongside origin/variety/etc. This enables Visualizer roaster creation with the verified badge. Currently the roaster UUID is only in the in-memory `m_roasterUuidCache` and lost on restart.

### Decision: startWeightG is local-only

Visualizer's CoffeeBag has no weight field. `startWeightG` is stored locally, used to display approximate remaining weight (startWeightG minus sum of shot doseWeightG for this bag). It is not included in Visualizer sync payloads.

## Risks / Trade-offs

**Migration data loss** — QSettings preset array is the only source of truth for presets. If the DB write fails mid-migration, presets are lost.
→ Mitigation: Write all rows to DB first; only clear QSettings keys after transaction commits. Log migration outcome. Merge-import handles presets reintroduced by device transfer or restore.

**Orphaned Visualizer bag on CM probe failure** — If the PATCH or read-back after the 201 fails (network drop), the CM state stays unconfirmed while a bag exists remotely.
→ Mitigation: Because the probe is the real bag, an orphan is not garbage — it is the user's actual coffee. On next launch, if `visualizerBagId` is set but CM state is unconfirmed, re-run the confirmation (PATCH + read-back) rather than deleting. Only the confirmed `PREMIUM_NO_CM` outcome deletes.

**History search performance with large shot libraries** — Querying all shots for unique coffees on every dialog open could be slow on large libraries (1000+ shots).
→ Mitigation: Real `beanbase_id` column + indexes in migration 19 (see decision). Cap history results at 50 unique coffees sorted by recency. Search is debounced (300ms) before querying. All queries on background threads via `withTempDb()`.

**Grinder setting regression for users who dial in mid-bag** — Write-through means every edit and every shot updates the bag's grinder setting. A user who deliberately goes back to a previous setting for a comparison shot will see the bag's stored setting follow them.
→ Accepted trade-off. This matches user expectation ("the bag knows my last setting"). Power users who track dial-in trajectory have shot history for that.

**Loss of the modified-state workflow for users who relied on it** — Some users may have used the preset-divergence display to preview settings without committing. That affordance disappears.
→ Accepted. The Change Beans dialog provides a safer explicit selection flow. The modified state was a symptom of the unclear model, not a feature.

## Migration Plan

1. **DB migration 19** (in `src/history/shothistorystorage.cpp`, runs at app launch on upgrade):
   - Create `coffee_bags` table (schema in `coffee-bag-model` spec)
   - Add nullable `bag_id`, `frozen_date`, `defrost_date`, `beanbase_id` columns to `shots`
   - Backfill `shots.beanbase_id` from `json_extract(beanbase_json, '$.id')`
   - Add indexes: `shots(beanbase_id)`, `shots(bean_brand, bean_type)`
   - Read `bean/presets` from QSettings; INSERT each preset as a bag row (field mapping per decision above)
   - Map `bean/selectedPreset` index → migrated bag's DB id. The migration runs inside `ShotHistoryStorage` after `SettingsDye` is already constructed and cached, so a raw QSettings write would bypass the cache and NOTIFY (the migration-16 precedent at `shothistorystorage.cpp:863` has this flaw). The storage exposes the migrated active id and `SettingsDye` adopts it through its setter.
   - On transaction success: remove `bean/presets` and `bean/selectedPreset` from QSettings
   - On failure: log error, leave QSettings intact (app falls back to empty bag list)
   - Note: the `json_extract` backfill is one-time synchronous startup cost — measure on tablet hardware with a large library before shipping.

2. **Merge-import on key reappearance**: if `bean/presets` reappears later (old-version device transfer), import non-duplicate presets as bags; never skip wholesale. The dedup key (roasterName+coffeeName+roastDate) is deliberately conservative — presets carry no lifecycle data, so a skipped duplicate loses nothing.

3. **Settings import path**: `SettingsSerializer::import` translates a legacy `beans.presets` JSON section into bag rows at runtime (it writes through `SettingsDye` methods, not raw keys, so step 2's key detection never fires for this path). `dye/activeBagId` is excluded from export/import.

4. **DB transfer path**: `importDatabaseStatic` migrates `coffee_bags` rows with id remapping applied to `shots.bag_id`; the existing `stopped_by`/`beanbase_json` column omissions are fixed in the same pass.

5. **Rollback**: No automatic rollback. If migration fails, app starts with empty inventory and the user can recreate bags. QSettings is preserved until migration succeeds.

## Open Questions

- **Bag ordering in inventory** — Sort by most recently used? Alphabetical? User-draggable? Decision deferred to implementation; most-recently-used is the sensible default.
- **Multiple bags of same coffee simultaneously** — e.g., one open bag and one frozen bag of the same roast. Inventory shows both; both appear as separate Tier 0 entries in the dialog (distinguished by roast date / freeze state). No deduplication across inventory bags.
- **Upstream Visualizer PR** — `GET /api/me` returning `coffee_management_enabled` + `premium` would simplify detection significantly. Tracked as future work; not blocking this change.
