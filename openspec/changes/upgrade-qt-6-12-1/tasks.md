# Tasks

Group 1 must finish before groups 2 and 3 delete anything. Tasks marked **(ask Jeff)** touch a
shared resource or spend CI time and need a go-ahead first.

## 1. Write the retirement record before deleting

- [x] 1.1 Add a "Retired with Qt 6.12.1" section at the top of
      `tools/qt-gc-diagnostics/README.md`: why (no #2179 report since the allocator fix shipped;
      QTBUG-149512 is stock in 6.12.1, citing `qv4mm.cpp:265-316`) and what remains (three
      patches and `tests/` on 6.12.0; `ownership-repro/` on 6.12.1). Verify by reading it cold:
      it must make sense without `android/qt-overrides/README.md` existing.
- [x] 1.2 Move the build recipe and the "additional evidence" notes from
      `android/qt-overrides/README.md` into that section, cut to what a rebuild needs: kit, NDK
      27.2.12479018, API 28, `-Oz -DNDEBUG`, patch order, the ABI comparison, never defining
      `DECENZA_QML_GC_TEST_HOOKS`. Record port status: patch 1 applies to 6.12.1, patch 2
      applies on top of it, patch 3 superseded. Verify each `patch --dry-run` claim against
      `~/Qt/6.12.1/Src/qtdeclarative` before writing it.
- [x] 1.3 Record recovery of the binary: last commit holding it (`4229107cf`), its SHA-256 and
      build ID copied from `manifest.json`, and that the override README, manifest and
      `verify_artifact.py` are at that commit. Verify with
      `git show 4229107cf:android/qt-overrides/manifest.json` that the values match.
- [x] 1.4 Record recovery of the app-side code: the commit this branch was cut from, and the
      list of what group 3 removes (cross-validation switch, deferred-abort coordinator,
      `gcdeferredabort.h`, the GC half of `btlogfilter`, the `DE1Device` operation block, the
      `MARK_PATH` clip). State that the patched library is what emits `DEFERRED_ABORT`, so the
      two return together or not at all.
- [x] 1.5 Record the re-entry condition: a #2179 recurrence, and that the `build-config`
      "decision is revisited" scenario requires stating the observed rate and restoring the
      version-lock guard first. Fix the README's existing references to
      `android/qt-overrides/manifest.json` so none points at a path that will not exist.
- [x] 1.6 Add a pointer entry to `docs/qt-patches/README.md` for the GC diagnostics. Verify the
      relative link resolves.

## 2. Remove the Android override

- [x] 2.1 Delete `android/qt-overrides/` and `tools/qt-gc-diagnostics/verify_artifact.py`.
      Verify `git grep -n "qt-overrides\|verify_artifact"` returns only the retirement record
      and `docs/qt-patches/README.md`.
- [x] 2.2 In `.github/workflows/release.yml` remove the "Install instrumented QtQml" step and
      the "Verify instrumented QtQml in the APK" step. Verify no later step reads
      `$RUNNER_TEMP/qt-android-gc` or depends on the staged `QT_ROOT_DIR`, and that the workflow
      still parses (`python3 -c "import yaml,sys; yaml.safe_load(open(sys.argv[1]))"`).
- [x] 2.3 Rewrite the `CLAUDE.md` "Decenza normally ships stock Qt" bullet: no exception, GC
      diagnostics kept as source in `tools/qt-gc-diagnostics/`. One sentence replacing one.

## 3. Remove the app-side GC diagnostics

- [x] 3.1 `src/main.cpp`: remove the cross-validation block at `:562-566`, the
      `gcdeferredabort.h` include, the Android `GcDeferredAbort::Coordinator` block with its two
      connections and `setGcMismatchHandler` call (`:1306-1333`), and the handler reset at
      `:5276`. Check the two `QObject::connect` handles are not disconnected or referenced
      further down before deleting them.
- [x] 3.2 Delete `src/core/gcdeferredabort.h` and drop it from any source list. Verify
      `git grep -n gcdeferredabort` is empty.
- [x] 3.3 `src/core/btlogfilter.{h,cpp}`: remove `enableGcCrossValidation`, `setGcPageStack`,
      `setGcMismatchHandler`, `gcCrossValidationLine` and its helpers, the batch state and
      `recordGcMiss`, the `DEFERRED_ABORT` branch, the Android stack capture, and the includes
      only they used. Keep the `CAP_NET_ADMIN` filter (the "window doesn't exist" one goes in 6b.2). Rewrite the
      header comment to list the two filters that remain.
- [x] 3.4 `src/network/crashreporter.cpp`: remove the `BtLogFilter::setGcPageStack` call from
      `setPageStack` and the include if nothing else uses it.
- [x] 3.5 `src/core/crashhandler.cpp`: remove the 900-character `MARK_PATH` case in `clip`,
      leaving `kMaxLineChars`. Leave the fault-address mapping line at `:1965-1970` alone.
- [x] 3.6 `src/ble/de1device.{h,cpp}`: remove `setGcAbortPending`, `m_gcAbortPending` and the
      block at the top of `requestState`. Then for each of `hasCurrentStateInfo`,
      `requestCurrentStateInfo`, `m_stateInfoReceived` and the `stateInfoReceived` signal, run
      `git grep` for readers outside tests; delete each one that has none, and keep any that
      does. Record which went in the PR description. (All four went: each was added by #2135 for
      the abort path and had no other reader.)
- [x] 3.7 Tests: remove the GC slots from `tests/tst_webdebuglogger.cpp` (the `MARK_PATH` clip
      case, the cross-validation rewrite and batch cases, the `DEFERRED_ABORT` capture, the
      `GcDeferredAbort` cases) and its `gcdeferredabort.h` include; remove
      `gcAbortPendingBlocksNewOperationsButAllowsStop` and the tests of any member 3.6 deleted
      from `tests/tst_de1device_headless.cpp`. Verify
      `git grep -nE "QmlGc|GcDeferredAbort|GcAbortPending|Cross Validation" -- src tests` is
      empty.
- [x] 3.8 `docs/CLAUDE_MD/LOGGING.md`: remove the QML GC cross-validation section (around
      `:373-380`) and any `QmlGc` entry in the subsystem list. Run
      `python3 scripts/check_log_markers.py` and verify it passes.

## 4. Give the Qt version one source, at 6.12.1

Already on the branch (2026-11-23, needed to build locally on the 6.12.1 kit): the Wasm pins in
`scripts/build_webclient.py`, `cmake/webclient_bundle.cmake`, `src/webclient/CMakeLists.txt`
and `tools/qtmodules-wasm/` were moved to 6.12.1 as literals, and `build_webclient.py` gained two
fixes. 4.4-4.6 replace those literals with the single source.

- [x] 4.0a `scripts/build_webclient.py` rebuilds a Wasm Multimedia or Remote Objects install
      left from another Qt version instead of reusing it. Verified: the first 6.12.1 build
      deleted both 6.12.0 prefixes and reinstalled them as 6.12.1.
- [x] 4.0b `scripts/build_webclient.py` re-runs the Wasm configure when `build.ninja` is
      missing, the state a part-failed configure leaves. Covered by a new case in
      `tools/webclient/test_package.py`, which fails without the fix; all 11 tests pass.
- [x] 4.1 **Before editing workflows**, confirm 6.12.1 is installable for every CI target with
      `aqt list-qt`: Windows, macOS, Linux x64, Linux arm64, Android arm64, Wasm singlethread,
      and iOS through the pinned `aqtinstall` fork (`ios_device`). Record the result in this
      file. If iOS is unavailable, stop and report.
      Result 2026-11-23 (aqt at the iOS fork's pinned commit, `list-qt <host> <target> --arch 6.12.1`):
      linux `linux_gcc_64`, linux_arm64 `linux_gcc_arm64`, mac `clang_64`, windows
      `win64_msvc2022_64`, all_os android `android_arm64_v8a`, all_os wasm `wasm_singlethread`, mac
      ios `ios_device`. All present.
- [x] 4.2 Add `qt-version.txt` (`6.12.1`), `cmake/DecenzaQtVersion.cmake` (reads it into
      `DECENZA_QT_VERSION`, fails on a missing or malformed file) and `scripts/qt_version.py`
      (the one Python reader, same validation). Verify by pointing each at a malformed copy and
      watching it refuse.
- [x] 4.3 CMake: the root `CMakeLists.txt` (minimum), `src/webclient/CMakeLists.txt`,
      `tools/qtmodules-wasm/CMakeLists.txt`, `tools/webclient/media-repro/CMakeLists.txt` and
      `tools/qt-gc-diagnostics/ownership-repro/CMakeLists.txt` (all `EXACT`) use
      `${DECENZA_QT_VERSION}` in `find_package` and in their version checks and messages.
      Leave `tools/qt-gc-diagnostics/tests/CMakeLists.txt` on its literal `6.12.0`.
- [x] 4.4 Python: `scripts/build_webclient.py` replaces its `QT_VERSION` constant with the
      helper; `scripts/qmllint_report.py` builds its two default `qmllint` paths from it;
      `tools/qtmodules-wasm/package-ci.py` and `verify-ci-package.py` use it. Verify
      `python3 tools/webclient/test_package.py` passes and the package and verify scripts agree
      on the artifact filename.
- [x] 4.5 `.github/workflows/release.yml`: `plan` gains a sparse checkout of `qt-version.txt`
      and a `qt_version` output, validated against `^\d+\.\d+\.\d+$`. Every
      `install-qt-action` `version:`, the `aqt install-src` line, the `Qt/<version>/…` paths,
      the Wasm artifact names and recipe keys, and the `wasm-qt…`, `qt-wasm-singlethread-…`,
      `gradle-android-qt…` and `sccache-windows-x64-qt…` keys use
      `needs.plan.outputs.qt_version`. Remove the workflow-level `QT_VERSION`. Rewrite the iOS
      package-layout comment from what 4.1 found. Verify the file still parses as YAML and
      `python3 scripts/check_qt_module_lists.py` still passes.
- [x] 4.6 `.github/workflows/nightly-sanitizers.yml`: read the file in a step after checkout and
      use it for `version:`.
- [x] 4.7 Add `scripts/check_qt_version_pins.py`: read the current version through the helper
      and fail, naming file and line, on that version as a literal outside comments in
      `.github/**`, `**/CMakeLists.txt`, `cmake/**`, `scripts/**` and `tools/**`. Run it from
      `text-invariants.yml` and add `qt-version.txt`, `**/CMakeLists.txt`, `cmake/**`,
      `scripts/**` and `tools/**` to both `paths:` lists. Verify it passes on the finished tree,
      then re-add one literal `6.12.1` to a workflow and watch it fail.

## 5. Record what the bump inherits

- [x] 5.1 `docs/qt-patches/`: delete `qtbase-android-deadlock-protector-no-abort.patch` and
      reduce its README entry to a record: fixed in 6.12.1 by the surface-lifecycle rewrite,
      citing that `qandroidplatformopenglwindow.cpp` no longer acquires the protector. Before
      writing, look up Gerrit 775580 with the `qt-gerrit` skill and cite its merge onto the
      6.12 branch; if the record does not show that, cite the released source only.
- [x] 5.2 `docs/CLAUDE_MD/QML_GOTCHAS.md` item 5 and the header comment in
      `scripts/qmllint_report.py`: past tense, with `qqmljslogger.cpp:387,419` in 6.12.1. Do
      not grow either; this is an edit. The gate keeps `--json`.
- [x] 5.3 Confirm `qml/components/OperationCompletionOverlay.qml` keeps its QTBUG-152061
      workaround and comment unchanged.
- [x] 5.4 Re-open every Qt file-and-line citation in `CLAUDE.md` and `docs/CLAUDE_MD/*.md` in
      `~/Qt/6.12.1/Src` and correct those that moved. List the ones that changed in the PR
      description.
      Done 2026-11-23 by mapping each cited 6.12.0 range to the identical text in 6.12.1: 46
      citations checked, 28 point into changed files, 15 shifted and were moved. One changed
      in content: `Qt6QmlMacros.cmake:4483–4503` now also covers Visual Studio and Makefile
      generators (QTBUG-151727), so `BUILD_PERFORMANCE.md` was reworded; Ninja is still exempt.
- [x] 5.5 Confirm `cmake/qmlcachegen_stable.py` and `cmake/QmlCacheGeneration.cmake` still
      match what they cite (`qqmljscompiler.cpp`, `Qt6QmlMacros.cmake`) in 6.12.1; update the
      cited lines if they moved.

## 6. Documentation that names the version

- [x] 6.1 `CLAUDE.md` Development Environment: name `qt-version.txt` as the version, and write
      the Qt path and Qt sources path as `<version>`, not a number. Same treatment in
      `README.md` (badge, install step), `openspec/config.yaml` context line,
      `tools/webclient/README.md`, `tools/qtmodules-wasm/README.md`.
- [x] 6.2 `docs/CLAUDE_MD/PLATFORM_BUILD.md`, `docs/CLAUDE_MD/TESTING.md`,
      `docs/RASPBERRY_PI.md`, `docs/IOS_CI_SETUP.md`, `docs/IOS_CI_FOR_CLAUDE.md`,
      `.claude/skills/audit-code/SKILL.md`.
- [x] 6.3 Final sweep: `git grep -n "6\.12\.0\|6_12_0"` and confirm every remaining hit is a
      dated statement this change keeps on purpose (GC patches and their test, upstream bug
      reports, "in 6.12.0" source citations, archived OpenSpec changes). List them in the PR
      description.
      Done 2026-11-23. Kept on purpose: the GC patches, their test pin and README record; the
      Wasm upstream bug reports and repro notes; source citations that name their release
      (`android/*.gradle` mirror templates that are byte-identical in 6.12.1,
      `DecenzaActivity.java`, `comboboxselection.h`, `cmake/qtmqtt.cmake`, the
      `OperationCompletionOverlay.qml` workaround); dated measurements in
      `BUILD_PERFORMANCE.md` and `FIRMWARE_UPDATE.md`; two workflow comments; and the
      `build-config` spec's "37 for Qt 6.12.0", still true of 6.12.1 (`android-37.0` in
      `QtPublicAndroidHelpers.cmake:70`). Fixed: `CI_CD.md` stated the Wasm producer's version in
      the present tense.

## 6b. Remove workarounds 6.12.1 makes unneeded

- [x] 6b.1 `qml/components/ValueInput.qml`: drop all four calls to
      `AccessibleValueChange.notifyNumericValueChanged`, the two `onValueChanged` ones and the two
      `Component.onCompleted` ones, delete the helper's numeric method, and cite the 6.12.1 source.
      `tst_sharedformcontrols` asserts exactly one ValueChanged for the starting value and for
      each change. Verified 2026-11-23: with one old `onValueChanged` call restored it fails with
      2 events for one change, and with the completion calls still present it failed with 2 for
      the starting value (`root:200 popup:200 root:200 popup:200`).
      Web client, Wasm screen reader on, with the completion calls still present (2026-11-23):
      Machine tab's three `ValueInput`s are `role="slider"` with correct starting
      `aria-valuenow` (3, 31, 5); stepping Water refill 5 -> 6 -> 5 mm wrote `aria-valuenow`
      exactly once per change. Rechecked without them (2026-11-23, bundle with no numeric
      helper): the same three start at 3, 31 and 5.
- [x] 6b.2 `src/core/btlogfilter.{h,cpp}`: remove the QTBUG-146779 drop, compile the filter only
      on Linux.
- [x] 6b.3 `src/ble/applebtstate.h`, `src/ble/blemanager.cpp`: replace the wrong QTBUG-50838
      citation with why the adapter stays.
- [x] 6b.5 `resources/webclient/index.html`: remove the "deleted val. handle" reload and its
      re-arming, and `tst_customwidgethtml::browserVideoHandleRecoveryReloadsOnce`. Fix the
      `playsinline` citation; note the numeric-value resolution in
      `tools/webclient/upstream-accessibility.md`. Verified: inline script parses; suite 203/203.
- [x] 6b.6 **(ask Jeff, browser)** In the web client, screensaver in Videos mode: switch the
      Wasm branch of `stopVideoPlayer()` to deactivating the loader like native, play through
      several videos, and confirm the `<video>` count in the DOM stays at one and EndOfMedia
      still advances. Pass: remove the reuse and the pause-before-stop. Fail: keep both.
      In the same run, watch the console for `Cannot use deleted val. handle` across at least
      five wake/sleep transitions: the reload removed in 6b.5 relied on 6.12.1 fixing
      QTBUG-151843. If it appears, restore the reload and its test.
      Done 2026-11-23 in the built-in browser against the tailnet web client, DE1 simulator.
      Old code: 6 sleeps and 5 wakes, waking 0.5-8 s into playback: no released-handle error,
      at most one `<video>`, none after waking. New code (reuse and pause-before-stop removed
      from `ScreensaverPage.qml`, app restarted 10:34:50 MST): 3 video-to-video transitions
      each advanced within 2.5 s with exactly one playing `<video>`; wake/sleep still clean;
      console empty. The `EndOfMedia` filter stays for QTBUG-151769 (fixed in 6.13).
- [x] 6b.7 Found while live-testing, not from 6.12.1: `Theme.capAccessibleText` and
      `Theme.stripMarkdown` declared an unused second parameter (`maxLen: var`) that no caller
      passed, and the one-argument call logged "Unable to assign [undefined] to QString" at
      `CrashReportDialog.qml:244` and `SubsystemLogView.qml:221` (on 6.12.0 too). Both now take
      only `text`, cap at `Theme.accessibleTextCap`. Verified: build and QML gate clean; after a
      restart the native log no longer prints the `CrashReportDialog` warning every earlier
      session printed at ~2 s, and showing both log views printed nothing in either client.

- [ ] 6b.4 **(ask Jeff, iPhone)** With `QGuiApplication::clipboard()->blockSignals(true)` removed
      from `src/main.cpp` in a local iOS build: copy text in another app, return to Decenza with
      the Brew dialog built, and watch for iOS's "Allow Paste" prompt. No prompt: remove the
      block and its comment in this change. A prompt: keep it, and cite that `pasteboardTypes`
      still prompts.

## 7. Build iOS and macOS with Xcode 27.2 on the macOS 27 runner

- [x] 7.1 `.github/workflows/release.yml`: add a workflow-level `XCODE_VERSION: '27.2'`; use it
      for both `setup-xcode` inputs and the iOS ccache key
      (`ios-arm64-xcode${{ env.XCODE_VERSION }}-s3`); change `build-ios` and `build-macos`
      from `runs-on: macos-26` to `runs-on: xcode-27`. Verify `grep -n "26\.4\.1\|macos-26"`
      finds no Apple-job pin left.
- [x] 7.2 Add `.github/actionlint.yaml` declaring `xcode-27` as a runner label. Verify
      `actionlint` is clean on all three edited workflows.
- [x] 7.3 `.github/actions/setup-xcode/action.yml`: describe the `xcode-27` image instead of
      `macos-26`, and rewrite the "Why the pin" comment for 27.2 (Apple's April 2027 iOS SDK
      rule; matches the local Xcode build 27B52). Keep the shims and their comment. Cut the old
      libc++ paragraph to one sentence of history.
- [x] 7.4 `docs/CLAUDE_MD/CI_CD.md`: the Apple jobs run on the `xcode-27` image with Xcode 27.2.
      An edit to the existing sentence, not a new paragraph.
- [x] 7.5 Confirm locally, without a CI run, that a macOS build with Xcode 27.2 still declares
      the 14.4 floor: `vtool -show-build` (or `otool -l`) on the 6.12.1 Debug `Decenza` binary
      reports `minos 14.4` and `sdk 27.2`.
      Done 2026-11-23: `platform MACOS, minos 14.4, sdk 27.2` (local Debug is `arm64` only;
      universal is a CI setting, checked in 9.4).

## 8. Local build and test

- [x] 8.1 Qt Creator kit on `~/Qt/6.12.1/macos`, new build directory
      `build/Qt_6_12_1_for_macOS_Debug`. Its Initial Configuration carries
      `DECENZA_WASM_EMSDK_DIR` and `DECENZA_MACOS_CODESIGN_IDENTITY`, the latter now the
      certificate NAME, since the SHA-1 changed with a certificate renewal. Verified 2026-11-23:
      a full build succeeded and `codesign -dvvv` reports `TeamIdentifier=VDSK39AZYD`. That
      build predates groups 1-6; 8.2 is the build of the finished change.
- [x] 8.2 **(ask Jeff)** Build through `mcp__qtcreator__build_project`. Verify the output
      reports Qt MQTT 6.12.1 and `QML diagnostics gate passed` with the full file count.
      Done 2026-11-23, build 1795447715270 on the finished tree: 0 errors, `Qt MQTT 6.12.1`,
      `QML diagnostics gate passed. clean: 374/374`.
- [x] 8.3 **(ask Jeff)** Run the full suite through `mcp__qtcreator__test_run`. Verify no
      failures and an empty warnings list.
      Done 2026-11-23, run 1795447715816: 203 passed, 0 failed, no warnings. The first run
      failed `tst_sharedformcontrols` on 6.12.1's own ValueChanged events; that led to 6b.1.
- [x] 8.4 Decide whether to run `tools/qt-gc-diagnostics/ownership-repro` on stock 6.12.1.
      Decided 2026-11-23 (Jeff): skip. It needs a second Qt Creator project and gates nothing;
      the retirement record says it was not run on 6.12.1.
- [x] 8.5 **(ask Jeff)** Start Decenza from the new build. Confirm it connects to the WiFi
      scale (the Local Network grant for the new build directory), that the log has no
      `cross-validation enabled` line, and that starting and stopping a flush works.
      Done 2026-11-23 (app started 10:34:50 MST, Qt 6.12.1, DE1 simulator): the WiFi scale
      connected at 99.8 s with `local=192.168.10.158` once the new build directory's Local
      Network approval landed (earlier attempts showed `local=<unbound>`). The session logged no
      cross-validation, `QmlGc`, `gcdiag` or "window doesn't exist" line; the 6.12.0 sessions
      logged "cross-validation enabled" 19 times, so the search can match. A flush ran and
      stopped at its 3 s limit; a second, stopped by command, returned to Idle/Ready.
      Pre-existing, not from this change: `Theme.capAccessibleText` returns undefined for its
      one-argument callers (`CrashReportDialog.qml:244`, `SubsystemLogView.qml:221`), also on 6.12.0.

- [ ] 8.6 **(ask Jeff, destructive)** Once 8.2-8.5 pass, reclaim the 6.12.0 build space. Before
      deleting, re-list and re-measure, and confirm the exact list with Jeff. Candidates measured
      2026-11-23: `Decenza-Desktop/build/Qt_6_12_0_for_macOS_Debug` (13 GB) and
      `Qt_6_12_0_for_iOS_Device` (14 MB). In the other clone, `~/Development/GitHub/Decenza`
      (12 GB macOS build, plus Wasm and iOS 6.12.0 dirs), only if that clone is also moved to
      6.12.1 — it is on another branch. The `~/Qt/6.12.0` SDK (31 GB) is removed through Qt's
      Maintenance Tool by Jeff, not by deleting the folder; removing it leaves the unported
      `tools/qt-gc-diagnostics/tests` and patch-1 dry runs without their source tree.

## 9. CI and field validation

- [ ] 9.1 Read the `text-invariants.yml` run on the PR before anything else.
- [ ] 9.2 **(ask Jeff)** Dispatch `release.yml` on the branch for all platforms with
      `upload_to_release=false`. Verify every platform job is green and the Wasm module
      artifact carries the 6.12.1 name. This is the only compile of the Android-guarded code
      group 3 removes.
      Dispatch 45238440136 found two 6.12.1/Xcode 27 changes, both fixed: Linux Multimedia now
      links `libva.so.2`/`libva-drm.so.2` (added to `install-linux-packages`), and Xcode 27.2's
      `lipo` rejects two archs per `-verify_arch` (now one call per arch).
- [ ] 9.3 From the Android job's APK, confirm `lib/arm64-v8a/libQt6Qml_arm64-v8a.so` does not
      contain the string `gcdiag` and that the manifest still declares targetSdkVersion 37 and
      minSdkVersion 28.
- [ ] 9.4 From the same dispatch: the iOS and macOS jobs ran on the `xcode-27` image with
      Xcode 27.2 (`xcodebuild -version` in the setup step); the iOS app linked; the macOS
      `arch -x86_64` smoke run passed. If it failed for lack of Rosetta, stop and put the
      choice in the design's Rosetta risk to Jeff. From the artifacts, `vtool -show-build` on
      each app binary reports `sdk 27.2` with `minos 18.0` (iOS) and `minos 14.4` (macOS), and
      `lipo -archs` on the macOS app lists `x86_64 arm64`. (A Ninja-built `Info.plist` carries
      no `DTSDKName`, so read the binary.)
- [ ] 9.5 Record as held for the next beta, not passed: Android pause/resume, screensaver wake,
      TalkBack start with the app in the foreground, and absence of #2179 and #1663 reports.

## Workflow follow-up

- Run the `audit-code` skill on the PR's final commit and resolve Codex P1/P2 threads.
- Archive with `openspec archive upgrade-qt-6-12-1 --yes` as the PR's last commit.
- Squash-merge and delete the branch.
