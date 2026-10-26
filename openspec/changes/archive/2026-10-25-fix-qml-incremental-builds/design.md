# Design

## Context

See proposal.md for why, and `specs/qml-cache-generation/spec.md` for the requirements. These measurements were taken on 2026-10-25 with macOS Debug, the paired native/Wasm build, at `8d4cd1028`.

One QML edit (~104 s) breaks down as follows:

| Phase | Wall | Cause |
|---|---|---|
| Native CMake re-run | ~11 s | `cmake/wasm_package_identity.cmake:81` makes every identity input (all QML) a `CMAKE_CONFIGURE_DEPENDS` |
| Wasm child build | 27 s | includes its own 7.6 s CMake re-run |
| rcc pass 1 + 2 of the embedded bundle | 19 s | ~131 MB (`.wasm` plus `.wasm.gz`) |
| cachegen ×361 | 7.5 s | the all-to-all wiring at `CMakeLists.txt:2408-2468` (correct, intentional) |
| QML objects ×360 | 33 s | qmlcachegen rewrites identical output (`qqmljscompiler.cpp:584-588, 707-709`), so `restat` cannot prune |
| qmllint gate | 23.5 s | in parallel; not on the critical path |
| link | 2.5 s | |

Native cachegen is order-only on `Decenza_autogen`. `Decenza_autogen_timestamp_deps` contains `libdecenza_webclient_resources.a`, because CMake's `AUTOGEN_ORIGIN_DEPENDS` (default ON) forwards every origin-target dependency to autogen. That is why native QML work waits ~48 s for the Wasm bundle.

`decenza-web` and the six test QML modules carry only Qt's default per-file inputs, and both were shown to hold stale units. Since Qt 6.12 the runtime compares each loaded unit's AOT lookup signatures against the live types. On a mismatch it ignores that unit's AOT code, and with `QV4_FAIL_ON_INVALID_AOT` set it `qFatal`s instead (`qv4executablecompilationunit.cpp:153-177`). It covers only loaded units, and only lookups the recorder does not deem unable to desync (`qqmljslookupsignatures.cpp:79-92`). A stale unit is therefore a silent local performance loss, not a wrong result, and release builds start clean.

Upstream, checked 2026-10-25 on qtdeclarative `6.12` and `dev`, Gerrit and the web: nothing addresses identical-output rewrites or build-time QML dependencies. Two upstream commits affect this design:
- `af005e31b2` (`dev`, 6.13) makes generated files depend on `libQmlCompiler` only when the cachegen executable is Qt's own.
- `e120726909` (`6.12`, 6.12.1) bumps the compiled-unit format, so that Qt bump regenerates everything once.

## Goals / Non-Goals

**Goals:**
- Measure before building: the two cheapest, highest-payoff pieces (D4, D1) go first, and a gate decides whether the rest is worth doing.
- Nothing depends on Qt accepting a patch.
- Keep the native all-to-all dependency exactly as it is; this change only makes it cheaper.

**Non-Goals:**
- A precise dependency graph computed by Decenza.
- Upstream Qt patches. Skipping identical writes in `qSaveQmlJSUnitAsCpp`, a qmlcachegen depfile, and the `libQmlCompiler` dependency for custom executables are optional follow-ups. Any of them would let a workaround here be deleted later.
- The Wasm rebuild and embed in the Debug loop (~37 s per edit), the largest remaining lever. It is a product decision about the paired build.
- The Xcode generator path beyond staying correct; Xcode has no `restat`.

## Decisions

### D1. Content-stable native cachegen output through Qt's documented executable override
Set `QT_QMLCACHEGEN_EXECUTABLE` on `Decenza` before `qt_add_qml_module` (`qt_add_qml_module.qdoc:270-278`; a non-`qmlcachegen` value goes through `find_program`, `Qt6QmlMacros.cmake:3677-3690`). It points at a wrapper that:
- runs the real tool with `-o` redirected to a temp **directory** that keeps the original file name, because qmlcachegen picks its mode from the output name (`qmlcache_loader.cpp` selects loader generation);
- replaces the `.cpp` and `.aotstats` only when their bytes differ;
- propagates the exit code and touches nothing on failure.

Qt's existing `restat = 1` then prunes the compile. It is applied under Ninja only: Xcode, Visual Studio and Makefiles have no `restat`, so an identical output left older than its inputs would re-run its cachegen on every build. The wrapper logic is one Python file run by the build's `Python3_EXECUTABLE`. CMake generates a per-platform launcher for `find_program`: `sh` on Unix, `.cmd` on Windows. If Windows will not accept the `.cmd`, the fallback is `RULE_LAUNCH_CUSTOM` on the `Decenza` target with the same Python file. Qt 6.13 skips its new `libQmlCompiler` dependency for a custom executable (`af005e31b2`). That is deliberately not re-added: the dependency exists for Qt's compiler being rebuilt in place inside a Qt build. Qt's commands now depend on the launcher rather than the compiler, so the D7 wiring appends the compiler path to each unit; a Qt reinstalled in place at the same path still regenerates every unit.

*Alternatives:*
- `--only-bytecode`: rejected. It drops AOT and changes which code executes.
- Removing the all-to-all edge: rejected. That is the staleness bug.
- A compiled host tool: rejected. Cross-compiling kits would need a host build step.

### D2. Stop native QML work waiting for the Wasm bundle
`Decenza` reaches the bundle through `target_link_libraries(Decenza PRIVATE decenza_webclient_resources)` and `add_dependencies(Decenza decenza-local-webclient)` (`cmake/webclient_bundle.cmake:85,130`). The Ninja generator puts every target-level dependency of `Decenza` as an order-only input on each of its custom commands. That includes `moc --collect-json`, which feeds `Decenza.qmltypes` and therefore every cachegen step.

So on non-Xcode generators, drop both target-level links and attach rcc's pass-2 output object (`.qt/rcc/qrc_packaged_webclient.o`) to `Decenza` by file, as a generated external object. Ninja then orders only the link edge after the bundle. Xcode keeps today's target-level path, because it has no pass 2 there and its resource inputs need `_qt_resource_target_dependency`.

*Rejected:* `AUTOGEN_ORIGIN_DEPENDS OFF`. It changes only autogen's own ordering. The custom-command order-only inputs stay, as `ninja -t query meta_types/qt6decenza_metatypes.json` showed on 2026-10-25.

### D3. Detect staleness the wiring misses
- **Test AOT validation check.** `QV4_FAIL_ON_INVALID_AOT=1` is set on every test by `decenza_fail_on_invalid_aot()`, called at the end of each test directory (configure fails if a test directory skips it), so a test that loads a stale unit fails instead of passing interpreted. This would have caught `tst_sharedformcontrols`.
- **Consistency check.** `qml_cache_consistency_check` reads each build directory's `build.ninja` (native, Wasm child), regenerates every qmlcachegen output into a scratch directory in parallel, and fails naming each differing unit, with per-module counts. It fails on a regeneration error. It bypasses the D1 wrapper by using the real tool. It takes ~7 s for Wasm and ~13 s for native, so it runs on demand and after Qt upgrades, not in the default build. A session prototype exists and found both known stale cases.
- **Native wiring check.** `qml_dep_wiring_check` gains a second assertion:
  1. After touching `Theme.qml`, every cachegen edge is dirty (as today).
  2. After building only the cachegen outputs, at most one QML object is dirty.
  3. The file is restored on every exit path.

### D4. Package identity computed at build time on the local paired path
Identity hashing moves into one CMake script used two ways: `include()`d at configure time on the PREBUILT/release path (unchanged), and run as a build-time custom command on the local paired path. In the build-time form, `DEPENDS` lists the inputs and the script writes the header and an id file only on change (`restat`). The per-file `CMAKE_CONFIGURE_DEPENDS` on contents goes away; `CONFIGURE_DEPENDS` globs still catch additions and removals.

Build-time consumers of `${DECENZA_PACKAGE_ID}` (`cmake/webclient_bundle.cmake:77`, `CMakeLists.txt:2634`) read the id file. The existing packaging verification still refuses a native/Wasm mismatch.

### D5. Write QML so Qt 6.12's AOT validation accepts it
The AOT validation check exposed two shapes where qmlcachegen and the run-time property cache describe the same member differently. When that happens, the run time discards the calling unit's AOT code without saying so (`qv4executablecompilationunit.cpp:153-177`):
- **Typed parameters without a return annotation.** qmlcachegen records `void`; the run time records `QVariant`. Only an explicit `: void` is kept as `void` at run time (qtdeclarative `44fa9625e7`).
- **A signal declared above a property.** qmlcachegen numbers property-change signals in declaration order; the run time puts them all first.

Decenza's fix is to annotate every such function and to declare properties before signals in the types whose signals AOT code uses. Neither changes behaviour. Aliases declared above properties showed no mismatch, so they were left alone. Both shapes are Qt bugs worth reporting under QTBUG-128323's neighbourhood; nothing upstream addresses them yet.

### D6. Embed the Wasm bundle uncompressed in local builds
rcc compresses the ~131 MB bundle at zlib -9, single-threaded, in both passes (7.5 s each), and the local paired build re-embeds on every source edit. Local builds pass `--no-compress` (0.6 s a pass); PREBUILT/release keeps zlib -9, where binary size matters. Rejected: narrowing the package identity so native-only edits skip the Wasm relink, because that changes the matched-pair contract owned by the in-flight Wasm change.

### D7. Wire the Wasm and test modules, and enforce the declaration rules per PR
`decenza_wire_qml_cache_deps(<target>)` (`cmake/QmlCacheGeneration.cmake`) replaces the native-only block and is called for `Decenza`, `decenza-web` and the six test modules, each after its `qt_add_qml_module`. The Wasm and test modules were first left to detection. The Wasm module was wired after the `AppShell` reorder left 49 Wasm units stale. The test modules followed once the fatal AOT validation check was on for every test: an unwired test unit made a test abort until the unit was regenerated by hand. It takes each unit's output path from Qt's `_qt_internal_qml_get_cachegen_compiled_file`, the helper Qt extracted for exactly this (`6207c5ddbc`). The hand-copied formula only ever matched by luck: it ignored the helper's shorter-of-source-or-binary rule. The Wasm and test modules also get the D1 wrapper, so wiring them costs a cachegen pass rather than a recompile.

`scripts/check_qml_aot_declarations.py` runs in `text-invariants.yml` with a self-test, like its siblings. It lexes strings and comments out before matching. Rather than carry an exception list, all 60 files with a root signal above a property were reordered: signal declarations, and the comment lines directly above them, moved below the last root property, with token-for-token equality asserted per file.

## Risks / Trade-offs

- **[Risk]** The wrapper hides a qmlcachegen failure or leaves a partial output. → Mitigation: exit code propagated, outputs untouched on failure, and the consistency check uses the real tool.
- **[Risk]** Removing the target-level links drops something `Decenza` needed besides the object: the static resource initializer, or a step that reads the staged bundle. → Mitigation: the embedded-bundle check that already runs at packaging, two from-scratch builds, a query that no `Decenza` custom command still orders on the bundle targets, and Android/Windows CI builds on the maintainer's go-ahead.
- **[Risk]** A build-time identity changes value or diverges between native and Wasm. → Mitigation: equality check on an unchanged tree before and after; packaging verification fails on mismatch.
- **[Trade-off]** Projected, unmeasured wall times for one QML edit: D2 alone ~74 s; D1 adds a ~600 CPU-s saving and ~14 s once D2 has removed the serial chain; D4 brings it to ~43 s. The spike measures the first two before anything else is built.

## Migration Plan

1. **Spike:** D2, then D1, each measured with three probes (a comment on a leaf, a `Theme` signature change, and the revert) through Qt Creator's build MCP, with build IDs recorded.
2. **Gate:** continue only if the spike saves at least 25 s per leaf edit with the full suite green and generated code byte-identical. Otherwise record the numbers in `BUILD_PERFORMANCE.md` and revert what didn't pay for itself.
3. **Then** D3 and D4.

Each decision touches separate CMake blocks and reverts alone.
