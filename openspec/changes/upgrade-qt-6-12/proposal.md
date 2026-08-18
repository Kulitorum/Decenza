# Change: Upgrade Qt from 6.11.1 to 6.12

## Status: READY — starts after Qt 6.12 GA (2026-09-22)

Qt 6.12 GA is **2026-09-22** (feature freeze was 2026-06-02; Beta 2 shipped 2026-07-14, Beta 3 is
2026-08-18, RC 2026-09-08).

**Decision taken 2026-07-29: take iOS 18.** Qt 6.12 raises the iOS minimum from 17.0 to iOS 18, and
Decenza follows it on one Qt version across every platform. Holding iOS back or deferring the whole
upgrade were both considered and rejected — the world moves on, and paying for two Qt versions in CI
to keep a 2017 iPad alive is the wrong trade.

**The one obligation that comes with it**: the release notes must say plainly *why* the iOS minimum
rose — it is Qt's floor, inherited from the framework upgrade, not a Decenza choice — and name the
device classes affected, so an iOS 17 user who stops getting updates can see the cause. See §"iOS 18
is a hard floor" and `tasks.md` §2.

## Why

- **Unblocks `charts-qt-6-12-polish`.** Every item in that change is preconditioned on this one.
  `ValueAxis.labelPostFormat`, `XYSeries.values`, and `GraphsView.dynamicLabelMargins` are all 6.12
  APIs, already verified present on the `qt/qtgraphs` `6.12` branch.
- **Deletes most of `android/qt-overrides/`.** Both TalkBack fixes we ship as a patched Qt platform
  plugin + jar are now upstream **on the 6.12 branch** — see §"Android accessibility overrides".
- **Qt Canvas Painter exits Technology Preview** and becomes a fully supported module. `CupFillView`
  and its `JsCanvasPainterItem` wrapper (added in `upgrade-qt-6-11-1`) stop depending on a TP API
  that can shift between minors — the `synchronize()` → `synchronizeData()` rename we absorbed in
  6.11.1 is exactly the class of churn that stops here.
- **6.12 is the next commercial LTS**, so it is where Qt's own bug-fix attention goes next.
- Minor wins we get for free: faster `QDateTime` parsing in Qt Core (shot-history and DYE metadata
  paths), a new `Color` QML singleton (`Theme.qml` hand-rolls colour math today), `QMap`/`QMultiMap`
  relational operators, `AnimatedImage.loops` + `finished()`.

## iOS 18 is a hard floor

Qt 6.12's iOS page states **"iOS 18 or higher (including iOS 26)"** with **"Xcode 16 (iOS 18 SDK) or
higher"**. Qt 6.11 was iOS 17. Consequences:

1. **`CMakeLists.txt:160` sets `CMAKE_OSX_DEPLOYMENT_TARGET "17.0"` for iOS** and must go to 18.0.
   The `build-config` spec's "iOS Minimum Deployment Target" requirement changes with it.
2. **Jeff's iOS test device stops working.** The test iPad is an **iPad7,4** (iPad Pro 10.5", A10X),
   which tops out at iPadOS 17 — iPadOS 18 requires A12 or newer. A Qt 6.12 build cannot be installed
   on it at all, so the `devicectl install` deploy path in `project_ios_deploy_workflow` dies with
   the upgrade. Testing iOS 2.x would need newer hardware, and the iOS Simulator is not an option on
   this Mac (Qt's iOS simulator libraries are x86_64-only against an Apple Silicon host).
3. **Existing iOS users on 17 stop receiving updates.** App Store builds would raise their minimum
   OS, and iOS 17 devices silently stop being offered the new version.
4. macOS minimum also rises to **14.4** (from 13). Not a problem for this Mac (macOS 26) but it is a
   published support change.

**Decided (2026-07-29): take iOS 18, single Qt version everywhere.** Recorded here so the two
rejected options are not re-proposed later:

- **Rejected — upgrade everything except iOS**, keeping the iOS workflow on 6.11.1. Costs two Qt
  versions in CI, a second `install-qt-action` pin, and a standing divergence risk in shared C++/QML
  (any 6.12-only API used outside an `#ifdef` breaks the iOS build silently until a tag push). The
  Home Screen widget has iOS as its driver (`feedback_ios_primary_driver`), so a rotting iOS build is
  not survivable.
- **Rejected — defer the upgrade** until the iOS 17 install base is negligible. That parks
  `charts-qt-6-12-polish` indefinitely and accumulates Qt debt for a shrinking device class.

**What the decision obliges us to do**, and the only part that is not mechanical:

- **Say why, in the release notes.** The iOS minimum rose because Qt 6.12 requires iOS 18 — an
  upstream framework floor Decenza inherits, not a product decision to drop anyone. Name the affected
  devices (pre-A12: iPad Pro 1st/2nd gen, iPad 6th gen, iPhone X and earlier) so a user who stops
  seeing updates can find the reason instead of assuming the app broke. This is the whole ask: the
  cause is external, and the notes should make that legible without sounding defensive.
- **Get an A12+ iPad for testing.** Not a blocker for starting, but iOS ships untested without it,
  and the Simulator is not available on this Mac (`project_qt_ios_simulator_gap`).

## Android accessibility overrides — SUPERSEDED (2026-08-18)

> **This whole section is obsolete.** `android/qt-overrides/` no longer exists. The
> `upgrade-qt-6-11-2` change deleted it outright: Qt 6.11.2 carries both TalkBack fixes upstream
> (verified against the `v6.11.2` tag, not the 6.12 branch), and the Android `qFatal` crash patch
> was dropped rather than rebuilt — 9 reports in roughly 8 months across a user base in the
> hundreds did not justify a permanent fork. Its source is preserved in `docs/qt-patches/`, and
> `build-config` now carries a `Decenza Ships Stock Qt Runtime Binaries` requirement, which also
> supersedes the "Patched Qt Platform Artifacts Are Version-Locked" requirement this change ADDs
> below — drop that requirement when this change is next revised. Gerrit **735089** is still the
> better outcome and is still worth asking to have picked.
>
> The two a11y improvements named at the end of this section that were *not* patched locally are
> still worth re-checking on device, and that check moved to `upgrade-qt-6-11-2` task 8.1.
>
> The original text follows, for the reasoning only.

## Android accessibility overrides — delete two thirds, keep the crash patch

`android/qt-overrides/` ships a patched Qt Android platform plugin (`.so`) plus a patched
`Qt6Android.jar` to fix three unfixed-upstream bugs. Verified against the `qt/qtbase` **`6.12`
branch** (not `dev`, and not the doc pages):

| Bug | Upstream on 6.12? | Evidence |
|---|---|---|
| **QTBUG-145786** keyboard opens on a11y focus (focus trap) | **Yes** | `41e6aecb7cd3`, 2026-06-23, "Android a11y: do not open keyboard on focus when TalkBack is enabled". Dev commit `e019acaaae87` carries `Pick-to: 6.12 6.11` |
| **QTBUG-118858** typed/deleted characters not spoken under TalkBack | **Yes** | `505853d02184`, 2026-07-16, "Android a11y: announce text edits so screen readers echo typing". Touches **both** `src/plugins/platforms/android/` and `src/android/jar/…` — the same two artifacts our override patches. Dev commit `66acbeda7c59` carries `Pick-to: 6.12 6.11` |
| **QTBUG-140490 / QTBUG-144207** `qFatal` abort on contended `AndroidDeadlockProtector` | **No** | `qandroidplatformopenglwindow.cpp:68` on the `6.12` branch still reads `qFatal("Failed to acquire deadlock protector for %s.", funcName);`. Gerrit **735089** (the real fix, which removes the protector from the EGL/Vk surface paths) has no `Pick-to:` footer and targets `dev` — i.e. 6.13 |

So the override **cannot simply be deleted**. What changes:

- **Delete the `Qt6Android.jar` override entirely.** Its only purpose was the a11y Java classes
  (`QtAccessibilityDelegate` / `QtAccessibilityInterface` / `QtNativeAccessibility`) that send
  `TYPE_VIEW_TEXT_CHANGED`; 6.12 ships that upstream. Stock jar from here on.
- **Rebuild the `.so` from Qt 6.12 sources carrying only the crash patch** (`358540b2` in
  `skialpine/qtbase`, rebased onto the 6.12 tag), dropping the 12 a11y commits. The crash fix is
  C++-only, so the `.so` alone is sufficient once the jar override is gone.
- **Update `BUILT_AGAINST_QT`** in the same commit as the new binary. `android-release.yml`
  (≈:190-215) *fails the build* when `BUILT_AGAINST_QT` ≠ `env.QT_VERSION`, so a Qt bump without
  touching the override gives a red build rather than an APK that dies at startup — that guard is
  working as designed and must not be loosened.
- **Better outcome, worth one attempt first**: ask for `735089` to be picked to 6.12. Both a11y
  patches went upstream through the same Gerrit account (`skialpine`, see `reference_qt_gerrit`), so
  requesting a `Pick-to: 6.12` on a crash fix is a reasonable ask. If it lands, `android/qt-overrides/`
  is deleted outright — the `.so`, the jar, `BUILT_AGAINST_QT`, and the workflow step — which is the
  README's own stated preference over rebuilding. Try this **before** rebuilding anything.

The crash matters enough to justify the remaining patch: **9 of 42** Android crash reports since
2026-01-18, and **2 of 2** on 6.11.1 builds. Reporting is opt-in, so that is a floor.

Also note two a11y improvements 6.12 brings that we did *not* patch and should re-check on device,
since they may change TalkBack behaviour on screens we tuned: `0e3e5d8aacb0` "Inform TalkBack of
scrolled viewport", `cbd6b48998ce` "Update TalkBack with ScrollingPositionChanged",
`56ae71c6cb27` "Implement expandable/expanded state", `a52d5d20893f` "Map
QAccessible::ButtonDropDown to correct classname".

## What Changes

- **CI workflows** (7 files: 6 platform + `nightly-sanitizers.yml`): bump `version: '6.11.1'` →
  `'6.12.x'`; `android-release.yml` bumps `env.QT_VERSION`. Module lists need no change —
  `qtcanvaspainter` is already listed everywhere and stays a module in 6.12.
- **`java-version: '17'` → `'21'`** in `android-release.yml:69`. Qt 6.12 requires **JDK 21**.
- **NDK**: Qt 6.12 pins Clang 17.0.2 / **NDK r27c (27.2.12479018)** — the same revision 6.11.1 used,
  and the workflow already derives `QT_NDK_VERSION` from Qt's own toolchain file (≈:88-97), so this
  should be a no-op. Verify rather than assume.
- **Windows sccache key**: `sccache-windows-x64-qt6.11.1-vs2026` → `…-qt6.12.x-vs2026`, so a stale
  cache cannot be hit.
- **Windows `aqtsource` pin** (`windows-release.yml:80-85`, `TODO(qt-6.11)`): the pin exists because
  aqtinstall 3.3.0 could not parse the per-arch Windows online-installer layout for 6.11.x. Re-test
  with PyPI head against 6.12; drop the pin if it works, re-point it if not.
- **iOS Xcode pin**: `ios-release.yml` pins Xcode 26.4.1 for a libc++ ABI reason. Qt 6.12 requires
  Xcode 16+, which 26.4.1 satisfies; verify the pin is still needed at all against 6.12's prebuilt
  binaries.
- **Release notes**: an entry stating that the iOS minimum is now 18 **because Qt 6.12 requires it**,
  with the affected device list. Per `feedback_release_notes_user_visible` this is exactly the kind of
  proven user-visible change that belongs there.
- **`CMakeLists.txt`**: iOS deployment target 17.0 → 18.0; check for new `QTP` policies
  introduced in 6.12 and set them NEW inside the existing `VERSION_GREATER_EQUAL "6.5.0"` guard;
  `cmake_minimum_required(VERSION 3.21)` stays valid (Qt 6.12 requires ≥ 3.16 in that command) but
  **CMake 3.25 is the recommended configuring tool version** — confirm the CMake that Qt Creator and
  each runner actually invoke is ≥ 3.25.
- **`android/qt-overrides/`**: delete `Qt6Android.jar`, rebuild the `.so` against 6.12 with the crash
  patch only, update `BUILT_AGAINST_QT` and the README (three bugs → one). Or delete the directory
  entirely if Gerrit 735089 is picked to 6.12.
- **Docs and metadata**: `CLAUDE.md` (Qt version + `C:/Qt/6.11.1/…`, `~/Qt/6.11.1/Src` paths),
  `CLAUDE.local.md` build-dir names (Jeff's, uncommitted), `README.md` badge and install minimum,
  `openspec/config.yaml` tech-stack line, `docs/CLAUDE_MD/PLATFORM_BUILD.md`,
  `docs/CLAUDE_MD/TESTING.md`, `docs/IOS_CI_SETUP.md`, `docs/IOS_CI_FOR_CLAUDE.md`,
  `docs/CLAUDE_MD/BUILD_PERFORMANCE.md` if its numbers were measured on 6.11.1.
- **Local installs**: Qt 6.12 on the Windows dev machine and on Jeff's Mac (macOS + iOS targets),
  plus a Qt Creator kit. `CLAUDE.local.md` notes that
  `-DDECENZA_MACOS_CODESIGN_IDENTITY=…` must go in the kit's **Initial Configuration**, since a fresh
  cache is what a new-Qt build directory is.

## Qt 6.11 → 6.12 delta relevant to Decenza

Minor upgrade, binary compatible. Audited, and the only source-affecting items are the platform
minimums above. Specifically:

- **Qt Bluetooth**: no new API, but three Android changes touch code paths we depend on and want
  on-device verification, not just a green build — `Android scan record parsing: fix length and type
  extraction`, `Android scan record parsing: add length checks when extracting UUIDs`, and
  `Android/BLE: defer canceled() to match classic scan` (that last one changes
  `QBluetoothDeviceDiscoveryAgent::canceled()` timing, and our scan-stop path orders on it). Also
  `BTLE: do not log CSRK and MAC` removes fields from Qt's BLE logging, and
  `Bluetooth Darwin: check size before parsing manufacturer data` affects macOS scanning. Nothing
  about descriptors/CCCD, so the native-CoreBluetooth iOS scale transport
  (`project_ios_scale_transport_revisit`) still cannot be retired.
- **MQTT: unaffected.** Decenza uses Eclipse Paho C (`src/network/mqttclient.cpp`), not Qt MQTT —
  and Qt does not ship Qt MQTT binaries to open-source users anyway (no `qtmqtt` in the addons
  repo), so this is not the moment to reconsider.
- **Qt Graphs**: `labelPostFormat`, `XYSeries.values` + `valueMapping`/`valueMin`/`stepSize`,
  `GraphsView.dynamicLabelMargins`, `useCanvasPainter` (compile-gated, see below), `QLogValueAxis`,
  a multi-axis margin fix. Still no built-in legend, dashed-stroke style, or pixel↔data mapping, so
  all four Stage 0 bridges stay ours.
- **The Canvas Painter Graphs backend is off in stock Qt.** `graphs-2d-high-performance-backend` is
  `AUTODETECT OFF` upstream, and `QGraphsView::setUseCanvasPainter()` is compiled out without it, so
  `useCanvasPainter: true` is a silent no-op on a stock build. This change's job is only to
  **record which state the installed 6.12 is in**; whether to build `qtgraphs` from source to get the
  backend is a shipping-a-non-stock-Qt-module decision that belongs here rather than in a polish PR,
  and the default answer is no.
- **New modules we do not want**: Qt Qml Design Support (design tooling), HarmonyOS (Technology
  Preview). No action.

## Impact

- **Affected specs**: `build-config` (Qt version requirement, iOS deployment target, and — new — the
  Android platform-override policy that ties `BUILT_AGAINST_QT` to the Qt version).
- **Affected code**: `.github/workflows/*.yml` (7), `CMakeLists.txt`, `android/qt-overrides/*`,
  `CLAUDE.md`, `README.md`, `openspec/config.yaml`, four docs under `docs/`.
- **Risk**: **Medium-high**, and unusually so for a Qt minor. Not the API surface — the platform
  floor. iOS 18 removes a device class and Jeff's only test device; the Android override rebuild has
  a failure mode (stale/mismatched plugin) that bricks the app at startup for every user, which is
  why the CI guard fails the build instead of warning.
- **Unblocks**: `charts-qt-6-12-polish` (all six items).
- **Verification**: full local suite via `mcp__qtcreator__run_tests` (scope `all`) — no CI job builds
  or tests a PR — plus the qmllint gate, plus a CI test build of **iOS and Android specifically**,
  since their code is `#ifdef`-guarded and only the tag-push release workflows compile it.
