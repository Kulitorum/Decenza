# Proposal

## Why

Editing any one QML file, even adding a comment, takes ~104 s and recompiles all ~360 native QML cache objects. Measured on 2026-10-25: 0 of 377 generated files changed after a comment edit, and 9 of 361 after a real signature change. Most of that time is serial steps unrelated to the edit.

Separately, the Wasm client and the test QML modules can hold stale cache units: 8 of 9 Wasm callers were not regenerated after a `Theme.qml` signature change, and one test-module unit was stale on a clean tree. Since Qt 6.12 a stale unit silently falls back to the interpreter (`qv4executablecompilationunit.cpp:153-177`) rather than misbehaving. Release builds start clean, so this affects local builds only, and nothing reports it.

## What Changes

The change is ordered by payoff, and starts with a measured spike.

- **Spike: native QML work stops waiting for the Wasm bundle.** Native cachegen is ordered behind the Wasm child build and both rcc passes of the ~131 MB embedded bundle (~48 s). Only the final link needs the bundle.
- **Spike: content-stable native QML cache output.** qmlcachegen's output is replaced only when its bytes change, so Ninja's existing `restat` stops recompiling unchanged units. This uses Qt's documented `QT_QMLCACHEGEN_EXECUTABLE` override. The native all-to-all cross-file dependency stays as it is.
- **Decision gate.** The rest proceeds only if the spike's measured savings hold.
- **QML edits stop re-running CMake.** The package identity is hashed at configure time, so every QML file is a configure dependency, costing ~11 s native and ~7.6 s Wasm per edit. It moves to a build-time step with the same deterministic value.
- **The Wasm module gets the same cross-file wiring as native.** The narrowed plan detected Wasm staleness instead, but reordering `AppShell` then silently left 49 Wasm units stale; with content-stable cachegen the wiring costs one cachegen pass (~4-5 s) per QML edit.
- **A per-PR check rejects both AOT-breaking declaration shapes** (`scripts/check_qml_aot_declarations.py` in `text-invariants.yml`). That required declaring properties before signals in all 60 files that had them the other way round.
- **Staleness elsewhere is detected.** A test-environment AOT validation check (Qt's `QV4_FAIL_ON_INVALID_AOT`) makes a test fail when it loads a stale unit. An on-demand consistency check regenerates every unit of every QML module and names any that differ.
- **Fix the silent AOT rejections the AOT validation check found.** 29 QML functions get an explicit return annotation, and three types declare their properties before their signals. Before this, every unit calling those members ran interpreted in the shipped app; at startup alone that was 9 units.
- **Embed the Wasm bundle uncompressed in local builds.** zlib -9 over ~131 MB cost ~15 s on every source edit; release builds keep it.
- **Non-goals:**
  - Upstream Qt patches. Skipping identical output writes, a qmlcachegen depfile, and the 6.13 `libQmlCompiler` dependency for custom executables would all let local workarounds be deleted later. They are optional, separate contributions, and nothing here depends on them.
  - Dropping AOT, and parallelising qmllint.
  - Skipping the Wasm rebuild and embed in the Debug loop. That is the largest remaining lever (~37 s per edit), but it is a product decision about the paired build.

## Capabilities

### New Capabilities
- `qml-cache-generation`: How QML sources become compiled cache units: incrementality (an edit recompiles only units whose generated output changed), build ordering, the configure behaviour on QML edits, and how stale units are detected.

### Modified Capabilities
None. The deterministic package identity and matched-pair contract owned by the in-flight `replace-web-interface-with-wasm` change keep their value and semantics; only *when* the identity is computed changes.

## Impact

- `CMakeLists.txt`: the cachegen executable override on `Decenza`, the autogen ordering relative to `decenza_webclient_resources`, and `qml_dep_wiring_check`.
- `cmake/`: the cachegen wrapper, and the consistency-check target. `cmake/wasm_package_identity.cmake` and `cmake/webclient_bundle.cmake` move identity hashing to build time on the local paired path; the release/prebuilt path keeps configure-time verification.
- Test registration: the AOT-validation environment variable.
- `docs/CLAUDE_MD/BUILD_PERFORMANCE.md`: the restat, cross-file and levers sections.
- No runtime, UI or user-visible behaviour changes, so no manual entry. Generated QML code stays byte-identical for an unchanged tree.
