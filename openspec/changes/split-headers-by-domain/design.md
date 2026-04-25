# Design: Split Headers by Domain

## Architecture

### Settings composition pattern

`Settings` becomes a **composition façade**. It owns the domain objects and exists as the single QML-registered context property. During the migration, it temporarily retains forwarding `Q_PROPERTY`s; these are removed domain-by-domain as QML is migrated.

```
Settings (QObject, QML context property "Settings")
├── SettingsMqtt*        m_mqtt        (owned, parent = Settings)
├── SettingsAutoWake*    m_autoWake    (owned, parent = Settings)
├── SettingsHardware*    m_hardware    (owned, parent = Settings)
├── SettingsAI*          m_ai          (owned, parent = Settings)
├── SettingsTheme*       m_theme       (owned, parent = Settings)
├── SettingsVisualizer*  m_visualizer  (owned, parent = Settings)
├── SettingsMcp*         m_mcp         (owned, parent = Settings)
├── SettingsBrew*        m_brew        (owned, parent = Settings)
├── SettingsDye*         m_dye         (owned, parent = Settings)
├── SettingsNetwork*     m_network     (owned, parent = Settings)
└── SettingsApp*         m_app         (owned, parent = Settings)
```

### Domain class structure

Each domain class:
- Inherits `QObject`
- Has its own `Q_PROPERTY` declarations and `NOTIFY` signals
- Creates its own `QSettings` instance via the default constructor (same backing store — Qt guarantees thread-safe multi-instance access to the same application settings)
- Takes no pointer to `Settings`; it is fully independent

```cpp
class SettingsMqtt : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool mqttEnabled READ mqttEnabled WRITE setMqttEnabled NOTIFY mqttEnabledChanged)
    // ...
public:
    explicit SettingsMqtt(QObject* parent = nullptr);
    bool mqttEnabled() const;
    void setMqttEnabled(bool enabled);
signals:
    void mqttEnabledChanged();
private:
    mutable QSettings m_settings;
};
```

### Per-domain migration sequence

For each domain, the work follows this exact sequence:

**Step 1 — Add sub-object + forwarding:**
- Implement domain class
- `Settings` gains `Q_PROPERTY(SettingsMqtt* mqtt READ mqtt CONSTANT)` accessor
- `Settings` retains existing `Q_PROPERTY(bool mqttEnabled …)` forwarding — delegates getter/setter to `m_mqtt`, re-emits signal via `connect(m_mqtt, &SettingsMqtt::mqttEnabledChanged, this, &Settings::mqttEnabledChanged)` in constructor

**Step 2 — Migrate QML:**
- Replace `Settings.mqttEnabled` → `Settings.mqtt.mqttEnabled` in all QML files for that domain
- Replace `Connections { target: Settings; function onMqttEnabledChanged() {} }` → `Connections { target: Settings.mqtt … }`
- Verify the app builds and functions correctly

**Step 3 — Remove forwarding from Settings:**
- Delete the forwarding `Q_PROPERTY`, getter, setter, signal, and `connect()` line for the domain from `settings.h` / `settings.cpp`
- `settings.h` shrinks; files that include `settings.h` no longer see MQTT properties → they no longer recompile on MQTT changes

**Step 4 — Update C++ consumers:**
- Narrow consumers change constructor parameter from `Settings*` to the domain type
- `#include "settings.h"` → `#include "settings_mqtt.h"` in those files
- `main.cpp` passes `settings.mqtt()` at the call site

This sequence means `settings.h` never has a moment where a property is missing — forwarding is live until QML is fully migrated for that domain.

### C++ injection pattern

```cpp
// Before:
MqttClient::MqttClient(Settings* settings, ...) : m_settings(settings) {}

// After:
MqttClient::MqttClient(SettingsMqtt* settings, ...) : m_settings(settings) {}
```

Consumers that need more than one domain take multiple domain pointers. The rule: **include only the domain header(s) you use.**

### Final state of Settings.h

After all domains are extracted, `settings.h` contains:
- Sub-object accessor `Q_PROPERTY`s only (`Q_PROPERTY(SettingsMqtt* mqtt READ mqtt CONSTANT)` etc.)
- Any properties that proved too cross-cutting to assign to a single domain
- The `sync()` method and `factoryReset()` which coordinate across domains
- Constructor, destructor

The file shrinks from ~1,170 lines to roughly 100–150 lines.

## Why composition, not pimpl

Pimpl hides private *state* from the header, stabilising it against internal cache changes. It doesn't help narrow consumers — they still include `settings.h` just to access one domain's public API. Domain composition solves both: narrow consumers include only their domain header, and `settings.h` itself shrinks as forwarding is removed.

## QSettings instance per domain

Each domain creates its own `QSettings` via the default constructor. Qt guarantees that multiple `QSettings` instances with the same application scope access the same backing store safely from a single thread (the main thread). There is no file locking issue because all reads/writes happen on the Qt main thread. Domain objects use the same key prefixes as before (e.g., `"mqtt/enabled"`) so no data migration is needed.

## shothistory_types.h extraction

`shothistorystorage.h` mixes class definition with data structure definitions. Files that only call simple methods on a `ShotHistoryStorage*` pointer are forced to compile all struct definitions.

Solution: create `src/history/shothistory_types.h` containing only the structs. `shothistorystorage.h` `#include`s `shothistory_types.h`. The 9 pointer-only consumers switch to `class ShotHistoryStorage;` forward declaration in their headers and only include `shothistorystorage.h` in their `.cpp` where actually needed.

## File locations

Domain headers live alongside `settings.h` in `src/core/`:

```
src/core/
├── settings.h             (façade — thins progressively)
├── settings.cpp
├── settings_mqtt.h / .cpp
├── settings_autowake.h / .cpp
├── settings_hardware.h / .cpp
├── settings_ai.h / .cpp
├── settings_theme.h / .cpp
├── settings_visualizer.h / .cpp
├── settings_mcp.h / .cpp
├── settings_brew.h / .cpp
├── settings_dye.h / .cpp
├── settings_network.h / .cpp
└── settings_app.h / .cpp
```

```
src/history/
├── shothistory_types.h    (new — structs only)
├── shothistorystorage.h   (includes shothistory_types.h)
└── shothistorystorage.cpp
```

## Validation approach

After each domain's Step 4:
1. `grep -rl '#include.*settings\.h' src/ --include='*.cpp' | wc -l` decreases by expected count
2. Touch the domain header; confirm `make --dry-run` only rebuilds its narrow consumers
3. No `Settings.domainProperty` access patterns remain in QML for that domain (grep check)
4. Build succeeds with no warnings on all touched files
