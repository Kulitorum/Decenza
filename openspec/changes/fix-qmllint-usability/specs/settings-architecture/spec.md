## MODIFIED Requirements

### Requirement: Settings Domain Decomposition

The `Settings` class SHALL be decomposed into domain sub-objects. Each domain sub-object SHALL be a standalone `QObject` subclass owning its own `QSettings` instance and containing only the properties, signals, and methods for its domain. `Settings` SHALL own the domain objects, construct them as children, and expose each via a `Q_PROPERTY(Settings<Domain>* <domain> READ <domain> CONSTANT)` accessor declared with the **concrete sub-object type**. `settings.h` SHALL `#include` each domain header, because a pointer `Q_PROPERTY` requires a complete type for moc to build a metatype. The same typed accessor (`Settings<Domain>* <domain>() const`) serves both the `Q_PROPERTY` READ and C++ callers; no parallel `QObject*` accessor SHALL exist. The final `settings.h` SHALL contain only sub-object accessors and cross-domain methods (`sync`, `factoryReset`, cross-domain `connect()` declarations).

The complete domain set SHALL be: `SettingsMqtt`, `SettingsAutoWake`, `SettingsHardware`, `SettingsAI`, `SettingsTheme`, `SettingsVisualizer`, `SettingsMcp`, `SettingsBrew`, `SettingsDye`, `SettingsNetwork`, `SettingsApp`, `SettingsCalibration`.

**This reverses the original requirement**, which mandated `Q_PROPERTY(QObject* domain READ domainQObject CONSTANT)` with forward declarations, to keep `settings.h` free of domain includes. That erasure was measured to be the wrong trade: a property behind a `QObject*` is opaque to `qmllint`, `qmlcachegen` and the QML language server, so **1,310 QML call sites across 281 settings** could not be checked at all. `Settings.brew.slectedFlushPreset` compiled, linted clean, and failed silently at runtime — the defect class that shipped in 2.0.1 as #1661. The rebuild saving the erasure bought is paid by developers and absorbed by caching; the defects it hid are paid by users.

`Q_DECLARE_OPAQUE_POINTER` SHALL NOT be used to satisfy moc without the include. It compiles and satisfies the linter, then hands QML a `QVariant(Settings<Domain>*)` rather than an object, so every property and method under `Settings.<domain>` fails at runtime.

#### Scenario: Domain sub-object is independently includable
- **WHEN** a component depends only on a single domain's settings
- **THEN** it includes that domain's header (e.g., `settings_calibration.h`) and receives a `SettingsCalibration*` — not `Settings*`
- **AND** changing any non-Calibration domain header does not trigger recompilation of that component

#### Scenario: Settings.h is a thin façade
- **WHEN** all 12 domain splits are complete
- **THEN** `settings.h` contains only the twelve domain headers' includes, the twelve typed `Q_PROPERTY` accessors, cross-domain method declarations, and `sync`/`factoryReset`
- **AND** `settings.h` contains no `Q_PROPERTY` for any property that has been moved to a sub-object

*(The original "under 200 lines" bound is dropped. It measured the wrong thing: the twelve includes and the explanatory comment that keeps the erasure from being reintroduced take the file to 264 lines while removing declarations rather than adding them. The bound that matters is the second bullet — no migrated property declared here.)*

#### Scenario: Domain property types are statically resolvable from QML
- **WHEN** `qmllint` analyses a QML file containing `Settings.brew.<prop>`
- **THEN** it resolves `Settings.brew` to `SettingsBrew` and checks `<prop>` against that class
- **AND** a misspelt property is reported as `missing-property` rather than passing silently

#### Scenario: Each new domain sub-object is QML-introspectable
- **WHEN** a new `Settings<Domain>` class is added
- **THEN** `src/core/settings_qml.h` declares a `Settings<Domain>Foreign` gadget carrying `QML_FOREIGN(Settings<Domain>)`, `QML_NAMED_ELEMENT(Settings<Domain>Type)` and `QML_UNCREATABLE`
- **AND** the gadget is written out literally, not produced by a macro — `moc` does not expand macros that declare a `Q_GADGET`, so a generated one compiles and registers nothing
- **AND** QML expressions like `Settings.<domain>.<prop>` resolve to the sub-object's property at runtime, not to `undefined`

*(This replaces the runtime `qmlRegisterUncreatableType<Settings<Domain>>("Decenza", ...)` call in `main.cpp`. `qmltyperegistrar` cannot see a runtime call, so the registration never reached `Decenza.qmltypes` and no static tool knew the type existed.)*

#### Scenario: Cross-domain side effects use connect-based wiring
- **WHEN** changing a property on one domain must trigger an update on another domain (e.g., `resetSawLearning` on `SettingsCalibration` must reset hot-water SAW offset state on `SettingsBrew`)
- **THEN** the wiring is established via `connect()` in the `Settings::Settings()` constructor body, after all `m_<domain>` members are constructed
- **AND** the sub-object's setter does not directly call methods on another domain

#### Scenario: Cross-domain wiring SHALL NOT mirror a setting onto per-shot state
- **WHEN** a proposed cross-domain wiring would copy a configured setting into a field that is snapshotted onto a shot at save time
- **THEN** the wiring SHALL NOT be established
- **AND** the value SHALL be written to the shot record at the point a person supplies it instead
- **AND** the reason SHALL be understood as concrete rather than stylistic: the removed `setDefaultShotRating` → `setDyeEspressoEnjoyment` wiring is what made a deleted setting keep rating shots, because the mirrored field outlived the setting that fed it

### Requirement: Narrow Consumer Header Isolation

Each domain-specific C++ consumer (a class that reads only one domain's settings) SHALL include only the domain header, not `settings.h`. Its constructor SHALL accept the domain sub-object pointer instead of `Settings*`. `main.cpp` SHALL pass the sub-object accessor (e.g., `settings.brew()`) at the call site. Wide consumers (classes that touch multiple domains, e.g., `MainController`, `settingsserializer.cpp`) MAY keep `Settings*` and access domains via `settings->domain()->X()`.

Narrowing consumers is now the **only** sanctioned lever for reducing the recompile blast of a domain-header edit. Re-erasing the property types SHALL NOT be used for that purpose, whatever the measured saving.

#### Scenario: Narrow consumer does not include settings.h
- **WHEN** a domain-specific consumer's header is compiled
- **THEN** the file does not `#include "settings.h"` and does not include any other domain's header
- **AND** changing `settings.h` does not trigger its recompilation

#### Scenario: Settings.h transitive includer count is bounded
- **WHEN** the project is fully migrated
- **THEN** the count of `.cpp` files that transitively include `settings.h` is 10 or fewer
- **AND** measurement comparison with the pre-Tier-1 baseline (39 includers) is documented in the merging PR

#### Scenario: The cost of the typed properties is recorded, not re-litigated
- **WHEN** a contributor proposes reverting the domain property types to `QObject*` to speed up builds
- **THEN** the proposal is rejected, and the measured figures are the ones on record: a domain-header edit takes ~60 s against ~26 s before, of which the marginal cost attributable to this decision is +129 C++ translation units (the 218 QML cache units in the dirty set rebuild either way, because a domain header carries `Q_OBJECT`)
- **AND** `tst_settings::qmlChainsThroughDomainSubObjects` remains in the suite to catch a reintroduction that compiles and lints clean
