# Qt Android platform-plugin override

This directory holds a **patched Qt Android platform plugin** that the Android
CI build drops in over the stock one before packaging, to fix three bugs that
are unfixed upstream:

- **QTBUG-118858** — typed/deleted characters are not spoken in editable fields
  under TalkBack (issue #1300).
- **QTBUG-145786** — the keyboard opens the instant a field gets accessibility
  focus, a focus trap (issue #1300).
- **QTBUG-140490 / QTBUG-144207** — `qFatal` abort when the process-wide
  `AndroidDeadlockProtector` is contended during a surface re-create
  (issue #1663). Our largest single source of Android crash reports:
  **9 of 42** Android reports since 2026-01-18, and **2 of 2** on the Qt 6.11.1
  builds — i.e. every Android crash reported since that upgrade, though on a
  sample of two. Reporting is opt-in, so treat these as a floor.

The a11y half spans **two** Qt artifacts and BOTH must be overridden (the crash
fix is C++ only and needs just the `.so`):
- `arm64-v8a/libplugins_platforms_qtforandroid_arm64-v8a.so` — the C++ platform
  plugin (keyboard-on-focus fix, node `setText`, IME text-change synthesis).
- `Qt6Android.jar` — the accessibility **Java** classes
  (`QtAccessibilityDelegate` / `QtAccessibilityInterface` / `QtNativeAccessibility`),
  which actually send the `TYPE_VIEW_TEXT_CHANGED` event. The C++ calls into these;
  without the jar override the method is missing and the event is never sent (this
  was the cause of the persistently-silent typing echo). This jar is the **stock**
  `Qt6Android.jar` with only the patched a11y `.class` files swapped in, so
  everything else stays byte-identical to stock.

We ship arm64-v8a only (`install-qt-action … arch: android_arm64_v8a`).

## Layout

```
arm64-v8a/libplugins_platforms_qtforandroid_arm64-v8a.so   (the patched plugin; binary)
```

The `android-release.yml` workflow copies that file over
`$QT_ROOT_DIR/plugins/platforms/libplugins_platforms_qtforandroid_arm64-v8a.so`
after Qt is installed and before `qt-cmake`, so `androiddeployqt` bundles our
build. The step is a no-op if the file is absent (safe before it's committed).

## ⚠️ Pinned to the exact Qt version

The plugin is **ABI-locked to the Qt version Decenza builds against**
(currently **6.11.1**, qtbase commit `59c81a3c2247b821b9b84b4eb8d939b77e07e276`).
When the Qt version is bumped in `android-release.yml`, this `.so` **must be
rebuilt from the matching Qt source** or removed — a stale plugin against a
different Qt will crash the app at startup.

If the upstream bugs are fixed in the new Qt version, **delete this override**
(the `.so`, the jar, `BUILT_AGAINST_QT`, and the workflow step) instead of
rebuilding.

**This is enforced, not just documented.** `BUILT_AGAINST_QT` records the Qt
version the artifacts were built from, and the workflow's override step fails
the build when it does not match `env.QT_VERSION` in `android-release.yml`. So
bumping Qt without dealing with the override gives you a red build, not an APK
that dies at startup on every device. After rebuilding, update
`BUILT_AGAINST_QT` in the same commit as the new binaries.

### What to watch for the crash fix

The upstream fix is [Gerrit 735089](https://codereview.qt-project.org/c/qt/qtbase/+/735089),
*"Android: drop deadlock protector from EGL/Vk surface paths"* — it removes the
protector from these paths entirely rather than just declawing the abort. As of
2026-07-27 it is **unmerged**: seventh in a 17-change series, no human reviewer
assigned, no `Pick-to:` footer, so it targets `dev` only. Our patch is a
stop-gap for exactly that gap. Drop it once 735089 (or an equivalent) reaches
the Qt version we build against — check before every Qt bump.

## How the `.so` is built

From the fork `github.com/skialpine/qtbase`, branch
`a11y/android-talkback-fixes` — 13 commits on top of **`59c81a3c`** (the qtbase
commit for 6.11.1; the clone is shallow and has no tags, so use the SHA, not
`v6.11.1`). The crash fix is `032d3941`, the rest a11y:

1. Configure that qtbase for Android with Qt's own configure (matches the
   official feature flags — do NOT hand-roll a standalone CMake for a shipped
   platform plugin; a mismatched feature define can crash devices):
   ```
   <qtbase>/configure -android-ndk <ndk> -android-sdk <sdk> \
       -qt-host-path <host Qt 6.11.1> -android-abis arm64-v8a \
       -nomake examples -nomake tests -- -DCMAKE_BUILD_TYPE=Release
   ```
2. Build just the platform-plugin target:
   ```
   cmake --build . --target QAndroidIntegrationPlugin
   ```
3. Copy the result here:
   ```
   cp .../plugins/platforms/libplugins_platforms_qtforandroid_arm64-v8a.so \
      android/qt-overrides/arm64-v8a/
   ```

On Jeff's Mac (2026-07-27) `cmake`/`ninja` are not on `PATH`; Qt ships them at
`~/Qt/Tools/CMake/CMake.app/Contents/bin` and `~/Qt/Tools/Ninja`. Host Qt is
`~/Qt/6.11.1/macos`, NDK `~/Library/Android/sdk/ndk/27.2.12479018` (same version
the Android workflow pins). The build tree lives outside the source tree at
`~/Development/GitHub/qtbase-android-build`; once configured, a rebuild after a
source change is minutes, not hours.

## Verifying an override actually contains the fixes

The `.so` is a binary — a diff tells you nothing. Check the strings instead:

```
strings android/qt-overrides/arm64-v8a/libplugins_platforms_qtforandroid_arm64-v8a.so \
  | grep -iE "deadlock protector|DecenzaQPA"
```

- Crash fix present → `Could not acquire deadlock protector for %s; skipping
  surface creation for this frame.`
  Stock Qt instead has → `Failed to acquire deadlock protector for %s.`
- A11y fixes present → one or more `[DecenzaQPA-echo] …` lines.

Validate on-device with TalkBack before relying on the a11y half (the
`[a11y-dbg]` logging in the app prints the keyboard show/hide + focus trace).

The crash fix cannot be staged on demand — it needs the protector contended at
the instant of a surface re-create. The honest verification is shipping it and
watching whether the crash reports in #1663 stop.
