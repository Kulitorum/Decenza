import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

ApplicationWindow {
    id: window
    width: 800
    height: 520
    visible: true
    title: "Qt Wasm Media Lifecycle Reproduction"
    color: "#20242a"

    property bool runningCycles: false
    property int targetCycles: 50
    property int completedCycles: 0

    function startCycles() {
        if (runningCycles)
            return
        completedCycles = 0
        runningCycles = true
        mediaLoader.active = false
    }

    function reportCount(label) {
        const count = MediaDiagnostics.videoElementCount()
        console.log("[QtWasmMediaLifecycleRepro] " + label + ": " + count + " video element(s)")
        return count
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14

        Label {
            Layout.fillWidth: true
            text: "Repeatedly create and destroy a MediaPlayer with VideoOutput. No media or network request is used."
            color: "white"
            wrapMode: Text.WordWrap
            font.pixelSize: 20
        }

        Label {
            Layout.fillWidth: true
            text: window.runningCycles
                  ? "Creating and destroying player " + window.completedCycles + " / " + window.targetCycles
                  : "Completed cycles: " + window.completedCycles
            color: "#d5dbe3"
            font.pixelSize: 16
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                text: "Run 50 create/destroy cycles"
                enabled: !window.runningCycles
                onClicked: window.startCycles()
            }
            Button {
                text: "Log browser media diagnostics"
                onClicked: console.log("[QtWasmMediaLifecycleRepro] browser media: " + MediaDiagnostics.browserMediaReport())
            }
        }

        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: sourceUrl
                Layout.fillWidth: true
                placeholderText: "Optional same-origin media URL (not logged)"
                selectByMouse: true
            }
            Button {
                text: "Load and play URL"
                enabled: !window.runningCycles && mediaLoader.item !== null && sourceUrl.text.length > 0
                onClicked: {
                    mediaLoader.item.player.source = sourceUrl.text
                    mediaLoader.item.player.play()
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: "Open the browser console. Each cycle creates one QMediaPlayer and VideoOutput, then destroys both. The final diagnostic reports document.querySelectorAll('video').length."
            color: "#aeb8c4"
            wrapMode: Text.WordWrap
        }
    }

    Loader {
        id: mediaLoader
        active: true
        sourceComponent: Item {
            property alias player: mediaPlayer

            MediaPlayer {
                id: mediaPlayer
                videoOutput: output
                onMediaStatusChanged: {
                    console.log("[QtWasmMediaLifecycleRepro] media status=" + mediaStatus
                        + " position=" + position + " duration=" + duration)
                    if (mediaStatus === MediaPlayer.EndOfMedia)
                        console.log("[QtWasmMediaLifecycleRepro] EndOfMedia browser state: "
                            + MediaDiagnostics.browserMediaReport())
                }
                onErrorOccurred: function(error, errorString) {
                    console.log("[QtWasmMediaLifecycleRepro] error=" + error + ": " + errorString)
                }
            }
            VideoOutput {
                id: output
                visible: false
                width: 1
                height: 1
            }
        }

        onItemChanged: {
            if (!window.runningCycles)
                return
            if (item === null) {
                Qt.callLater(function() { mediaLoader.active = true })
                return
            }

            window.completedCycles++
            if (window.completedCycles < window.targetCycles) {
                Qt.callLater(function() { mediaLoader.active = false })
            } else {
                window.runningCycles = false
                Qt.callLater(function() { window.reportCount("after " + window.completedCycles + " cycles") })
            }
        }
    }
}
