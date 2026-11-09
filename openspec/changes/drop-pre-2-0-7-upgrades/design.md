# Design

## Context

See proposal.md for motivation. Facts the approach depends on:

- **A fresh install replays the whole chain.** `createTables()` creates the original version-1 `shots` table and seeds `schema_version` to 1. `runMigrations()` then runs every step. The bag, equipment and recipe tables are created by `CoffeeBagStorage::ensureTableStatic`, `EquipmentStorage::ensureTablesStatic` and `RecipeStorage::ensureTableStatic`. Those are called from inside migrations 19, 22 and 24, and they create those tables in their *current* shape. Later steps converge through `hasColumn` guards.
- **Schema per release**: 2.0.6/2.0.7 = 41, 2.0.8 = 45, 2.1.0 = 47, HEAD = 52. Steps 42 and up are already written `currentVersion >= N-1 && currentVersion < N`. So on an older database they are skipped, not run: without a check, the app would start against a schema ~20 versions short.
- **Settings migrations run before the database opens.** `runSettingsStoreMigrationOnce()` / `runAppNameMigrationOnce()` run at `main.cpp:1104`, and the `Settings` constructor runs after that. Any launch check must come before all three.
- **Settings carry no version stamp.** The shot database's `schema_version` is the only reliable witness that a device has run a given release. A database at 41 or above means the 2.0.7-era settings migrations already ran in the same process.
- **Every restore and transfer funnels through `ShotHistoryStorage::importDatabaseStatic`**. Callers: `DatabaseBackupManager` (`:928`), `FullBackupRestore` (`:807`), `DataMigrationClient` (`:1347`). That function copies column by column into the live database; it never replaces the live file.
- **A transfer source already reports its version.** The `/api/backup/manifest` response has carried `appVersion` since January 2026, 1.x included (`shotserver_backup.cpp:70`).
- **Layout normalisation can go even where it isn't persisted.** The statusBar and lowerMidBar additions in `SettingsNetwork::getLayoutObject()` are never saved; they run on every read. But a stored layout lacks them only until its next save, and readers already treat a missing zone as empty:
  - `IdlePage.qml:105`, `StatusBar.qml:15` and `LibraryItemCard.qml:125` read `zones.<name> || []`
  - adding an item writes the zone in (`settings_network.cpp:626-632`)

  The text→custom and connectionStatus→machineStatus rewrites were persisted the first time they ran. The `connectionStatus` render alias in `LayoutItemDelegate.qml` stays.
- **Some upgrade-looking code must stay.**
  - The BLE pre-epoch carry-forward (`BleEpochDecision::MigrateForward`) maps a pre-epoch record to "honour". Removing it would re-test a latched device, which `BLE_PROTOCOL.md` forbids.

## Goals / Non-Goals

**Goals:**
- One floor constant and one stepping-stone release string, used by the launch check, the database layer and the import refusal.
- A fresh install that reaches the same schema as an upgraded 2.0.7 database, proven by one test.
- Older data is never written to: not at launch, not by a failed restore, not by a transfer.

**Non-Goals:**
- No rescue copy of old data and no "Decenza DE1" folder check. Users before the floor are covered by the release notes and the 2.1.0 stepping stone.
- No version field added to settings-only exports. Those stay ungated; keys only older releases wrote are simply no longer understood.
- No change to the BLE carry-forward, to the read-time key fallbacks, or to any 2.0.8+ upgrade step.
- No squashing of migrations 42 and up.

## Decisions

### 1. The floor is a schema number, and the stepping stone is named once
The floor and the stepping stone live in one header-only file, `src/history/upgradefloor.h` (`namespace UpgradeFloor`):
- `kSchemaVersion = 41` and the matching release `"2.0.7"`
- the stepping-stone release `"2.1.0"`
- the shared refusal text, and the release check a transfer manifest uses

It is header-only so the transfer client and its tests need no storage sources. Every check and message reads it. *Alternative:* key the floor on the app version string. Rejected: the version isn't stored anywhere a later build can read reliably, and the schema is.

### 2. Launch check: a read of the database before anything else
A static probe, `ShotHistoryStorage::probeUpgradeFloor(path)`, returns one of four results: `NoDatabase`, `Supported`, `BelowFloor` or `Unknown`. It runs right after `setApplicationVersion()` in `main()`, before every settings and database migration.

How it decides:
- **No file:** `NoDatabase`.
- **A `shots` table but no `schema_version` row:** `BelowFloor`. That matches what `createTables()` does today, re-seeding to 1.
- **The connection fails to open or the read fails:** `Unknown`.

The probe opens through `withTempDb()` (`dbutils.h`) and runs a single `SELECT MAX(version)`. It sets no `journal_mode`, executes no DDL or DML, and closes. Closing the last connection may checkpoint the WAL into the main file. That moves committed pages but changes no table, row or version, which is what the spec promises. *Alternative:* `SQLITE_OPEN_READONLY`. Rejected: a read-only open of a WAL database without a writable `-shm` can fail outright. That would turn every such device into `Unknown`, with nothing gained.

### 3. `Unknown` continues, and the database layer refuses as a backstop
A probe failure must not lock a user out of a working database. So `Unknown` continues startup. `runMigrations()` then reads the version *first*, before its duplicate-row cleanup `DELETE`. If the database is below the floor, it returns false without writing, and logs the refusal; `initialize()` reports the existing open failure.

This path has a weakness: the settings migrations and `Settings` construction have already run against a pre-floor store by then. It needs the probe to fail on a database that the next connection reads successfully, so it is accepted (see Risks).

### 4. The notice is a standalone QML window, English only
`qml/UpgradeFloorNotice.qml` is a new module file. It uses only `QtQuick` and `QtQuick.Controls`, plus `Accessible` roles, names and press actions on the message and the button. It references no Decenza singleton, because none exists yet.

`main()` loads it in its own `QQmlApplicationEngine` and runs `app.exec()`. Confirm, back or window close calls `Qt.quit()`, and `main()` returns without reaching the rest of startup. That is a top-level event loop, not a nested one, so the QML-handler-reentrancy rule does not apply.

The text is English: `TranslationManager` needs `Settings`, and building `Settings` is what the check exists to avoid. *Alternative:* `QMessageBox`. Rejected: Qt Widgets get almost no TalkBack support on Android, the platform most likely to see this screen.

### 5. Fresh install: a frozen `shots` schema plus the living storage tables
`createTables()` changes in four ways:
- Its `shots`, `shot_samples`, `shot_phases`, FTS and trigger DDL becomes the schema-41 shape.
- It also calls the three storage `ensureTable*Static` functions.
- It creates any table that migrations 3–41 create inline.
- It stamps `schema_version` to 41 only when it created `shots` in that call.

The storage tables keep coming from their own functions, in current shape. That is the convergence pattern fresh installs already rely on. Migrations 42 and up then run as they do today.

*Alternative:* a baseline at the current schema (52). Rejected: every future migration would then have to edit both the migration and the baseline, two definitions of one schema.

The schema-41 DDL is produced once, mechanically, before the old steps are deleted. A temporary local edit caps the current chain at 41 on a fresh database, and its `sqlite_master` is dumped. A real 2.0.7 database would be a stronger source if one is to hand.

### 6. One equivalence test replaces the old per-step tests
A committed fixture, `tests/data/db/schema41.db`, holds that schema with a handful of rows. One test runs the fixture and a fresh install through `initialize()`. It asserts the same final version and the same tables, columns (name, type, default, not-null), indexes and triggers, ignoring column order. It also asserts the fixture rows survive.

It goes into `tst_dbmigration.cpp`, not a new file, per the per-file build cost in `TESTING.md`. Before keeping it, break the baseline (drop one column) and watch it go red.

### 7. Import refusal: early where possible, and always at the funnel
- **`importDatabaseStatic`** reads the *source* `schema_version` before touching the destination. A source below the floor, or with none, returns a distinct `sourceTooOld` outcome on its existing result.
- **`FullBackupRestore` and `DatabaseBackupManager`** run the same source check in their prepare step, against the archive's `shots.db`, before any settings, profiles or media are applied. That keeps a refused restore from landing half-applied.
- **`DataMigrationClient`** refuses at the manifest when `appVersion` is below 2.0.7, before queueing any step. A missing `appVersion` counts as below; only builds older than January 2026 omit it.

Each path shows a translated "restore it in Decenza 2.1.0 first" message through its existing failure surface. *Alternative:* refuse only at the funnel. Rejected: a full restore applies settings before the database, so the refusal would come after the damage.

### 8. What is removed, and what stays
Removed, with tests:
- Migration steps 3–41, and their helpers in the storage classes once they have no remaining caller: the bag-link and grind-ownership steps, the retired-link heal, the fork heal and the borrowed-link unlink.
- `crossedSchemaVersion` and the Equipment/Recipes button injections.
- The four layout normalisations in `getLayoutObject()` and their persist call.
- The recipes-upgrade offer and its dialog.
- The migration-16 rating back-sync and the Visualizer reconciliation.
- Legacy bean-preset import.
- The settings-store and app-name moves, the accessibility legacy store, and the legacy wipe in `factoryReset()`.
- The old one-time `Settings` constructor steps.
- The profile folder, format and read-only migrations. The `mkpath` calls stay.
- `migrateHistoryToNewPath`.
- The AI one-time clears and the legacy conversation migration.
- The SAW pre-basket seed copy. The copied-bucket exclusion stays.
- The flow-calibration v4/v5/v6 clears, the favourites merge, the heater-off preset migration and the firmware nightly reset.
- The serializer and importer branches that only pre-2.0.7 sources reach: `beans.presets`, pre-34 yield relabel, `legacyGrind`, disabled-pitcher remap, `headless.skipPurgeConfirm`, old palette, `espressoNotes`.

Also removed after review: the import orphan-bag link, the shot importer's pre-34 yield relabel (both shot write paths always store a `yield_mode`), the uploader signals only the back-sync listened to, the `hasBrewYieldOverride` downgrade write, and the bag dial columns a fresh install created only to drop.

### 9. The shot importer copies by name
`importDatabaseStatic` copies every `shots` column the source and destination share, so a new column crosses restores and transfers with no importer edit. What is not carried is an explicit set: `id` (kept or relocated), `created_at`/`updated_at`, and `bean_repair_pending`, which queues work for the source device's Visualizer account. Overrides cover the remapped `equipment_id`/`bag_id`/`recipe_id` and the PORTAL reliability provenance check. *Alternative:* the per-column list and positional binds it replaces, which had to be edited in three places per new column and silently dropped any column a change forgot.

### 10. Backups carry stored theme palettes only
The settings export used to write the active palette as `customColors` beside the stored ones, and the import used it when `customColorsDark` was absent. Both go. A device that never customised a palette now exports none, and a restore leaves the destination's palette alone instead of pinning the source's computed one. Theme FILES keep their own `colors` fallback; they are shareable artifacts, not backups.

Kept: everything in Context's "must stay", read-time fallbacks for keys a 2.0.7 install may still hold (right-axis mode, yield mode, theme name, shot-plan booleans, screensaver fit mode), the scale orphan-heal, the migration-31 temperature-offset pass (it re-runs after imports), profile load-time compatibility, and the importers' missing-column NULL filling.

## Risks / Trade-offs

- **[A user jumps from before 2.0.7 straight to the new release on Android]** They can't downgrade to 2.1.0 without uninstalling, and uninstalling deletes `shots.db`. → Release notes in the 2.1.1 pre-release stream tell users to turn on backups and to install 2.1.0 first. The updater skips pre-releases, so stable-channel users land on 2.1.0 until 2.1.1 goes stable. Accepted per the decision to not handle pre-2.0.7 users further.
- **[The probe returns `Unknown` on a pre-floor database that the next connection reads]** The settings migrations and `Settings` construction would write defaults into the canonical store before the database layer refuses. → Requires an open/read failure on the first connection only. Logged at WARN so it is visible in a submitted log. Accepted.
- **[The baseline drifts from what 2.0.7 actually produced]** → The equivalence test (Decision 6) against a fixture captured before the old steps are deleted. Optionally cross-check the fixture against a real 2.0.7 database.
- **[A 2.0.7+ device never finished a one-time step]** Examples: a settings-store migration deferred because the old store was unreadable; a recipes-upgrade offer suppressed by another dialog; a Visualizer reconciliation waiting for credentials. → Each needs an earlier failure or a user who never configured the feature. The effect is default settings or an unrepaired link, never lost shots. Accepted.
- **[A stored layout saved before 1.3.8 and never saved since]** Its status bar renders empty instead of with the old default items. → Any layout edit, reset or accepted recipes-upgrade offer since then saved the normalised layout. Reset to default restores the bar. Accepted.
- **[A settings-only export from before 2.0.7 is imported]** Keys only older releases wrote are ignored. A `disabled` steam pitcher imports as an ordinary pitcher, and `beans.presets` are skipped. → Accepted; settings exports carry no version to gate on.
- **[The English-only notice fails a translation lint]** → If a gate rejects literal user-visible text, record a scoped exemption naming the reason (no `TranslationManager` before `Settings`), not a file-wide suppression.
- **[Build blast]** Header changes to `shothistorystorage.h` and `settings*.h` recompile most of the tree. → One-time cost; `BUILD_PERFORMANCE.md`.

## Migration Plan

1. Ship in the 2.1.1 pre-release stream. 2.1.0 stays the latest stable release with the full chain.
2. Release notes for every 2.1.1 pre-release, and for 2.1.1 stable: turn on backups; install 2.1.0 before 2.1.1 if you are on an older release.
3. Rollback: revert the change before 2.1.1 goes stable. Nothing in it alters data written by 2.0.7+, so a reverted build reads every database this one wrote.
