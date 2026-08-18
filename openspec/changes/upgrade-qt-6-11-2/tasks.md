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

- [ ] 2.1 `.github/workflows/windows-release.yml`: `version: '6.11.1'` → `'6.11.2'`; sccache key
      `sccache-windows-x64-qt6.11.1-vs2026` → `…-qt6.11.2-vs2026`; update the two OpenSSL comments
      that name 6.11.1 (:111, :144).
- [ ] 2.2 `macos-release.yml`, `linux-release.yml`, `linux-arm64-release.yml`: `version:` → `'6.11.2'`.
- [ ] 2.3 `ios-release.yml`: `version:` → `'6.11.2'`; update the prebuilt-binary comments at :55-62
      that name 6.11.1. Re-check whether the Xcode 26.4.1 pin is still needed against 6.11.2's
      binaries — record the answer either way rather than leaving the pin unexamined.
- [ ] 2.4 `android-release.yml`: `env.QT_VERSION` → `'6.11.2'`; gradle cache keys
      `gradle-android-qt6.11.1-*` → `qt6.11.2`. Confirm the NDK revision Qt's toolchain file
      resolves is unchanged (it should be — patch release), and that `java-version: '17'` still
      matches what 6.11.2 requires.
- [ ] 2.5 `nightly-sanitizers.yml`: `version:` → `'6.11.2'`.
- [ ] 2.6 `CMakeLists.txt:214`: update the "Qt 6.11.1 was built for iOS 17.0" comment. The iOS
      deployment target itself stays at 17.0 — 6.11.2 does not move it.
- [ ] 2.7 Grep the tree for any remaining `6.11.1` outside `openspec/changes/archive/` and the four
      dated source comments listed in task 5.4, and confirm each remaining hit is deliberate.

## 3. Delete `android/qt-overrides/`

- [ ] 3.1 Delete `android/qt-overrides/` entirely: `arm64-v8a/libplugins_platforms_qtforandroid_arm64-v8a.so`,
      `Qt6Android.jar`, `BUILT_AGAINST_QT`, `README.md`.
- [ ] 3.2 Remove the override step from `android-release.yml` (≈:186-215), including the
      `BUILT_AGAINST_QT` version-lock guard it contains. Confirm no later step in that workflow
      references `OVERRIDE`, `JAR_OVERRIDE` or `BUILT_AGAINST_FILE`.
- [ ] 3.3 Remove the reference to `android/qt-overrides/` in the `CMakeLists.txt` qmllint block
      comment (≈:1823) — it is deleted with that block in group 4, so verify rather than edit twice.
- [ ] 3.4 Confirm no workflow step writes into `$QT_ROOT_DIR` any more, on any platform. This is the
      `build-config` spec's "A Qt upgrade lands" scenario and it is the one that keeps a package from
      being built out of a mixture of Qt versions.

## 4. Delete the qmllint bundle and the skip mechanism

- [ ] 4.1 Delete `tools/qmllint-macos/` (7 files, 5.1 MB) including the bundled
      `QtQmlCompiler.framework`.
- [ ] 4.2 `CMakeLists.txt`: delete the staging / `install_name_tool` / `codesign` / `--version`-proof
      block (≈:1810-1870), the `QMLLINT_SKIP_UNLINTABLE` option and its `_qmllint_skip_default`
      computation, and the stale-cache `STATUS` message. Keep the `find_program(QMLLINT_EXECUTABLE)`
      lookup beside Qt's `bin/` and the gate target itself.
- [ ] 4.3 `CMakeLists.txt`: rewrite the surviving comment block. It currently explains a two-mode
      gate, cites "221 of 222", and names the Gerrit change as an expiry condition — all three are
      now historical. Say what the gate does in one mode, and drop the numbers that were only
      meaningful as a comparison between modes.
- [ ] 4.4 `scripts/qmllint_report.py`: delete `UNLINTABLE_BY_TOOL_BUG`, the `--skip-unlintable`
      argument, the `CustomItem.qml` entry in the per-file table, the stock-tool detection block
      (≈:947-975), the `--update-baseline` refusal that exists only for partial runs, and the
      complete-run/partial-run ceiling reconciliation (≈:82, :208, :247-271, :776, :813, :849).
      Keep every check that is about a run *failing* — exit status, file-count coverage, the refusal
      to lower a count from an unfinished run.
- [ ] 4.5 `.github/workflows/nightly-sanitizers.yml` (≈:429-437): delete the `QMLLINT_SKIP_UNLINTABLE`
      explanation. Keep the `ninja -t targets all | grep -q '^qmllint_check:'` guard — that is the
      green-while-blind check and it is unrelated to skipping.
- [ ] 4.6 Confirm `scripts/qmllint_report.py` still reports the analysed file count and still fails
      when it does not equal the number of `.qml` files in `qt_add_qml_module`. If that check does
      not exist today, add it — it is the `qml-diagnostics` spec's "Every file is analysed" scenario
      and it is what replaces the deleted skip bookkeeping.

## 5. Documentation

- [ ] 5.1 `CLAUDE.md`: Qt version → 6.11.2; `C:/Qt/6.11.1/msvc2022_64` → `6.11.2`;
      `~/Qt/6.11.1/Src` → `~/Qt/6.11.2/Src` (it exists — verified).
- [ ] 5.2 `README.md`: badge `Qt-6.11.1+` → `Qt-6.11.2+`; the "Select **Qt 6.11.1** or newer"
      install step.
- [ ] 5.3 `docs/CLAUDE_MD/PLATFORM_BUILD.md` (6 paths), `TESTING.md` (3 `CMAKE_PREFIX_PATH` lines),
      `BUILD_PERFORMANCE.md`, `PERFORMANCE_BASELINE.md`, `QML_GOTCHAS.md`, `docs/IOS_CI_SETUP.md`,
      `docs/IOS_CI_FOR_CLAUDE.md`.
- [ ] 5.4 Leave the four dated "verified against Qt 6.11.1" source comments unchanged
      (`qml/Theme.qml:314`, `:421`, `src/core/markdownrenderer.h:14`,
      `android/src/…/DecenzaActivity.java:83`). They record an observation on a version; rewriting
      the version without re-observing makes them false. Add nothing; this task is to confirm they
      were not swept up by a global replace.
- [ ] 5.5 `docs/CLAUDE_MD/BUILD_PERFORMANCE.md` / `PERFORMANCE_BASELINE.md`: the measurements are
      labelled "Qt 6.11.1" as the conditions they were taken under. Do not relabel them — mark them
      as pre-6.11.2 measurements instead, same reasoning as 5.4.
- [ ] 5.6 `CLAUDE.local.md` (Jeff's, uncommitted, not in the PR): the whole "QML diagnostics gate —
      one file needs a PATCHED qmllint" section is obsolete, as is the `~/Qt/6.11.1` reference and
      the `Qt_6_11_1_for_macOS_Debug` build-dir names. Flag it for hand-editing; do not commit it.
- [ ] 5.7 `openspec/changes/upgrade-qt-6-12/proposal.md`: add a note that its "Android accessibility
      overrides" section and its ADDED "Patched Qt Platform Artifacts Are Version-Locked"
      requirement are superseded — `android/qt-overrides/` no longer exists as of this change. Do not
      rewrite that change; it starts after 6.12 GA and is otherwise unaffected.
- [ ] 5.8 No wiki manual entry. Nothing here is user-visible: no feature, no UI, no behaviour change,
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
- [ ] 6.5 Recreate the macOS build directory in each clone as `Qt_6_11_2_for_macOS_Debug` against
      `~/Qt/6.11.2/macos`. Both clones currently have **no** build directory at all.
- [ ] 6.6 Before configuring, confirm `DECENZA_MACOS_CODESIGN_IDENTITY=DFA23C5DAFA64BEC7CA9D9D1DFA1746CE0E1C560`
      is present in the Qt Creator kit's **Initial Configuration** — not typed into the cache, not
      passed on a command line. Initial Configuration is the only place that reapplies after a cache
      wipe, and a wipe just happened: the previous value lived only in the deleted caches.
- [ ] 6.7 **Verify the identity came back from Initial Configuration by itself.** After the first
      configure of each new build directory, and *before* setting anything by hand, read the cache:

      grep DECENZA_MACOS_CODESIGN_IDENTITY <builddir>/CMakeCache.txt

      The value must be there. If it is absent, Initial Configuration does not carry it and 6.6 is
      not actually done — fix the kit rather than setting the cache variable, because a hand-set
      cache value passes every downstream check and then vanishes at the next wipe, which is exactly
      how this was lost the first time.

      This ordering is the whole point of the task. Checking `codesign` output first would pass just
      as well with a hand-set value, and would prove signing works while proving nothing about
      whether the configuration survives.
- [ ] 6.8 Then confirm the signature took, per `CLAUDE.local.md`:
      `codesign -dvvv <builddir>/Decenza.app 2>&1 | grep TeamIdentifier` → `VDSK39AZYD`. "not set"
      means ad-hoc signing and silently dropped Local Network traffic — which presents as a WiFi
      scale bug, not a signing one. Note the re-sign runs POST_BUILD, so it only fires once the
      target actually relinks.
- [ ] 6.9 Re-approve Local Network for each rebuilt app. The grant is keyed to bundle id +
      certificate + **path**, and the path changed with the rename, so one approval per clone is
      expected. If it does not take, reboot — toggling the row is documented as unreliable here.
- [ ] 6.10 Update `CLAUDE.local.md`'s "Configured build dirs (both clones)" list to the new
      `Qt_6_11_2_for_macOS_Debug` paths. Uncommitted file; hand-edit, do not commit.

## 7. Verify

- [ ] 7.1 Full build via `mcp__qtcreator__build` — **ask before invoking it**; Jeff shares the Qt
      Creator window. Confirm `mcp__qtcreator__get_current_project` first: two clones exist and
      Qt Creator's active project has drifted mid-session before.
- [ ] 7.2 Full test suite via `mcp__qtcreator__run_tests` (scope `all`) — same asking rule. Green
      suite is the merge gate; there is no PR CI that runs tests.
- [ ] 7.3 Run the qmllint gate against the **fresh** build dir and confirm **222 of 222** analysed,
      exit zero, zero diagnostics. A stale build dir describes the last build; use Qt's generated
      `.rcc/qmllint/Decenza.rsp`, never hand-assembled flags.
- [ ] 7.4 Diff the per-file and per-category diagnostic sets against
      `qml-diagnostics-baseline.json`, not the totals. 6.11.2 changes the import and singleton rules
      (QTBUG-144377, QTBUG-146759, QTBUG-146688), so movement in either direction is possible and is
      data, not a verdict. Fix anything that appears; do not baseline it.
- [ ] 7.5 Confirm `CustomItem.qml` is analysed by the **stock** `qmllint` — that it completes at all
      is the single upstream fix this change's tooling half rests on.
- [ ] 7.6 Launch the app and confirm no `qrc:/…qml` TypeErrors in the log. A clean build and a clean
      qmllint run do not prove a screen opens.
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
