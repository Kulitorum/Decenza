## Why

The current bean management model conflates three separate concepts — a live editable state, saved preset shortcuts, and canonical bean links — creating persistent confusion about which is authoritative, whether you've saved your changes, and why the same coffee appears twice. Replacing presets with a first-class **bag** concept gives users one clear thing to manage, eliminates the modified-state ambiguity entirely, and unlocks lifecycle tracking (freeze/defrost, portion management, notes) that has no home in the current model.

## What Changes

- **BREAKING** `SettingsDye` bean presets replaced by a `CoffeeBag` model stored in the local SQLite database (migration converts existing presets to bags on first launch)
- **BREAKING** Editable bean fields removed from BeanInfoPage, brew settings, and post-shot review; replaced by a read-only bean summary + "Change Beans" button everywhere
- New **Beans window** shows a live inventory of bags currently in use (inInventory = true); cards adapt to available data (canonical-linked bags show richer detail)
- New **Change Beans dialog** with unified search across Bean Base canonical database and full shot history, quality-ranked results, merge logic for canonical+history matches, and a pre-filling bag details form
- New **bag lifecycle fields**: `frozenDate`, `defrostDate` (current portion only), `notes`, `startWeightG`
- New **Next Portion** one-tap action on frozen bag cards — stamps `defrostDate = today`, no dialog
- New **Mark as Empty** action removes a bag from inventory; historical shots retain their snapshots
- Bags are **editable in place** (Edit action on the card — typo fixes, adding a roast date later) and usable as **save-as templates** ("New Bag" action pre-fills the creation form from an existing bag, roast date blank)
- Roast date on bag creation is always blank and never inferred, but **optional** — unknown roast dates (supermarket beans, gifts) don't block bag creation
- Grinder settings (`grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting`) and dose persisted per bag as "last used" — edits write through to the active bag, dose/yield stamped on each shot, no save prompt
- Change Beans is context-aware: from post-shot review it also fixes the just-saved shot's snapshot ("wrong bag selected" path); from historical shot detail it re-links only that shot without touching the active bag
- Idle-page bean widget (`BeansItem.qml`) reworked from showOnIdle-filtered preset pills to inventory bag pills
- Read-only bean summary follows a **show less when we know more** principle: full canonical + freeze data collapses to a single dense line; absent data collapses to silence, not empty fields
- Visualizer Coffee Management sync: when `coffee_management_enabled` is detected, bags are created on Visualizer at shot upload time and shots are linked via `coffee_bag_id` instead of `canonical_coffee_bag_id` alone
- `canonical_roaster_id` added to beanBaseData blob (one-line fix in `parseCanonicalPayload`) to enable verified roaster linking on Visualizer bag creation
- Shot history drives the history lane in the Change Beans search from day one — full backwards compatibility, no data loss

## Capabilities

### New Capabilities

- `coffee-bag-model`: The `CoffeeBag` data model — identity, lifecycle, last-used grinder/dose, Visualizer sync fields, DB schema and migration from presets
- `bag-inventory-view`: Beans window reimagined as a bag inventory — cards, canonical badge display, Next Portion action, Mark as Empty, Add New Bag entry point
- `change-beans-dialog`: Unified Bean Base + history search dialog with quality-ranked results, merge logic, source labels, pre-filling bag details form, and manual entry fallback
- `bag-read-only-summary`: Read-only bean summary component used in brew settings, post-shot review, and shot detail — adapts to data confidence, "show less when more known"
- `bag-freeze-lifecycle`: Freeze/defrost tracking — frozenDate, defrostDate (current portion), Next Portion quick action, freeze toggle in bag creation form
- `visualizer-coffee-management`: Visualizer Coffee Management sync — CM detection via GET /api/coffee_bags + upload-time probe, find-or-create Roaster + CoffeeBag, PATCH shots with coffee_bag_id

### Modified Capabilities

- `visualizer-upload-persistence`: Shots now optionally carry `coffee_bag_id` (when CM enabled) in addition to `canonical_coffee_bag_id`; upload logic gains find-or-create bag path
- `shot-save-filter`: Shot snapshots must capture bag fields (frozenDate, defrostDate) at save time in addition to existing bean fields

## Impact

**C++ / Settings:**
- `src/core/settings_dye.{h,cpp}`: bean preset CRUD methods removed; `bean/presets` + `bean/selectedPreset` QSettings keys migrated to DB bags on first launch; live DYE bean state replaced by `activeBagId` with edits writing through to the bag
- New `src/history/coffeebagstorage.{h,cpp}`: `CoffeeBag` value type + DB CRUD via `withTempDb()`, following the `ShotHistoryStorage` background-thread pattern
- `src/history/shothistorystorage.cpp`: migration 19 (coffee_bags table, new shots columns, preset import)
- `src/network/beanbaseclient.cpp`: `parseCanonicalPayload` gains `canonical_roaster_id` field
- `src/network/visualizeruploader.{h,cpp}`: gains CM detection state, find-or-create Roaster/CoffeeBag, `coffee_bag_id` on shot PATCH

**QML:**
- `qml/pages/BeanInfoPage.qml`: full rewrite as inventory list
- New `qml/components/BagCard.qml`, `qml/components/ChangeBeansDialog.qml`, `qml/components/BeanSummary.qml` (dialogs live in `qml/components/`, matching `BrewDialog.qml`)
- `qml/pages/PostShotReviewPage.qml`, `qml/components/BrewDialog.qml`: bean editable fields replaced by `BeanSummary` + Change Beans button
- `qml/pages/ShotDetailPage.qml`: bean block becomes `BeanSummary` + re-link action (updates that shot only)
- `qml/components/layout/items/BeansItem.qml`: idle-page preset pills → inventory bag pills

**Database:**
- Migration 19 (in `shothistorystorage.cpp`): `coffee_bags` table; migrate `bean/presets` QSettings array → DB rows
- Shots table: nullable `bag_id`, `frozen_date`, `defrost_date`, `beanbase_id` columns; `beanbase_id` backfilled from the `beanbase_json` blob and indexed for the history search lane

**MCP:**
- `shots_get_detail` gains `frozenDate`, `defrostDate` in bean block
- New `bag_list`, `bag_update`, `bag_select` tools (inventory management + setting the active bag)
- Bean block in `mcpresources.cpp` re-sourced from the active bag; `settings_get`/`settings_set` dye bean/grinder keys become active-bag read/write-throughs so agent dialing workflows keep working (`currentBean` in `mcptools_dialing.cpp` is unaffected — it is built from the resolved shot record, not live DYE)

**Transfer/backup:**
- `ShotHistoryStorage::importDatabaseStatic`: migrate `coffee_bags` with `shots.bag_id` id-remap; fix existing `stopped_by`/`beanbase_json` column omissions
- `SettingsSerializer`: legacy `beans.presets` JSON section translated to bag rows on import; `dye/activeBagId` excluded from export
