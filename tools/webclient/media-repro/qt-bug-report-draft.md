# Draft Qt Multimedia report: Wasm video element survives player destruction

**Status:** standalone reproduction confirmed on Qt 6.12.0 Wasm, 2026-10-22.
Filed as [QTBUG-151767](https://qt-project.atlassian.net/browse/QTBUG-151767), with the standalone source ZIP attached. Tracker searches found no matching lifecycle report; QTBUG-151746 concerns a separate no-`exec()` deferred-deletion scenario.

**Title:** Qt Multimedia Wasm leaves an HTML video element in the document when a MediaPlayer is destroyed

**Affected component/version:** Qt Multimedia 6.12.0, WebAssembly backend.

**Description:** The Wasm backend creates an HTML `<video>` element and appends
it to `document.body`. Destroying the QML `MediaPlayer` destroys its
`QWasmVideoOutput`, but that destructor does not remove the DOM element. Each
new output instance creates a new element, so applications that repeatedly
create and destroy players can leave an increasing number of video elements
and retain associated DOM nodes. Retained decoder/heap allocation size was not measured.

**Reproduction:** Build the accompanying standalone Qt Wasm project, serve its
output over HTTP, open it in a browser, and click **Run 50 create/destroy
cycles**. The project creates no media request by default. Compare the initial
`video` node count with the final console diagnostic.

**Expected:** Destroying a `MediaPlayer` and its output removes the associated
video element, or the backend otherwise reuses/cleans up that element.

**Observed:** Initial count 1; after 50 replacement cycles, count 51. No
media or network request was used. Qt Creator Release build completed without
warnings on macOS. Separate H.264 playback succeeded in the same browser.

**Source references:** `QWasmVideoOutput::createVideoElement()` appends the
element to `document.body` in Qt Multimedia's
`src/plugins/multimedia/wasm/common/qwasmvideooutput.cpp`; its destructor
currently releases camera state only. `QWasmMediaPlayer::~QWasmMediaPlayer()`
deletes the output.

**Attachments:** standalone CMake/QML reproduction in this directory. No
application-specific sources or data are required.

## Separate candidate: HTML media error code mapping

Qt's Wasm video `error` event forwards `HTMLMediaElement.error.code` as an
integer and a player callback casts that integer directly to
`QMediaPlayer::Error`. Those enums have different meanings at values 2–4. A
runtime failure can therefore be surfaced as the wrong Qt error category.
The standalone project reports the browser's raw error code and message;
add a Qt error-signal diagnostic before testing the mapping. Keep this
as a separate report only if a playback run reproduces a misclassification;
the no-media lifecycle test alone does not confirm it.
