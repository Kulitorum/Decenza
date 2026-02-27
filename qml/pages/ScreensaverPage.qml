import QtQuick
import QtQuick.Controls
import QtMultimedia
import DecenzaDE1
import "../components"

// Screensaver modes:
// "disabled"  - Turn screen off (let system timeout handle it)
// "videos"    - Video/image slideshow from catalog
// "pipes"     - Classic 3D pipes animation
// "flipclock" - Classic flip clock display
// "attractor" - Strange attractor visualization
// "shotmap"   - Shot location map

Page {
    id: screensaverPage
    objectName: "screensaverPage"
    background: Rectangle { color: "black" }

    // Current screensaver mode
    property string screensaverType: ScreensaverManager.screensaverType
    property bool isVideosMode: screensaverType === "videos"
    property bool isPipesMode: screensaverType === "pipes"
    property bool isFlipClockMode: screensaverType === "flipclock"
    property bool isAttractorMode: screensaverType === "attractor"
    property bool isDisabledMode: screensaverType === "disabled"
    property bool isShotMapMode: screensaverType === "shotmap"

    property int videoFailCount: 0
    property bool mediaPlaying: false
    property bool isCurrentItemImage: false
    property string lastFailedSource: ""
    property string currentImageSource: ""
    property bool useFirstImage: true  // Toggle for cross-fade between two images

    Component.onCompleted: {
        console.log("[ScreensaverPage] Loaded, type:", screensaverType,
                    "videos:", isVideosMode, "pipes:", isPipesMode, "flipclock:", isFlipClockMode,
                    "disabled:", isDisabledMode)
        // For "disabled" mode, turn OFF keep-screen-on to let Android's
        // system timeout turn off the screen naturally
        if (isDisabledMode) {
            ScreensaverManager.setKeepScreenOn(false)
        }
        if (isVideosMode) {
            playNextMedia()
        }
        // Start screen dimming if configured
        if (!isDisabledMode && ScreensaverManager.dimPercent > 0) {
            startDimming()
        }
    }

    function applyDim() {
        dimOverlay.opacity = ScreensaverManager.dimPercent / 100.0
    }

    function startDimming() {
        if (ScreensaverManager.dimDelayMinutes === 0) {
            applyDim()
        } else {
            dimTimer.restart()
        }
    }

    // Listen for new media becoming available (downloaded) and screen dimming changes
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
        function onDimPercentChanged() {
            if (ScreensaverManager.dimPercent === 0) {
                dimTimer.stop()
                dimOverlay.opacity = 0
            } else if (dimOverlay.opacity > 0) {
                applyDim()
            } else {
                startDimming()
            }
        }
        function onDimDelayMinutesChanged() {
            // Only restart if dim hasn't triggered yet
            if (dimOverlay.opacity === 0 && ScreensaverManager.dimPercent > 0 && !isDisabledMode) {
                dimTimer.stop()
                startDimming()
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
        console.warn("[Screensaver] Media failed (" + videoFailCount + "/5):", currentSource)

        if (videoFailCount >= 5) {
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

        onErrorOccurred: function(error, errorString) {
            console.warn("[Screensaver] MediaPlayer error:", error, errorString)
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
        visible: isVideosMode && mediaPlaying && !isCurrentItemImage
    }

    // Image display with cross-fade transition
    Item {
        id: imageContainer
        anchors.fill: parent
        visible: isVideosMode && mediaPlaying && isCurrentItemImage

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

    // 3D Pipes screensaver (requires Quick3D)
    Loader {
        id: pipesLoader
        anchors.fill: parent
        active: Settings.hasQuick3D && isPipesMode
        visible: isPipesMode
        z: 0
        source: "qrc:/qt/qml/DecenzaDE1/qml/components/PipesScreensaver.qml"
        onLoaded: item.running = Qt.binding(function() { return isPipesMode && screensaverPage.visible })
    }

    // Flip Clock screensaver
    FlipClockScreensaver {
        id: flipClockScreensaver
        anchors.fill: parent
        visible: isFlipClockMode
        running: isFlipClockMode && screensaverPage.visible
        z: 0
    }

    // Strange Attractor screensaver
    StrangeAttractorScreensaver {
        id: attractorScreensaver
        anchors.fill: parent
        visible: isAttractorMode
        running: isAttractorMode && screensaverPage.visible
        z: 0
    }

    // Shot Map screensaver (flat map works without Quick3D, globe loaded conditionally)
    ShotMapScreensaver {
        id: shotMapScreensaver
        anchors.fill: parent
        visible: isShotMapMode
        running: isShotMapMode && screensaverPage.visible
        z: 0
    }

    // Fallback: show a subtle animation while no cached media (videos mode only)
    Rectangle {
        id: fallbackBackground
        anchors.fill: parent
        visible: isVideosMode && (!mediaPlaying || (!isCurrentItemImage && mediaPlayer.playbackState !== MediaPlayer.PlayingState))
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

        visible: isVideosMode &&
                 (showDate || ScreensaverManager.currentVideoAuthor.length > 0) &&
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

    // Clock display (controlled per-screensaver type via settings)
    Text {
        id: clockDisplay
        z: 2
        // Show clock based on screensaver type and user preference
        // Flip clock and disabled modes never show the clock
        visible: (isVideosMode && ScreensaverManager.videosShowClock) ||
                 (isPipesMode && ScreensaverManager.pipesShowClock) ||
                 (isAttractorMode && ScreensaverManager.attractorShowClock)
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
            running: clockDisplay.visible
            repeat: true
            onTriggered: parent.currentTime = new Date()
        }
    }

    // Download progress indicator (subtle, videos mode only)
    Rectangle {
        z: 2
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.scaled(3)
        color: "transparent"
        visible: isVideosMode && ScreensaverManager.isDownloading

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

    // Screen dimming overlay - fades in after configured delay
    Timer {
        id: dimTimer
        interval: Math.max(1, ScreensaverManager.dimDelayMinutes) * 60 * 1000
        repeat: false
        running: false
        onTriggered: applyDim()
    }

    // z:2.5 positions this above clock/credits (z:2) but below the touch MouseArea (z:3)
    Rectangle {
        id: dimOverlay
        anchors.fill: parent
        z: 2.5
        color: "black"
        opacity: 0
        visible: !isDisabledMode

        Behavior on opacity {
            id: dimBehavior
            enabled: true
            NumberAnimation { duration: 2000; easing.type: Easing.InOutQuad }
        }
    }

    // Touch hint (fades out) - hidden in disabled mode for pure black screen
    Tr {
        id: touchHint
        z: 2
        visible: !isDisabledMode
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
            running: !isDisabledMode
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
        dimTimer.stop()
        dimBehavior.enabled = false
        dimOverlay.opacity = 0
        dimBehavior.enabled = true
        // Re-enable keep-screen-on when leaving screensaver
        // (especially needed if we were in "disabled" mode which turned it off)
        ScreensaverManager.setKeepScreenOn(true)
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
