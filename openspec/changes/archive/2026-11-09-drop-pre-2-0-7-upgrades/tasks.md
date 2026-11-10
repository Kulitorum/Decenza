# Tasks

Builds and tests run through the Qt Creator MCP (`mcp__qtcreator__build`, `mcp__qtcreator__run_tests`) only. A green build includes the qmllint gate. Read `docs/CLAUDE_MD/LOGGING.md` before adding any log line.

## 1. Floor constant and launch check

- [x] 1.1 Add the floor (`kSchemaVersion = 41`, release `"2.0.7"`) and the stepping-stone release `"2.1.0"` to a header-only `src/history/upgradefloor.h` (design D1), with the shared refusal text and release check. Verify: grep finds the literals `41` and `"2.1.0"` only there, outside migrations 42 and up.
- [x] 1.2 Add `ShotHistoryStorage::probeUpgradeFloor(path)` returning `NoDatabase` / `Supported` / `BelowFloor` / `Unknown`, opening through `withTempDb()` with one `SELECT MAX(version)` (D2). Verify: `_data` rows in `tst_dbmigration` for no file, schema 41, 47, 40, 23, a `shots` table with no `schema_version` row, and a file that is not a database. Each also asserts the file's tables, rows and version are unchanged afterwards. Break the comparison and watch them go red.
- [x] 1.3 Make `runMigrations()` read the version first and return false without writing when it is below the floor (D3). Log the refusal through the STORAGE helper. Verify: a `tst_dbmigration` case opens a schema-40 database through `initialize()`, which fails, and the database is byte-for-byte unchanged apart from WAL housekeeping (compare a table dump).
- [x] 1.4 Add `qml/UpgradeFloorNotice.qml` (D4): `QtQuick` + `QtQuick.Controls` only, no Decenza singletons. It has the message naming 2.1.0 and one confirm button with `Accessible.role`, `name`, `focusable` and `onPressAction`; back and close also quit. Register it in `CMakeLists.txt`. Verify: the build's qmllint gate passes. If a translation lint rejects the literal text, add a scoped exemption naming the reason.
- [x] 1.5 In `main()`, run the probe right after `setApplicationVersion()` and before every settings and database migration. On `BelowFloor`, load the notice in its own engine, `app.exec()`, and return. First confirm nothing between `QApplication` construction and that point writes settings or data. Verify: read the startup order and record it in the PR. Manual check in 6.3.

## 2. Fresh-install baseline

- [x] 2.1 Before deleting any step, capture the schema-41 baseline (D5): with a temporary, uncommitted cap at 41 in `runMigrations()`, initialise a fresh database and dump its `sqlite_master`. Commit it as `tests/data/db/schema41.db` with a handful of rows in `shots`, `coffee_bags`, `equipment_packages` and `recipes`. If a real 2.0.7 database is available, compare the dump against it and note any difference. Verify: the fixture opens and reads `schema_version = 41`.
- [x] 2.2 Rewrite `createTables()`: schema-41 DDL for `shots`, `shot_samples`, `shot_phases`, FTS and triggers; call the three storage `ensureTable*Static` functions; create any table migrations 3–41 created inline; stamp `schema_version` to 41 only when `shots` was created in this call. Verify: 2.3 passes.
- [x] 2.3 Add the equivalence test to `tst_dbmigration` (D6). Run `schema41.db` and a fresh database through `initialize()` and assert:
  - the same final version
  - the same tables, columns (name, type, default, not-null), indexes and triggers, ignoring column order
  - the fixture rows survive

  Verify: drop one column from the baseline DDL and watch it go red.

## 3. Remove migration steps 3–41

- [x] 3.1 Delete steps 3–41 from `runMigrations()` (`shothistorystorage.cpp` ~607–2453) and any helper left without a caller. Candidates: `RecipeStorage::migrateBagLinksStatic` / `migrateGrindOwnershipStatic` / `healRetiredEquipmentLinksStatic`, `EquipmentStorage::healEnrichmentForksStatic`, the migration-16/19/22 helpers. Keep `linkOrphanShotsStatic` if the importer still calls it. Verify: build passes; `cpp_find_references` shows no dangling callers.
- [x] 3.2 Remove `crossedSchemaVersion`, `m_schemaVersionAtStart*`, and the Equipment/Recipes button injections (`maincontroller.cpp:722-735`, `settings_network.cpp` `inject*ButtonIfMissing`). Verify: build passes; grep finds no `crossedSchemaVersion`.
- [x] 3.3 Delete the `tst_dbmigration` cases for removed steps (v1 → v40 slots, the `crossedSchemaVersion_*` cases, fresh-chain cases now covered by 2.3), and the storage tests for removed helpers (`tst_recipestorage` 1931-2067 and 436, `tst_equipment` 836-1082). Keep every case for migration 42 and up. Verify: `run_tests` on those targets passes, and no `tst_*` still sets a version below 41 except the refusal cases.
- [x] 3.4 Update the `CLAUDE.md` migrations paragraph. It cites `shothistorystorage.cpp:269/:323/:496` and says an empty `schema_version` re-seeds to 1 and re-runs the chain; it now refuses as below the floor. Update `docs/CLAUDE_MD/TESTING.md` wherever it describes the full-chain tests. Verify: every cited line number matches the new file.

## 4. Refuse old sources on restore and transfer

- [x] 4.1 In `importDatabaseStatic`, read the source `schema_version` before touching the destination. Return a distinct `sourceTooOld` outcome for a source below the floor or with none (D7). Verify: `tst_dbmigration` imports a schema-40 source in merge and replace mode; both return `sourceTooOld` and leave the destination unchanged. A schema-41 source still imports.
- [x] 4.2 Run the same check in the prepare step of `FullBackupRestore` and `DatabaseBackupManager`, against the archive's `shots.db`, before applying settings, profiles or media. Show a translated "restore it in Decenza 2.1.0 first" message through the existing failure surface. Verify: a restore test with a schema-40 archive asserts no setting, profile or media file changed.
- [x] 4.3 In `DataMigrationClient`, refuse at the manifest when `appVersion` is below 2.0.7 or missing, before queueing any step, with the same translated message. Verify: `tst_datamigrationcommandpolicy` (or the existing manifest test) covers 2.0.6, 2.0.7 and a missing `appVersion`.
- [x] 4.4 Remove the importer branches only pre-floor sources reach:
  - pre-34 yield relabel (shots, bags, recipes)
  - `legacyGrind` (`recipestorage.cpp:2173-2201, 2257-2274`)
  - pre-19 `beanbase_json` backfill
  - pre-32 and pre-39 special cases beyond NULL filling

  Delete their tests (`tst_recipestorage` 2068-2172 and 2219-2284, `tst_coffeebags:1334`). Keep NULL filling. Verify: suites pass.
- [x] 4.5 Update `docs/CLAUDE_MD/DATA_MIGRATION.md` with the refusal in one or two sentences. Verify: the doc names the floor and the stepping-stone release.

## 5. Remove settings-level upgrade code

- [x] 5.1 Delete `settingsstoremigration.{h,cpp}`, its call in `main.cpp`, `runAppNameMigrationOnce` and its helpers (`main.cpp:393-585`), and the legacy-store wipe in `Settings::factoryReset`. Delete the accessibility legacy-store migration (`accessibilitymanager.cpp:141-227`) and its restore guard stamp (`fullbackuprestore.cpp:472`). Remove them from `CMakeLists.txt` and the test targets. Delete `tst_settingsstoremigration.cpp`, the accessibility legacy cases (`tst_accessibility_announcements.cpp:375-535`) and the guard-key line in `tst_settings.cpp:193`. Verify: build passes, including `check_test_source_duplication.py`; grep finds no `"DE1Qt"` outside docs.
- [x] 5.2 Delete the one-time steps in the `Settings` constructor. Agent inventory items:
  - a4: dead rating keys, shader, autoFlowCal default, headless seeds, flow-cal v2/v3, theme colours, knownScales
  - a4: scale typeIds and `migrateScaleTypeIds`, after confirming no import path calls it
  - b3 (firmware nightly), b6 (flow-cal v4/v5/v6 clears, `looksLikeFreshInstall` if left unused)
  - a5 (DYE grinder split), a9 (steam global rate), b4 (heater-off presets), b8 (favourites merge)

  Delete their tests in `tst_settings`, `tst_saw_settings` and `tst_autoflowcal`. Keep the scale orphan-heal, the read-time fallbacks, and everything from 2.0.8 on. Verify: suites pass.
- [x] 5.3 Delete the `SettingsSerializer` branches for pre-2.0.7 exports: `beans.presets`, `headless.skipPurgeConfirm`, old palette, `espressoNotes`, disabled-pitcher remap, `keepHeaterOn` fallback, and the steam-rate re-seed. Delete their tests (`tst_settings` 2046-2092, 2110). Verify: an import test with a 2.0.7 export still round-trips.
- [x] 5.4 Update `docs/CLAUDE_MD/SETTINGS.md` wherever it describes the legacy store or the store migration. Verify: no reference to `runSettingsStoreMigrationOnce` remains in `docs/`.

## 6. Remove app-level one-time steps

- [x] 6.1 Delete the recipes-upgrade offer: `maincontroller.cpp:2342-2430`, `settings_network.cpp` frozen old default and transform (~571-579, 683-892), and the `qml/main.qml` dialog and `maybeShowRecipesUpgradeDialog`. Delete `tst_settings.cpp:2732-3083`. Verify: build and qmllint pass; the idle page still opens.
- [x] 6.2 Delete the four normalisations in `getLayoutObject()` (`settings_network.cpp:437-512`) and their persist call: the statusBar and lowerMidBar zone additions, text→custom, and connectionStatus→machineStatus. Keep the `connectionStatus` render alias in `LayoutItemDelegate.qml` and its catalog row. Verify: a `tst_settings` case loads a stored layout with no `lowerMidBar` key; adding an item to `lowerMidBar` saves it with that zone, and the layout read back is unchanged otherwise. Break the add path and watch it go red.
- [x] 6.3 Delete the remaining one-time steps:
  - migration-16 rating back-sync and Visualizer reconciliation (`maincontroller.cpp` 854-1001, 5988-6201)
  - legacy bean-preset import (`importLegacyBeanPresets`, `convertLegacyPresetSettings`, `importLegacyPresetsStatic`, `bagFromLegacyPreset`)
  - profile migrations (`migrateProfileFolders` except its `mkpath` calls, `migrateProfileFormat`, `migrateReadOnlyProfiles`, `stripStoredRecipeBlocks`)
  - `migrateHistoryToNewPath`
  - AI one-time clears and `migrateFromLegacyConversation`
  - SAW pre-basket seed copy (`settings_calibration.cpp:1387-1588`, `maincontroller.cpp:636-679`)

  Delete their tests: `tst_coffeebags:401`, `tst_profilemanager` 301-569 and 4677, `tst_saw_settings` 522-811. Keep the strip-on-load and the copied-bucket exclusion. Verify: suites pass, and the existing copied-bucket bootstrap test still passes.

## 7. Integration checks

- [x] 7.1 Full suite with `mcp__qtcreator__run_tests` scope `all`, and a clean desktop build. Verify: zero failures, the qmllint gate passed, and `check_log_markers.py` and the other `text-invariants.yml` scripts pass locally.
- [x] 7.2 Record the line delta (`git diff --stat` against `main`) in the PR. Verify: production lines removed are roughly 5,000 or more.
- [x] 7.3 Manual check of the one path no automated test reaches: the launch gate and notice. The maintainer launches the app; the assistant never does. A data folder holding only a schema-23 `shots.db`: the notice names 2.1.0, Close Decenza exits, and `shots.db`, a settings export and the folder listing are byte-identical afterwards. Passed 2026-11-09. Schema-41 upgrade, fresh install and below-floor restores are covered by `tst_dbmigration`, `tst_backupoperationpolicy` and `tst_datamigrationcommandpolicy`, so they are not repeated by hand.
- [x] 7.4 Draft the 2.1.1 pre-release notes text: turn on backups; install 2.1.0 before 2.1.1 if on an older release. Verify: the text is in the PR description for the release owner.

## 8. Follow-up cleanup from review

- [x] 8.1 Delete the import orphan-bag link (`linkOrphanShotsStatic`, its importer call and tests): it served pre-bag sources only, and on a 2.0.7+ source it would attach shots saved without a bag. Verify: suite passes.
- [x] 8.2 Remove the uploader's `updateSuccess`/`updateFailed` signals (no listener since the migration-16 back-sync went), the `brew/hasBrewYieldOverride` write kept for pre-2.0.0 downgrades, and the `coffee_bags` dial columns a fresh install created only for migration 52 to drop (52 now checks the column first). Verify: suite passes, including the equivalence test.
- [x] 8.3 Make the shot importer table-driven: copy every column both databases share by name, with an explicit not-carried set (`id`, `created_at`, `updated_at`, `bean_repair_pending`) and overrides for the remapped ids and PORTAL provenance. This drops the pre-34 yield relabel, which a 2.0.7+ source cannot need. Verify: import tests in `tst_dbmigration`, `tst_coffeebags`, `tst_recipestorage` and `tst_backupoperationpolicy` pass.
- [x] 8.4 Backups export only stored theme palettes: drop the `customColors` key from export and import. Verify: suite passes.
- [x] 8.5 Transfer test fixtures report `UpgradeFloor::kRelease` instead of a literal version. Verify: `tst_datamigrationcommandpolicy` passes.

## 9. Review fixes (code review and Codex)

- [x] 9.1 Make the floor schema-only: drop the release constant from `upgradefloor.h`, and stop the refusal text and the launch notice naming a floor release (the notice had hard-coded it). Verify: grep finds no floor release literal outside docs.
- [x] 9.2 Gate transfers on the source's schema: the manifest reports `shotSchemaVersion`, the client refuses a source below the floor at connect, and for a source too old to report it, shots import first and a refusal clears the rest of the queue. Verify: `tst_datamigrationcommandpolicy` cases for a below-floor manifest and for an unreported source whose database is below the floor (no settings fetched).
- [x] 9.3 Treat a desktop shot database found only under the pre-1.5.6 "Decenza DE1" app name as below the floor at launch, so no fresh database blocks 2.1.0's app-name move. Verify: read the startup path; covered by manual check 7.3.
- [x] 9.4 Check an unreported transfer source's database before any category is applied, not only when shots are imported (Codex P1 on 541baabbb), failing closed when it cannot be fetched or read; and check a zip backup's database before applying settings even when shots are unchecked. Verify: `tst_datamigrationcommandpolicy::uncheckedSourceStopsBeforeOtherData` (import all, settings only, database unavailable) and `tst_backupoperationpolicy::restoresRefuseADatabaseBelowTheFloor` with shots unchecked.
- [x] 9.5 Restores require the backup's shot database to be confirmed at or above the floor, not only "not below" it: a database the probe cannot read, or one with no `shots` table, is refused before anything is applied (Codex P1 on ce6ef979f). Verify: `tst_backupoperationpolicy::restoresRefuseADatabaseBelowTheFloor` rows "below floor" and "unconfirmed", both with shots deselected.

## Workflow follow-up

- Open the PR, then run `/pr-review-toolkit:review-pr` before merging.
- Read the `text-invariants.yml` run on the PR before merging.
- Archive with `openspec archive drop-pre-2-0-7-upgrades --yes` as the PR's final commit. `layout-recipes-upgrade-offer` loses every requirement, so delete its now-empty main spec in that commit if the archive leaves it behind.
