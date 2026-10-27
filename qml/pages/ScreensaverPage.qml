// The video/image Loader sourceComponents below are nested components, so `screensaverPage`
// and the other ids in this file are not statically resolvable inside them without this
// pragma. No Repeater or delegate in this file, so no `required property` is needed.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtMultimedia
import Decenza

// Screensaver modes:
// "disabled"  - Dims backlight to minimum with black overlay (keeps screen on to avoid EGL surface issues)
// "videos"    - Video/image slideshow from catalog
// "pipes"     - Classic 3D pipes animation
// "flipclock" - Classic flip clock display
// "attractor" - Strange attractor visualization
// "shotmap"   - Shot location map

T.Page {
    id: screensaverPage
    objectName: "screensaverPage"
    background: Rectangle { color: "black" }

    // Current screensaver mode
    // A disconnected replica can briefly expose its empty default snapshot.
    // Keep the local display in a visible fallback mode until the host has
    // published a fresh snapshot; in particular, an empty type must not be
    // interpreted as the configured disabled/black-screen mode.
    property string screensaverType: ScreensaverService.ready && ScreensaverService.state
                                     ? ScreensaverService.state.screensaverType : "videos"
    property bool isVideosMode: screensaverType === "videos"
    property bool isPipesMode: screensaverType === "pipes"
    property bool isFlipClockMode: screensaverType === "flipclock"
    property bool isAttractorMode: screensaverType === "attractor"
    property bool isDisabledMode: screensaverType === "disabled"
    property bool isShotMapMode: screensaverType === "shotmap"

    // Anti-burn-in: nudges the fixed overlay anchors (clock, readout row, link
    // button) a few px on a slow cycle so no pixel stays lit at exactly the
    // same position indefinitely. Matters on OLED/AMOLED tablets, harmless on
    // LCD. No user-facing setting — just built-in behavior.
    property real driftX: 0
    property real driftY: 0
    Behavior on driftX { NumberAnimation { duration: 4000; easing.type: Easing.InOutQuad } }
    Behavior on driftY { NumberAnimation { duration: 4000; easing.type: Easing.InOutQuad } }
    Timer {
        interval: 4 * 60 * 1000  // 4 minutes — slow enough to be imperceptible
        running: !screensaverPage.appSuspended
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            screensaverPage.driftX = Math.random() * 8 - 4  // ±4px
            screensaverPage.driftY = Math.random() * 8 - 4
        }
    }

    property int videoFailCount: 0
    property bool mediaPlaying: false
    property bool isCurrentItemImage: false
    property string lastFailedSource: ""
    property string currentImageSource: ""
    property string currentMediaTicket: ""
    property var currentMedia: ({})
    property string pendingMediaRequestId: ""
    property bool screensaverServiceWasReady: ScreensaverService.ready
    property bool componentReady: false
    property bool useFirstImage: true  // Toggle for cross-fade between two images
    // No hardware video decoder (e.g. Android emulator) — skip all videos
    property bool videoDecoderBroken: !ScreensaverService.hasHardwareVideoDecoder

    Component.onCompleted: {
        componentReady = true
        WebDebugLogger.debug("Screensaver", "ScreensaverPage", ["Loaded, type:", screensaverType,
                    "videos:", isVideosMode, "pipes:", isPipesMode, "flipclock:", isFlipClockMode,
                    "disabled:", isDisabledMode].map(String).join(" "))
        refreshDimming(true)
        if (isVideosMode) {
            playNextMedia()
        }
    }

    onIsDisabledModeChanged: refreshDimming(true)

    function refreshDimming(resetDelay) {
        if (!componentReady)
            return
        const ready = ScreensaverService.ready
        if (resetDelay || !ready || isDisabledMode) {
            dimTimer.stop()
            dimBehavior.enabled = false
            dimOverlay.opacity = ready && isDisabledMode ? 1 : 0
            dimBehavior.enabled = true
        }
        // A disconnected snapshot can clear this display's overlay, but cannot
        // command host brightness. The facade keeps browser dimming local.
        if (!ready)
            return
        if (isDisabledMode) {
            ScreensaverService.setScreenDimming(100)
        } else if (ScreensaverService.state.dimPercent === 0) {
            dimTimer.stop()
            dimOverlay.opacity = 0
            ScreensaverService.setScreenDimming(0)
        } else {
            if (resetDelay)
                ScreensaverService.setScreenDimming(0)
            if (dimOverlay.opacity > 0)
                applyDim()
            else
                startDimming()
        }
    }

    function applyDim() {
        if (!ScreensaverService.ready)
            return
        WebDebugLogger.debug("Screensaver", "ScreensaverPage", ["Applying dim:", ScreensaverService.state.dimPercent + "% (delay was",
                    ScreensaverService.state.dimDelayMinutes, "min)"].map(String).join(" "))
        dimOverlay.opacity = ScreensaverService.state.dimPercent / 100.0
        ScreensaverService.setScreenDimming(ScreensaverService.state.dimPercent)
        // Stop gradient animation (only relevant in videos fallback mode)
        gradientAnimation.running = false
    }

    function startDimming() {
        if (!ScreensaverService.ready)
            return
        if (ScreensaverService.state.dimDelayMinutes === 0) {
            applyDim()
        } else {
            dimTimer.restart()
        }
    }

    // Listen for new media becoming available (downloaded) and screen dimming changes
    Connections {
        target: ScreensaverService
        function onStateChanged() {
            screensaverPage.handleScreensaverServiceReadiness()
        }
        function onMediaAvailable() {
            if (!screensaverPage.mediaPlaying)
                screensaverPage.playNextMedia()
        }
        function onCompleted(requestId, operation, success, result, reason) {
            if (operation !== "next-media" || requestId !== screensaverPage.pendingMediaRequestId)
                return
            screensaverPage.pendingMediaRequestId = ""
            if (!success || !result || !result.ticket) {
                if (screensaverPage.visible && screensaverPage.isVideosMode && !screensaverPage.appSuspended) {
                    screensaverPage.mediaPlaying = false
                    screensaverPage.currentMedia = ({})
                    screensaverPage.currentMediaTicket = ""
                }
                return
            }
            if (!screensaverPage.visible || !screensaverPage.isVideosMode || screensaverPage.appSuspended) {
                // The request is still correlated even if the display changed
                // mode while it was in flight. Release its newly-created host
                // ticket instead of dropping an unreachable lease.
                ScreensaverService.markPlayed(result.ticket)
                return
            }
            screensaverPage.acceptMedia(result)
        }
    }

    function handleScreensaverServiceReadiness() {
        var ready = ScreensaverService.ready
        if (ready === screensaverServiceWasReady)
            return

        screensaverServiceWasReady = ready
        refreshDimming(true)
        if (!ready) {
            // Media tickets are host-owned and revoked with the connection.
            // Drop all client references without reporting them as played or
            // corrupt; any queued completion is ignored after clearing its ID.
            pendingMediaRequestId = ""
            stopVideoPlayer()
            mediaPlaying = false
            isCurrentItemImage = false
            currentMedia = ({})
            currentMediaTicket = ""
            currentImageSource = ""
            imageDisplay1.source = ""
            imageDisplay2.source = ""
            return
        }

        // Fetch a new ticket only after the replica is ready and this retained
        // page is visible. Never replay a request or reuse the revoked ticket.
        if (visible && !appSuspended && isVideosMode)
            playNextMedia()
    }

    Connections {
        target: ScreensaverService.state
        function onItemCountChanged() {
            if (ScreensaverService.ready && !screensaverPage.mediaPlaying && ScreensaverService.state.itemCount > 0)
                screensaverPage.playNextMedia()
        }
        function onDimPercentChanged() {
            screensaverPage.refreshDimming(false)
        }
        function onDimDelayMinutesChanged() {
            if (!ScreensaverService.ready) return
            // Only restart if dim hasn't triggered yet
            if (dimOverlay.opacity === 0 && ScreensaverService.state.dimPercent > 0 && !screensaverPage.isDisabledMode) {
                screensaverPage.refreshDimming(false)
            }
        }
    }

    property int videoSkipCount: 0  // Guard against deep recursion when skipping broken videos
    property int videoTransitionCount: 0  // Track transitions for memory leak monitoring

    // RSS at which video playback is abandoned (see the ceiling check below).
    // 500 MB suits a Release build, which starts around 180 MB. An instrumented
    // build does not: ASan+UBSan Debug starts at ~463 MB on macOS and plateaus
    // near 1280 MB after the decoder's buffers reach their high-water mark —
    // so the Release threshold is breached from launch and the guard fires on
    // essentially every screensaver session, stopping videos when nothing is
    // actually wrong. Scaled rather than disabled: a real runaway still gets
    // caught well before it reaches SIGBUS territory.
    readonly property real videoRssCeilingMB: ScreensaverService.instrumentedBuild() ? 2000 : 500
    property string pendingVideoSource: ""  // Source queued for next video after decoder teardown
    property bool currentVideoLoaded: false
    property real preDestroyRss: 0  // RSS at the last transition line PRINTED (for delta logging)
    property int videoTransitionsSinceLog: 0  // transitions folded into the next line that speaks

    // Track app suspend state to stop rendering when display is off.
    // On Android, the EGL surface is destroyed when the display turns off
    // (QTBUG-118231). If we keep rendering (especially video playback),
    // Qt's render thread gets stuck on the dead surface and the app
    // freezes (ANR on resume).
    property bool appSuspended: false

    onVisibleChanged: {
        if (visible && ScreensaverService.ready && isVideosMode && !appSuspended
                && !mediaPlaying && pendingMediaRequestId.length === 0)
            playNextMedia()
    }

    // The screensaver page can stay mounted while another display changes the
    // shared mode preference. Entering Videos therefore cannot rely on page
    // creation or visibility changing to start playback.
    onIsVideosModeChanged: {
        if (!componentReady)
            return

        if (isVideosMode) {
            if (visible && ScreensaverService.ready && !appSuspended)
                playNextMedia()
            return
        }

        // Stop a retained video/image presentation. Keep an in-flight request
        // correlated: its completion either supplies media if Videos is
        // selected again, or releases the ticket if another mode remains active.
        imageDisplayTimer.stop()
        if (ScreensaverService.ready && currentMediaTicket.length > 0)
            ScreensaverService.markPlayed(currentMediaTicket)
        currentMediaTicket = ""
        currentMedia = ({})
        mediaPlaying = false
        isCurrentItemImage = false
        currentImageSource = ""
        imageDisplay1.source = ""
        imageDisplay2.source = ""
        stopVideoPlayer()
    }

    Connections {
        target: Qt.application
        // qmllint disable missing-property
        // `Qt.application.state` is real: in a QtQuick app the object is a QQuickApplication
        // (qtdeclarative/src/quick/util/qquickglobal.cpp:239-241), which declares `state`
        // (qtdeclarative/src/quick/util/qquickapplication_p.h:38). qmllint types it as the
        // qtqml base class QQmlApplication, which does not — a tool limitation, not a
        // missing property.
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationSuspended) {
                screensaverPage.appSuspended = true
                WebDebugLogger.debug("Screensaver", "ScreensaverPage", ["App suspended — pausing all rendering"].map(String).join(" "))
                // Destroy video decoder to stop rendering to dead EGL surface
                if (mediaPlayerLoader.active) {
                    screensaverPage.stopVideoPlayer()
                    mediaPlayerLoader.active = false
                }
                imageDisplayTimer.stop()
                gradientAnimation.running = false
            } else if (Qt.application.state === Qt.ApplicationActive && screensaverPage.appSuspended) {
                screensaverPage.appSuspended = false
                WebDebugLogger.debug("Screensaver", "ScreensaverPage", ["App resumed — restarting media"].map(String).join(" "))
                if (screensaverPage.isVideosMode && ScreensaverService.state.enabled) {
                    screensaverPage.playNextMedia()
                    // playNextMedia() may call wake() which navigates away —
                    // don't touch animation state on a page being torn down
                    if (!screensaverPage.visible) return
                }
                // Restart gradient animation if in fallback mode (no playable media)
                if (screensaverPage.isVideosMode && !screensaverPage.mediaPlaying) {
                    gradientAnimation.running = true
                }
                // Restart image rotation if an image was already loaded pre-suspend
                // (onStatusChanged won't re-fire for an already-loaded source)
                if (screensaverPage.isVideosMode && screensaverPage.isCurrentItemImage && screensaverPage.mediaPlaying) {
                    imageDisplayTimer.restart()
                }
            }
        }
        // qmllint enable missing-property
    }

    function playNextMedia() {
        if (!isVideosMode || !ScreensaverService.ready || !ScreensaverService.state || !visible
                || !ScreensaverService.state.enabled || appSuspended
                || pendingMediaRequestId.length > 0) {
            return
        }
        pendingMediaRequestId = ScreensaverService.nextMedia()
    }

    function stopVideoPlayer() {
        currentVideoLoaded = false
        pendingVideoSource = ""
        if (ScreensaverService.isRemoteClient && mediaPlayerLoader.item) {
            // Qt 6.12 Wasm appends a video element per output but does not remove
            // it on destruction (qwasmvideooutput.cpp:107-116,566-599).
            // Reuse this output across videos and images to bound that growth.
            // qmllint disable missing-property
            // Pause before stop: Qt's Wasm stop removes the DOM node without
            // pausing unless pause was requested (qwasmvideooutput.cpp:320-354).
            mediaPlayerLoader.item.player.pause()
            mediaPlayerLoader.item.player.stop()
            mediaPlayerLoader.item.player.source = ""
            // qmllint enable missing-property
        } else {
            mediaPlayerLoader.active = false
        }
    }

    function acceptMedia(media) {
        currentVideoLoaded = false
        currentMedia = media
        currentMediaTicket = media.ticket
        isCurrentItemImage = media.isImage === true
        var source = ScreensaverService.mediaSource(media.ticket)
        if (!source) {
            ScreensaverService.markPlayed(currentMediaTicket)
            currentMediaTicket = ""
            mediaPlaying = false
            return
        }

        if (isCurrentItemImage) {
            // Display image with cross-fade transition
            // Destroy video decoder while showing images to free memory
            stopVideoPlayer()
            mediaPlaying = true
            videoSkipCount = 0

            if (useFirstImage)
                imageDisplay1.source = source
            else
                imageDisplay2.source = source
            currentImageSource = source
        } else if (videoDecoderBroken) {
            // Native decoder capability is host-local. A browser attempts its
            // own decoder and skips failed media without changing the host cache.
            videoSkipCount++
            if (videoSkipCount > ScreensaverService.state.itemCount + 5) {
                WebDebugLogger.warn("Screensaver", "ScreensaverPage", ["No playable content (no hardware decoder) — auto-waking"].map(String).join(" "))
                videoSkipCount = 0
                mediaPlaying = false
                isCurrentItemImage = false
                wake()
                return
            }
            ScreensaverService.markPlayed(currentMediaTicket)
            currentMediaTicket = ""
            playNextMedia()
            return
        } else {
                // Play video — destroy and recreate MediaPlayer + VideoOutput to fully
                // release FFmpeg decoder resources and VideoToolbox frame pool. Both
                // must be destroyed together; a persistent VideoOutput retains the last
                // decoded frame's CVPixelBuffer/IOSurface on macOS.
                videoTransitionCount++
                let liveRss = ScreensaverService.liveRssMB()

                // Speak only when RSS has MOVED 5 MB from the last line printed.
                //
                // This logged every transition, and a screensaver left running
                // overnight put 116 and 84 lines into two runs of a single session
                // (3,190 across a real tablet's whole 24,000-line buffer — the
                // largest subsystem in it) to report that memory was flat. Flat is
                // the expected outcome, so it is the one thing not worth 200 lines.
                // What the line exists to catch is RSS CLIMBING across transitions,
                // i.e. the Qt FFmpeg/VideoToolbox leak handled below.
                //
                // HYSTERESIS AGAINST THE LAST LINE PRINTED, NOT A FIXED BUCKET. The
                // first version of this bucketed on Math.floor(rss/5), the same
                // shape as MemoryMonitor's own gate, and measuring it against the
                // real log showed why that is wrong here: RSS sits at ~135 MB and
                // jitters ±3 MB, which straddles a bucket edge, so it would have
                // re-logged on every crossing — 36 lines of 116 rather than 2.
                // MemoryMonitor gets away with buckets because it samples once a
                // minute; this fires on every transition. Measuring against the last
                // PRINTED value has no edge to oscillate across: on the same real
                // data it prints 2 lines per run, and a genuine leak still prints
                // one line per 5 MB climbed.
                //
                // Hand-rolled because no QML-side LogCollapse helper exists yet. If
                // one is added, this is a caller — but it would need this
                // magnitude-threshold mode, not just the identical-text collapse.
                //
                // Suppressed transitions are counted and reported on the next line
                // that speaks, so the record never implies the screensaver stopped
                // cycling.
                if (liveRss >= 0)
                    videoTransitionsSinceLog++
                let movedMB = liveRss >= 0 ? liveRss - preDestroyRss : 0
                if (liveRss >= 0 && (videoTransitionCount === 1 || Math.abs(movedMB) >= 5)) {
                    let delta = videoTransitionCount > 1
                        ? " delta:" + movedMB.toFixed(1) + " MB" : ""
                    let folded = videoTransitionsSinceLog > 1
                        ? " (+" + (videoTransitionsSinceLog - 1) +
                          " transitions within 5 MB of the last line)" : ""
                    WebDebugLogger.debug("Screensaver", "ScreensaverPage", ["Video transition #" + videoTransitionCount +
                                " RSS:" + liveRss.toFixed(1) + " MB" + delta + folded +
                                " src:" + source.substring(source.lastIndexOf("/") + 1)].map(String).join(" "))
                    videoTransitionsSinceLog = 0
                    preDestroyRss = liveRss
                }

                // Qt's FFmpeg/VideoToolbox backend leaks ~5-10 MB per video transition
                // on macOS (CVPixelBufferPool not fully released). Restart the screensaver
                // when RSS grows too high to prevent eventual SIGBUS crash.
                if (liveRss > screensaverPage.videoRssCeilingMB && screensaverPage.videoTransitionCount > 5) {
                    WebDebugLogger.warn("Screensaver", "ScreensaverPage", ["RSS ceiling exceeded (" + liveRss.toFixed(0) +
                                 " MB of " + screensaverPage.videoRssCeilingMB +
                                 " MB at transition #" + videoTransitionCount +
                                 ") — stopping video playback (Qt FFmpeg leak is unrecoverable)" +
                                 (ScreensaverService.instrumentedBuild()
                                    ? " [instrumented build: ceiling scaled up, so this is a real"
                                      + " runaway rather than sanitizer overhead]" : "")].map(String).join(" "))
                    // Qt's FFmpeg/VideoToolbox backend leaks Metal/IOSurface memory
                    // that survives both Loader destruction and full page teardown.
                    // Stop playing videos to prevent the leak from reaching SIGBUS.
                    // The screensaver stays visible with the fallback gradient.
                    pendingVideoSource = ""
                    mediaPlayerLoader.active = false
                    mediaPlaying = false
                    return
                }

                // Reset image state
                imageDisplayTimer.stop()
                currentImageSource = ""
                useFirstImage = true
                imageDisplay1.source = ""
                imageDisplay2.source = ""
                mediaPlaying = true
                videoSkipCount = 0

                // Destroy old decoder, then create fresh one with new source.
                // Setting active=false destroys the MediaPlayer; onItemChanged
                // fires when item becomes null, then re-activates the Loader.
                pendingVideoSource = source
                if (ScreensaverService.isRemoteClient && mediaPlayerLoader.item) {
                    // Source replacement uses the existing HTML element:
                    // qwasmvideooutput.cpp:406-436 in Qt 6.12.
                    // qmllint disable missing-property
                    mediaPlayerLoader.item.player.source = source
                    mediaPlayerLoader.item.player.play()
                    // qmllint enable missing-property
                    return
                }
                mediaPlayerLoader.active = false
                // If no previous item existed (first play), activate directly
                if (!mediaPlayerLoader.item) {
                    mediaPlayerLoader.active = true
                }
        }
    }

    function handleVideoFailure(failedPlayer, formatError) {
        // Ignore stale signals from a destroyed MediaPlayer
        if (!mediaPlayerLoader.item || currentMediaTicket.length === 0) return

        // The video surface is an inline sourceComponent, so `mediaPlayerLoader.item` has no type
        // name to cast to and every read of its `player`/`output` aliases is a QObject member
        // access. Extracting it to a file is the real fix and is deliberately NOT done here: the
        // component reaches back into this page for seven things (playNextMedia,
        // handleVideoFailure, videoFailCount, lastFailedSource, isVideosMode, mediaPlaying,
        // isCurrentItemImage), so the extraction is an interface design, and this screen carries
        // the Qt FFmpeg MediaCodec leak history that makes an untested change here expensive.
        // Left as a marked debt rather than a silent one.
        // qmllint disable missing-property
        // An old decoder can finish an event after a replacement was requested.
        if (mediaPlayerLoader.item.player !== failedPlayer) return
        // Prevent handling the same failure twice.
        var playerSource = failedPlayer.source.toString()
        // qmllint enable missing-property
        if (playerSource === lastFailedSource) return
        lastFailedSource = playerSource

        videoFailCount++
        WebDebugLogger.warn("Screensaver", "ScreensaverPage", ["Media failed (" + videoFailCount + "/5):", playerSource].map(String).join(" "))

        // Only a native client can identify a local host file as corrupt.
        // A browser's decoder or media request can fail because of browser
        // codec support, networking, or access policy; none of those failures
        // may evict the host's cached copy.
        if (!ScreensaverService.isRemoteClient && formatError === true
                && playerSource.indexOf("file://") === 0 && currentMediaTicket.length > 0) {
            ScreensaverService.markCorrupt(currentMediaTicket)
        } else if (currentMediaTicket.length > 0) {
            ScreensaverService.markPlayed(currentMediaTicket)
        }
        currentMediaTicket = ""

        if (videoFailCount >= 5) {
            mediaPlaying = false
            stopVideoPlayer()
            videoFailCount = 0  // Reset for when new media downloads
            return
        }

        // Try next cached media
        playNextMedia()
    }

    function handleImageFailure(source) {
        if (source !== currentImageSource || source === lastFailedSource)
            return
        lastFailedSource = source
        videoFailCount++
        WebDebugLogger.warn("Screensaver", "ScreensaverPage", ["Image failed:", source].map(String).join(" "))
        if (currentMediaTicket.length > 0)
            ScreensaverService.markPlayed(currentMediaTicket)
        currentMediaTicket = ""
        mediaPlaying = false
        if (videoFailCount >= 5) {
            videoFailCount = 0
            return
        }
        playNextMedia()
    }

    // Timer for image display duration
    Timer {
        id: imageDisplayTimer
        interval: ScreensaverService.state.imageDisplayDuration * 1000
        repeat: false
        onTriggered: {
            // Mark current image as played for LRU tracking
            if (screensaverPage.currentMediaTicket.length > 0)
                ScreensaverService.markPlayed(screensaverPage.currentMediaTicket)
            screensaverPage.currentMediaTicket = ""
            screensaverPage.videoFailCount = 0
            screensaverPage.lastFailedSource = ""
            // Toggle to other image for cross-fade effect
            screensaverPage.useFirstImage = !screensaverPage.useFirstImage
            screensaverPage.playNextMedia()
        }
    }

    // MediaPlayer + VideoOutput wrapped in Loader — destroyed and recreated
    // between videos to fully release FFmpeg decoder resources (including the
    // VideoToolbox CVPixelBufferPool on macOS). Both must be co-located so that
    // the VideoOutput's retained last frame is released with the decoder.
    Loader {
        id: mediaPlayerLoader
        anchors.fill: parent
        active: false

        // Event-driven recreation: when the old component is destroyed (item
        // becomes null), re-activate the Loader to create a fresh decoder.
        onItemChanged: {
            if (item === null && screensaverPage.pendingVideoSource.length > 0) {
                mediaPlayerLoader.active = true
            }
        }

        sourceComponent: Item {
            anchors.fill: parent

            property alias player: mediaPlayer
            property alias output: videoOut

            MediaPlayer {
                id: mediaPlayer
                audioOutput: AudioOutput {}

                onMediaStatusChanged: {
                    if (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia) {
                        screensaverPage.currentVideoLoaded = true
                    } else if (mediaStatus === MediaPlayer.EndOfMedia) {
                        // Qt 6.12 Wasm maps HTML "emptied" (including load/source
                        // replacement) to EndOfMedia: qwasmvideooutput.cpp:825-834.
                        // Do not consume the new host ticket before it has played.
                        if (ScreensaverService.isRemoteClient
                                && (!screensaverPage.currentVideoLoaded || duration <= 0 || position < duration))
                            return
                        if (!screensaverPage.currentMediaTicket.length) return
                        ScreensaverService.markPlayed(screensaverPage.currentMediaTicket)
                        screensaverPage.currentMediaTicket = ""
                        screensaverPage.videoFailCount = 0
                        screensaverPage.lastFailedSource = ""
                        screensaverPage.playNextMedia()
                    } else if (mediaStatus === MediaPlayer.InvalidMedia) {
                        // InvalidMedia is the parser's verdict that the bytes
                        // aren't valid media — treat as a format error so the
                        // cached file gets evicted and re-fetched.
                        screensaverPage.handleVideoFailure(mediaPlayer, true)
                    }
                }

                onErrorOccurred: function(error, errorString) {
                    WebDebugLogger.warn("Screensaver", "ScreensaverPage", ["MediaPlayer error:", error, errorString].map(String).join(" "))
                    // Only Qt's FormatError implies the on-disk bytes are
                    // bad. Other codes (ResourceError, NetworkError,
                    // AccessDeniedError) are transient — leave the file alone.
                    screensaverPage.handleVideoFailure(mediaPlayer, error === MediaPlayer.FormatError)
                }

                onPositionChanged: function(position) {
                    // A PlayingState notification alone does not prove that a
                    // frame played. Keep consecutive startup failures bounded.
                    if (screensaverPage.currentVideoLoaded && position > 0 && playbackState === MediaPlayer.PlayingState) {
                        screensaverPage.videoFailCount = 0
                        screensaverPage.lastFailedSource = ""
                    }
                }
            }

            VideoOutput {
                id: videoOut
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectCrop
                visible: screensaverPage.isVideosMode && screensaverPage.mediaPlaying && !screensaverPage.isCurrentItemImage
            }
        }

        onLoaded: {
            // Inline sourceComponent — see handleVideoFailure for why this is not extracted.
            // qmllint disable missing-property
            item.player.videoOutput = item.output
            // Mute after attaching the video surface and before playback. Qt 6.12's
            // Wasm backend binds its HTML element in qwasmmediaplayer.cpp:275-277;
            // qwasmaudiooutput.cpp:45-50 applies this change to that element.
            item.player.audioOutput.muted = true
            item.player.source = screensaverPage.pendingVideoSource
            item.player.play()
            // qmllint enable missing-property
        }
    }

    // Image display with cross-fade transition
    Item {
        id: imageContainer
        anchors.fill: parent
        visible: screensaverPage.isVideosMode && screensaverPage.mediaPlaying && screensaverPage.isCurrentItemImage

        // Two images for cross-fade effect (2 second dissolve)
        Image {
            id: imageDisplay1
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            opacity: screensaverPage.useFirstImage ? 1.0 : 0.0

            Behavior on opacity {
                NumberAnimation { duration: 2000; easing.type: Easing.InOutQuad }
            }

            onStatusChanged: {
                if (status === Image.Ready && screensaverPage.useFirstImage && source.toString().length > 0) {
                    // Image loaded — only cycle if there are multiple items to show
                    if (ScreensaverService.state.itemCount > 1 || ScreensaverService.state.personalMediaCount > 1)
                        imageDisplayTimer.restart()
                } else if (status === Image.Error) {
                    screensaverPage.handleImageFailure(source.toString())
                }
            }
        }

        Image {
            id: imageDisplay2
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            opacity: screensaverPage.useFirstImage ? 0.0 : 1.0

            Behavior on opacity {
                NumberAnimation { duration: 2000; easing.type: Easing.InOutQuad }
            }

            onStatusChanged: {
                if (status === Image.Ready && !screensaverPage.useFirstImage && source.toString().length > 0) {
                    if (ScreensaverService.state.itemCount > 1 || ScreensaverService.state.personalMediaCount > 1)
                        imageDisplayTimer.restart()
                } else if (status === Image.Error) {
                    screensaverPage.handleImageFailure(source.toString())
                }
            }
        }
    }

    // 3D Pipes screensaver (requires Quick3D)
    Loader {
        id: pipesLoader
        anchors.fill: parent
        active: Settings.app.hasQuick3D && screensaverPage.isPipesMode && !screensaverPage.appSuspended
        visible: screensaverPage.isPipesMode
        z: 0
        // By URL: PipesScreensaver is only in the module when Quick3D is.
        source: Qt.resolvedUrl("../components/PipesScreensaver.qml")
        Binding {
            target: pipesLoader.item
            property: "running"
            value: screensaverPage.isPipesMode && screensaverPage.visible && !screensaverPage.appSuspended
        }
    }

    // Flip Clock screensaver
    Loader {
        id: flipClockLoader
        anchors.fill: parent
        active: screensaverPage.isFlipClockMode && !screensaverPage.appSuspended
        visible: screensaverPage.isFlipClockMode
        z: 0
        sourceComponent: FlipClockScreensaver {
            running: screensaverPage.isFlipClockMode && screensaverPage.visible && !screensaverPage.appSuspended
        }
    }

    // Strange Attractor screensaver
    Loader {
        id: attractorLoader
        anchors.fill: parent
        active: screensaverPage.isAttractorMode && !screensaverPage.appSuspended
        visible: screensaverPage.isAttractorMode
        z: 0
        sourceComponent: StrangeAttractorScreensaver {
            running: screensaverPage.isAttractorMode && screensaverPage.visible && !screensaverPage.appSuspended
        }
    }

    // Shot Map screensaver (flat map works without Quick3D, globe loaded conditionally)
    Loader {
        id: shotMapLoader
        anchors.fill: parent
        active: screensaverPage.isShotMapMode && !screensaverPage.appSuspended
        visible: screensaverPage.isShotMapMode
        z: 0
        sourceComponent: ShotMapScreensaver {
            running: screensaverPage.isShotMapMode && screensaverPage.visible && !screensaverPage.appSuspended
            topContentInset: overlayReadoutRow.visible ? overlayReadoutRow.y + overlayReadoutBackground.y + overlayReadoutBackground.height + Theme.spacingSmall : 0
        }
    }

    // Fallback: show a subtle animation while no cached media (videos mode only)
    Rectangle {
        id: fallbackBackground
        anchors.fill: parent
        // Inline sourceComponent — see handleVideoFailure for why this is not extracted.
        // qmllint disable missing-property
        visible: screensaverPage.isVideosMode && (!screensaverPage.mediaPlaying || (!screensaverPage.isCurrentItemImage && (!mediaPlayerLoader.item || mediaPlayerLoader.item.player.playbackState !== MediaPlayer.PlayingState)))
        // qmllint enable missing-property
        z: 1

        Rectangle {
            id: gradientRect
            anchors.fill: parent
            // No initialiser: `NumberAnimation on gradientHue` below is a value source WITH AN EXPLICIT
            // `from: 0`, so it starts there and the initialiser is discarded immediately.
            // (A value source with no `from` DOES start from the property's current value —
            // qquickanimation.cpp:1324. The rule is about the explicit `from`, not about
            // value sources in general.) The `: 0.6` that
            // used to be here was dead — the gradient has always started at 0, not at 0.6. Removed
            // rather than honoured, so the rendering is unchanged; set the animation's `from` if a
            // different start hue is ever wanted.
            property real gradientHue

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
                id: gradientAnimation
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

        property bool showDate: ScreensaverService.state.isPersonalCategory &&
                               ScreensaverService.state.showDateOnPersonal &&
                               (screensaverPage.currentMedia.mediaDate || "").length > 0

        // Inline sourceComponent — see handleVideoFailure for why this is not extracted.
        // qmllint disable missing-property
        visible: screensaverPage.isVideosMode &&
                 (showDate || (screensaverPage.currentMedia.author || "").length > 0) &&
                 ((mediaPlayerLoader.item && mediaPlayerLoader.item.player.playbackState === MediaPlayer.PlayingState) ||
                  (screensaverPage.isCurrentItemImage && screensaverPage.mediaPlaying))
        // qmllint enable missing-property

        Text {
            anchors.centerIn: parent
            textFormat: Text.StyledText
            text: {
                // Sanitize author name — Pexels usernames can contain emoji
                // which triggers CoreText CopyEmojiImage crash on render thread
                var author = Theme.replaceEmojiWithImg(screensaverPage.currentMedia.author || "", Theme.scaled(14))
                if (parent.showDate) {
                    return Theme.replaceEmojiWithImg(screensaverPage.currentMedia.mediaDate || "", Theme.scaled(14))
                } else if (screensaverPage.isCurrentItemImage) {
                    return TranslationManager.translate("screensaver.photo_by", "Photo by %1 (Pexels)")
                           .arg(author)
                } else {
                    return TranslationManager.translate("screensaver.video_by", "Video by %1 (Pexels)")
                           .arg(author)
                }
            }
            color: Theme.primaryContrastColor
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
        visible: (screensaverPage.isVideosMode && ScreensaverService.state.videosShowClock) ||
                 (screensaverPage.isPipesMode && ScreensaverService.state.pipesShowClock) ||
                 (screensaverPage.isAttractorMode && ScreensaverService.state.attractorShowClock)
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.rightMargin: Theme.scaled(50) + screensaverPage.driftX
        anchors.bottomMargin: Theme.chartMarginLarge + Theme.scaled(20) + screensaverPage.driftY  // Above credits bar
        text: Theme.clockTime(currentTime)
        color: Theme.primaryContrastColor
        opacity: 0.8
        font.pixelSize: Theme.scaled(80)
        font.weight: Font.Light

        property date currentTime: new Date()

        Timer {
            interval: 1000
            running: clockDisplay.visible
            repeat: true
            // clockDisplay, not `parent`: Timer is a QObject, not an Item, so it has no parent
            // of its own. Unqualified `parent` walked past it to the document scope and resolved
            // to screensaverPage.parent — the StackView content item — so the write landed on an
            // object with no currentTime and the clock never ticked. qmllint does not flag this:
            // `parent` is a known identifier, not an unqualified one.
            onTriggered: clockDisplay.currentTime = new Date()
        }
    }

    // Overlay readout row — Water Level / Shot Plan / Battery, whichever are
    // enabled (each a single global toggle, shared across every background).
    // Reuses the same compact rendering as the home screen's persistent
    // StatusBar. A semi-opaque background band (matching the credits bar
    // below) keeps it legible over bright video content regardless of theme.
    Item {
        id: overlayReadoutRow
        z: 2
        visible: !screensaverPage.isDisabledMode && overlayRow.implicitWidth > 0
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Theme.scaled(20) + screensaverPage.driftY
        anchors.rightMargin: Theme.scaled(50) + screensaverPage.driftX
        width: overlayRow.implicitWidth
        height: overlayRow.implicitHeight

        Rectangle {
            id: overlayReadoutBackground
            anchors.fill: overlayRow
            anchors.margins: -Theme.scaled(8)
            radius: Theme.scaled(8)
            color: Qt.rgba(0, 0, 0, 0.5)
        }

        Row {
            id: overlayRow
            spacing: Theme.scaled(16)

            WaterLevelItem {
                isCompact: true
                visible: ScreensaverService.state.overlayShowWaterLevel
                modelData: ({ displayMode: "icon" })
            }
            ShotPlanItem {
                isCompact: true
                visible: ScreensaverService.state.overlayShowShotPlan
            }
            BatteryLevelItem {
                isCompact: true
                visible: ScreensaverService.state.overlayShowBattery
                modelData: ({ displayMode: "icon" })
            }
        }
    }

    // Link Button — interactive, must NOT wake the machine on tap. Stacked above
    // the full-screen wake-on-tap MouseArea below so it captures its own taps
    // first; its handler opens the URL only and never calls wake(). (That wake
    // MouseArea must stay below this button's z — see the comment there.)
    Rectangle {
        id: linkButton
        z: 4
        visible: !screensaverPage.isDisabledMode && ScreensaverService.state.overlayLinkButtonEnabled
                 && ScreensaverService.state.overlayLinkButtonLabel !== ""
        // Dims along with the rest of the screen, same as everything below the
        // dim overlay — otherwise it'd be the one static element that never
        // dims, defeating the point of the anti-burn-in drift above.
        opacity: 1.0 - dimOverlay.opacity
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.scaled(50) - screensaverPage.driftX
        anchors.bottomMargin: Theme.chartMarginLarge + Theme.scaled(20) + screensaverPage.driftY
        radius: Theme.scaled(8)
        color: Qt.rgba(0, 0, 0, 0.5)
        border.color: Qt.rgba(1, 1, 1, 0.4)
        border.width: 1
        width: linkButtonText.implicitWidth + Theme.scaled(32)
        height: Theme.scaled(44)

        Text {
            id: linkButtonText
            anchors.centerIn: parent
            text: ScreensaverService.state.overlayLinkButtonLabel
            color: Theme.primaryContrastColor
            font.pixelSize: Theme.scaled(16)
            Accessible.ignored: true
        }

        AccessibleMouseArea {
            id: linkButtonArea
            anchors.fill: parent
            accessibleName: ScreensaverService.state.overlayLinkButtonLabel
            onAccessibleClicked: Qt.openUrlExternally(screensaverPage.normalizedLinkUrl(ScreensaverService.state.overlayLinkButtonUrl))
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
        visible: screensaverPage.isVideosMode && ScreensaverService.state.isDownloading

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
        width: parent.width * ScreensaverService.state.downloadProgress
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
        interval: Math.max(1, ScreensaverService.state.dimDelayMinutes) * 60 * 1000
        repeat: false
        running: false
        onTriggered: screensaverPage.applyDim()
    }

    // z:2.5 positions this above clock/credits (z:2) but below the touch MouseArea (z:3)
    Rectangle {
        id: dimOverlay
        anchors.fill: parent
        z: 2.5
        color: "black"
        opacity: 0

        Behavior on opacity {
            id: dimBehavior
            enabled: true
            NumberAnimation { duration: 2000; easing.type: Easing.InOutQuad }
        }
    }

    // Touch hint (fades out) - shown briefly so users know to tap
    // z:2.75 positions above dimOverlay (z:2.5) but below touch MouseArea (z:3)
    Tr {
        id: touchHint
        z: 2.75
        anchors.centerIn: parent
        key: "screensaver.touch_to_wake"
        fallback: "Touch to wake"
        color: Theme.primaryContrastColor
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

    // Touch anywhere to wake. Must stay below linkButton's z (currently 4) so
    // that button can capture its own taps instead of also waking the machine.
    MouseArea {
        z: 3
        anchors.fill: parent
        Accessible.role: Accessible.Button
        Accessible.name: TranslationManager.translate("screensaver.touch_to_wake", "Touch to wake")
        Accessible.focusable: true
        Accessible.onPressAction: screensaverPage.wake()
        onPressed: screensaverPage.wake()
    }

    // Also wake on key press
    Keys.onPressed: wake()

    // Adds a scheme if the configured Link Button URL is missing one (e.g. a
    // user typing "example.com") — Qt.openUrlExternally may not resolve a
    // bare host on every platform.
    function normalizedLinkUrl(url) {
        if (!url) return url
        return url.indexOf("://") === -1 ? "https://" + url : url
    }

    function wake() {
        HomePresetActions.wakeMachine()
    }

    // Clean up media when page is being removed
    StackView.onRemoved: {
        WebDebugLogger.debug("Screensaver", "ScreensaverPage", ["Waking: restoring brightness and cleaning up"].map(String).join(" "))
        stopVideoPlayer()
        mediaPlayerLoader.active = false
        mediaPlaying = false
        if (currentMediaTicket.length > 0) {
            ScreensaverService.markPlayed(currentMediaTicket)
            currentMediaTicket = ""
        }
        imageDisplayTimer.stop()
        dimTimer.stop()
        dimBehavior.enabled = false
        dimOverlay.opacity = 0
        dimBehavior.enabled = true
        // Restore screen brightness and keep-screen-on when leaving screensaver
        ScreensaverService.restoreScreenBrightness()
        ScreensaverService.setKeepScreenOn(true)
    }

    // Auto-wake when DE1 wakes up externally (button press on machine).
    // Device state is shared; native-only scale behavior stays behind the facade.
    Connections {
        target: DE1Device
        function onStateChanged() {
            if (ScreensaverService.isRemoteClient)
                return
            var state = DE1Device.stateString
            if (state !== "Sleep" && state !== "GoingToSleep") {
                ScreensaverService.wakeScale()
                AppShell.scaleDialogDeferred = true
                AppShell.idleFromScreensaverRequested()
            }
        }
    }

}
