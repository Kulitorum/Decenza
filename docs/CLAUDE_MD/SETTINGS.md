# Settings Architecture

`Settings` is a **composition façade** that owns 12 domain sub-objects. Each sub-object is its own `QObject` with its own `QSettings` instance, its own `Q_PROPERTY` declarations, and its own NOTIFY signals. The split exists so that touching one domain's header recompiles only its narrow set of consumers (~9 files for `settings_mqtt.h`) instead of every consumer of the monolithic `settings.h` (~39 files pre-split, 41 pre-refactor).

The split was tricky to get right — the rules below capture every gotcha that came up during PR #852 (issue #743). Follow them and the architecture stays healthy.

## Domain classes (today)

`SettingsMqtt`, `SettingsAutoWake`, `SettingsHardware`, `SettingsAI`, `SettingsTheme`, `SettingsVisualizer`, `SettingsMcp`, `SettingsBrew`, `SettingsDye`, `SettingsNetwork`, `SettingsApp`, `SettingsCalibration`. What remains on `Settings` itself is machine/scale/refractometer/USB-serial — a candidate for a future `SettingsHardware` extension or a Tier 4 `SettingsMachine` split.

## Where new settings go

- **Existing domain fits** — add the property to the appropriate `Settings<Domain>` class. Never add it back to `Settings`.
- **No domain fits** — prefer creating a new sub-object class over adding to `Settings`. Use the checklist below.
- **Truly cross-domain** (e.g. coordinator state that touches multiple domains) — keep on `Settings`, but be deliberate about it.

## Adding a new property to an existing domain class

Edit `src/core/settings_<domain>.h` and `.cpp`. Add:

1. `Q_PROPERTY(...)` line in the header
2. Getter + setter declarations in the header
3. NOTIFY signal in `signals:` section
4. Getter/setter bodies in the `.cpp`, reading/writing through `m_settings.value(...)` / `m_settings.setValue(...)` with the domain's existing key prefix (e.g. `mqtt/`, `theme/`)

That's it — no other files need to change. The narrow consumer set defined in `settings.cpp`'s constructor and the header's `Q_PROPERTY` exposure are unchanged.

## Adding a new domain sub-object class

Full checklist (8 steps — missing one will silently break things):

1. **Create `src/core/settings_<domain>.h` + `.cpp`**. Inherit `QObject`, own a `mutable QSettings m_settings("DecentEspresso", "DE1Qt")`, declare properties + getters + setters + NOTIFY signals.
2. **Add forward declaration in `src/core/settings.h`** at the top. NEVER `#include "settings_<domain>.h"` in `settings.h` — that pulls the new header into ~39 .cpp files transitively and undoes the build win.
3. **Add `Q_PROPERTY(QObject* <domain> READ <domain>QObject CONSTANT)` to `Settings`**. The property type **must be `QObject*`**, not `Settings<Domain>*`. The typed pointer requires the full type for moc-generated code, which means including the header — losing the build win. `QObject*` lets QML resolve via the runtime metaObject.
4. **Add typed inline accessor in header**: `Settings<Domain>* <domain>() const { return m_<domain>; }`. C++ callers use this (they include `settings_<domain>.h` themselves).
5. **Add out-of-line `QObject*` accessor in `settings.cpp`**: `QObject* Settings::<domain>QObject() const { return m_<domain>; }`. The upcast requires the full type, which `settings.cpp` already has via its construction includes.
6. **Construct in `Settings::Settings()` member-init list**: `, m_<domain>(new Settings<Domain>(this))`. Add the `#include "settings_<domain>.h"` in `settings.cpp`.
7. **Register with QML in `main.cpp`**: `qmlRegisterUncreatableType<Settings<Domain>>("Decenza", 1, 0, "Settings<Domain>Type", "Settings<Domain> is created in C++");`. **Without this, QML resolves `Settings.<domain>.<prop>` to `undefined` at runtime even though `QObject*` is the property type.** This is the single most painful failure mode in the architecture — it doesn't show up until runtime, and it blanks every binding that walks through the sub-object.
8. **Wire into the build**: add to `CMakeLists.txt` (both `SOURCES` and `HEADERS` lists) and `tests/CMakeLists.txt` (`CORE_SOURCES`).

## QML access pattern

Always: `Settings.<domain>.<property>` (chained through the sub-object).

Never: `Settings.<property>` (flat, only valid for properties that remain on `Settings` itself).

```qml
// Right
checked: Settings.mqtt.mqttEnabled
text: Settings.theme.activeThemeName
onValueModified: Settings.visualizer.defaultShotRating = newValue

// Wrong — silently fails (Settings has no flat mqttEnabled property anymore)
checked: Settings.mqttEnabled
```

### `Connections` blocks

`Connections { target: Settings }` listening for signals that have moved to a sub-object **silently never fire** — QML doesn't warn. Re-target to the sub-object:

```qml
// Right
Connections {
    target: Settings.theme
    function onCustomThemeColorsChanged() { ... }
    function onIsDarkModeChanged() { ... }
}

// Wrong — handlers never fire because Settings doesn't emit these signals
Connections {
    target: Settings
    function onCustomThemeColorsChanged() { ... }
}
```

This was the bug that broke the entire theme editor's swatch refresh and dark/light auto-switch in the original PR.

## C++ consumer pattern

Narrow consumers (a class that only needs one domain's settings) take the **typed domain pointer**, not `Settings*`:

```cpp
// Right — narrow consumer
class AutoWakeManager {
public:
    explicit AutoWakeManager(SettingsAutoWake* settings, QObject* parent = nullptr);
};

// In main.cpp / MainController:
AutoWakeManager autoWakeManager(settings.autoWake());
```

The narrow header (`#include "settings_autowake.h"`) means the consumer recompiles only when `settings_autowake.h` changes — not when `settings.h` or any other domain header changes. **This is where the build win comes from.** A narrow consumer that takes `Settings*` defeats the purpose.

Wide consumers (e.g. `MainController`, `settingsserializer.cpp`) that touch multiple domains keep `Settings*` and use `settings->mqtt()->X()` — they pay the include cost because they need it.

## Cross-domain side effects

Setters on a sub-object can't directly call methods on another domain (they only see their own type). Wire cross-domain reactions via `connect()` in the `Settings::Settings()` constructor body, where every sub-object is reachable:

```cpp
// In Settings::Settings(), after all m_X members are constructed:
connect(m_visualizer, &SettingsVisualizer::defaultShotRatingChanged, this, [this]() {
    setDyeEspressoEnjoyment(m_visualizer->defaultShotRating());
});
```

Don't try to inline the cross-call inside the sub-object's setter — `SettingsVisualizer::setDefaultShotRating` doesn't see `setDyeEspressoEnjoyment`, and adding the dependency would couple two domains that have no business knowing about each other.

### When `connect()` isn't enough: signal-out, then forward

Some cross-domain effects originate from a method on the sub-object itself rather than a property change. `SettingsCalibration::resetSawLearning()` is the canonical example: it's invoked directly from QML and MCP tools and must, as a side effect, reset hot-water-SAW state on `SettingsBrew`. The sub-object can't call into `SettingsBrew` directly — same isolation rule as above. Instead:

1. The sub-object emits a dedicated **request signal** (`sawLearningResetRequested`) inside the method.
2. `Settings::Settings()` connects that signal to a lambda that performs the cross-domain action: `connect(m_calibration, &SettingsCalibration::sawLearningResetRequested, this, [this]{ m_brew->setHotWaterSawOffset(2.0); m_brew->setHotWaterSawSampleCount(0); });`
3. The signal name encodes intent ("X happened, please react"), not implementation ("call brew::setHotWaterSawOffset"). This keeps the request-emitter decoupled from the responder.

The pattern generalises to any "method-call → cross-domain effect": signal-out, then forward via `connect()` in the parent constructor.

### Holding a non-owning back-pointer to `Settings`

The default rule is sub-objects do not see each other and do not see `Settings`. `SettingsCalibration` is the documented exception: it holds a non-owning `Settings* m_owner` (set in its constructor) so `sawLearnedLag()` and `getExpectedDrip()` can read `m_owner->scaleType()` without changing their public API (both are zero-arg methods called from QML; threading `scaleType` through every call would force a QML-side migration). The discipline that keeps this safe:

- The back-pointer is dereferenced **only for `m_owner->scaleType()`**. No other `Settings` surface is allowed via the back-pointer — that would re-couple the domain to the parent and defeat the split.
- `settings_calibration.h` only forward-declares `Settings`. The full include lives in `settings_calibration.cpp`.
- The pointer is `nullptr`-guarded at every read (e.g. for tests that construct the sub-object standalone) with a sensible fallback.

Use this pattern only when the alternative (changing a public API) would force a migration that's larger than the cost of the coupling. For most domains, plain `connect()`-based wiring is the right answer.

## Null-guard discipline

When a class holds both `Settings*` and a sub-object pointer (e.g. `MqttClient` has `m_settings` for steam state + `m_settingsMqtt` for MQTT state), each guard must check the pointer it's about to dereference. Mismatched guards (`if (!m_settings) return;` followed by `m_settingsMqtt->X()`) are a recurring trap — they don't crash today only because the call sites in `main.cpp` always pass both non-null. The sed-based migration in PR #852 hit this twice; check carefully when you split a new domain.

```cpp
// Right
QString clientId = m_settingsMqtt ? m_settingsMqtt->mqttClientId() : "";

// Wrong — guard checks the wrong pointer
QString clientId = m_settings ? m_settingsMqtt->mqttClientId() : "";
```

## Storage keys

Each sub-object's `mutable QSettings m_settings("DecentEspresso", "DE1Qt")` opens a **separate handle to the same backing store**. Qt makes that thread-safe on the main thread. Use the same key prefix the property had before the split (e.g. MQTT keys stay `mqtt/enabled`, `mqtt/brokerHost`, etc.) so existing user settings persist across the upgrade.

Do **not** rename keys when moving a property between domains — that silently loses every user's saved value.

## When in doubt

The `openspec/changes/split-headers-by-domain/` folder has the proposal, design notes, and tasks list documenting why the architecture looks the way it does and what's still pending.
