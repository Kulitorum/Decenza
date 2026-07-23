# Consolidate QSettings onto a single store

## Why

Decenza writes preferences to **two** independent QSettings stores on every platform. Most
settings use an explicit `QSettings("DecentEspresso", "DE1Qt")`; a second group uses a bare
`QSettings()`, which resolves via the org/app names set in `main.cpp` to a *different* store
named after the app. On macOS that surfaces as two preference files side by side —
`com.decentespresso.DE1Qt.plist` and `com.decentespresso.Decenza.plist` — and the same split
exists as two registry keys on Windows and two `.conf` files on Linux and Android.

The split is invisible to users until it isn't: one of the two files is named after `DE1Qt`, an
app name Decenza has not shipped under, so anyone inspecting their own preferences, writing a
backup script, or debugging a support case meets a file they cannot identify. Internally it is a
standing footgun — a bare `QSettings()` in new code silently lands in the wrong store, which has
already cost enough incidents that four separate source files carry explanatory comments warning
about it (`shothistorystorage.cpp`, `maincontroller.cpp`, `coffeebagstorage.cpp`,
`accessibilitymanager.cpp`), and a third store (`"Decenza"/"DE1"`) had to be migrated away after
accessibility settings landed in an isolated scope nothing else read. The ~37 bare-`QSettings()`
call sites also bypass the `DECENZA_TESTING` isolation hook, so tests that touch them write into
the developer's real preferences.

## What Changes

- **Consolidate onto the `Decenza`-named store** — the one whose file/registry name matches the
  shipped app on every platform. The `DE1Qt` name is retired as a storage identity.
- **Introduce a single accessor** (`appSettings()`) that every settings read and write goes
  through. The store's organization/application identity is named in exactly one place instead of
  ~34 constructor call sites, and the `DECENZA_TESTING` isolation override is applied centrally so
  no call site can bypass it.
- **Add a one-time startup migration** that copies every key from the legacy `DE1Qt` store into
  the canonical store (skip-if-present), guarded by a done-flag, following the existing
  `runAppNameMigrationOnce()` precedent in `main.cpp`. Once the copy is **verified** key-by-key,
  the legacy store is emptied and its backing file removed — leaving it behind would mean users
  still see two preference files, which is the problem this change exists to solve.
- **Convert all ~71 call sites** — ~34 explicit `("DecentEspresso", "DE1Qt")` constructions
  (including the 15 `Settings*` domain sub-object member initialisers and `SteamHealthTracker`)
  and ~37 bare `QSettings()` constructions — to the accessor.
- **Extend factory reset** to wipe the legacy `DE1Qt` store alongside the canonical one, so a
  reset performed before the migration has run cannot be undone by the migration running
  afterwards. This mirrors the existing legacy-accessibility-store carve-out in `factoryReset()`,
  which exists for exactly this reason.
- **Update the reference docs** that instruct contributors to write `QSettings("DecentEspresso",
  "DE1Qt")` directly: `SETTINGS.md`, `TESTING.md`, and the factory-reset design note.
- Not a breaking change for users: settings survive the upgrade. **Downgrade is explicitly out of
  scope** — a build older than this change reads the legacy store, which by then is gone, and
  starts from defaults. Accepted deliberately rather than overlooked; it is what makes deleting
  the legacy store possible, and that deletion is the visible payoff of the change.

## Capabilities

### New Capabilities
- `settings-store-identity`: Defines the single canonical QSettings store, the mandatory accessor
  that every settings read/write goes through, the one-time migration from the legacy store, the
  test-isolation guarantee, and the factory-reset obligations across both stores.

### Modified Capabilities
- `settings-architecture`: The **Storage Key Stability** requirement currently mandates that each
  domain sub-object open `QSettings("DecentEspresso", "DE1Qt")` directly. That construction is
  replaced by the shared accessor, and the requirement's guarantee changes from "same store, no
  migration needed" to "same keys, migrated store" — key strings stay byte-identical, but the
  backing store changes and a migration is now required.

## Impact

**Code**
- `src/core/` — new accessor header/source; `settings.cpp` (`factoryReset()`, member init), the
  12 `settings_<domain>.cpp` sub-objects, `accessibilitymanager.cpp`, `settingsserializer.cpp`,
  `datamigrationclient.cpp`, `profilestorage.cpp`, `databasebackupmanager.cpp`
- `src/main.cpp` — the new migration, run alongside `runAppNameMigrationOnce()`
- `src/network/` — `shotserver_auth.cpp`, `shotserver_backup.cpp`, `shotserver_ai.cpp`,
  `locationprovider.cpp`
- `src/ai/` — `aimanager.cpp`, `aiconversation.cpp`; `src/mcp/mcptools_ai_conversations.cpp`
- `src/history/` — `shothistorystorage.cpp`, `coffeebagstorage.cpp`;
  `src/controllers/maincontroller.cpp`; `src/machine/steamhealthtracker.cpp`
- Obsolete warning comments about bare `QSettings()` are deleted — the hazard they describe stops
  existing.

**Tests**
- `tst_settings`, `tst_dbmigration`, `tst_autowakewindow`, `tst_accessibility_announcements`,
  `tst_steamhealth`, `tst_aimanager`, `tst_livesteamcoach` all reference the store identity in
  comments or setup and need review; new coverage for the migration itself.

**Docs**
- `docs/CLAUDE_MD/SETTINGS.md`, `docs/CLAUDE_MD/TESTING.md`,
  `docs/plans/2026-02-21-factory-reset-design.md`.

**Platforms** — the store rename lands identically on macOS/iOS (plist), Windows (registry),
Linux and Android (`.conf`). No wire-protocol impact: the device-to-device migration in
`datamigrationclient.cpp` transfers plain key/value JSON and never names a store, so old and new
builds interoperate unchanged.

**Not affected** — key strings, which stay byte-identical throughout.
