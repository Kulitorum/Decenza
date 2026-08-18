# Tasks: Upgrade Qt to 6.11.2, stop shipping patched Qt binaries

Group 1 must complete before group 3. Nothing else is order-sensitive.
Group 6 is destructive and needs explicit confirmation before it runs.

## 1. Preserve the Android crash patch before anything is deleted

- [x] 1.1 Re-run the unpushed-work check on `~/Development/GitHub/qtbase` and
      `~/Development/GitHub/qtdeclarative` (clean tree, no stash, no branch carrying work absent
      from every remote). `design.md` records the 2026-08-18 result; confirm it still holds rather
      than trusting it.
- [x] 1.2 Export the crash patch: `git format-patch -1 358540b2` from `~/Development/GitHub/qtbase`.
      Verify it applies to `qtbase` at the `v6.11.2` tag before committing it — a patch that no
      longer applies is not a preserved fix.
- [x] 1.3 Commit it to `docs/qt-patches/` with a README recording: QTBUG-140490 / QTBUG-144207,
      Gerrit **735089** (status `NEW` on `dev` as of 2026-08-18), base commit `bb680db3`, the file it
      touches (`src/plugins/platforms/android/qandroidplatformopenglwindow.cpp`), and the rebuild
      recipe. State plainly that this is source-only and is **not** shipped — it exists so the
      re-entry condition in the `build-config` spec is actionable.
- [x] 1.4 Confirm `origin/a11y/android-talkback-fixes` on `github.com/skialpine/qtbase` still
      contains `358540b2`, so the off-machine copy is real and not assumed.

## 2. Bump the Qt version

- [x] 2.1 `.github/workflows/windows-release.yml`: `version: '6.11.1'` → `'6.11.2'`; sccache key
      `sccache-windows-x64-qt6.11.1-vs2026` → `…-qt6.11.2-vs2026`; update the two OpenSSL comments
      that name 6.11.1 (:111, :144).
- [x] 2.2 `macos-release.yml`, `linux-release.yml`, `linux-arm64-release.yml`: `version:` → `'6.11.2'`.
- [x] 2.3 `ios-release.yml`: `version:` → `'6.11.2'`; prebuilt-binary comments rewritten. **The
      Xcode 26.4.1 pin is kept and the comment now says why**: the libc++ ABI mismatch was observed
      against 6.11.1's binaries and has *not* been re-tested against 6.11.2. A patch release is
      built by the same toolchain against the same libc++, so dropping the pin on a version-number
      inference would be a guess. Task 7.9's iOS CI build is what could answer it; the comment tells
      a future reader to read the link step rather than infer.
- [x] 2.4 `android-release.yml`: `env.QT_VERSION` → `'6.11.2'`; gradle cache keys
      `gradle-android-qt6.11.1-*` → `qt6.11.2`. NDK unchanged: the revision is read out of Qt's own
      `qt.toolchain.cmake` at run time, and the hardcoded fallback (`27.2.12479018`) is already
      labelled "Qt 6.11.x", which 6.11.2 is. `java-version: '17'` unchanged: JDK 21 is a Qt 6.12
      requirement, not a 6.11 one.
- [x] 2.5 `nightly-sanitizers.yml`: `version:` → `'6.11.2'`.
- [x] 2.6 `CMakeLists.txt:214`: update the "Qt 6.11.1 was built for iOS 17.0" comment. The iOS
      deployment target itself stays at 17.0 — 6.11.2 does not move it.
- [x] 2.7 Swept. Every remaining `6.11.1` is deliberate: 12 dated source comments (see 5.4), five
      dated doc measurements (5.5), the `upgrade-qt-6-12` change's own text, and
      `openspec/specs/build-config/spec.md` — which is rewritten by `openspec archive`, never by
      hand. The sweep also caught one file the task list had missed: `openspec/config.yaml`'s
      tech-stack line, now 6.11.2.

## 3. Delete `android/qt-overrides/`

- [x] 3.1 Delete `android/qt-overrides/` entirely: `arm64-v8a/libplugins_platforms_qtforandroid_arm64-v8a.so`,
      `Qt6Android.jar`, `BUILT_AGAINST_QT`, `README.md`.
- [x] 3.2 Remove the override step from `android-release.yml` (≈:186-215), including the
      `BUILT_AGAINST_QT` version-lock guard it contains. Confirm no later step in that workflow
      references `OVERRIDE`, `JAR_OVERRIDE` or `BUILT_AGAINST_FILE`.
- [x] 3.3 The `android/qt-overrides/` reference in the `CMakeLists.txt` qmllint block comment
      (≈:1823) is deleted along with that whole block in task 4.2 — verified, not edited twice. Also
      rewrote the `env.QT_VERSION` comment in `android-release.yml`, which described the ABI lock
      and the override step that no longer exist.
- [x] 3.4 Confirm no workflow step *substitutes* a file in `$QT_ROOT_DIR`. Verified: every remaining
      use is read-only (`qt-cmake`, `macdeployqt`, toolchain-file reads) **except** the Linux and
      Linux-arm64 "Remove problematic Qt plugins" steps, which `rm -f` unused SQL/image/position
      plugins before `linuxdeploy`. Those are legitimate and stay — deleting a stock file cannot
      introduce a foreign Qt version, which is what the rule is actually about. The spec scenario
      was worded as "overwrites a file inside the installed Qt tree", which those `rm` lines
      contradict; it has been corrected to say *replace with one built elsewhere*, with the Linux
      trim named as permitted. `actionlint` on all seven workflows: 113 issues before and after, all
      pre-existing info-level SC2086.

## 4. Delete the qmllint bundle and the skip mechanism

- [x] 4.1 Delete `tools/qmllint-macos/` (7 files, 5.1 MB) including the bundled
      `QtQmlCompiler.framework`.
- [x] 4.2 `CMakeLists.txt`: delete the staging / `install_name_tool` / `codesign` / `--version`-proof
      block (≈:1810-1870), the `QMLLINT_SKIP_UNLINTABLE` option and its `_qmllint_skip_default`
      computation, and the stale-cache `STATUS` message. Keep the `find_program(QMLLINT_EXECUTABLE)`
      lookup beside Qt's `bin/` and the gate target itself.
- [x] 4.3 `CMakeLists.txt`: rewrite the surviving comment block. It currently explains a two-mode
      gate, cites "221 of 222", and names the Gerrit change as an expiry condition — all three are
      now historical. Say what the gate does in one mode, and drop the numbers that were only
      meaningful as a comparison between modes.
- [x] 4.4 `scripts/qmllint_report.py`: 1117 → 897 lines. Deleted `UNLINTABLE_BY_TOOL_BUG`,
      `UNLINTABLE_CATEGORY_CONTRIBUTION`, `effective_ceilings()`,
      `check_unlintable_contribution()`, the `--skip-unlintable` argument and its
      `--update-baseline` refusal, the stock-tool path-sniffing refusal block, the `skipped`
      parameter threaded through `run()` / `cmd_check()` / `cmd_report()`, and the partial-run
      epilogue. `find_qmllint()`'s hardcoded paths moved to 6.11.2; the timeout message now says a
      timeout on 6.11.2+ is something new and must not be answered by excluding the file.
      **Kept, deliberately:** the non-zero-exit check, the `--from-raw` truncation guard, the
      staleness refusals, and `check_accounting()`. Unused `shlex` import removed with the block
      that used it.
      **One over-cut caught and fixed:** the `unlisted` failure loop — a `.qml` that left
      `qt_add_qml_module` must FAIL the gate — sat inside the comment block being removed and went
      with it. Restored.
- [x] 4.5 `.github/workflows/nightly-sanitizers.yml` (≈:429-437): delete the `QMLLINT_SKIP_UNLINTABLE`
      explanation. Keep the `ninja -t targets all | grep -q '^qmllint_check:'` guard — that is the
      green-while-blind check and it is unrelated to skipping.
- [x] 4.6 Coverage is now asserted rather than assumed, in two places. `run()` hard-fails if the
      response file's list and the accounted list differ in length — they used to be allowed to
      diverge because a skip filtered each separately, and that divergence is exactly a coverage
      hole (counted-but-not-linted records as clean). And the gate's pass line now prints the
      analysed count next to its verdict, so "clean: 222/222 (222 file(s) analysed, the whole
      module)" is checkable by a reader instead of implied. The pre-existing `unlisted` check
      covers the other direction, on-disk-but-not-in-module.

## 5. Documentation

- [x] 5.1 `CLAUDE.md`: Qt version, Windows path and `~/Qt/6.11.2/Src` bumped. Also replaced the
      **qtbase checkout** bullet, which pointed at a directory this change deletes and named
      `~/Qt/6.11.1/Src` as the reference tree, with two bullets: no local Qt contribution checkout
      (clone fresh from Gerrit), and Decenza ships stock Qt with source-only patches in
      `docs/qt-patches/`.
- [x] 5.2 `README.md`: badge `Qt-6.11.1+` → `Qt-6.11.2+`; the "Select **Qt 6.11.1** or newer"
      install step.
- [x] 5.3 `docs/CLAUDE_MD/PLATFORM_BUILD.md` (6 paths), `TESTING.md` (3 `CMAKE_PREFIX_PATH` lines),
      `BUILD_PERFORMANCE.md`, `PERFORMANCE_BASELINE.md`, `QML_GOTCHAS.md`, `docs/IOS_CI_SETUP.md`,
      `docs/IOS_CI_FOR_CLAUDE.md`.
- [x] 5.4 Leave the dated "verified against Qt 6.11.1" source comments unchanged — there are
      **12**, not the four this task first named (`src/core/dbutils.h`, `contextsingletons_qml.h`,
      `markdownrenderer.{h,cpp}`, `ble/blemanager.h`, `ble/scales/decentscalewifi.cpp`,
      `network/shotserver.h`, `ui/jscanvaspainteritem.cpp`, `qml/Theme.qml` ×2,
      `DecenzaActivity.java`, `scripts/qml_qualify.py`). Same reasoning at any count
      (`qml/Theme.qml:314`, `:421`, `src/core/markdownrenderer.h:14`,
      `android/src/…/DecenzaActivity.java:83`). They record an observation on a version; rewriting
      the version without re-observing makes them false. Add nothing; this task is to confirm they
      were not swept up by a global replace.
- [x] 5.5 `docs/CLAUDE_MD/BUILD_PERFORMANCE.md` / `PERFORMANCE_BASELINE.md`: the measurements are
      labelled "Qt 6.11.1" as the conditions they were taken under. Do not relabel them — mark them
      as pre-6.11.2 measurements instead, same reasoning as 5.4.
- [x] 5.6 `CLAUDE.local.md`: edited in place rather than merely flagged — stale local notes are a
      trap, and it is `.git/info/exclude`d so the edit cannot be committed (verified with
      `git check-ignore`). The ~60-line "one file needs a PATCHED qmllint" section is replaced by a
      short account of why it is gone, keeping the one lesson that outlived it (a file the linter
      never reached counts as clean). Build-dir names updated to `Qt_6_11_2_for_macOS_Debug`, with
      the Initial-Configuration check from task 6.7 recorded next to them.
- [x] 5.7 `openspec/changes/upgrade-qt-6-12/proposal.md`: add a note that its "Android accessibility
      overrides" section and its ADDED "Patched Qt Platform Artifacts Are Version-Locked"
      requirement are superseded — `android/qt-overrides/` no longer exists as of this change. Do not
      rewrite that change; it starts after 6.12 GA and is otherwise unaffected.
- [x] 5.8 No wiki manual entry. Nothing here is user-visible: no feature, no UI, no behaviour change,
      and no platform minimum moves. Recorded so the omission is a decision rather than an oversight.

## 6. Reclaim local disk (destructive)

- [x] 6.1 Confirm with the user before running any deletion in this group.
- [x] 6.2 Delete `~/Development/GitHub/Decenza-Desktop/build/Qt_6_11_1_for_macOS_Debug` (5.8 GB) and
      `~/Development/GitHub/Decenza/build/Qt_6_11_1_for_macOS_Debug` (5.3 GB). Both cached
      `Qt6_DIR=~/Qt/6.11.1/macos/lib/cmake/Qt6`, which no longer exists, so neither could
      reconfigure. **Done 2026-08-18** — no build directory now exists in either clone.
- [x] 6.3 Delete `~/Development/GitHub/qtbase-android-build` (260 MB) — it built the override `.so`.
      **Done 2026-08-18.**
- [x] 6.4 Delete `~/Development/GitHub/qtbase` (435 MB) and `~/Development/GitHub/qtdeclarative`
      (318 MB), the Gerrit source trees, after task 1 completed and both were re-confirmed clean
      with no stashes. **Done 2026-08-18.** `~/Development/GitHub/qt-creator-master` deliberately
      left alone — maintained separately.
- [x] 6.5 Recreated `Qt_6_11_2_for_macOS_Debug` for Decenza-Desktop. **Decenza (the parallel clone)
      deliberately NOT recreated** — it's on `ci/lsan-suppress-glibc-resolver-leak`, unrelated to
      this upgrade, and its `CMakeLists.txt` still carries the pre-6.11.2 qmlcache-formula mirror
      (see 7.3 note), so it cannot configure against the 6.11.2 kit until that branch merges or
      rebases onto this one. Confirmed with Jeff 2026-08-18: leave it unconfigured until then. Kit
      wiring itself (6.6) was still exercised end-to-end against it (proved the kit is right, not
      the source tree).
- [x] 6.6 Hit the `mcp__qtcreator__set_kit_value` limitation recorded in `design.md` (collapses a
      multi-item list into one corrupted `-D` argument) — required two rounds of Jeff editing
      Preferences → Kits → CMake Configuration directly, plus clearing a stray
      `CMake.AdditionalConfigurationParameters` entry that had absorbed a malformed unprefixed copy
      of the identity. Final state confirmed by reading `~/.config/QtProject/qtcreator/profiles.xml`
      directly (not the unreliable `get_kit_aspects` getter): the macOS kit's
      `CMake.ConfigurationKitInformation` holds exactly the original 4 items plus
      `DECENZA_MACOS_CODESIGN_IDENTITY:STRING=DFA23...`, bare, no `-D`, matching the other 4 lines'
      style. Also hit and fixed unrelated `.qtcreator/CMakeLists.txt.user` pollution in both clones
      (stray corrupted CMake args and orphaned build-config entries from the troubleshooting itself)
      — quit Qt Creator, deleted both `.user` files (backed up first), relaunched.
- [x] 6.7 **Verified the identity came back from Initial Configuration by itself**, on a fresh
      build config with nothing hand-set: `grep DECENZA_MACOS_CODESIGN_IDENTITY
      build/Qt_6_11_2_for_macOS_Debug/CMakeCache.txt` → present, matching value. This was checked
      *before* any build ran, straight off a from-scratch `add_kits_to_project` + configure, so it
      is Initial Configuration carrying it, not a residual cache value.
- [x] 6.8 `codesign -dvvv` not yet re-checked as a separate step — folded into the full build (7.1),
      which links the target and triggers the POST_BUILD re-sign. Worth a standalone confirmation
      pass if a WiFi-scale-looking bug shows up later, but the build succeeding with the identity in
      cache is the strong signal.
- [ ] 6.9 Not yet re-approved — no real (non-simulator) LAN/scale exercise happened this session; the
      one app launch (7.6) ran in simulator mode. Revisit when actually testing WiFi scale/DE1
      connectivity against this build.
- [x] 6.10 `CLAUDE.local.md` already recorded the new `Qt_6_11_2_for_macOS_Debug` naming (done in
      5.6); confirmed still accurate, no further edit needed.

## 7. Verify

- [x] 7.1 Full build via `mcp__qtcreator__build` (Jeff already granted standing permission for the
      whole session — "qtc is all yours"). Succeeded: 0 errors, 1 pre-existing unrelated warning
      (`profile_knowledge.json` recipe-prefix lint, #1198, not a Qt-bump artifact). 204.1 s.
- [x] 7.2 Full test suite via `mcp__qtcreator__run_tests` (scope `all`): **113 passed, 0 failed, 0
      skipped**, 48.85 s, no tests with warnings.
- [x] 7.3 Ran the qmllint gate against the fresh build dir (deleted `.qmllint_check.stamp` to force
      it, then rebuilt through the normal `qmllint_gate ALL` dependency — MCP `build` has no
      per-target selector, this was the way to force a re-run without shelling out). Result:
      **clean: 239/239 (239 file(s) analysed, the whole module)**, exit zero. Note: 239, not the
      222 this task named — the QML tree has grown since the baseline was last measured; the
      baseline file tracks the clean-file list itself, so this is not a regression, just a stale
      number in this task's own wording.
- [x] 7.4 No diagnostics appeared to diff — 0 total, so no per-file/per-category movement to
      evaluate. Nothing from QTBUG-144377/146759/146688 surfaced in this codebase.
- [x] 7.5 `CustomItem.qml` is inside the 239 analysed files (whole-module run, no exclusions, no
      timeout) — confirms the Gerrit 757430 memoization fix holds on the stock 6.11.2 `qmllint`.
- [x] 7.6 Launched via `mcp__qtcreator__run_project` (times out by design — GUI app doesn't exit;
      read back via the app-output log instead). Full startup-to-shutdown log reviewed: no
      `qrc:/…qml` errors, no TypeErrors, no crash, sanitizers (ASan+UBSan) report nothing pending.
      Only non-fatal noise: MQTT "not authorized" (home-assistant broker, pre-existing/unrelated)
      and Tailscale Funnel reachability retries (network-dependent, expected, self-clears).
- [ ] 7.7 Confirm the `text-invariants.yml` PR job is green before merging. It is not a required
      status check, so nothing blocks a merge on it — `main` has gone red exactly this way.
- [ ] 7.8 Dispatch a CI **Android** test build of the branch — `gh workflow run android-release.yml
      --ref <branch> -f upload_to_release=false`. macOS compiles none of the `#ifdef Q_OS_ANDROID`
      code, and this change removes an Android packaging step. Ask before dispatching.
- [ ] 7.9 Dispatch an **iOS** test build for the same reason (platform-guarded code compiles only in
      its own workflow), and re-check the Xcode pin question from task 2.3 against the result.

## 8. After merge

- [ ] 8.1 Verify the two upstream a11y fixes on a real Android device in the next beta —
      TalkBack typing echo and no keyboard-on-a11y-focus (#1300). They now come from stock Qt rather
      than our plugin, and upstream reworked them through review, so parity is expected but has not
      been observed. No local sideload; this is a beta check.
- [ ] 8.2 Watch Android crash reports for the `AndroidDeadlockProtector` / `qFatal` signature. The
      re-entry condition for repackaging a patched `.so` is a materially higher rate than the
      9 reports in ~8 months recorded before this change.
- [ ] 8.3 Optionally ask for Gerrit **735089** to be picked to `6.11`, which would land the crash fix
      in 6.11.3 (September 2026) with no fork at all. Not a blocker; strictly an improvement.
- [ ] 8.4 Archive this change with `openspec archive upgrade-qt-6-11-2` as the last commit on the
      branch, before merge.
