# Qt Wasm media investigations

Confirmed premature completion: [QTBUG-151769](https://qt-project.atlassian.net/browse/QTBUG-151769), filed with a standalone source reproduction on 2026-10-22. Assigning a playable same-origin MP4 and calling `play()` emits `EndOfMedia` before `LoadingMedia`, with position/duration zero and HTML `ended=false`. Qt 6.12's `qwasmvideooutput.cpp:825-834` maps `emptied` to `EndOfMedia`. The shared screensaver ignores completion until the current media has loaded; live app validation remains pending.

Separate confirmed lifecycle defect: [QTBUG-151767](https://qt-project.atlassian.net/browse/QTBUG-151767), filed with the source-only reproduction in `media-repro/`. Fifty player/output replacements leave 51 HTML video elements; repeating leaves 101. This does not establish the autoplay hypothesis below or quantify heap retention.

Autoplay-only hypothesis below remains unconfirmed; neither filed issue establishes it. Do not file an autoplay bug from this evidence alone.

## Minimal reproduction

Run this QML in a Qt 6.12 Wasm app with a same-origin MP4 that has an audio track, before the user interacts with the page:

```qml
import QtQuick
import QtMultimedia

Item {
    MediaPlayer {
        id: player
        source: "media-with-audio.mp4"
        videoOutput: output
        // No AudioOutput is assigned.
        Component.onCompleted: play()
        onErrorOccurred: function(error, errorString) {
            console.log("playback error", error, errorString)
        }
    }

    VideoOutput {
        id: output
        anchors.fill: parent
    }
}
```

Record whether `play()` reaches `PlayingState`, the browser console's DOMException, and whether playback starts after a user click. Repeat with an `AudioOutput { id: silentOutput; muted: true }` assigned before setting the source or calling `play()`. That comparison separates a browser autoplay-policy failure from source loading, codec support, and CORS issues.

## Source evidence

The checked-in Qt 6.12.0 source documents `QMediaPlayer::setAudioOutput(nullptr)` as disabling audio output (`qtmultimedia/src/multimedia/playback/qmediaplayer.cpp:707-713`). The Wasm video output creates an HTML `<video>` element and does not set its `muted` property in that setup path (`qtmultimedia/src/plugins/multimedia/wasm/common/qwasmvideooutput.cpp:566-605`). When an `AudioOutput` is present, the Wasm audio-output implementation can set `muted` on its associated video element (`qtmultimedia/src/plugins/multimedia/wasm/common/qwasmaudiooutput.cpp:45-50`).

These paths suggest that disabling Qt audio output may leave the browser element unmuted, which can make autoplay policy reject `play()`. The app-level workaround under review is to create an `AudioOutput`, set it muted after attaching the video output, and only then assign the source and play. The standalone comparison above has not yet been run, so the behavior and any upstream defect remain unconfirmed.

## Upstream status

Searches for the autoplay/muted Wasm behavior did not identify a matching Qt issue. No upstream bug has been filed. Re-check the reproduction and search the Qt Multimedia tracker before reporting this as a confirmed issue.
