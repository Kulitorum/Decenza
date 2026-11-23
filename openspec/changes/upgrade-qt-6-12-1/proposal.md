# Proposal

## Why

Qt 6.12.1 ships the QML garbage-collector allocator fix (QTBUG-149512) that Decenza currently
carries as a patched Android `libQt6Qml` binary, and it removes the Android
"Failed to acquire deadlock protector" abort (QTBUG-140490 / QTBUG-144207, #1663). No #2179
crash has been reported since the allocator fix shipped in the override, so the override's
remaining job — diagnostics for a crash nobody is seeing — no longer justifies a binary that
locks the Android build to one exact Qt version.

Separately, Apple requires iOS and iPadOS apps uploaded to App Store Connect from April 2027 to
be built with the iOS 27 SDK or later. Our iOS and macOS jobs build with Xcode 26.4.1 on GitHub's
`macos-26` image, which carries no Xcode 27.

## What Changes

- **Give the Qt version one source.** Today it is spelled about 32 times in the workflows and in
  about 10 CMake files and scripts, and this bump found two of them only by failing to build.
  One root file holds the version; CMake, the build scripts and every CI job read it; a per-PR
  check fails on a new hard-coded copy. Done in this change because the bump already edits
  every one of those pins.
- Move that version from 6.12.0 to 6.12.1, and update the docs that name it.
- Raise the CMake minimum to 6.12.1. Without the override, a 6.12.0 build would ship the
  allocator bug again.
- **Remove the Android QtQml override**: the 10 MB `android/qt-overrides/` binary, its manifest
  and README, the two `release.yml` steps that install and verify it, and
  `tools/qt-gc-diagnostics/verify_artifact.py`. Android packages stock Qt again.
- **Remove the app-side GC diagnostics that exist to serve it.** The app turns on Qt's
  incremental-GC cross-validation for every user on every platform, which costs a full GC mark
  per collection on the GUI thread (`main.cpp:562-566`). It also carries a deferred-abort path
  that only the patched library can trigger: the coordinator in `main.cpp`, `gcdeferredabort.h`,
  the GC half of `btlogfilter`, the operation block in `DE1Device`, and a crash-report special
  case. All of it goes, with its tests and its `LOGGING.md` section.
- **Keep the GC diagnostics as source, unported.** The three patches and the instrumentation
  test stay in `tools/qt-gc-diagnostics/` under their 6.12.0 names, with a retirement record:
  why it was removed, how to rebuild it, where the last shipped binary and the app-side code are
  in git history, and what would bring it back.
- **Re-pin `ownership-repro/` to 6.12.1, without running it.** It builds against stock Qt, so it
  follows the current version and stays runnable. Running it once would only add a data point to
  the retirement record, which records that it was not run on 6.12.1.
- Retire what 6.12.1 makes obsolete, each with source evidence: the saved deadlock-protector
  patch in `docs/qt-patches/`, and the "qmllint text output drops findings" warning in
  `QML_GOTCHAS.md` (QTBUG-151969).
- **Build iOS and macOS with Xcode 27.2 (iOS 27 / macOS 27 SDKs) on GitHub's macOS 27 runner.**
  Both Apple jobs move from `macos-26` to the `xcode-27` image and select Xcode 27.2, written
  once in the workflow. Apple's April 2027 rule names iOS only; macOS moves with it so the two
  Apple jobs keep one toolchain. Qt 6.12.1 supports both SDKs (`QT_SUPPORTED_MAX_IOS_SDK_VERSION`
  and `..._MACOS_SDK_VERSION` are 27). Deployment floors stay iOS 18.0 and macOS 14.4, and the
  macOS app stays universal (`x86_64` and `arm64`): the 27.2 SDKs accept both floors and both
  architectures.
- State what is not fixed: the `OperationCompletionOverlay.qml` qmlcachegen workaround stays
  (QTBUG-152061, 6.12.2), and none of the filed Wasm accessibility or media bugs are in 6.12.1.

Nothing here changes what a user sees or does, so there is no manual entry; the platforms
Decenza runs on are unchanged. The one thing a
user may feel is fewer GC pauses, since each collection no longer runs a second full mark.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `build-config`: adds "The Qt Version Has One Source" and "Apple Builds Use The Current SDK".
  The existing requirements are unchanged:
  `build-config` already requires stock Qt runtime binaries, one Qt patch version across
  platforms, cache keys that follow the version, and a recorded account of what a bump
  inherits, and reintroducing an override is already governed by its "decision is revisited"
  scenario. The override removal brings the tree back into line with those.

## Impact

- **Version source**: a new root `qt-version.txt`, a CMake include and a Python helper that read
  it, and a new `scripts/check_qt_version_pins.py` in `text-invariants.yml`.
- **CI**: `.github/workflows/release.yml` (31 version references and cache keys, the two
  override steps), `.github/workflows/nightly-sanitizers.yml`, `text-invariants.yml`. Every Qt
  cache is repopulated once.
- **Build**: `CMakeLists.txt`, `tools/qtmodules-wasm/`, `tools/webclient/media-repro/`,
  `scripts/build_webclient.py`, `scripts/qmllint_report.py`, `cmake/webclient_bundle.cmake`.
  Qt MQTT follows automatically; the `v6.12.1` tag exists upstream.
- **App code**: `src/main.cpp`, `src/core/btlogfilter.{h,cpp}`, `src/core/gcdeferredabort.h`
  (deleted), `src/core/crashhandler.cpp`, `src/network/crashreporter.cpp`,
  `src/ble/de1device.{h,cpp}`, `tests/tst_webdebuglogger.cpp`,
  `tests/tst_de1device_headless.cpp`, `docs/CLAUDE_MD/LOGGING.md`. `DE1Device` no longer refuses
  to start an operation while a GC crash report is pending, because nothing can set that state.
- **Android**: the packaged `libQt6Qml` is stock. The `gcdiag-v2` startup log marker and
  `WEAK_HISTORY` crash-report lines disappear. The EGL surface lifecycle is rewritten upstream,
  so pause/resume and screensaver wake are the behaviours to watch in the next beta.
- **Apple CI**: `release.yml` `build-ios` and `build-macos` jobs (runner label, Xcode version,
  iOS ccache key), `.github/actions/setup-xcode/action.yml`, a new `.github/actionlint.yaml`
  naming the `xcode-27` label, `docs/CLAUDE_MD/CI_CD.md`. The iOS ccache is repopulated once. The
  macOS job's `arch -x86_64` smoke run needs Rosetta on the runner; Apple calls macOS 26 the last
  release supporting it, though it still runs on macOS 27.3 today.
- **Security**: picks up 17 CVE fixes, two of them in Qt WebSockets, which backs the shot
  server, the remote session server and screen capture.
- **Docs**: `CLAUDE.md`, `README.md`, `openspec/config.yaml`, `docs/CLAUDE_MD/*`,
  `docs/RASPBERRY_PI.md`, `docs/IOS_CI_*.md`, `docs/qt-patches/README.md`,
  `tools/qt-gc-diagnostics/README.md`.
- **Developer machines**: a Qt Creator kit on 6.12.1 and a new build directory. On macOS a new
  build directory needs its code-signing identity and one Local Network approval again.
