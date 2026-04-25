## ADDED Requirements

### Requirement: Settings Domain Decomposition

The `Settings` class SHALL be decomposed into domain sub-objects. Each domain sub-object SHALL be a standalone `QObject` subclass owning its own `QSettings` instance and containing only the properties, signals, and methods for its domain. `Settings` SHALL own the domain objects, construct them as children, and expose each via a `Q_PROPERTY(Domain* domain READ domain CONSTANT)` accessor. The final `settings.h` SHALL contain only sub-object accessors and cross-domain methods (`sync`, `factoryReset`).

#### Scenario: Domain sub-object is independently includable
- **WHEN** a component depends only on MQTT settings
- **THEN** it includes `settings_mqtt.h` and receives a `SettingsMqtt*` — not `Settings*`
- **AND** changing any non-MQTT domain header does not trigger recompilation of that component

#### Scenario: Settings.h shrinks to a thin façade
- **WHEN** all domain splits are complete
- **THEN** `settings.h` contains only sub-object `Q_PROPERTY` accessors and cross-domain methods
- **AND** `settings.h` is under 200 lines

### Requirement: QML Sub-Object Access

All QML code that accesses settings SHALL use the domain sub-object accessor (e.g., `Settings.mqtt.mqttEnabled`) rather than the flat `Settings` property (e.g., `Settings.mqttEnabled`). Forwarding `Q_PROPERTY` declarations on `Settings` are a temporary migration bridge and SHALL be removed after QML is migrated for each domain. `Connections` blocks that target settings properties SHALL target the domain sub-object (`Connections { target: Settings.mqtt }`).

#### Scenario: QML reads a setting via domain sub-object
- **WHEN** `SettingsHomeAutomationTab.qml` reads the MQTT broker host
- **THEN** it accesses `Settings.mqtt.mqttBrokerHost`
- **AND** the binding updates when `SettingsMqtt::mqttBrokerHostChanged` fires

#### Scenario: No flat Settings.* access for migrated domains
- **WHEN** the codebase is searched for `Settings\.mqttEnabled` in QML files
- **THEN** zero matches are found
- **AND** all MQTT settings are accessed via `Settings.mqtt.*`

#### Scenario: QML signal connection uses domain target
- **WHEN** a QML component listens for theme changes
- **THEN** it uses `Connections { target: Settings.theme; function onActiveThemeNameChanged() {} }`
- **AND** not `Connections { target: Settings; function onActiveThemeNameChanged() {} }`

### Requirement: Narrow Consumer Header Isolation

Each domain-specific C++ consumer (a class that reads only one domain's settings) SHALL include only the domain header, not `settings.h`. Its constructor SHALL accept the domain sub-object pointer instead of `Settings*`. `main.cpp` SHALL pass the sub-object accessor (e.g., `settings.mqtt()`) at the call site.

#### Scenario: MqttClient does not include settings.h
- **WHEN** `settings_mqtt.h` is modified
- **THEN** only files that directly include `settings_mqtt.h` are recompiled
- **AND** `settings.h` is not among them

#### Scenario: AutoWakeManager does not include settings.h
- **WHEN** the auto-wake schedule is read at startup
- **THEN** `AutoWakeManager` receives a `SettingsAutoWake*` and calls `autoWakeSchedule()` on it
- **AND** `autowakemanager.cpp` does not `#include` `settings.h`

### Requirement: Shot History Types Extraction

The data structures defined in `shothistorystorage.h` (`ShotRecord`, `HistoryShotSummary`, `ShotFilter`, `ShotSaveData`, `GrinderContext`, `HistoryPhaseMarker`) SHALL be extracted to `shothistory_types.h`. `shothistorystorage.h` SHALL `#include "shothistory_types.h"`. Components that only store a `ShotHistoryStorage*` pointer and call simple accessors (`databasePath()`, `totalShots()`, `isReady()`) SHALL forward-declare `ShotHistoryStorage` in their headers and include `shothistorystorage.h` only in their `.cpp`.

#### Scenario: Pointer-only consumer avoids full header
- **WHEN** `DatabaseBackupManager` is compiled
- **THEN** it uses `class ShotHistoryStorage;` forward declaration in its header
- **AND** includes `shothistorystorage.h` only in its `.cpp`
- **AND** changing shot history struct definitions does not trigger its recompilation

#### Scenario: Struct consumers include the types header
- **WHEN** `ShotAnalysis` uses `ShotRecord` fields
- **THEN** it includes `shothistory_types.h` (or `shothistorystorage.h` which re-exports it)
- **AND** its compile behaviour is unchanged from before the extraction
