# Change: Complete Settings domain split (Tier 2)

## Why

PR #852 (`split-headers-by-domain`) extracted 7 domain sub-objects from `Settings` (Mqtt, AutoWake, Hardware, AI, Theme, Visualizer, Mcp) and shrank `settings.h` from 1,170 → 887 lines. Four domains were deferred because they carried more cross-coupling and QML/C++ surface than fit safely into one PR:

- **Brew** — ~70 methods (espresso, steam, hot water, flush, presets, overrides), the largest single domain
- **Dye** — 13 metadata fields + bean-preset CRUD, tightly coupled to Brew via the `setDefaultShotRating` ↔ `setDyeEspressoEnjoyment` cross-call and the `beansModified` recompute chain
- **Network** — shot server, web security, auto-favorites, saved searches, layout configuration (~10 invokables)
- **App** — updates, backup, dev flags, simulation/launcher mode, water-level/refill, profile management (favorites, hidden, current, builtins), platform capability flags

Without finishing Tier 2, the recompile-blast goal (`settings.h` ≤ 200 lines, ≤ 10 transitive includers) is blocked: today `settings.h` is 904 lines with 111 `Q_PROPERTY` and 83 `Q_INVOKABLE` declarations remaining, and 39 `.cpp` files still pull it in transitively.

## What Changes

- Add 4 new domain sub-objects: `SettingsBrew`, `SettingsDye`, `SettingsNetwork`, `SettingsApp`
- Move every remaining property/invokable off `Settings` into the matching domain sub-object
- Re-wire the Brew↔Dye cross-domain side effect (`setDefaultShotRating` → `setDyeEspressoEnjoyment`) via `connect()` in `Settings::Settings()`, matching the pattern documented in `docs/CLAUDE_MD/SETTINGS.md`
- Re-wire the `beansModified` recompute chain (today fires from many setters across brew/dye) via cross-domain connects
- Migrate all QML readers to `Settings.<domain>.<prop>` and re-target affected `Connections` blocks
- Migrate narrow C++ consumers to take the typed sub-object pointer instead of `Settings*`
- Add `qmlRegisterUncreatableType<Settings<Domain>>(...)` calls in `main.cpp` for each new sub-object
- **BREAKING (internal API only):** removes flat `Settings::X()` accessors for all migrated properties; QML and C++ must use `Settings.<domain>.X` / `settings->domain()->X()`
- Final state: `settings.h` ≤ 200 lines containing only the 11 sub-object accessors plus cross-domain methods (`sync`, `factoryReset`, the cross-domain `connect()` wiring)

## Impact

- **Affected specs:** `settings-architecture` (MODIFIED — completes the domain decomposition; the existing "settings.h SHALL be under 200 lines" scenario becomes verifiable rather than aspirational)
- **Affected code:**
  - `src/core/settings.{h,cpp}` — strip ~700 lines, add 4 new sub-object accessors and cross-domain connects
  - 4 new file pairs: `src/core/settings_brew.{h,cpp}`, `settings_dye.{h,cpp}`, `settings_network.{h,cpp}`, `settings_app.{h,cpp}`
  - `src/main.cpp` — 4 new `qmlRegisterUncreatableType` calls + sub-object pointer wiring at narrow consumer call sites
  - `CMakeLists.txt` + `tests/CMakeLists.txt` — add new sources to `SOURCES`/`HEADERS`/`CORE_SOURCES`
  - ~13 QML files (brew settings, dye/bean editors, settings tabs, idle screens, layout editor, network/web tabs)
  - ~6+ C++ consumers including `MainController`, `settingsserializer.cpp`, several `mcptools_*.cpp` files, several `shotserver_*.cpp` files
  - `tests/tst_settings.cpp` — switch to sub-object accessors for any tests that touched migrated properties
- **Risk:** medium — Brew/Dye coupling is the trickiest piece; the `beansModified` chain and the rating↔enjoyment sync have multiple call sites that must keep working. PR #852 broke screen rendering once because `Q_DECLARE_OPAQUE_POINTER` blocked QML introspection — the established `QObject*` Q_PROPERTY + `qmlRegisterUncreatableType` pattern (see `SETTINGS.md` step 7) avoids that trap.
