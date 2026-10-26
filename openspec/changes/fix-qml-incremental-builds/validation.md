# Validation

macOS Debug, paired native/Wasm build directory `build/Qt_6_12_0_for_macOS_Debug`. Builds are run from the command line with the build's own Ninja (maintainer-approved for this change, because it is easier to measure). Phase windows are seconds from the start of the native Ninja run, taken from `.ninja_log`. The CMake re-run happens before that run starts, so it shows up only in the wall time.

## 1.1 Baseline (`8d4cd1028`, 2026-10-25)

Each probe was followed by a settle build; every settle build was a no-op (0.2-0.3 s).

| Probe | Wall | CMake re-runs | cachegen | native QML .o | Wasm child | rcc bundle | native link ends |
|---|---|---|---|---|---|---|---|
| leaf comment | 73.7 s | 2 | 358, 34.8-39.6 | 360, 39.6-64.3 | 0.2-18.9 | 18.9-34.1 | 65.9 |
| leaf revert | 74.6 s | 2 | 358, 33.9-38.9 | 360, 38.9-64.8 | 0.2-18.1 | 18.1-33.3 | 66.8 |
| Theme signature | 72.7 s | 2 | 358, 34.5-39.2 | 360, 39.2-63.4 | 0.2-18.8 | 18.8-33.9 | 65.0 |
| Theme revert | 77.1 s | 2 | 358, 34.0-39.4 | 360, 39.4-67.3 | 0.2-18.0 | 18.0-33.2 | 69.5 |

- **Critical path:** CMake (~8 s) → Wasm child (~18.5 s) → rcc of the bundle (~15 s) → cachegen (~5 s) → QML objects (~25 s) → link. The qmllint gate (~17 s) runs in parallel and ends ~8 s before the link.
- **Content of the 360 recompiled objects:** an exploration run on the same commit found 0 of 377 generated files changed for the leaf comment, and 9 (Theme plus 8 callers) for the Theme signature.
- **Byte-identity reference:** `shasum` of all 377 generated native `.cpp` files at `8d4cd1028`, kept in the session scratchpad as `hash_reference.txt`.
- **Unexplained one-off:** the first build of the day, with no source edit since the last build, ran the package-identity object, Wasm bundle verification, rcc and all 360 QML objects (49.6 s). It did not recur in any of the eight probe and settle builds above. It is noted here in case D4 (build-time identity) explains it.

## 2.1 / 2.3 D2: file-level bundle attachment

`cmake/webclient_bundle.cmake` keeps `add_dependencies(Decenza decenza-local-webclient)` for Xcode only, and links rcc's pass-2 object by path (`.qt/rcc/qrc_packaged_webclient.o`) instead of the carrier target.

- **Graph:** `ninja -t query meta_types/qt6decenza_metatypes.json` lists no webclient target, and `Decenza_autogen_timestamp_deps` lists none. No `Decenza` autogen, cachegen or object edge orders on `decenza-local-webclient`, `libdecenza_webclient_resources.a` or the pass-2 target. The link edge has the pass-2 object as an input and on `LINK_LIBRARIES`.
- **Binary:** the linked executable still carries `qInitResources_packaged_webclient` and its static initializer `__GLOBAL__sub_I_qrc_packaged_webclient_tmp.cpp`.

| Probe | Wall | cachegen | native QML .o | Wasm child | rcc bundle | native link ends |
|---|---|---|---|---|---|---|
| leaf comment | 74.5 s | 4.0-11.8 | 11.8-45.6 | 0.2-42.5 | 42.5-61.2 | 63.8 |
| leaf revert | 73.9 s | 3.5-12.5 | 12.5-45.9 | 0.2-43.1 | 43.1-61.5 | 64.8 |
| Theme signature | 86.6 s | 4.3-13.3 | 13.3-52.3 | 0.2-50.8 | 50.8-71.9 | 75.6 |
| Theme revert | 99.5 s | 5.7-17.0 | 17.0-62.3 | 0.3-60.2 | 60.2-81.4 | 86.0 |

All settle builds were no-ops.

**Result:** the ordering fix works (cachegen now starts at ~4 s instead of ~35 s), but wall time does not improve. The Wasm child slows from ~18.5 s to 42-60 s, and its link from ~10 s to ~20-28 s, because it now competes with the 360 native QML compiles for CPU. The machine was CPU-bound during the old serial wait, not idle. D2 is expected to pay off only once D1 removes the redundant compiles, and that combination is what the gate judges.

## 2.2 From-scratch builds

Two fresh build directories, configured like Qt Creator's kit (Debug, tests, PCH, local paired Wasm with a fresh child directory):
- `fresh1` (D2 only): built in 314 s with 0 FAILED steps; QML gate clean 360/360; bundle initializer linked.
- `fresh2` (D1 + D2): built in 275 s with 0 FAILED steps; QML gate clean 360/360. The wrapper ran on all cachegen edges under Xcode's Python 3.9, with no temp directories left behind.
- Full suite on `fresh2`: `ctest -j 8` passed **207/207** (50.3 s).
- **Byte identity:** all 361 generated native units in `fresh2` match the 1.1 baseline hashes. The 16 extra files in the main build directory are orphans of deleted QML files and are not in the build graph.

## 3.1-3.3 D1: content-stable cachegen (with D2)

- **Wrapper unit checks:** an identical output keeps its nanosecond mtime; a changed output is replaced with correct bytes; a failing run returns rc=1, leaves the old output untouched and removes its temp directory.
- **First build after enabling it:** all 358 cachegen steps re-ran (the command changed), every output was identical, and **0** native QML objects recompiled.
- **Syntax error:** the build fails with qmlcachegen's `ClockItem.qml:111:Syntax error`, the previous unit keeps its bytes and mtime, and after the revert the build is clean and recompiles 0 objects.

| Probe | Wall | native QML .o | Wasm child | rcc bundle | native link ends |
|---|---|---|---|---|---|
| leaf comment | 65.5 s* | 0 | 0.3-35.3 | 35.3-50.6 | 52.6 |
| leaf revert | 59.1 s* | 0 | 0.2-31.8 | 31.8-48.9 | 51.0 |
| Theme signature | 49.0 s | **9** | 0.2-23.9 | 23.9-39.1 | 40.9 |
| Theme revert | 49.9 s | **9** | 0.2-24.7 | 24.7-39.9 | 41.8 |

\* Taken under outside load (load average 24-30, including a Codex computer-use session). The A/B below supersedes them.

The 9 recompiled units are exactly Theme, GraphRightAxisLabels, PortalGraphOverlay, ShotGraph, SteamGraph, DFlowEditorPage, EspressoPage, ProfileEditorPage and SimpleProfileEditorPage, the same set as the exploration run, so native cross-file invalidation is intact.

### Back-to-back A/B (leaf comment and revert, same conditions)

| Tree | Runs | Mean |
|---|---|---|
| baseline (changes set aside) | 72.9, 77.2, 75.2, 75.6 s | **75.2 s** |
| D1 + D2 | 48.8, 46.0, 46.1, 42.8 s | **45.9 s** |

That is **-29.3 s (-39%)** per QML edit, and ~600 CPU-seconds of redundant QML compiles removed per edit. The critical path is now the Wasm child (~23 s, including its ~7 s CMake re-run) → rcc of the bundle (~15 s) → link (~2 s), plus the native CMake re-run (~8 s). Both CMake re-runs are what D4 targets.

## 4.1 Gate

**Passed (maintainer decision, 2026-10-25).** The leaf-edit saving is 29.3 s against the 25 s bar; the suite is green (207/207) and generated code is byte-identical. Both spike pieces are kept: D2 alone saved nothing, but it is what lets D1's saving reach the wall clock. The 6.13 `libQmlCompiler` dependency was dropped from task 3.1, for the reason recorded in design D1.

## 5.1-5.3 D4: build-time package identity

- **Same value:** the old configure-time algorithm (HEAD's `cmake/wasm_package_identity.cmake`, run in script mode with only its `CMAKE_CONFIGURE_DEPENDS` line and `CONFIGURE_DEPENDS` flag removed) and the new build step both give `d5a4bf9e…74dd` on the same tree. The native and Wasm headers agree.
- **Cost:** hashing the 5,586 inputs takes 0.68 s.
- **Configure dependencies:** the native re-run edge now watches 1,201 files, none of them QML; the Wasm child's went from 349 QML files to 0.
- **Packaging:** `package_webclient.py install --expected-package-header` accepts the matching header and refuses a mismatched one ("Package ID does not match the native application", rc=1, no output directory). `--expected-package-id` still works.
- **PREBUILT:** a fresh directory configured with `DECENZA_WEB_BUNDLE_MODE=PREBUILT` against the local deployment verifies the bundle at configure time and writes the same header.
- **File set changes:** adding and then removing a QML file each re-run CMake in both builds (2 re-runs each).

| Probe | Wall | CMake re-runs | native QML .o | Wasm child | rcc bundle | link ends |
|---|---|---|---|---|---|---|
| leaf comment | 32.1 s | 0 | 0 | 0.9-15.8 | 15.8-30.3 | 32.1 |
| leaf revert | 33.0 s | 0 | 0 | | | |
| leaf comment | 32.5 s | 0 | 0 | 0.9-16.1 | 16.1-31.2 | 33.0 |
| leaf revert | 32.5 s | 0 | 0 | | | |
| Theme signature | 34.4 s | 0 | 9 | | | |
| Theme revert | 38.2 s | 0 | 9 | | | |

Each leaf edit changed both identities, which still matched each other. Every settle build was a no-op.

## 6.1 Consistency check

`scripts/qml_cache_consistency_check.py` (target `qml_cache_consistency_check`) regenerates every unit with the real qmlcachegen, bypassing the D1 wrapper, and reports per module. Module ownership comes from each unit's object path. A run takes ~9 s for 804 units across 8 modules (native, Wasm, six test modules).

- **Real staleness found:** `tst_sharedformcontrols`' `SharedDecenzaDialog`, stale since 2026-10-24 (`AppShell` method indices 52/51 against a fresh 51/50).
- **Deliberate staleness found:** one appended native unit and one appended Wasm unit were both named, and the check exited 1.
- **Fails closed:** a deleted Wasm unit is reported as "failed to regenerate".
- **Repair:** the message prints `cmake --build <dir>` for each affected build directory. Touching a `.qml` does not repair a Wasm unit, because the native build re-enters the Wasm child only when the package id changes.
- After regeneration, the check passes for all 8 modules.

## 6.2 AOT validation check: blocked on false positives (pending maintainer decision)

With `QV4_FAIL_ON_INVALID_AOT=1` on all 207 tests:
- **Positive control:** `tst_sharedformcontrols` passed without the AOT validation check while its stale unit ran interpreted, and aborted with it: "AOT lookup signatures validation failed for file qrc:/qt/qml/Decenza/SharedDecenzaDialog.qml".
- **The problem:** after that unit was regenerated, and with the consistency check passing everywhere, two tests still abort.
  - `tst_sharedformcontrols` aborts on `SharedAccessibleButton.qml`; the first failure had been masking the rest.
  - `tst_localfiletransfer` aborts on its own `tst_localfiletransfer.qml`. That unit's recorded lookups (`LocalFileTransfer.busy` property 13, `abort` method 26) match the source.
- **Cause not established:** removing the document's explicit `import Decenza 1.0` did not change it (edit reverted). Qt prints no detail about which lookup failed.

These are pre-existing run-time AOT rejections of current units, not staleness, so the AOT validation check as specified fails the suite on something this change does not cause.

## Beyond QML (maintainer asked, 2026-10-25)

- **Native-only C++ edit** (comment in `src/main.cpp`, not compiled into Wasm): **29.1 s**, 0 CMake re-runs. 27 s of it is Wasm pairing:
  - the package id changes, so Wasm recompiles its id object and relinks (child 0.8-11.9 s);
  - rcc re-embeds the bundle (11.9-27.1 s);
  - then the native link (2 s).
- **rcc embed:** pass 1 takes 7.45 s and pass 2 7.48 s, each compressing ~131 MB single-threaded with `--compress-algo zlib --compress 9`. With `--no-compress` each takes ~0.57 s; the object grows from 59.7 MB to 131.5 MB.
- **Clean build** (`fresh2`): Ninja wall 260 s. The Wasm child sub-build is one 204 s edge on the critical path. CPU split: tests (compile, link, moc) ~46%, app compile 26.5%, QML objects 13.8%. The tests already share one PCH via `REUSE_FROM`.

## 6.2 investigation: why current units fail AOT validation

Traced under lldb with Qt's dSYMs (breakpoints in `Validator::validateMethodLookup` and in each unit's generated `validateLookupSignatures`):

- `tst_localfiletransfer.qml` fails on `Decenza.LocalFileTransfer.abort`. The run-time metaobject has `abort(QString) -> QVariant` at relative index 26; the compiled signature expects `void` at index 26. The name, index and parameters match; only the return type differs.
- `SharedAccessibleButton.qml` fails on `Decenza.SharedButtonInteraction.clearIf`, and after that is fixed, on `remember`. Same shape.
- **Cause:** a QML function with typed parameters and no return annotation. qmlcachegen records its return as `void`. The run-time property cache maps an *unannotated* return to `QVariant`, and only an explicit `: void` to `void` (qtdeclarative `44fa9625e7`, in 6.12.0). The validator then rejects the call site and drops the **whole calling unit's** AOT code (`qv4executablecompilationunit.cpp:153-177`), silently.
- **Fix confirmed on the first case:** adding an explicit `: void` to `abort` and `clearIf` made `tst_localfiletransfer` pass under `QV4_FAIL_ON_INVALID_AOT=1`. Only the two defining units recompiled; their callers' compiled signatures already said `void`.
- **Scope:** 28 functions in 11 QML files have typed parameters and no return annotation. Nothing in the 6.12 or dev branch logs addresses this mismatch.

## 6.2 Fixes for silent AOT rejection (maintainer-approved scope)

**Class 1, typed parameters without a return annotation.** All 29 such functions in `qml/` got an explicit `: void`. Every body was checked; none returns a value (only bare `return`). A re-scan finds none left in `qml/`, `tests/qml` or `tools/webclient`. `tst_customwidgethtml` evaluates shipping QML functions as plain JavaScript, so its two hand-written annotation strippers became one helper (`stripQmlTypeAnnotations`) that also removes return annotations.

**Class 2, signals declared above properties.** Found in the running app, not in tests. Launching through `open` (so Decenza, not the Claude app, owns its Bluetooth permission) with `QV4_FAIL_ON_INVALID_AOT=1` aborted on `PageTitleItem.qml`. lldb, attached with `--waitfor` and with breakpoints pre-resolved on every generated unit's failure line, recorded 9 failing units at startup. All 9 failed on an `AppShell` signal (`settingsRequested`, `brewSettingsRequested`, `steamRequested`, …).
- `AppShell` declares 41 signals and then 8 properties. The compiled index of `settingsRequested` is 19, its ordinal among the signals. The run-time metaobject puts every property-change signal first, so the run-time index is 27.
- Moving `AppShell`'s state block above its signals took startup failures from 9 to 0. 50 native units recompiled.
- Generated code shows signal lookups on only 4 types. Besides `AppShell`, two had the bad ordering, `AccessibleMouseArea` and `AccessibleTapHandler` (`accessibleClicked`); both were reordered the same way.
- Aliases declared above normal properties occur in 23 files (16 with looked-up properties) but are evidently numbered consistently, so they were left alone. `SharedSuggestionField` has that shape with 8 own properties looked up, yet passes under the AOT validation check, and no app screen tripped.

**Results:**
- Full suite: **207/207** with the AOT validation check on every test.
- The app with the AOT validation check made fatal survived startup, all 12 Settings tabs, the Recipes/Beans/Equipment/Profiles panels, History and Shot Review.
- The consistency check then reported **49 stale Wasm units**, the counterparts of the 50 native units that recompiled for the `AppShell` reorder. Wasm has no cross-file dependencies, so it did not regenerate them. After deleting them and running `cmake --build wasm`, the check passed.

## Uncompressed local embed

Local (`DECENZA_WEB_BUNDLE_MODE=LOCAL`) builds pass `--no-compress` to rcc for the bundle; PREBUILT/release keeps `--compress-algo zlib --compress 9`. The bundle object is 131.8 MB. Nothing reads raw resource data (no `QResource` use), so compression is transparent to the app.

| Probe | Before (D1+D2+D4) | After |
|---|---|---|
| leaf QML comment (×2, each with revert) | 32.1-33.0 s | **21.0-22.0 s** |
| native C++ comment, `src/main.cpp` (×2, each with revert) | 29.1 s | **14.7-15.4 s** |

## 6.3 Wiring check

Two pre-existing defects meant `qml_dep_wiring_check` could not have passed from the command line:
- It looked for `ninja` on `PATH`; this kit's Ninja is `~/Qt/Tools/Ninja/ninja`. The target now passes `CMAKE_MAKE_PROGRAM`.
- It relied on `ninja -n`, which since the `CONFIGURE_DEPENDS` globs stops at "Re-running CMake", because the glob-verification edge is an always-dirty phony. The check now reads the unit and object outputs from `build.ninja`, builds them for real, and counts what ran.

It also asserts the D1 half: after touching `Theme.qml`, all 360 units regenerate and **no** QML object recompiles.

| State | Result |
|---|---|
| as committed | passes: 360 of 360 units dirtied, 0 objects recompiled |
| `decenza_use_stable_qmlcachegen(Decenza)` commented out | fails: "recompiled 360 QML cache objects" |
| restored | passes |

## 7.1 Integration (final tree, 2026-10-25)

| Probe | Baseline (1.1) | Final | CMake re-runs | native QML .o |
|---|---|---|---|---|
| leaf QML comment (×2) | 73.7 / 75.2 s mean | 21.1 / 21.1 s | 2 → 0 | 360 → 0 |
| leaf revert (×2) | 74.6 s | 21.3 / 22.7 s | 2 → 0 | 360 → 0 |
| Theme signature | 72.7 s | 21.9 s | 2 → 0 | 360 → 9 |
| Theme revert | 77.1 s | 21.9 s | 2 → 0 | 360 → 9 |
| native C++ comment (`src/main.cpp`) | 29.1 s (after D4) | 14.9 s | 0 | 0 |

- **Suite:** 207/207 with the AOT validation check on every test.
- **Consistency check:** passes for all 8 modules.
- **Wiring check:** passes, and fails with the wrapper disabled.
- **`text-invariants.yml` checks:** all 15 pass locally.
- **Byte identity:** checked at 3.2/2.2 (all 361 shared units identical to the 1.1 reference, across two build directories) before any QML source changed. Since then 12 QML files changed on purpose (D5), so the 1.1 hashes no longer apply to their units.

## 6c Wasm cross-file wiring (maintainer-requested)

- `decenza_wire_qml_cache_deps()` uses Qt's `_qt_internal_qml_get_cachegen_compiled_file`. Configure wires 360 `Decenza` and 364 `decenza-web` units. The first build after wiring regenerated through the wrapper and recompiled 0 objects in either module.
- **Theme-signature probe:** native and Wasm each recompiled exactly 9 units. The consistency check, run **before** reverting, passed for both modules with no manual repair; before this change the same probe left 8 Wasm callers stale.
- **`qml_dep_wiring_check`:** passes for `Decenza` (360/360) and `decenza-web` (364/364).
- **Consistency check fix:** it now recognises any build directory's `qmlcachegen_stable` launcher. With only the native launcher substituted it had found no Wasm units and failed, and my filter had hidden its message.
- **Cost:** leaf-edit A/B alternating wired and unwired, at load average 23-31 from unrelated work (a VM at ~520% CPU, `rspec`, Node). Off: 35.3, 38.5, 32.3, 24.3 s (mean 32.6). On: 43.9, 41.5, 30.8, 29.2 s (mean 36.4). That is roughly +4 s, consistent with one extra 364-unit cachegen pass. Idle-machine timings were not re-measured, because the load never dropped.

## 6a.4 / 6a.5 Per-PR declaration check

- `scripts/check_qml_aot_declarations.py`: self-test 4 flagged and 7 passed (including comments, strings containing `:` and URLs, and nested objects). It runs in `text-invariants.yml` after the ASI check, and the workflow's path filters gained `tests/qml/**` and `tools/webclient/*.qml`; the workflow parses (Ruby YAML).
- The check found 0 untyped-return functions (all 29 were already fixed) and 60 files with a root signal above a property. All 60 were reordered by script, none refused, with token equality asserted. Spot-checked diffs read sensibly; where a file's last properties are trailing `Connections` objects (`LocalFileTransfer`), the signal moves to the bottom.
- **After the reorder:** build passed with 60 native and 60 Wasm units recompiled consistently; QML gate clean (360/360); suite 207/207 with the AOT validation check; consistency check passes; wiring check passes for both modules; all 17 per-PR checks pass locally. The app (one instance, `open`, fatal AOT validation) passed startup, the four home panels, History, Shot Review and all 12 Settings tabs. The update prompt was answered "Later". The app quit cleanly.
