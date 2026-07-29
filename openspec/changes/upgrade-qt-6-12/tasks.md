# Tasks: Upgrade Qt from 6.11.1 to 6.12

Work starts at Qt 6.12 GA (**2026-09-22**; Beta 3 **2026-08-18**, RC **2026-09-08**). Nothing here is
blocked.

## 0. Decisions

- [x] **iOS floor — decided 2026-07-29: take iOS 18, one Qt version on every platform.** Qt 6.12
      requires iOS 18, so Decenza inherits that floor. Holding iOS on 6.11.1 and deferring the
      upgrade were both rejected; proposal §"iOS 18 is a hard floor" records why, so they are not
      re-proposed
- [ ] **Carries one obligation, tracked in §2**: the release notes must state that the iOS minimum
      rose *because of the Qt upgrade* — an upstream floor, not a decision to drop anyone — and name
      the affected devices. Do not ship the upgrade without it
- [ ] Acquire an **A12+ iPad** for iOS testing. Not a blocker for starting, but the iPad7,4 cannot
      install a 6.12 build at all and the Simulator is unusable on this Mac
      (`project_qt_ios_simulator_gap`), so iOS ships untested until this exists. The Home Screen
      widget has iOS as its driver, which makes "test it later" expensive
- [ ] **Gerrit 735089 first**: ask for the `AndroidDeadlockProtector` fix to be picked to 6.12 before
      rebuilding any override. If it lands, `android/qt-overrides/` is deleted outright and §4 shrinks
      to a deletion. Both a11y patches went upstream through the same account, so this is a
      reasonable ask — and it is the README's own stated preference over rebuilding
- [ ] **Canvas Painter Graphs backend**: after installing 6.12, record whether
      `graphs-2d-high-performance-backend` is ON in the shipped Qt (read
      `<QtDir>/lib/cmake/Qt6Graphs/*Config*.cmake`, or check whether `Qt6::CanvasPainter` is a link
      dependency of `Qt6::Graphs`). Default answer to "should we build qtgraphs from source to get
      it" is **no**; record the finding so `charts-qt-6-12-polish` §3 can act on it either way

## 1. Prerequisites

- [ ] Install Qt 6.12 on Jeff's Mac (macOS + iOS targets) via Qt Maintenance Tool, including **Qt
      Canvas Painter** and **Qt Graphs**
- [ ] Install Qt 6.12 on the Windows dev machine (MSVC 2022 x64), same addons
- [ ] Create the Qt Creator kits. Put `-DDECENZA_MACOS_CODESIGN_IDENTITY=DFA23C5D…` in the kit's
      **Initial Configuration**, not just the cache — a new Qt means a fresh build directory, and
      Initial Configuration is the only place that survives a cache wipe (`CLAUDE.local.md`). Without
      it, macOS silently blocks LAN traffic and the WiFi scale "breaks"
- [ ] Confirm the `cmake` each toolchain actually invokes is **≥ 3.25** (Qt 6.12 requires ≥ 3.16 in
      `cmake_minimum_required` — ours says 3.21, still fine — but recommends 3.25 for the configuring
      tool). Qt ships one at `~/Qt/Tools/CMake/CMake.app/Contents/bin`
- [ ] Verify Qt 6.12 is installable through `install-qt-action`/aqtinstall at all before touching
      seven workflows: dry-run one workflow via `workflow_dispatch` on the branch

## 2. Source changes

- [ ] `CMakeLists.txt`:
  - [ ] iOS `CMAKE_OSX_DEPLOYMENT_TARGET "17.0"` → `"18.0"` (line ≈160) and update its comment to say
        Qt 6.12 requires iOS 18
  - [ ] After the first configure, check for new `QTP` policy warnings and set any new ones NEW inside
        the existing `Qt6_VERSION VERSION_GREATER_EQUAL "6.5.0"` guard (≈:129-132). Zero policy
        warnings is a `build-config` spec requirement, so this is not optional
  - [ ] Confirm `QT_ANDROID_COMPILE_SDK_VERSION "android-35"` (≈:1128) is still what 6.12 wants;
        `ANDROID_MIN_SDK_VERSION 28` is unchanged in 6.12 (API 28-36 supported)
- [ ] `CLAUDE.md`: Qt version, `C:/Qt/6.11.1/msvc2022_64`, `~/Qt/6.11.1/Src` (both the path and the
      "read it instead of guessing" instruction), and any build command quoting a 6.11.1 path
- [ ] `README.md`: Qt badge → 6.12+, install-step minimum → 6.12
- [ ] `openspec/config.yaml`: tech-stack line `Qt 6.11.1` → `Qt 6.12`
- [ ] `docs/CLAUDE_MD/PLATFORM_BUILD.md`, `docs/CLAUDE_MD/TESTING.md`, `docs/IOS_CI_SETUP.md`,
      `docs/IOS_CI_FOR_CLAUDE.md`: Qt version and example build-directory names
      (`build/Qt_6_11_1_for_macOS_Debug` → 6.12)
- [ ] `docs/CLAUDE_MD/BUILD_PERFORMANCE.md`: if its measurements were taken on 6.11.1, either
      re-measure or label them as 6.11.1 numbers rather than leaving them to be read as current
- [ ] Tell Jeff to update `CLAUDE.local.md` build-dir paths himself (uncommitted, his file)
- [ ] **Release notes — the §0 obligation.** State that iOS now requires **iOS 18 or newer**, that the
      cause is the **Qt 6.12 framework upgrade** (Qt dropped iOS 17 support; every Qt app on 6.12
      inherits the same floor), and name the devices that cannot go to iOS 18 (pre-A12: iPad Pro
      1st/2nd gen, iPad 6th gen, iPhone X and earlier). Factual and short — the cause is external, so
      say so without apologising or padding. An iOS 17 user who stops seeing updates should be able to
      find this line and understand it. Android, desktop and Linux users are unaffected; say that too,
      so nobody reads a platform floor as an app-wide one
- [ ] Check whether the wiki manual states a minimum iOS version anywhere
      (https://github.com/Kulitorum/Decenza/wiki/Manual — separate repo, `Kulitorum/Decenza.wiki.git`).
      If it does, update it in the same change; per `project_wiki_edits_held_for_release`, ask before
      pushing the wiki edit

## 3. CI workflows (7 files)

- [ ] `windows-release.yml`: `version: '6.11.1'` → 6.12; sccache key
      `sccache-windows-x64-qt6.11.1-vs2026` → `…-qt6.12.x-vs2026`; **re-test the `aqtsource` pin**
      (≈:80-85, `TODO(qt-6.11)`) against PyPI-head aqtinstall on 6.12 and drop it if it works
- [ ] `macos-release.yml`: version bump
- [ ] `ios-release.yml`: version bump; re-check whether the Xcode 26.4.1 pin is still needed for
      6.12's prebuilt iOS binaries (Qt 6.12 requires Xcode 16+, which it satisfies either way)
- [ ] `android-release.yml`: `env.QT_VERSION` bump; **`java-version: '17'` → `'21'`** (≈:69 — Qt 6.12
      requires JDK 21); confirm the derived `QT_NDK_VERSION` (≈:88-97) resolves to r27c
      `27.2.12479018`, the same revision 6.11.1 used
- [ ] `linux-release.yml`, `linux-arm64-release.yml`: version bump
- [ ] `nightly-sanitizers.yml`: version bump (easy to miss — it is not one of the six platform
      workflows)
- [ ] Module lists need **no** change: `qtcanvaspainter` is already in all seven and remains a module
      in 6.12
- [ ] Trigger all seven via `workflow_dispatch` on the branch and confirm green before merge

## 4. Android platform overrides

Depends on the §0 Gerrit-735089 answer.

**If 735089 is picked to 6.12** — delete the whole mechanism:
- [ ] `git rm -r android/qt-overrides/`
- [ ] Remove the override + validation step from `android-release.yml` (≈:190-215)
- [ ] Note in the PR that three upstream bugs closed and the patch mechanism is gone

**If not** — shrink it to the crash patch only:
- [ ] Rebase `358540b2` (crash fix) from `skialpine/qtbase` onto the qtbase 6.12 tag; **drop the 12
      a11y commits** — QTBUG-118858 and QTBUG-145786 are upstream on 6.12 (`505853d02184`,
      `41e6aecb7cd3`)
- [ ] Rebuild `libplugins_platforms_qtforandroid_arm64-v8a.so` via Qt's own `configure` +
      `cmake --build . --target QAndroidIntegrationPlugin` (never a hand-rolled CMake — a mismatched
      feature define crashes devices; see the README)
- [ ] **Delete `android/qt-overrides/Qt6Android.jar`** — it existed only for the a11y Java classes,
      now stock
- [ ] Remove the jar handling from `android-release.yml` while keeping the `.so` copy and the
      `BUILT_AGAINST_QT` guard
- [ ] Update `BUILT_AGAINST_QT` to the exact 6.12 version **in the same commit as the new binary**
- [ ] Rewrite `android/qt-overrides/README.md`: three bugs → one, drop the jar section, update the
      qtbase SHA, and keep the `strings`-based verification recipe (check `[decenza-patch]` present
      **and** stock `Failed to acquire deadlock protector for %s.` absent)
- [ ] Verify the guard still fails a mismatched build (temporarily set `BUILT_AGAINST_QT` wrong in a
      throwaway `workflow_dispatch` run) — it is the only thing standing between a stale plugin and an
      APK that dies at startup for every user

## 5. Verification

- [ ] Full local suite: `mcp__qtcreator__run_tests` scope `all`, zero failures, zero WARN lines. No
      CI job builds or tests a PR, so this is the gate
- [ ] qmllint clean per `QML_GOTCHAS.md` — diff per-file/per-category sets, do not read totals
- [ ] Build Debug on macOS and confirm ASan/UBSan instrumentation still engages on 6.12
- [ ] CI test builds of **iOS and Android** specifically: their code is `#ifdef`-guarded and only the
      tag-push release workflows compile it
- [ ] `openspec validate --all` passes after the `build-config` delta lands

## 6. On-device verification (Android first — it is the primary platform)

- [ ] **TalkBack**, on the Samsung tablet, with the a11y overrides now stock: typing echo in an
      editable field (QTBUG-118858) and no keyboard-on-focus trap (QTBUG-145786). These are the two
      behaviours we shipped patches for; if upstream's version regresses either, that is a blocker
      and the a11y patches go back
- [ ] Re-check TalkBack on screens we tuned, for 6.12's *new* a11y behaviour we never had:
      scrolled-viewport announcements (`0e3e5d8aacb0`, `cbd6b48998ce`), expandable/expanded state
      (`56ae71c6cb27`), `ButtonDropDown` classname (`a52d5d20893f`)
- [ ] **BLE scanning on Android**: 6.12 changes scan-record UUID/length parsing and defers
      `canceled()` to match classic scan. Confirm the DE1 and every scale still appear in discovery,
      and that our scan-stop path still behaves when it orders on `canceled()`. Use the real machine
      (`mcp__de1__*`), and remember every operation starts from the GHC button
      (`project_jeff_de1_has_ghc`)
- [ ] Pull a full shot end-to-end on Android: graphs render, weight arrives, shot saves, Visualizer
      upload succeeds
- [ ] macOS smoke test: simulator extraction end-to-end; `JsCanvasPainterItem` initialises on Metal
      with no slow-record warnings now that Canvas Painter is out of Technology Preview
- [ ] WiFi scale on the macOS debug build — proves the re-signing identity took (`codesign -dvvv …
      | grep TeamIdentifier` → `VDSK39AZYD`)
- [ ] iOS, if (a): install on an A12+ device, confirm launch, BLE scale, and the Home Screen widget
      snapshot path

## 7. Follow-ups

- [ ] Update `charts-qt-6-12-polish` `tasks.md`: mark the precondition met, and fill in §3's gate with
      the §0 Canvas-Painter-backend finding
- [ ] Open the PR, run `/pr-review-toolkit:review-pr`, then merge with `/merge-pr`
- [ ] Archive this change as the final commit on the same PR (`feedback_archive_last_commit_on_pr`)
