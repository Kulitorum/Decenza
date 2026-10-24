# Qt Wasm media lifecycle reproduction

This standalone Qt 6.12.0 WebAssembly project tests whether destroying a QML
`MediaPlayer` and `VideoOutput` removes the HTML `<video>` element created by
Qt Multimedia. The default run needs no media file and makes no network request.

Open this directory as a CMake project in Qt Creator and select the same Qt
6.12.0 `wasm_singlethread` kit used for the stock Qt Multimedia Wasm package.
The CMake project uses `build/qtmultimedia-wasm-install` by default; override
`DECENZA_QTMULTIMEDIA_PREFIX` if that package is elsewhere. Build and run the
generated web bundle from an HTTP server. Click **Run 50 create/destroy
cycles**, then inspect the browser console. The final line reports the number
of `video` elements in the document. The diagnostics button also reports
`canPlayType()` results for H.264 baseline/main/high and WebM VP9 plus the
most recently created video's HTML media error code/message, ready state, network state, and
paused state. It intentionally omits media URLs.

An optional same-origin URL can be entered to test playback. It is not needed
for the lifecycle case and the diagnostic never logs the URL.

For the premature-completion case, enter a playable same-origin MP4 and click
**Load and play URL** without running destruction cycles. The console logs
Qt media status and HTML `ended`, `currentTime`, duration and readiness. On
Qt 6.12.0, the first `EndOfMedia` arrived at position/duration zero, with HTML
`ended=false`, before loading and normal playback. Reported as
[QTBUG-151769](https://qt-project.atlassian.net/browse/QTBUG-151769) with a
source-only attachment. The separate retention report is
[QTBUG-151767](https://qt-project.atlassian.net/browse/QTBUG-151767).

## Verified on 2026-10-22

Qt Creator Release build `1792683623282` passed with no warnings using Qt
6.12.0 wasm_singlethread. In the Mac in-app Chromium browser, the initial DOM
contained one video element; one run of 50 replacement cycles left 51 elements.
No media URL or network request was needed for that result. A separate playback
check of a cached H.264 MP4 succeeded (`readyState: 4`, `paused: false`, no HTML
media error), and all three H.264 `canPlayType` probes returned `probably`.

The destruction result is a DOM-retention reproduction, not a measured native
heap allocation total. Qt's destructor does not remove its appended video node.
A retained output in the application bounds growth during playlist changes;
page/player destruction still needs an upstream fix or a lifecycle workaround.
