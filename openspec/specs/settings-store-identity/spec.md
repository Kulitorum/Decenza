# settings-store-identity Specification

## Purpose
Keeps every `QSettings`-backed preference in one store, organization `DecentEspresso` and application `Decenza`, reached only through the `AppSettings` accessor. Covers the one-time migration from the legacy stores (verified before the legacy store is destroyed), test isolation, factory reset across every store, and the single-store rule on every platform.

## Requirements

### Requirement: Single Canonical Settings Store
The application SHALL persist all `QSettings`-backed preferences in exactly one store, organization `"DecentEspresso"` and application `"Decenza"`. All access SHALL go through the `AppSettings` handle type, a `QSettings` subclass, which is the only place naming that identity. Direct construction of a settings handle, explicit or default, SHALL NOT appear outside the accessor.

#### Scenario: Every settings consumer goes through the accessor
- **WHEN** the codebase is searched for `QSettings` constructions outside the accessor's source file and test-support code
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

- **WHEN** `applicationName()` is reassigned while a bare default-constructed handle is made
- **THEN** that handle would resolve against the wrong store, which is why it is prohibited

### Requirement: Test Isolation Through The Accessor

Under `DECENZA_TESTING` the accessor SHALL return a handle to the PID-scoped `IniFormat` file
given by `Settings::testQSettingsPath()`, so no test can write to a developer's real preferences.
Because every consumer goes through the accessor, this isolation SHALL cover call sites that
previously default-constructed `QSettings` and thereby bypassed it.

#### Scenario: A previously-bypassing consumer is isolated
- **WHEN** a test exercises a component that formerly default-constructed `QSettings` — AI conversation storage, web-auth sessions, manual shot-map location, or profile storage
- **THEN** its writes land in the PID-scoped test store
- **AND** the developer's real preference file is unchanged after the suite runs

#### Scenario: Test seeding uses the same accessor
- **WHEN** a test seeds raw settings state before constructing the object under test
- **THEN** it writes through an `AppSettings` handle
- **AND** the object under test reads back exactly what was seeded

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

### Requirement: Factory reset clears the canonical store

`Settings::factoryReset()` SHALL clear the canonical store. Every installation the app upgrades
from has already moved its legacy stores into the canonical one, so the canonical store is the
only store a reset needs to clear.

#### Scenario: Reset stays reset
- **WHEN** a factory reset runs
- **THEN** the reset completes without error
- **AND** the next launch starts from defaults
