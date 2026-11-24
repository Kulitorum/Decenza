# Qt workarounds

Every place Decenza works around a Qt defect or missing Qt feature, with the Qt issue that tracks it. CLAUDE.md's rule: no new row without a verified defect and a QTBUG (a missing feature or a build-speed workaround says why instead), and on each Qt upgrade check every issue's status and remove what the new release fixes. Line numbers drift; search for the cited QTBUG.

Last checked against Qt 6.12.1 (2026-11-23). A report is filed when we are at least 75% sure it is a Qt bug rather than intended behaviour.

## Kept

| Where | Qt problem | QTBUG | Status (fix version) |
|---|---|---|---|
| `resources/webclient/index.html` (srcObject shim) | Wasm/iOS: `getVideoTracks()` called on a null `srcObject` | QTBUG-151807 | Open |
| `resources/webclient/index.html` (`playsinline`) | Wasm: the hidden `<video>` lacks `playsinline`, so iOS plays it outside the page | QTBUG-152548 | Reported |
| `resources/webclient/index.html` (`touch-action`) | Wasm: `.qt-screen div { touch-action: none }` blocks browser pinch-zoom, with no opt-out | None: feature request, not filed | |
| `resources/webclient/index.html` (16px inputs) | Wasm: shadow inputs use a small default font, so iPhone Safari zooms on focus | None: not filed (Qt's own page disables zoom; confirm on iPhone first) | |
| `resources/webclient/qt-dialog-accessibility.js` | Wasm: `<dialog>` is never `open`, so its content is hidden from assistive technology | QTBUG-144330 | In Progress |
| `src/webclient/main.cpp`, `src/remote/browserdisplay.cpp` (wheel) | Wasm: pixel deltas also sent as angle deltas, with no scroll phases | QTBUG-141540 | In Review |
| `qml/pages/ScreensaverPage.qml` (`emptied` filter) | Wasm: `emptied` reported as EndOfMedia on load | QTBUG-151769 | Fixed (6.13) |
| `qml/pages/ScreensaverPage.qml` (mute order) | Wasm: `AudioOutput.muted` set before the video element is attached is lost | QTBUG-152549 | Reported |
| `qml/components/SharedTextField.qml`, `src/core/accessiblevaluechange.*` | Wasm: programmatic text changes (TextUpdated) never reach the shadow input | QTBUG-151711 | Fixed (6.12.2) |
| `qml/components/SharedSuggestionFieldChrome.qml` | Wasm: accessible children of an EditableText land inside its `<input>` and are lost | QTBUG-152558 | Reported |
| `qml/components/SharedSuggestionField.qml` (iOS tap) | iOS: `QIOSTapRecognizer`'s queued block reads a stale `_focusView` and crashes | QTBUG-146020 | Reported |
| `src/remote/systemlogservice.cpp` | QtRO: a second in-process replica of a live source gets no signals | QTBUG-152550 | Reported |
| `src/remote/remoteservice.h` | QtRO: an in-process replica never reaches `State::Valid` | QTBUG-152555 | Reported |
| `src/remote/localfileselection.cpp` | Wasm: `weblocalfile:` QFile reads block the main thread (`suspendExclusive`) | None: feature request, not filed | |
| `src/controllers/maincontroller.h` (QML_FOREIGN wrappers) | QML: `new T` is preferred over `T::create()` when T is default-constructible | QTBUG-119369 | Reported |
| `qml/components/OperationCompletionOverlay.qml` | qmlcachegen segfaults on the call inside the bool binding | QTBUG-152061 | Fixed (6.12.2) |
| `qml/components/graphs/DecenzaGraphsTheme.qml` | Qt Graphs: no tick-length setting | QTBUG-151286 | Open |
| `src/history/coffeebagstorage.cpp` | QSQLITE: `numRowsAffected()` reports the connection's latest statement | QTBUG-152551 | Reported |
| `src/core/markdownrenderer.h` | Markdown import: an inline `<img>` without `/>` swallows the rest of the document | QTBUG-134796 | Reported |
| `src/main.cpp`, `DecenzaActivity.java` (BLE binder death) | Android QtBluetoothLE: DeadObject in `writeCharacteristic`/`writeDescriptor` kills the app | QTBUG-152552 | Reported |
| `src/ble/blegattqueue.h` | Android QtBluetoothLE: a GATT timeout emits no error | QTBUG-152556 | Reported |
| `DecenzaActivity.java` (alias launch) | Android: an `activity-alias` launch skips loading Qt, then crashes in `onResume` (regression from the QTBUG-142822 fix) | QTBUG-152557 | Reported |
| `src/core/permissionrequests.h` | Android: a cancelled permission request is reported as Granted and cached | QTBUG-152553 | Reported |
| `src/network/locationprovider.cpp` | CoreLocation backend reports every error as AccessError | QTBUG-152554 | Reported |
| `src/ble/applebtstate.h` | iOS has no working QBluetoothLocalDevice | QTBUG-152204 | Reported |
| `src/ble/scales/decentscalewifi.cpp`, `src/network/mdnsresolver.h` | No `.local` resolution on Android; no DNS-SD browse | QTBUG-97534, QTBUG-30823 | Open |
| `cmake/qmlcachegen_stable.py` | qmlcachegen rewrites byte-identical output, defeating Ninja restat | None: build-speed request, not a defect | |
| `cmake/QmlCacheGeneration.cmake` | Cachegen units do not depend on the QML files they read types from | QTBUG-110961 | Closed: Qt added a runtime check, so a stale unit now runs interpreted instead of failing |
| `cmake/remote_object_replicas.cmake` | repc replica moc emits no metatypes JSON | QTBUG-130903 | Fixed on dev (expect 6.13) |
| `CMakeLists.txt` (per-directory includes) | qmltyperegistrar includes generated headers by basename | QTBUG-93443 | Reported |
| `tests/CMakeLists.txt` (duplicate URI) | Two modules with one URI in a directory overwrite each other's qmldir resource | QTBUG-123169 | Reported |
| `.github/qt-modules.txt` (qttasktree) | Qt6Qml pulls in AssetDownloader, which requires TaskTree | QTBUG-151927 | Reported |
| `src/webclient/CMakeLists.txt`, `release.yml` | No Multimedia or Remote Objects package for `wasm_singlethread` | QTBUG-137351 (Multimedia); QTBUG-145598 (RO, out of scope) | Closed, still unshipped |
| `cmake/webclient_bundle.cmake` (Xcode) | `BIG_RESOURCES` does not work with the Xcode generator | QTBUG-103497 | Documented |

## Possibly no longer needed (needs a device or CI check)

| Where | Why it may be fixed | Check |
|---|---|---|
| `src/main.cpp` (iOS clipboard `blockSignals`) | QTBUG-149610 fixed in 6.12.1: text fields only test formats | iPhone: remove it, copy in Safari, return; no "Allow Paste" prompt means remove |
| `src/main.cpp` (Android `QAccessible::setActive(false)` on suspend) | `eglSurface()` takes no lock in 6.12.1; QTBUG-140674 and QTBUG-140490 fixed | TalkBack tablet: background and resume |
| `resources/webclient/index.html` (resize hook) | 6.12.1 observes container resizes itself | iPhone Safari: rotate, keyboard, back-forward cache |
| `qml/pages/ScreensaverPage.qml` (suspend gating) | QTBUG-118231 fixed in 6.9 | Tablet: screensaver video, power off and on, check for ANR |
| `src/main.cpp` (iOS `QSG_RENDER_LOOP=basic`) | No UIKit call from Qt Multimedia's render path in 6.12.1 | iPad: cycle screensaver videos |
| `src/main.cpp`, `CMakeLists.txt` (FFmpeg backend off on Apple) | Leak not decidable from source | macOS with `QT_MEDIA_BACKEND=ffmpeg`: 20 videos, watch memory |

## Our code, not Qt's: change it rather than work around

| Where | What we do against Qt's design | Qt's intended way |
|---|---|---|
| `qml/main.qml` (singleton guard) | A singleton can be registered with no instance, which reads as truthy | Always provide an instance |
| `qml/components/layout/items/ShotPlanItem.qml` | Changes `Accessible.role` at runtime; Qt cannot signal role changes | A fixed role per item |
| `qml/pages/ScreensaverPage.qml` | Reads `Qt.application.state`, which is not guaranteed to be a QQuickApplication (QTBUG-119643) | The QtQuick `Application` singleton |
| `src/history/shotprojection.h`, `src/ai/aimanager.cpp` | Passes JS objects to `QVariantMap` parameters through a registered converter, a compatibility-only path | `QML_VALUE_TYPE` / `QML_STRUCTURED_VALUE` |
