# Tasks

Builds and test runs go through the Qt Creator MCP (ask before each call). The three probes used throughout are:
- **leaf comment**: append `// probe` to `qml/components/layout/items/ClockItem.qml`;
- **Theme signature**: drop `: string` from `Theme.tempUnitSuffix()`;
- **revert**: restore either one.

Record each build ID, wall time, QML objects compiled and CMake re-runs in `validation.md` in this change.

## 1. Baseline

- [x] 1.1 Run the three probes on the unchanged tree and record the per-phase numbers. Save full-regenerate hashes of every native generated `.cpp` as the byte-identity reference.

## 2. Spike: native QML work no longer waits for the Wasm bundle (D2)

- [x] 2.1 On non-Xcode generators, replace `Decenza`'s target-level links to `decenza_webclient_resources` and `decenza-local-webclient` with a file-level attachment of rcc's pass-2 bundle object. Verify `ninja -t query meta_types/qt6decenza_metatypes.json` no longer lists any webclient target, and that the `Decenza` link edge lists the pass-2 object.
- [x] 2.2 Verify two from-scratch builds in a fresh build directory succeed and the full suite passes.
- [x] 2.3 Run the probes. Verify native cachegen starts before the Wasm child finishes (`.ninja_log` start times), and record the wall times.

## 3. Spike: content-stable native cachegen output (D1)

- [x] 3.1 Add the wrapper (one Python file plus generated per-platform launcher) and set `QT_QMLCACHEGEN_EXECUTABLE` on `Decenza` before `qt_add_qml_module`. Keep the output file name, copy `.cpp` and `.aotstats` only on byte change. (The 6.13 `libQmlCompiler` dependency was dropped with the maintainer's agreement: per `af005e31b2` it matters only when Qt's compiler is rebuilt in place, and with an installed Qt the tool path changes with every version.) Verify the leaf-comment probe recompiles at most one native QML object, and the Theme-signature probe still recompiles every changed caller (9 at baseline).
- [x] 3.2 Verify generated output is byte-identical to the 1.1 hashes, and that a QML syntax error fails the build with qmlcachegen's message and leaves previous outputs untouched.
- [x] 3.3 Run the probes and record the wall times.

## 4. Decision gate

- [x] 4.1 Compare 2.3 and 3.3 with 1.1. Continue to groups 5-6 only if the spike saves at least 25 s per leaf edit, the full suite is green, and generated code is byte-identical. Otherwise record the measured numbers in `docs/CLAUDE_MD/BUILD_PERFORMANCE.md`, revert whichever spike piece did not pay for itself, and stop this change there. Record the decision in `validation.md`.
- [ ] 4.2 Ask the maintainer for Windows and Android CI test builds (`platforms=windows,android`), after groups 5-6 so one run covers every CMake change in this change. Verify the generated launcher works on Windows; if it does not, switch to the `RULE_LAUNCH_CUSTOM` fallback and repeat 3.1-3.2.
- [x] 4.3 Update `BUILD_PERFORMANCE.md` (the restat, ordering and levers sections) and the `CMakeLists.txt` cross-file comment for what landed, with the measured numbers.

## 5. QML edits no longer reconfigure (D4)

- [x] 5.1 Move identity hashing into one CMake script used at configure time on the PREBUILT path and as a build-time `restat` step on the local paired path. Remove the per-file `CMAKE_CONFIGURE_DEPENDS` on contents and keep the `CONFIGURE_DEPENDS` globs. Verify the identity is byte-identical to the pre-change value on an unchanged tree.
- [x] 5.2 Switch the build-time consumers (`cmake/webclient_bundle.cmake:77`, `CMakeLists.txt:2634`) to read the id file. Verify packaging verification passes, and fails on a deliberately mismatched id.
- [x] 5.3 Verify a QML content edit re-runs CMake in neither build, that the native and Wasm identities match and changed, and that adding a QML file still reconfigures. Also verify the PREBUILT path by configuring a fresh directory with `DECENZA_WEB_BUNDLE_DIR` set to the local bundle.
- [x] 5.4 Document build-time identity in `BUILD_PERFORMANCE.md` and beside the identity script, with the measured saving.

## 6. Staleness detection (D3)

- [x] 6.1 Add `scripts/qml_cache_consistency_check.py` and a `qml_cache_consistency_check` target covering the native and Wasm build directories, using the real qmlcachegen. Verify it names a deliberately staled unit in each of the native, Wasm and test modules, prints per-module counts, and passes once they are regenerated.
- [x] 6.2 Set `QV4_FAIL_ON_INVALID_AOT=1` once in the shared test registration. Verify the full suite passes, and that a deliberately staled unit loaded by a test makes that test fail with Qt's AOT validation message.
- [x] 6.3 Extend `qml_dep_wiring_check` with the at-most-one-dirty-object assertion after cachegen, restoring the edited file on every exit path. Verify it passes, then fails with the wrapper disabled, then passes again.
- [x] 6.4 In `BUILD_PERFORMANCE.md`'s cross-file section, document the AOT validation check and the consistency check. Correct its claim that a mixed cache reproduces `Unable to assign [undefined]`: true on 6.11; on 6.12 the unit falls back to the interpreter.

## 6a. Silent AOT rejections the AOT validation check exposed (D5)

- [x] 6a.1 Give every QML function with typed parameters an explicit return annotation (29 functions, all `: void` after reading each body). Make `tst_customwidgethtml` strip QML type annotations through one helper. Verify a re-scan finds none, and `tst_localfiletransfer` and `tst_sharedformcontrols` pass under the AOT validation check.
- [x] 6a.2 Declare properties before signals in every type whose signals AOT code looks up (`AppShell`, `AccessibleMouseArea`, `AccessibleTapHandler`). Verify, with lldb attached to the real app, that startup goes from 9 failing units to 0.
- [x] 6a.3 Run the app (launched with `open`) under `QV4_FAIL_ON_INVALID_AOT=1` through startup, every Settings tab, the home-screen panels, History and Shot Review. Verify no validation failure.
- [x] 6a.4 Add a build-free check to `text-invariants.yml` that fails on a QML function with typed parameters and no return annotation, and on a type that declares a signal above a property. Verify it fails on a reverted fix and passes on the tree.

- [x] 6a.5 Reorder the 60 files the new check flags (signal declarations, with comments directly above them, moved below the last root property; token-for-token equality asserted per file). Verify the check passes on all 367 files, the QML gate is clean, the suite passes, and the app passes under the fatal AOT validation check.

## 6c. Wasm cross-file wiring (D7)

- [x] 6c.1 Replace the native wiring block with `decenza_wire_qml_cache_deps()` using Qt's output-path helper, and call it, plus the D1 wrapper, for `decenza-web`. Verify the Theme-signature probe recompiles the same 9 units in Wasm as in native, the consistency check passes right after the change, and `qml_dep_wiring_check` passes for both modules.

## 6b. Uncompressed local bundle embed (D6)

- [x] 6b.1 Pass `--no-compress` for the local paired build; keep zlib -9 for PREBUILT/release. Verify a leaf QML edit drops from ~32.5 s to ~21.5 s and a native C++ edit from 29.1 s to ~15 s, and that nothing reads raw resource data.

## 7. Integration

- [x] 7.1 Re-run the probes on the final tree and record the before/after table in `validation.md` and the `BUILD_PERFORMANCE.md` summary. Run the full suite and the consistency check. Verify all pass and generated code still matches the 1.1 hashes.

## Workflow follow-up

- Archive with `openspec archive fix-qml-incremental-builds --yes` as the PR's final commit, after review.
- Upstream issues to watch:
  - QTBUG-128323 (QML iteration-speed epic): add the measurements here.
  - QTBUG-147227 (depfile for qmlimportscanner, the precedent for a qmlcachegen depfile).
  - QTBUG-147816 (runtime dependency checks, 6.13).
  - QTBUG-137440 (qmlcachegen non-determinism, which the D1 wrapper relies on not having).
  No issue exists yet for identical-output rewrites or for build-time cross-file dependencies.
- Optional upstream Qt contributions, outside this change: skip identical writes in `qSaveQmlJSUnitAsCpp` (would retire the D1 wrapper); a qmlcachegen depfile; the `libQmlCompiler` dependency for custom executables.
