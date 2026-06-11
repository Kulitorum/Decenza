## 1. API Verification Spike (blocks group 14)

- [x] 1.1 Verify against the live Visualizer API: (a) accepted field names on POST /api/coffee_bags (roast date, freeze/defrost dates, canonical_coffee_bag_id, roaster reference); (b) GET /api/shots/{id} returns `coffee_bag_id`; (c) DELETE /api/coffee_bags/{id} exists; (d) whether the shot upload POST accepts `coffee_bag_id` directly or a post-upload PATCH is needed; (e) both `coffee_bag_id` + `canonical_coffee_bag_id` accepted together; (f) the premium-with-CM-off combination behaves as `PREMIUM_NO_CM` assumes. Record findings in design.md and adjust specs if needed

## 2. CoffeeBag C++ Model

- [x] 2.1 Define `CoffeeBag` value struct in `src/history/coffeebagstorage.h` with all fields (identity, lifecycle, grinder, Visualizer sync, inInventory)
- [x] 2.2 Implement `CoffeeBagStorage` class (`src/history/coffeebagstorage.{h,cpp}`) with CRUD methods using `withTempDb()` + background threads, following the `ShotHistoryStorage` pattern
- [x] 2.3 Add `coffee_bags` table to migration 19 in `src/history/shothistorystorage.cpp` (current version is 18)
- [x] 2.4 Add nullable `bag_id`, `frozen_date`, `defrost_date`, `beanbase_id` columns to the `shots` table in migration 19
- [x] 2.5 Backfill `shots.beanbase_id` from `json_extract(beanbase_json, '$.id')` in migration 19; measure one-time startup cost on tablet hardware with a large library
- [x] 2.6 Add index `shots(beanbase_id)` in migration 19 (`idx_shots_bean` on brand/type already exists)

## 3. Preset → Bag Migration

- [x] 3.1 In migration 19, read `bean/presets` from QSettings and INSERT each preset as a `CoffeeBag` row (`inInventory = true`, lifecycle fields null); map preset `name` → bag `notes` when it differs from "{brand} {type}"; drop `barista` and `showOnIdle`
- [x] 3.2 Map `bean/selectedPreset` (index) to the migrated bag's DB id; expose it from storage so `SettingsDye` adopts it through its setter (a raw QSettings write from the migration would bypass the settings cache and NOTIFY)
- [x] 3.3 Clear `bean/presets` and `bean/selectedPreset` from QSettings only after the DB transaction commits successfully
- [x] 3.4 Implement merge-import for reappearing presets (old-version device transfer): import presets not matching an existing bag on case-insensitive roasterName+coffeeName+roastDate, skip matches, log outcome
- [x] 3.5 Log migration outcome; on failure leave QSettings intact and continue with empty inventory

## 4. Transfer & Backup Survival

- [x] 4.1 Extend `ShotHistoryStorage::importDatabaseStatic` to migrate `coffee_bags` rows with an id-remap applied to imported `shots.bag_id` values
- [x] 4.2 Fix the pre-existing column omissions in `importDatabaseStatic`: `stopped_by` and `beanbase_json` are currently dropped on restore; include them plus the new `frozen_date`/`defrost_date`/`beanbase_id` columns
- [x] 4.3 Rewrite the beans section of `SettingsSerializer` (`src/core/settingsserializer.cpp:128–148, 523–545`): export drops the preset section; import translates a legacy `beans.presets` JSON section into bag rows via the merge-import rules (runs at import time, mid-session)
- [x] 4.4 Exclude `dye/activeBagId` from `SettingsSerializer` export/import (DB row ids are device-local); update the dye field export/import (`:289–306`, `:720–738`) for the new model

## 5. SettingsDye Updates

- [x] 5.1 Add `activeBagId` Q_PROPERTY to `SettingsDye` (int, persisted as `dye/activeBagId`)
- [x] 5.2 Remove `selectedBeanPreset`, `beanPresets`, `idleBeanPresets`, and the computed `beansModified` property (incl. `recomputeBeansModified()`) from `SettingsDye`
- [x] 5.3 Remove `addBeanPreset`, `updateBeanPreset`, `removeBeanPreset`, `moveBeanPreset`, `getBeanPreset`, `applyBeanPreset`, `saveBeanPresetFromCurrent`, `setBeanPresetShowOnIdle`, and the `findBeanPreset*` helpers from `SettingsDye`
- [x] 5.4 Re-point the live DYE bean/grinder/dose properties (`dyeBeanBrand` … `dyeBeanBaseData`, `dyeGrinderBrand` … `dyeGrinderSetting`, `dyeBeanWeight`, `dyeDrinkWeight`) as read/write-throughs of the active bag (falling back to stored globals when no bag is active) so `ProfileManager` (`profilemanager.cpp:336, 402–410`), `ShotPlanText.qml`, `CustomItem.qml` placeholders, and MCP keep working

## 6. Fix Remaining Preset Consumers

- [x] 6.1 `qml/main.qml:831–836`: replace the startup `findBeanPresetByContent` call (currently inside `Component.onCompleted` — an unguarded failure here aborts startup including `appInitialized`)
- [x] 6.2 `qml/components/layout/LayoutItemDelegate.qml:83`: replace the `Settings.dye.selectedBeanPreset` binding in the layout-editor beans preview
- [x] 6.3 `qml/components/PresetPillRow.qml`: remove/replace the `beansModified` contract
- [x] 6.4 Rewrite the preset/`beansModified` suites in `tests/tst_settings.cpp:212–380` for the bag model
- [x] 6.5 Repo-wide grep for `beanPresets`, `selectedBeanPreset`, `beansModified`, `applyBeanPreset`, `findBeanPreset` to catch any remaining consumers

## 7. Canonical Roaster ID in beanBaseData

- [x] 7.1 In `parseCanonicalPayload` (`src/network/beanbaseclient.cpp`), add `canonicalRoasterId` key to the beanBaseData blob using the UUID from `m_roasterUuidCache`

## 8. Shot Save Modifications

- [x] 8.1 In the shot save path (`MainController::onShotEnded()` → `ShotHistoryStorage::saveShot()`), snapshot `bagId`, `frozenDate`, `defrostDate` from the active bag and populate `shots.beanbase_id` directly
- [x] 8.2 After shot save, stamp the active bag's `doseWeightG`/`yieldTargetG` from the shot values (background DB thread, no user prompt; failure logged, never blocks shot save)
- [x] 8.3 Remove `beansModified`-driven save-to-preset prompts from QML and C++

## 9. Unified Bean Search Model

- [x] 9.1 History lane implemented as `UnifiedBeanSearchModel::queryHistoryStatic` (consolidated — a separate `CoffeeBagHistoryModel` class had no other consumer): distinct coffees grouped by `COALESCE(beanbase_id, brand|type)`, capped at 50, MRU order, background thread with event-based query coalescing
- [x] 9.2 Implement `UnifiedBeanSearchModel` (`src/history/unifiedbeansearchmodel.{h,cpp}`): Tier 0 inventory lane first, then merge Bean Base autocomplete and history results using the Tier 1–5 ranking; deduplicate on `beanBaseId` or case-insensitive roaster+name; absorb history/canonical matches of inventory bags into their Tier 0 entry; merge linked+unlinked duplicates within the history lane
- [x] 9.3 Register both models with `qmlRegisterType` in `main.cpp`

## 10. Change Beans Dialog (QML)

- [x] 10.1 Create `qml/components/ChangeBeansDialog.qml` with a search field, Tier 0 inventory results shown on empty query, ranked result list, and source labels (In inventory / Bean Base / History / Both) on each row
- [x] 10.2 Selecting a Tier 0 inventory bag applies immediately — no details form, no new bag
- [x] 10.3 Implement the Bag Details form for Tier 1–5 paths: conditional field visibility (show only unknown fields), roast date blank/never inferred/optional, "More options" expander with freeze toggle and startWeightG
- [x] 10.4 Add edit mode to the Bag Details form: opens pre-filled from an existing bag, saves in place (same row, `activeBagId` and shot snapshots untouched)
- [x] 10.5 Implement context-dependent selection semantics (selection AND creation): brew/Beans/idle → set `activeBagId`; post-shot review → set `activeBagId` AND update the just-saved shot's snapshot; historical shot detail → update that shot's snapshot only
- [x] 10.6 Add `ChangeBeansDialog.qml` to `CMakeLists.txt` `qt_add_qml_module` file list

## 11. BeanSummary Component (QML)

- [x] 11.1 Create `qml/components/BeanSummary.qml`: adaptive single-line display with canonical badge, roast age, and defrost age; silences absent fields (no empty placeholders); reuses or supersedes `BeanBaseDetailsRow` (currently rendering canonical snapshots on `ShotDetailPage:781`) rather than duplicating it
- [x] 11.2 Implement the four display states: full canonical + freeze → canonical only → history only (with "Link" nudge) → no bag ("No beans selected")
- [x] 11.3 Add `BeanSummary.qml` to `CMakeLists.txt`

## 12. Beans Window (Bag Inventory)

- [x] 12.1 Rewrite `qml/pages/BeanInfoPage.qml` as an inventory `ListView` backed by `CoffeeBagStorage` filtered to `inInventory = true`
- [x] 12.2 Create `qml/components/BagCard.qml` with adaptive card layout (full canonical card vs. partial data card), "Next Portion" button for frozen bags, "Edit" action (Bag Details form in edit mode), "New Bag" save-as action (creation form pre-filled from this bag, roast date blank), and "Mark as Empty" action
- [x] 12.3 Implement "Mark as Empty": set `inInventory = false` in DB, clear `activeBagId` if the bag was active
- [x] 12.4 Implement delete for bags with zero linked shots (`shots.bag_id` count = 0); no delete action offered otherwise
- [x] 12.5 Add "Add New Bag" button that opens `ChangeBeansDialog` in creation mode
- [x] 12.6 Add `BagCard.qml` to `CMakeLists.txt`

## 13. Replace Editable Bean Fields in Shot Contexts

- [x] 13.1 `qml/components/BrewDialog.qml`: remove editable bean fields; add `BeanSummary` + "Change Beans" button; grinder/dose edits write through to the active bag
- [x] 13.2 `qml/pages/PostShotReviewPage.qml`: remove editable bean-identity fields; add `BeanSummary` + "Change Beans" with post-shot semantics; grinder/dose corrections update both the just-saved shot and the active bag (preserving today's dual-write at lines 624–638)
- [x] 13.3 `qml/pages/ShotDetailPage.qml`: replace bean block with `BeanSummary` + re-link action (updates that shot only, never `activeBagId`)
- [x] 13.4 Rework `qml/components/layout/items/BeansItem.qml` (idle-page widget): preset pills → inventory bag pills; tap sets `activeBagId`; update `qml/pages/IdlePage.qml` consumers of `idleBeanPresets`

## 14. Freeze Lifecycle UX

- [x] 14.1 Wire the freeze toggle in the Change Beans dialog bag details form: show `frozenDate` picker when enabled; store on bag creation
- [x] 14.2 Wire "Next Portion" action on `BagCard`: set `defrostDate = today` in DB, update card display immediately

## 15. Visualizer Coffee Management

- [x] 15.1 Add CM state enum (`UNKNOWN`, `COFFEE_MANAGEMENT_ACTIVE`, `NO_COFFEE_MANAGEMENT`, `PREMIUM_NO_CM`) to `VisualizerUploader`; reset to `UNKNOWN` on each connection test
- [x] 15.2 Implement the CM probe (spike-verified): after a shot upload POST succeeds and state is `UNKNOWN`, PATCH the just-uploaded shot with `{"shot":{"coffee_bag_id":"<id>"}}` + `Accept: application/json`; 200 → `COFFEE_MANAGEMENT_ACTIVE`, 400 → `PREMIUM_NO_CM`, 401/429/5xx/network → stay `UNKNOWN`; bag id from the shot's own synced bag or any id from GET /api/coffee_bags
- [x] 15.3 Implement roaster find-or-create: pass `beanBaseData.canonicalRoasterId` as `canonical_roaster_id` when creating; else GET /api/roasters name match; else POST; store in `bag.visualizerRoasterId` (zero-bag accounts: POST 403 → `NO_COFFEE_MANAGEMENT`)
- [x] 15.4 Implement bag find-or-create (CM active only): match name+roast_date in GET /api/coffee_bags?roaster_id= before POSTing; POST with verified field names incl. `defrosted_date` (maps our defrostDate), `frozen_date`, `canonical_coffee_bag_id`; never send `startWeightG`; store UUID in `bag.visualizerBagId`
- [x] 15.5 Add the post-upload PATCH link step: `coffee_bag_id` (+ `canonical_coffee_bag_id` when linked) when CM active; canonical-only for all other states; no bag/roaster creation when CM off
- [x] 15.6 Add `Accept: application/json` to the existing metadata PATCH path (works today via luck of Rails format negotiation in some paths; make it explicit)

## 16. MCP Updates

- [x] 16.1 Add `frozenDate`/`defrostDate` (ISO 8601) to the bean block in `shots_get_detail`
- [x] 16.2 Add `bag_list` MCP tool (inventory with human-readable fields per MCP conventions: `doseWeightG`, ISO dates, etc.)
- [x] 16.3 Add `bag_update` and `bag_select` MCP tools (lifecycle/metadata updates; setting the active bag); assign access levels per MCP_SERVER.md
- [x] 16.4 Re-source the bean block in `src/mcp/mcpresources.cpp:159–171` from the active bag; keep `settings_get`/`settings_set` dye bean/grinder keys working as active-bag read/write-throughs (`mcptools_settings.cpp:224–232`, `mcptools_write.cpp:500–520`) — do NOT touch `currentBean` in `mcptools_dialing.cpp` (it is correctly built from the resolved shot record, not live DYE)
- [x] 16.5 Update MCP_SERVER.md docs for the new/changed tools

## 17. Accessibility & i18n

- [x] 17.1 Add `Accessible.role`, `Accessible.name`, `Accessible.focusable`, `Accessible.onPressAction` to all interactive elements in the new components (`BagCard`, `ChangeBeansDialog`, `BeanSummary`, reworked `BeansItem`) AND fix pre-existing violations in every touched page (`BeanInfoPage`, `BrewDialog`, `PostShotReviewPage`, `ShotDetailPage`) per CLAUDE.md
- [x] 17.2 Internationalize all new user-visible text via `TranslationManager.translate()` / `Tr` components, reusing existing common keys where applicable

## 18. Tests

- [x] 18.1 Unit test migration 19: preset → bag conversion (incl. name→notes mapping, barista/showOnIdle drop), QSettings cleared on success, retained on failure, `beanbase_id` backfill
- [x] 18.2 Unit test merge-import: reappearing `bean/presets` key with non-empty `coffee_bags` imports only non-duplicates
- [x] 18.3 Unit test DB import: `coffee_bags` migrated with `shots.bag_id` remap; `stopped_by`/`beanbase_json` restored
- [x] 18.4 Unit test `SettingsSerializer`: export-side contract in tst_settings (bean identity + beans section excluded, legacy dye import ignored); the import conversion plumbing is exercised via `convertLegacyPresetSettings` in tst_coffeebags
- [x] 18.5 Unit test `UnifiedBeanSearchModel`: Tier 0 absorption of inventory matches; same coffee in both sources → one Tier 1 result; ranking order; linked+unlinked history merge
- [ ] 18.6 Unit test CM detection state machine: GET /api/coffee_bags transitions, probe POST transitions, transient errors stay `UNKNOWN`
- [x] 18.7 Unit test dose/yield stamp: shot save updates active bag fields; DB failure does not block shot save
- [x] 18.8 Context semantics live in QML (ChangeBeansDialog.applySelection); the storage primitives they compose (updateShotMetadataStatic bag keys, updateBagFieldsStatic, mergeLanes Tier 0 absorption) are unit-tested in tst_coffeebags
- [x] 18.9 Covered by tst_coffeebags insertLoadUpdateRoundTrip (update-in-place leaves other fields/rows untouched) + insertBagStatic null roast date; the form-mode wiring is QML

## 19. Documentation

- [x] 19.1 Update `docs/CLAUDE_MD/BEAN_BASE.md` with the bag model, migration, and CM sync architecture
- [ ] 19.2 Update the user manual wiki page for the new Beans window, Change Beans dialog, and freeze workflow
