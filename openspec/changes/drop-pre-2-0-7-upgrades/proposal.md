# Proposal

## Why

Every launch still carries the code needed to upgrade any database and settings store back to the first 1.x release. Download counts show the active user base taking each release, so almost no one needs that path. The code costs reading and review time on every change near it. It also shapes new code: guards against versions nobody runs, crossing gates that can no longer fire, and a fresh install that still builds its schema by replaying 40 migrations. 2.1.0, the current stable release, keeps the full chain and serves as the stepping stone for anyone still behind.

## What Changes

- **BREAKING**: The new release upgrades only from data that has run 2.0.7 or later (shot database schema 41). Older data is refused, never modified.
- A launch-time check reads the shot database's schema version before any settings or database migration runs. Below 41, it shows "install Decenza 2.1.0 first" and exits on confirm, writing nothing.
- A fresh install creates the schema-41 tables directly, then runs migrations 42 and up. It no longer replays the chain from version 1.
- Restoring a backup and receiving a device transfer refuse a source database below schema 41, with the same stepping-stone message, before applying anything.
- Removed: database migration steps 3–41, and every one-time settings, profile, layout, AI and calibration upgrade step introduced in 2.0.6 or earlier. Also removed: the old-source handling in the importers that only pre-2.0.7 data needs, and their tests.
- Kept: migrations 42 and up; the 2.0.8+ upgrade steps (MQTT discovery move, remote MCP independence, steam override discard); the BLE pre-epoch latch carry-forward (removing it would re-test a latched device); read-time fallbacks for keys a 2.0.7 install may still hold; and the importers' missing-column tolerance.
- Release notes for the 2.1.1 pre-release tell users to turn on backups and to install 2.1.0 before 2.1.1.

## Capabilities

### New Capabilities
- `upgrade-floor`: The oldest data the app upgrades from, how a fresh install reaches that point, and how older data is refused at launch and on import.

### Modified Capabilities
- `settings-store-identity`: The one-time legacy store migration and its done-flag are removed; factory reset clears only the canonical store.
- `settings-architecture`: The requirement that the legacy store migrates before sub-objects read it is removed.
- `coffee-bag-model`: The borrowed-canonical-record unlink migration and the legacy `beans.presets` settings import are removed.
- `equipment-package-model`: Migration 22 and the pre-enrichment fork heal (35/36) are removed.
- `shot-rating-capture`: Migration 16's inferred-rating reset is removed.
- `recipe-model`: The one-time yield-spec and bag-link migrations are removed.
- `data-transfer-coverage`: Legacy recipe grind recovery for pre-schema-30 sources is removed.
- `equipment-inventory-view`: The one-time Equipment button injection is removed.
- `idle-default-layout`: The requirement about injection migrations on the default layout is removed.
- `layout-lower-mid-bar-zone`: The zone is no longer added to saved layouts by a migration; a missing zone reads as empty.
- `layout-machine-status-widget`: The one-time rewrite of saved `connectionStatus` items is removed; the alias stays.
- `layout-recipes-upgrade-offer`: The one-time recipes-first upgrade offer is removed entirely.
- `visualizer-upload-persistence`: The one-time Visualizer reconciliation pass is removed.
- `scale-type-identity`: The one-time scale type-id migration is removed.
- `advisor-conversation-history`: The one-time clear of pre-upgrade threads is removed.
- `profile-favorites-order`: The one-time merge of Selected into favorites is removed.
- `recipe-block-retirement`: The one-time pass over saved profiles is removed; strip-on-load stays.
- `steam-heater-policy`: The one-time migration off user-created heater-off presets is removed.
- `stop-at-weight-learning`: The one-time pre-basket history copy is removed; copied buckets still do not vote.
- `firmware-update`: The one-time reset of the historical nightly preference is removed.

## Impact

- **Database**: `src/history/shothistorystorage.cpp` (`createTables`, `runMigrations`, `importDatabaseStatic`, `crossedSchemaVersion`, legacy preset import), plus the migration-only helpers in `coffeebagstorage.cpp`, `equipmentstorage.cpp` and `recipestorage.cpp`.
- **Startup**: `src/main.cpp` (the new check; the app-name and settings-store moves go), `src/controllers/maincontroller.cpp` (button injection, upgrade offer, rating sync, reconciliation, SAW seed, heal adoption).
- **Settings**: `settings.cpp`, `settings_brew.cpp`, `settings_calibration.cpp`, `settings_network.cpp`, `settings_dye.cpp`, `settings_app.cpp`, `settingsserializer.cpp`, `accessibilitymanager.cpp`; `settingsstoremigration.{h,cpp}` is deleted.
- **Profiles, AI**: `profilemanager.cpp`, `profilestorage.cpp`, `aimanager.cpp`.
- **Restore and transfer**: `fullbackuprestore.cpp`, `databasebackupmanager.cpp`, `datamigrationclient.cpp` gain the source-too-old refusal.
- **QML**: the recipes-upgrade dialog in `qml/main.qml` goes; one small standalone notice window is added.
- **Tests**: `tst_settingsstoremigration.cpp` is deleted; old-version cases in `tst_dbmigration`, `tst_settings`, `tst_saw_settings`, `tst_recipestorage`, `tst_coffeebags`, `tst_equipment`, `tst_profilemanager`, `tst_accessibility_announcements`, `tst_autoflowcal` and `tst_scaleskiphighlatch` go. One fixture database at schema 41 is added.
- **Docs**: `CLAUDE.md` (the migrations paragraph cites lines and a re-seed-to-1 behaviour that change), `docs/CLAUDE_MD/SETTINGS.md`, `TESTING.md` and `DATA_MIGRATION.md` where they describe removed paths. No wiki manual entry.
- **Size**: about 5,700 production lines and 2,700 test lines removed; about 250 added.
