# Qt Android platform-plugin override

This directory holds a **patched Qt Android platform plugin** that the Android
CI build drops in over the stock one before packaging, to fix two TalkBack
screen-reader bugs that are unfixed upstream (see GitHub issue #1300):

- **QTBUG-118858** — typed/deleted characters are not spoken in editable fields.
- **QTBUG-145786** — the keyboard opens the instant a field gets accessibility
  focus (focus trap).

The fix spans **two** Qt artifacts and BOTH must be overridden:
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
(the `.so` and the workflow step) instead of rebuilding.

## How the `.so` is built

From the fork `github.com/skialpine/qtbase`, branch
`a11y/android-talkback-fixes` (the two a11y commits on top of `v6.11.1`):

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

Validate on-device with TalkBack before relying on it (the `[a11y-dbg]` logging
in the app prints the keyboard show/hide + focus trace).
