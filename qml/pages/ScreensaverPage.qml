import QtQuick
import QtQuick.Controls
import QtMultimedia
import DecenzaDE1
import "../components"

Page {
    id: screensaverPage
    objectName: "screensaverPage"
    background: Rectangle { color: "black" }

    property int videoFailCount: 0
    property bool mediaPlaying: false
    property bool isCurrentItemImage: false
    property string lastFailedSource: ""
    property string currentImageSource: ""
    property bool useFirstImage: true  // Toggle for cross-fade between two images

    Component.onCompleted: {
        // Keep screen on while screensaver is active
        ScreensaverManager.setKeepScreenOn(true)
        playNextMedia()
    }

    // Listen for new media becoming available (downloaded)
    Connections {
        target: ScreensaverManager
        function onVideoReady(path) {
            // Media just finished downloading - try to play if we're showing fallback
            if (!mediaPlaying) {
                console.log("[Screensaver] New media ready, starting playback")
                playNextMedia()
            }
        }
        function onCatalogUpdated() {
            // Catalog loaded - try to play if we're showing fallback
            if (!mediaPlaying && ScreensaverManager.itemCount > 0) {
                console.log("[Screensaver] Catalog updated, trying playback")
                playNextMedia()
            }
        }
    }

    function playNextMedia() {
        if (!ScreensaverManager.enabled) {
            return
        }

        var source = ScreensaverManager.getNextVideoSource()
        if (source && source.length > 0) {
            isCurrentItemImage = ScreensaverManager.currentItemIsImage
            console.log("[Screensaver] Playing " + (isCurrentItemImage ? "image" : "video") + ":", source)

            if (isCurrentItemImage) {
                // Display image with cross-fade transition
                mediaPlayer.stop()
                mediaPlaying = true

                // Load into the inactive image, then cross-fade
                if (useFirstImage) {
                    imageDisplay1.source = source
                } else {
                    imageDisplay2.source = source
                }
                currentImageSource = source
                // Cross-fade will be triggered when image loads (onStatusChanged)
            } else {
                // Play video - reset image state
                imageDisplayTimer.stop()
                currentImageSource = ""
                useFirstImage = true
                imageDisplay1.source = ""
                imageDisplay2.source = ""
                mediaPlaying = true
                mediaPlayer.source = source
                mediaPlayer.play()
            }
        } else {
            // No cached media yet - show fallback, wait for downloads
            console.log("[Screensaver] No cached media yet, showing fallback")
            mediaPlaying = false
            isCurrentItemImage = false
        }
    }

    function handleVideoFailure() {
        // Prevent handling the same failure twice
        var currentSource = mediaPlayer.source.toString()
        if (currentSource === lastFailedSource) return
        lastFailedSource = currentSource

        videoFailCount++
        console.log("[Screensaver] Video failed (" + videoFailCount + ")")

        if (videoFailCount >= 5) {
            console.log("[Screensaver] Too many failures, showing fallback")
            mediaPlaying = false
            mediaPlayer.stop()
            videoFailCount = 0  // Reset for when new media downloads
            return
        }

        // Try next cached media
        playNextMedia()
    }

    // Timer for image display duration
    Timer {
        id: imageDisplayTimer
        interval: ScreensaverManager.imageDisplayDuration * 1000
        repeat: false
        onTriggered: {
            // Mark current image as played for LRU tracking
            if (currentImageSource.length > 0) {
                ScreensaverManager.markVideoPlayed(currentImageSource)
            }
            videoFailCount = 0
            lastFailedSource = ""
            // Toggle to other image for cross-fade effect
            useFirstImage = !useFirstImage
            playNextMedia()
        }
    }

    MediaPlayer {
        id: mediaPlayer
        audioOutput: AudioOutput { volume: 0 }  // Muted
        videoOutput: videoOutput

        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia) {
                // Mark current video as played for LRU tracking
                ScreensaverManager.markVideoPlayed(source.toString())
                // Play next media (reset fail count on success)
                videoFailCount = 0
                lastFailedSource = ""
                playNextMedia()
            } else if (mediaStatus === MediaPlayer.InvalidMedia ||
                       mediaStatus === MediaPlayer.NoMedia) {
                handleVideoFailure()
            }
        }

        onErrorOccurred: {
            handleVideoFailure()
        }

        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.PlayingState) {
                videoFailCount = 0
                lastFailedSource = ""
            }
        }
    }

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop
        visible: mediaPlaying && !isCurrentItemImage
    }

    // Image display with cross-fade transition
    Item {
        id: imageContainer
        anchors.fill: parent
        visible: mediaPlaying && isCurrentItemImage

        // Two images for cross-fade effect (2 second dissolve)
        Image {
            id: imageDisplay1
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            opacity: useFirstImage ? 1.0 : 0.0

            Behavior on opacity {
                NumberAnimation { duration: 2000; easing.type: Easing.InOutQuad }
            }

            onStatusChanged: {
                if (status === Image.Ready && useFirstImage && source.toString().length > 0) {
                    // Image loaded, start display timer
                    imageDisplayTimer.restart()
                }
            }
        }

        Image {
            id: imageDisplay2
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            opacity: useFirstImage ? 0.0 : 1.0

            Behavior on opacity {
                NumberAnimation { duration: 2000; easing.type: Easing.InOutQuad }
            }

            onStatusChanged: {
                if (status === Image.Ready && !useFirstImage && source.toString().length > 0) {
                    // Image loaded, start display timer
                    imageDisplayTimer.restart()
                }
            }
        }
    }

    // Fallback: show a subtle animation while no cached media
    Rectangle {
        id: fallbackBackground
        anchors.fill: parent
        visible: !mediaPlaying || (!isCurrentItemImage && mediaPlayer.playbackState !== MediaPlayer.PlayingState)
        z: 1

        Rectangle {
            id: gradientRect
            anchors.fill: parent
            property real gradientHue: 0.6

            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: Qt.hsla(gradientRect.gradientHue, 0.4, 0.15, 1.0)
                }
                GradientStop {
                    position: 1.0
                    color: Qt.hsla((gradientRect.gradientHue + 0.5) % 1.0, 0.4, 0.08, 1.0)
                }
            }

            NumberAnimation on gradientHue {
                from: 0
                to: 1
                duration: 30000
                loops: Animation.Infinite
            }
        }
    }

    // Credits display at bottom (one-liner for current media)
    // For personal media with showDateOnPersonal enabled, shows upload date instead
    Rectangle {
        z: 2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.scaled(40)
        color: Qt.rgba(0, 0, 0, 0.5)

        property bool showDate: ScreensaverManager.isPersonalCategory &&
                               ScreensaverManager.showDateOnPersonal &&
                               ScreensaverManager.currentMediaDate.length > 0

        visible: (showDate || ScreensaverManager.currentVideoAuthor.length > 0) &&
                 (mediaPlayer.playbackState === MediaPlayer.PlayingState ||
                  (isCurrentItemImage && mediaPlaying))

        Text {
            anchors.centerIn: parent
            text: {
                if (parent.showDate) {
                    return ScreensaverManager.currentMediaDate
                } else if (isCurrentItemImage) {
                    return TranslationManager.translate("screensaver.photo_by", "Photo by %1 (Pexels)")
                           .arg(ScreensaverManager.currentVideoAuthor)
                } else {
                    return TranslationManager.translate("screensaver.video_by", "Video by %1 (Pexels)")
                           .arg(ScreensaverManager.currentVideoAuthor)
                }
            }
            color: "white"
            opacity: 0.7
            font.pixelSize: Theme.scaled(14)
        }
    }

    // Clock display
    Text {
        z: 2
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: Theme.scaled(50)
        anchors.bottomMargin: Theme.chartMarginLarge + Theme.scaled(20)  // Above credits bar
        text: Qt.formatTime(currentTime, "hh:mm")
        color: "white"
        opacity: 0.8
        font.pixelSize: Theme.scaled(80)
        font.weight: Font.Light

        property date currentTime: new Date()

        Timer {
            interval: 1000
            running: true
            repeat: true
            onTriggered: parent.currentTime = new Date()
        }
    }

    // Download progress indicator (subtle)
    Rectangle {
        z: 2
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.scaled(3)
        color: "transparent"
        visible: ScreensaverManager.isDownloading

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * ScreensaverManager.downloadProgress
            color: Theme.primaryColor
            opacity: 0.6

            Behavior on width {
                NumberAnimation { duration: 300 }
            }
        }
    }

    // Touch hint (fades out)
    Tr {
        id: touchHint
        z: 2
        anchors.centerIn: parent
        key: "screensaver.touch_to_wake"
        fallback: "Touch to wake"
        color: "white"
        opacity: 0.5
        font.pixelSize: Theme.scaled(24)

        OpacityAnimator {
            target: touchHint
            from: 0.5
            to: 0
            duration: 3000
            running: true
        }
    }

    // Touch anywhere to wake
    MouseArea {
        z: 3
        anchors.fill: parent
        onClicked: wake()
        onPressed: wake()
    }

    // Also wake on key press
    Keys.onPressed: wake()

    function wake() {
        // Wake up the DE1
        if (DE1Device.connected) {
            DE1Device.wakeUp()
        }

        // Wake the scale (enable LCD) or try to reconnect
        if (ScaleDevice && ScaleDevice.connected) {
            ScaleDevice.wake()
        } else {
            BLEManager.tryDirectConnectToScale()
        }

        // Suppress scale dialogs briefly after waking
        root.justWokeFromSleep = true
        wakeSuppressionTimer.start()

        // Navigate back to idle
        root.goToIdleFromScreensaver()
    }

    // Clean up media when page is being removed
    StackView.onRemoved: {
        mediaPlayer.stop()
        imageDisplayTimer.stop()
        // Allow screen to turn off again
        ScreensaverManager.setKeepScreenOn(false)
    }

    // Auto-wake when DE1 wakes up externally (button press on machine)
    Connections {
        target: DE1Device
        function onStateChanged() {
            var state = DE1Device.stateString
            if (state !== "Sleep" && state !== "GoingToSleep") {
                if (ScaleDevice && ScaleDevice.connected) {
                    ScaleDevice.wake()
                } else {
                    BLEManager.tryDirectConnectToScale()
                }
                // Suppress scale dialogs briefly after waking
                root.justWokeFromSleep = true
                wakeSuppressionTimer.start()
                // Navigate back to idle
                root.goToIdleFromScreensaver()
            }
        }
    }
}
