# Change: Upgrade Qt from 6.11.1 to 6.11.2, and stop shipping patched Qt binaries

## Why

Qt 6.11.2 was released **2026-08-18** — roughly 400 bug fixes on top of 6.11.1, no API changes, no
platform-floor changes. It is a drop-in patch bump, and it is available now; Qt 6.12 GA is
**2026-09-22**, several weeks out. The existing `upgrade-qt-6-12` change stays as written and starts
after GA. This one is deliberately independent of it and much smaller.

Three reasons to take it, in order of weight:

1. **It carries the upstream fixes that let both of Decenza's local Qt hacks be deleted.** Verified
   against the real `v6.11.2` tag and Gerrit, not against doc pages:

   | Local hack | Upstream in 6.11.2? | Evidence |
   |---|---|---|
   | `tools/qmllint-macos/` — patched `qmllint` + `QtQmlCompiler.framework`, the only tool that can analyse `CustomItem.qml` | **Yes** | Gerrit **757430**, "QmlCompiler: Fix qmllint exhausting memory on some QML files", merged **onto the `6.11.2` release branch** 2026-08-11. This is the same change `scripts/qmllint_report.py:202` names as the bundle's expiry condition (Gerrit 755657, `Pick-to: 6.12 6.11`) |
   | `android/qt-overrides/` a11y half — **QTBUG-118858** (TalkBack typing echo, #1300) | **Yes** | `TYPE_VIEW_TEXT_CHANGED` appears twice in `QtAccessibilityDelegate.java` at `v6.11.2` and zero times at `v6.11.1` |
   | `android/qt-overrides/` a11y half — **QTBUG-145786** (keyboard opens on a11y focus) | **Yes** | `setAccessibilityFocusInProgress` present in `qandroidinputcontext.h` at `v6.11.2`, absent at `v6.11.1` |
   | `android/qt-overrides/` crash half — **QTBUG-140490 / QTBUG-144207** (`qFatal` on contended `AndroidDeadlockProtector`) | **No** | `qandroidplatformopenglwindow.cpp:68` at `v6.11.2` still reads `qFatal("Failed to acquire deadlock protector for %s.", funcName);`. Gerrit **735089** is still `NEW` on `dev` |

2. **Security.** CVE-2026-16762 and CVE-2026-19248 in qtbase (the latter, QTBUG-147191, is unbounded
   QDom recursion), and **CVE-2026-13326 in qtconnectivity** — the Bluetooth module Decenza's whole
   BLE stack sits on.

3. **Fixes in exactly the modules Decenza leans on hardest.** Not incidental:
   - **qtconnectivity**: QTBUG-146756 crash on Windows connecting to BLE after a scan; QTBUG-145898
     null deref in the WinRT `onAdvertisementDataReceived` completion chain. Both are on the path
     `d5b6bba6` (the shared GATT queue) just touched.
   - **qtmultimedia**: QTBUG-147200, *"[REG Qt 6.11.0->6.11.1] Android: Deadlock on app shutdown when
     `QAndroidAudioDevices` is destroyed"* — a regression **we currently ship**, on the Android
     screensaver's video path.
   - **qtgraphs**: five crash/leak fixes on the shot charts, including QTBUG-147262 (`DateTimeAxis`
     crash when min and max are identical), QTBUG-147540 (`QGraphsView::removeSeries` not
     thread-safe), and QTBUG-142559 (steady memory growth re-creating `GraphsView`).
   - **qtcanvaspainter**: QTBUG-144831 path caching, QTBUG-146192 path-group aliasing — `CupFillView`.
   - **qtdeclarative**: QTBUG-146127 `Accessible.labelFor: null` crash, QTBUG-146959 a11y crash in
     `QQuickScrollbar`, QTBUG-130116 broken a11y tree in lists; GC crashes QTBUG-138621 /
     QTBUG-134687; QTBUG-147277 `StyledText` `<span>` breaking paragraph direction.
   - **qtbase**: SQLite 3.53.1 → 3.53.4 under the shot-history database; QTBUG-147039
     `setTransferTimeout()` silently stops working after an HTTP redirect (Visualizer, Bean Base,
     firmware download); QTBUG-148481 Android `QDesktopServices::openUrl()` corrupting
     percent-encoded URLs; QTBUG-147161 Schannel corrupting concurrent TLS sessions on Windows.

## What Changes

- **Bump Qt 6.11.1 → 6.11.2** across all six platform workflows, `nightly-sanitizers.yml`,
  `CMakeLists.txt`, and the docs that name a version or a path. No deployment-target change, no JDK
  change, no NDK change, no module-list change — 6.11.2 is a patch release of the series already in
  use.

- **Delete `android/qt-overrides/` entirely** — the `.so`, `Qt6Android.jar`, `BUILT_AGAINST_QT`, the
  README, and the override step in `android-release.yml`. Decenza ships stock Qt runtime binaries
  from here on. Both a11y fixes are upstream, so nothing is lost there.

  **The Android `qFatal` crash patch is dropped with it — a deliberate, quantified trade.**
  `android/qt-overrides/README.md` records this abort as **9 of 42** Android crash reports since
  2026-01-18. That share is the wrong denominator to reason from: as an absolute rate it is **9
  reports in roughly 8 months across a user base in the hundreds**, which is not a common crash. The
  "2 of 2 on 6.11.1 builds" figure in the README is a sample of two and carries no weight either
  way. Weighed against a permanent fork — an ABI lock, a rebuild obligation on every Qt bump, and a
  binary in the tree nobody but its author can reproduce — the fork is the larger cost.
  **Re-entry condition:** if post-6.11.2 crash reports show this signature at a materially higher
  rate, rebuild the `.so` alone — or, better, get Gerrit 735089 picked to 6.11 so it arrives in
  6.11.3 (September 2026) with no fork at all.

- **Delete `tools/qmllint-macos/`** and the whole skip mechanism it exists to be an alternative to:
  the `QMLLINT_SKIP_UNLINTABLE` CMake option, `UNLINTABLE_BY_TOOL_BUG` and the `--skip-unlintable`
  flag in `scripts/qmllint_report.py`, the staging/re-signing/`--version`-proving block in
  `CMakeLists.txt`, and the explanatory comment in `nightly-sanitizers.yml`. The released 6.11.2
  `qmllint` analyses all 222 files, so every platform reaches full coverage with the stock tool and
  the CI/local asymmetry that mechanism existed to manage disappears.

- **Re-verify the QML diagnostics baseline against 6.11.2's `qmllint`, and treat any movement as
  data rather than as a regression.** 6.11.2 changes the import and singleton rules — QTBUG-144377
  ("qmllint suggests to remove an import that is actually used"), QTBUG-146759 ("qmllint: import
  rule"), QTBUG-146688 ("bogus singleton warnings"). The tree is at zero today; a different tool can
  legitimately move that number in either direction, and per `feedback_verify_by_refusal_not_plausible_number`
  a plausible new count is not evidence it is right.

- **BREAKING (build config, not user-facing):** a build directory configured before this change
  keeps its cached `QMLLINT_SKIP_UNLINTABLE`; the option is removed, so the stale cache entry becomes
  inert rather than wrong. Contributors who pointed `QMLLINT_EXECUTABLE` at a hand-built patched
  `qmllint` should drop the override.

## Capabilities

### New Capabilities

None. This change modifies existing build and tooling requirements only.

### Modified Capabilities

- `build-config`: the pinned Qt version moves from 6.11.1 to 6.11.2, and the project takes on a
  standing requirement to ship **stock** Qt runtime artifacts — no patched platform plugin or jar —
  with a stated re-entry condition rather than a standing fork.
- `qml-diagnostics`: the "A Bundled Tool May Close A Toolchain Gap, But Never Silently" requirement
  is removed — the gap it governs no longer exists — and the gate's coverage requirement becomes
  unconditional: every file in the module, on every platform, with the released tool.

## Impact

**Build and CI** — `CMakeLists.txt` (Qt version comment at :214, the entire qmllint-macos staging
block ≈:1810-1900, the `QMLLINT_SKIP_UNLINTABLE` option and its stale-cache `STATUS` message);
`.github/workflows/` × 7 (`windows-release.yml` incl. the `sccache-…-qt6.11.1-vs2026` key,
`macos-release.yml`, `linux-release.yml`, `linux-arm64-release.yml`, `ios-release.yml`,
`android-release.yml` incl. `env.QT_VERSION` and the override step ≈:186-215,
`nightly-sanitizers.yml` incl. the comment at ≈:433).

**Deleted** — `android/qt-overrides/` (4 files, incl. a 172 KB jar and the `.so`);
`tools/qmllint-macos/` (7 files, 5.1 MB).

**Tooling** — `scripts/qmllint_report.py` (`UNLINTABLE_BY_TOOL_BUG`, `--skip-unlintable`, the
per-file `{}` entry for `CustomItem.qml`, the stock-tool detection block ≈:947-975, and the
complete-run/partial-run ceiling arithmetic that only exists to reconcile the two modes);
`qml-diagnostics-baseline.json` (re-verified, not assumed).

**Docs** — `CLAUDE.md` (Qt version, `C:/Qt/6.11.1/…`, `~/Qt/6.11.1/Src`); `README.md` (badge and
install minimum); `docs/CLAUDE_MD/PLATFORM_BUILD.md`, `TESTING.md`, `BUILD_PERFORMANCE.md`,
`PERFORMANCE_BASELINE.md`, `QML_GOTCHAS.md`; `docs/IOS_CI_SETUP.md`, `docs/IOS_CI_FOR_CLAUDE.md`.
`CLAUDE.local.md` is Jeff's and uncommitted — its whole "QML diagnostics gate needs a PATCHED
qmllint" section becomes obsolete and should be cut by hand.

**Not touched, deliberately** — source comments citing "verified against Qt 6.11.1"
(`qml/Theme.qml:314`, `:421`, `src/core/markdownrenderer.h:14`,
`android/src/…/DecenzaActivity.java:83`) are dated observations, and rewriting the date without
re-observing would be a false claim. `docs/CLAUDE_MD/ACCESSIBILITY.md` and the `#1300` work are
unaffected in behaviour: the same two fixes arrive, from upstream instead of from a patched plugin.

**Depends on / relates to** — `upgrade-qt-6-12` (unchanged, starts after 2026-09-22; its
"Patched Qt Platform Artifacts Are Version-Locked" ADDED requirement is superseded by this change's
stock-binaries requirement and should be dropped from that proposal when it is next revised).
`charts-qt-6-12-polish` is **not** unblocked by this change — its APIs are 6.12.
