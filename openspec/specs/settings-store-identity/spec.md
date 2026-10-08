# settings-store-identity Specification

## Purpose
Keeps every `QSettings`-backed preference in one store, organization `DecentEspresso` and application `Decenza`, reached only through the `AppSettings` accessor. Covers the one-time migration from the legacy stores (verified before the legacy store is destroyed), test isolation, factory reset across every store, and the single-store rule on every platform.

## Requirements
### Requirement: Single Canonical Settings Store
The application SHALL persist all `QSettings`-backed preferences in exactly one store, organization `"DecentEspresso"` and application `"Decenza"`. All access SHALL go through the `AppSettings` handle type, a `QSettings` subclass, which is the only place naming that identity. Direct construction of a settings handle, explicit or default, SHALL NOT appear outside the accessor and the one-time migration.

#### Scenario: Every settings consumer goes through the accessor
- **WHEN** the codebase is searched for `QSettings` constructions outside the accessor's source file, the migration, and test-support code
- **THEN** zero matches are found for the explicit `QSettings("DecentEspresso", ...)` form
- **AND** zero matches are found for the bare `QSettings <name>;` form
- **AND** every settings read or write resolves through an `AppSettings` handle

#### Scenario: Store identity is immune to application-name mutation
- **WHEN** an `AppSettings` is constructed while `QCoreApplication::applicationName()` has been temporarily reassigned to a value other than `"Decenza"`
- **THEN** the returned handle addresses the canonical store
- **AND** the keys it reads are identical to those read when the application name is unmodified

#### Scenario: The canonical identity resolves to the app-named backing file
- **WHEN** the canonical store's `fileName()` is compared against that of a default-constructed `QSettings` under the application's normal organization/application/domain settings
- **THEN** the two paths are identical
- **AND** on macOS that path ends in `com.decentespresso.Decenza.plist`

#### Scenario: Bare handle would address a different store

- **WHEN** `main.cpp` reassigns `applicationName()` temporarily during app-name migration
- **THEN** a bare default-constructed handle made at that moment would resolve against the wrong store, which is why it is prohibited

### Requirement: One-Time Legacy Store Migration
On startup the application SHALL run a one-time migration that copies every key from the legacy `("DecentEspresso", "DE1Qt")` store into the canonical store, then destroys the legacy store. Keys already present in the canonical store SHALL NOT be overwritten. A done-flag stored in the canonical store SHALL guarantee at most one run.

#### Scenario: Legacy settings survive the upgrade
- **WHEN** a user upgrades from a build predating this change, with settings in the legacy store
- **THEN** on first launch every legacy key is readable through `AppSettings` at its original key string with its original value
- **AND** profiles, favourites, calibration, known scales, and the MCP API key are all present in the UI

#### Scenario: Canonical values win over legacy values
- **WHEN** a key exists in both stores with different values at migration time
- **THEN** the canonical store's value is preserved
- **AND** the legacy value is discarded

#### Scenario: Legacy store is destroyed after a verified copy
- **WHEN** the migration completes successfully
- **THEN** the legacy store contains no keys and its backing file or registry key no longer exists
- **AND** on macOS only `com.decentespresso.Decenza.plist` remains, with no `com.decentespresso.DE1Qt.plist` alongside it

#### Scenario: A failed copy leaves the legacy store intact
- **WHEN** a key written during migration does not read back with the value it was given, or the legacy store cannot be opened for reading
- **THEN** the legacy store is left with its keys intact and its backing file in place
- **AND** the done-flag is not stamped
- **AND** the next launch attempts the migration again

#### Scenario: Migration does not re-run
- **WHEN** the application starts with the done-flag already stamped
- **THEN** no keys are copied and no store is destroyed
- **AND** settings the user changed since the migration are not reverted to legacy values

### Requirement: Migration is a pure function over two handles
The migration SHALL be a pure function over two supplied `QSettings` handles, returning an outcome record (copied count, legacy key count, already-done, deferred-on-error, guard-stamped). It SHALL mirror `migrateAccessibilityLegacyStore()`, so it is unit-testable without a real store.

#### Scenario: Outcome record reports a deferred migration

- **WHEN** the legacy store cannot be read
- **THEN** the outcome record reports deferred-on-error and the done-flag is not stamped

### Requirement: The legacy store is destroyed only after a verified copy
The copy SHALL be verified before the legacy store is destroyed: every written key SHALL be read back from the canonical store and compared to its source value. If any key fails verification, or the legacy store cannot be read, the migration SHALL abort without destroying the legacy store and without stamping the done-flag, so a later launch retries.

#### Scenario: Verification failure aborts without stamping

- **WHEN** a key fails read-back verification
- **THEN** the legacy store is left intact and the done-flag is not stamped

### Requirement: Migration outcome is logged
The migration SHALL log its outcome at `qInfo` or higher, including the legacy key count, so a support log distinguishes nothing-to-migrate from everything-already-present.

#### Scenario: Support log distinguishes empty from already-present

- **WHEN** the migration finds no legacy keys
- **THEN** the logged outcome shows a legacy key count of zero, distinct from a run where every key was already present

### Requirement: Test Isolation Through The Accessor

Under `DECENZA_TESTING` the accessor SHALL return a handle to the PID-scoped `IniFormat` file
given by `Settings::testQSettingsPath()`, so no test can write to a developer's real preferences.
Because every consumer goes through the accessor, this isolation SHALL cover call sites that
previously default-constructed `QSettings` and thereby bypassed it.

The legacy-store migration SHALL NOT run in test builds, so a test process never reads or destroys
a real store.

#### Scenario: A previously-bypassing consumer is isolated
- **WHEN** a test exercises a component that formerly default-constructed `QSettings` — AI conversation storage, web-auth sessions, manual shot-map location, or profile storage
- **THEN** its writes land in the PID-scoped test store
- **AND** the developer's real preference file is unchanged after the suite runs

#### Scenario: Test seeding uses the same accessor
- **WHEN** a test seeds raw settings state before constructing the object under test
- **THEN** it writes through an `AppSettings` handle
- **AND** the object under test reads back exactly what was seeded

### Requirement: Factory Reset Covers Every Store

`Settings::factoryReset()` SHALL clear the canonical store and every legacy store the application
has ever written: `("DecentEspresso", "DE1Qt")` and `("Decenza", "DE1")`. Clearing only the
canonical store is insufficient, because the migration done-flags live in the canonical store — a
reset that erases a flag while leaving the corresponding legacy store populated would resurrect
the erased settings on the next launch.

#### Scenario: Reset before migration is not undone by migration
- **WHEN** a factory reset runs on an installation whose legacy store still holds settings
- **THEN** the legacy store is cleared along with the canonical store
- **AND** the next launch starts from defaults rather than repopulating from the legacy store

#### Scenario: Reset after migration stays reset
- **WHEN** a factory reset runs after the legacy store has already been migrated and destroyed
- **THEN** the reset completes without error despite the legacy store being absent
- **AND** the next launch starts from defaults

### Requirement: Single Store On Every Platform

The consolidation SHALL apply uniformly across all supported platforms. After migration each
platform SHALL expose exactly one Decenza settings location: a single `.plist` on macOS and iOS, a
single registry key on Windows, and a single `.conf` file on Linux and Android. No platform SHALL
retain a second store under a different name.

#### Scenario: No second store on any platform
- **WHEN** a migrated installation's settings location is inspected
- **THEN** exactly one Decenza store is present
- **AND** its name derives from the shipped application name, containing no reference to `DE1Qt`

#### Scenario: Device-to-device transfer is unaffected
- **WHEN** a device running this change exchanges settings with a device running a build predating it
- **THEN** the transfer succeeds in both directions
- **AND** the reason is that the transfer payload carries plain key/value JSON and never names a store, so each side applies values to whichever store is canonical for its own build

