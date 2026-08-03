# Settings Architecture

`Settings` is a **composition façade** that owns 13 domain sub-objects. Each sub-object is its own `QObject` with its own `QSettings` instance, its own `Q_PROPERTY` declarations, and its own NOTIFY signals.

The split exists so that a **narrow consumer** — one that takes a `Settings<Domain>*` and includes only that domain's header — recompiles when its own domain changes and not when any other does (~9 files for `settings_mqtt.h`). That is still true and is still the reason to write consumers that way.

What is **no longer** true: that a domain-header edit is cheap for everything else. `settings.h` includes all thirteen domain headers (see "Why the includes are back"), so anything taking a `Settings*` rebuilds on any domain change — **~60 s, against ~26 s before**. The blast was already large before the change, so this is a widening of an existing cost, not a new one. The way to reduce it is to make more consumers narrow, or to trim what the domain headers themselves include.

The split was tricky to get right — the rules below capture every gotcha that came up during PR #852 (issue #743). Follow them and the architecture stays healthy.

## Domain classes (today)

`SettingsMqtt`, `SettingsAutoWake`, `SettingsHardware`, `SettingsAI`, `SettingsTheme`, `SettingsVisualizer`, `SettingsMcp`, `SettingsBrew`, `SettingsDye`, `SettingsNetwork`, `SettingsApp`, `SettingsCalibration`, `SettingsGraph`. What remains on `Settings` itself is machine/scale/refractometer/USB-serial — a candidate for a future `SettingsHardware` extension or a Tier 4 `SettingsMachine` split.

## Where new settings go

- **Existing domain fits** — add the property to the appropriate `Settings<Domain>` class. Never add it back to `Settings`.
- **No domain fits** — prefer creating a new sub-object class over adding to `Settings`. Use the checklist below.
- **Truly cross-domain** (e.g. coordinator state that touches multiple domains) — keep on `Settings`, but be deliberate about it.

## Adding a new property to an existing domain class

Edit `src/core/settings_<domain>.h` and `.cpp`. Add:

1. `Q_PROPERTY(... FINAL)` line in the header — include `FINAL`; see step 3 of "Adding a new domain" below for why omitting it silently costs AOT compilation. It is load-bearing on anything QML then reads a property *off* (`Settings.brew.x`, or `foo.length` on a string), and inert otherwise — these classes apply it uniformly rather than tracking which properties are currently chained. Not to be confused with `QML_GOTCHAS.md`'s "never override FINAL properties on Qt types", which is about shadowing a base type you don't own.
2. Getter + setter declarations in the header
3. NOTIFY signal in `signals:` section
4. Getter/setter bodies in the `.cpp`, reading/writing through `m_settings.value(...)` / `m_settings.setValue(...)` with the domain's existing key prefix (e.g. `mqtt/`, `theme/`)

That's it — no other files need to change. The narrow consumer set defined in `settings.cpp`'s constructor and the header's `Q_PROPERTY` exposure are unchanged.

## Adding a new domain sub-object class

Full checklist (8 steps — missing one will silently break things):

1. **Create `src/core/settings_<domain>.h` + `.cpp`**. Inherit `QObject`, own a `mutable AppSettings m_settings` (default-constructed — `AppSettings` names the store, see `src/core/appsettings.h`), declare properties + getters + setters + NOTIFY signals.
2. **Add `#include "settings_<domain>.h"` to `src/core/settings.h`** with the other eleven.
   *(This reverses earlier guidance, which said never to include it. See "Why the includes are back" below — the short version is that avoiding it required erasing the property type, which blinded qmllint, `qmlcachegen` and the language server to 1,310 QML call sites.)*
3. **Add `Q_PROPERTY(Settings<Domain>* <domain> READ <domain> CONSTANT FINAL)` to `Settings`** — the CONCRETE type, never `QObject*`. This is what lets every tool follow `Settings.<domain>.<prop>` through to the property.
   *`FINAL` is required, not stylistic: without it `qmlcachegen` will not compile ANY chained lookup through the accessor. A non-final property could be shadowed by a subclass, so the base degrades to `var` and the next lookup off it fails with "Cannot use shadowable base type for further lookups" (`qqmljsshadowcheck.cpp:248`; `:197-198` is the final-property escape). Omitting it silently costs AOT compilation everywhere `Settings.<domain>.<prop>` is read — no error, no warning.*
4. **Add typed inline accessor in header**: `Settings<Domain>* <domain>() const { return m_<domain>; }`. C++ callers use this (they include `settings_<domain>.h` themselves).
5. *(No `QObject*` accessor. The `Q_PROPERTY` READs the typed accessor from step 4 directly — the old `<domain>QObject()` pair existed only to support the erased property type and has been deleted.)*
6. **Construct in `Settings::Settings()` member-init list**: `, m_<domain>(new Settings<Domain>(this))`. Add the `#include "settings_<domain>.h"` in `settings.cpp`.
7. **Register with QML in `src/core/settings_qml.h`**, not `main.cpp`:

   ```cpp
   struct Settings<Domain>Foreign
   {
       Q_GADGET
       QML_FOREIGN(Settings<Domain>)
       QML_NAMED_ELEMENT(Settings<Domain>Type)
       QML_UNCREATABLE("Settings<Domain> is created in C++")
   };
   ```

   **Without this, QML resolves `Settings.<domain>.<prop>` to `undefined` at runtime while compiling clean.** Still the single most painful failure mode in the architecture. It moved from a runtime `qmlRegisterUncreatableType<>` call in `main.cpp` because `qmltyperegistrar` cannot see runtime calls, so the linter could not either. Write the struct out literally — moc does not expand macros that declare a `Q_GADGET`, so a generated one registers nothing, silently.
8. **Wire into the build**: add to `CMakeLists.txt` (both `SOURCES` and `HEADERS` lists) and `tests/CMakeLists.txt` (`CORE_SOURCES`).

## Why the includes are back

`settings.h` includes all thirteen domain headers, and the domain `Q_PROPERTY`s carry their
concrete types. Earlier guidance said the opposite — forward-declare, and declare the properties
`QObject*` — purely to keep the recompile blast down.

That erasure cost more than it saved. qmllint cannot check a property behind a `QObject*`, so
**1,310 QML call sites across 281 distinct settings** were unverifiable: `Settings.brew.slectedX`
compiled, linted clean, and failed at runtime — the same class of defect that shipped in 2.0.1 as
#1661. `qmlcachegen` also could not resolve those bindings ahead of time, and the QML language
server could not complete or navigate them.

`Q_DECLARE_OPAQUE_POINTER` is **not** a way to have both. It compiles and satisfies the linter,
then hands QML a `QVariant(Settings<Domain>*)` rather than an object, so every property and
method under `Settings.<domain>` fails at runtime. That was tried and reverted;
`tst_settings::qmlChainsThroughDomainSubObjects` pins the working behaviour so it cannot be
reintroduced quietly.

Measured cost of the includes, touching one domain header (ASan+UBSan debug, warm ccache):
**60 s, against 26 s before**. Full build is 122 s for scale.

**Read the wall clock, not the file counts.** ninja reports a dirty set of 439 after and 310
before, but those are the *pre-`restat`* set — the build actually executed 297 edges, because
`restat` prunes the chain wherever regenerated content turns out unchanged. Splitting the dirty
set by kind: 221 C++ TUs + 218 QML cache units after, against roughly 92 + 218 before. The 218
QML units are in **both**, so they are not this decision's cost — a domain header carries
`Q_OBJECT`, and any moc-metadata change invalidates `Decenza.qmltypes` and with it every QML
unit. The marginal cost of declaring the types honestly is the **+129 C++ TUs**, ~34 s.

Note the *before* number too: ~92 C++ TUs already rebuilt on that edit, so the blast was large
regardless and the `QObject*` trick was buying less than it looked like.

The domain split still does its job — implementations stay in their own `.cpp` files, and narrow
consumers taking `Settings<Domain>*` still recompile only on their own header. If the 60 s
becomes painful, fix it by restructuring what the domain headers pull in. Never by erasing the
types again.

## QML access pattern

Always: `Settings.<domain>.<property>` (chained through the sub-object).

Never: `Settings.<property>` (flat, only valid for properties that remain on `Settings` itself).

```qml
// Right
checked: Settings.mqtt.mqttEnabled
text: Settings.theme.activeThemeName
onValueModified: Settings.visualizer.visualizerMinDuration = newValue

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
connect(m_calibration, &SettingsCalibration::sawLearningResetRequested, this, [this]() {
    m_brew->setHotWaterSawOffset(2.0);  // Back to default
    m_brew->setHotWaterSawSampleCount(0);
});
```

Don't try to inline the cross-call inside the sub-object's setter — `SettingsCalibration` doesn't see `SettingsBrew`'s setters, and adding the dependency would couple two domains that have no business knowing about each other.

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

Each sub-object's `mutable AppSettings m_settings` opens a **separate handle to the same backing store**. Qt makes that thread-safe on the main thread. Never construct a `QSettings` directly — `AppSettings` is the only place the store identity is named, and it is also what applies test isolation. Use the same key prefix the property had before the split (e.g. MQTT keys stay `mqtt/enabled`, `mqtt/brokerHost`, etc.) so existing user settings persist across the upgrade.

Do **not** rename keys when moving a property between domains — that silently loses every user's saved value.

## `SettingsTheme.backgroundImagePath`

Optional custom background image, applied app-wide (every page, both light and dark mode — coverage started at 8 pages and expanded to universal, see design.md Decision 6a). Empty = today's flat `Theme.backgroundColor`. Sourced entirely from the screensaver media library (`ScreensaverVideoManager`/`ScreensaverManager`): personal (web-uploaded) images always show up, but stock/catalog images only appear once they've been downloaded to disk by the existing rate-limited background download (`startBackgroundDownload()`), across every category ever selected — `getCachedCatalogImages()` reads `m_cacheIndex` directly rather than the currently-selected category's `m_catalog`, since the catalog list is replaced wholesale on every category switch but the on-disk cache index isn't. `ScreensaverVideoManager::getCachedCatalogImages()` deliberately does **not** force a download — a sparse `BackgroundPickerDialog` grid right after install or a fresh category is expected behavior, not a bug; it fills in over time.

The shared chrome (`StatusBar.qml`, `BottomBar.qml`, `IdlePage`'s own bottom nav bar, and every page-level card via `Theme.cardBackgroundColor`/`Theme.insetBackgroundColor`) automatically goes semi-transparent when a background image is active — each reads `Settings.theme.backgroundImagePath.length > 0` directly rather than through a shared intermediary property. No separate setting for this; see `openspec/changes/add-custom-background/design.md` Decision 6/6a. `ScreensaverVideoManager::deletePersonalMedia()`/`clearPersonalMedia()`/`clearCache()` clear `backgroundImagePath` if it points at the file being deleted, since none of these ~70 call sites are aware of whether the image actually loaded — only `ThemedPageBackground.qml` checks `Image.status` — so a stale path left in place would leave every card/bar stuck in translucent "background active" mode over nothing.

## When in doubt

The `openspec/changes/split-headers-by-domain/` folder has the proposal, design notes, and tasks list documenting why the architecture looks the way it does and what's still pending.
