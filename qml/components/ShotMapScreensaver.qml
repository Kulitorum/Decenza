import QtQuick
import QtQuick.Controls
import DecenzaDE1

Item {
    id: root

    property bool running: true
    property bool widgetMode: false  // When true: no overlays, smaller dots
    // Fall back to flat when Quick3D unavailable
    property string mapShape: (!Settings.hasQuick3D && ScreensaverManager.shotMapShape === "globe")
                              ? "flat" : ScreensaverManager.shotMapShape
    property string mapTexture: ScreensaverManager.shotMapTexture  // "dark", "bright", "satellite"
    property bool showClock: widgetMode ? false : ScreensaverManager.shotMapShowClock
    property bool showProfiles: widgetMode ? false : ScreensaverManager.shotMapShowProfiles

    // Test mode - show user's location with a marker
    property bool testMode: false
    property real testLatitude: 0
    property real testLongitude: 0

    // Shot data from API
    property var shots: []
    property var topProfiles: []
    property int shotCount: 0

    // Map bounds (Web Mercator projection stretched to 2:1)
    readonly property real mapMinLat: -85
    readonly property real mapMaxLat: 85
    readonly property real mapMinLon: -180
    readonly property real mapMaxLon: 180

    // Globe rotation
    property real globeRotation: 0

    // Globe sphere radius
    readonly property real globeRadius: 150

    Component.onCompleted: {
        console.log("[ShotMapScreensaver] Started, shape:", mapShape, "texture:", mapTexture)
        fetchShots()
    }

    // Poll API every 30 seconds
    Timer {
        id: pollTimer
        interval: 30000
        running: root.running && root.visible
        repeat: true
        onTriggered: fetchShots()
    }

    // Globe rotation animation
    NumberAnimation on globeRotation {
        running: root.running && root.visible && mapShape === "globe"
        from: 0
        to: 360
        duration: 120000  // 2 minutes per rotation
        loops: Animation.Infinite
    }

    function fetchShots() {
        var xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                if (xhr.status === 200) {
                    try {
                        var data = JSON.parse(xhr.responseText)
                        shots = data.shots || []
                        topProfiles = data.top_profiles || []
                        shotCount = shots.length
                        console.log("[ShotMapScreensaver] Loaded", shots.length, "shots,", topProfiles.length, "profiles")
                    } catch (e) {
                        console.log("[ShotMapScreensaver] JSON parse error:", e)
                    }
                } else {
                    console.log("[ShotMapScreensaver] Fetch failed:", xhr.status)
                }
            }
        }
        xhr.open("GET", "https://decenza.coffee/api/shots-latest.json")
        xhr.send()
    }

    // Convert latitude to Web Mercator Y (normalized 0-1)
    function latToMercatorY(lat) {
        lat = Math.max(-85, Math.min(85, lat))
        var latRad = lat * Math.PI / 180
        var mercN = Math.log(Math.tan(Math.PI / 4 + latRad / 2))
        var maxMercN = Math.log(Math.tan(Math.PI / 4 + 85 * Math.PI / 180 / 2))
        return (maxMercN - mercN) / (2 * maxMercN)
    }

    // Convert lat/lon to screen coordinates (Web Mercator projection) - for flat map
    function latLonToXY(lat, lon) {
        var x = (lon - mapMinLon) / (mapMaxLon - mapMinLon) * width
        var y = latToMercatorY(lat) * height
        return { x: x, y: y, visible: true }
    }

    // Convert lat/lon to 3D sphere coordinates (static, rotation handled by parent Node)
    function latLonTo3D(lat, lon, radius) {
        // Offset latitude to match texture position (empirical adjustment)
        var adjustedLat = lat - 22
        var latRad = adjustedLat * Math.PI / 180
        var lonRad = lon * Math.PI / 180

        // Standard spherical coordinates (Y-up)
        var x = radius * Math.cos(latRad) * Math.sin(lonRad)
        var y = radius * Math.sin(latRad)
        var z = radius * Math.cos(latRad) * Math.cos(lonRad)

        return { x: x, y: y, z: z }
    }

    // Calculate opacity based on age in hours (fade over 24 hours)
    function getOpacityFromAge(ageHours) {
        return Math.max(0.2, 1 - (ageHours / 24))
    }

    // Texture source based on mapTexture selection (reactive property binding)
    readonly property string textureSource: {
        if (mapTexture === "dark") return "qrc:/maps/earth_night.png"
        if (mapTexture === "satellite") return "qrc:/maps/earth_day.jpg"
        if (mapTexture === "bright") return "qrc:/maps/earth_bright.png"
        return "qrc:/maps/earth_night.png"
    }

    // Background
    Rectangle {
        anchors.fill: parent
        color: "#0a0a12"
    }

    // ==================== FLAT MAP VIEW ====================
    Item {
        id: flatMapView
        anchors.fill: parent
        visible: mapShape === "flat"

        // Map image background
        Image {
            id: mapImage
            anchors.fill: parent
            fillMode: Image.Stretch
            source: root.textureSource
            visible: status === Image.Ready
        }

        // Fallback grid if images not loaded
        Canvas {
            id: mapCanvas
            anchors.fill: parent
            visible: mapImage.status !== Image.Ready

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = "#1a2a3a"
                ctx.lineWidth = 1

                for (var lon = -180; lon <= 180; lon += 30) {
                    var pos = latLonToXY(0, lon)
                    ctx.beginPath()
                    ctx.moveTo(pos.x, 0)
                    ctx.lineTo(pos.x, height)
                    ctx.stroke()
                }

                for (var lat = -60; lat <= 75; lat += 15) {
                    var pos2 = latLonToXY(lat, 0)
                    ctx.beginPath()
                    ctx.moveTo(0, pos2.y)
                    ctx.lineTo(width, pos2.y)
                    ctx.stroke()
                }
            }
        }

        // Dark overlay for non-bright modes to make markers pop
        Rectangle {
            anchors.fill: parent
            color: "#000000"
            opacity: mapTexture === "bright" ? 0 : 0.3
            visible: mapImage.status === Image.Ready
        }

        // Shot markers (flat map)
        Repeater {
            model: shots

            Item {
                id: flatShotMarker
                property var pos: latLonToXY(modelData.lat, modelData.lon)
                property real ageHours: modelData.age || 0
                property real shotOpacity: getOpacityFromAge(ageHours)
                property bool isNew: ageHours < (1/60)

                x: pos.x - width / 2
                y: pos.y - height / 2
                width: root.widgetMode ? 6 : 20
                height: width
                visible: pos.visible
                opacity: shotOpacity

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 2
                    height: width
                    radius: width / 2
                    color: mapTexture === "bright" ? "#ff6b35" : "#4a9eff"
                    opacity: 0.3

                    SequentialAnimation on scale {
                        running: flatShotMarker.isNew && !root.widgetMode
                        loops: 3
                        NumberAnimation { from: 1; to: 2; duration: 500; easing.type: Easing.OutQuad }
                        NumberAnimation { from: 2; to: 1; duration: 500; easing.type: Easing.InQuad }
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: root.widgetMode ? 3 : 8
                    height: width
                    radius: width / 2
                    color: mapTexture === "bright" ? "#ff8c5a" : "#7abdff"
                }
            }
        }

        // Test mode marker (flat)
        Item {
            id: flatTestMarker
            visible: testMode && testLatitude !== 0 && testLongitude !== 0
            property var pos: latLonToXY(testLatitude, testLongitude)
            x: pos.x - width / 2
            y: pos.y - height / 2
            width: 40
            height: 40

            Rectangle {
                anchors.centerIn: parent
                width: 60
                height: 60
                radius: 30
                color: "#22ff6b35"
                border.color: "#ff6b35"
                border.width: 2

                SequentialAnimation on scale {
                    running: flatTestMarker.visible
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.5; to: 1.5; duration: 1000; easing.type: Easing.OutQuad }
                    NumberAnimation { from: 1.5; to: 0.5; duration: 1000; easing.type: Easing.InQuad }
                }

                SequentialAnimation on opacity {
                    running: flatTestMarker.visible
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 0.3; duration: 1000 }
                    NumberAnimation { from: 0.3; to: 1; duration: 1000 }
                }
            }

            Rectangle {
                anchors.centerIn: parent
                width: 16
                height: 16
                radius: 8
                color: "#ff6b35"
                border.color: "#ffffff"
                border.width: 2
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: 5
                width: flatYouLabel.width + 10
                height: flatYouLabel.height + 4
                radius: 4
                color: "#ff6b35"

                Text {
                    id: flatYouLabel
                    anchors.centerIn: parent
                    text: "YOU"
                    color: "#ffffff"
                    font.pixelSize: 10
                    font.bold: true
                    font.family: Theme.bodyFont.family
                }
            }
        }
    }

    // ==================== 3D GLOBE VIEW (loaded only when Quick3D available) ====================
    Loader {
        id: globeLoader
        anchors.fill: parent
        active: Settings.hasQuick3D && mapShape === "globe"
        visible: mapShape === "globe"
        source: "qrc:/qt/qml/DecenzaDE1/qml/components/ShotMapGlobe.qml"
        onLoaded: {
            item.shots = Qt.binding(function() { return root.shots })
            item.mapTexture = Qt.binding(function() { return root.mapTexture })
            item.textureSource = Qt.binding(function() { return root.textureSource })
            item.globeRotation = Qt.binding(function() { return root.globeRotation })
            item.globeRadius = Qt.binding(function() { return root.globeRadius })
            item.testMode = Qt.binding(function() { return root.testMode })
            item.testLatitude = Qt.binding(function() { return root.testLatitude })
            item.testLongitude = Qt.binding(function() { return root.testLongitude })
            item.widgetMode = Qt.binding(function() { return root.widgetMode })
        }
    }

    // ==================== OVERLAYS (shared for both views) ====================

    // Clock display (top center)
    Text {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 30
        text: Qt.formatTime(new Date(), WeatherManager.use12HourTime ? "h:mmap" : "HH:mm")
        color: mapTexture === "bright" ? "#ffffff" : "#aabbcc"
        font.pixelSize: 48
        font.bold: true
        font.family: Theme.bodyFont.family
        opacity: 0.9
        visible: showClock && !testMode

        Timer {
            interval: 1000
            running: showClock && root.visible
            repeat: true
            onTriggered: parent.text = Qt.formatTime(new Date(), WeatherManager.use12HourTime ? "h:mmap" : "HH:mm")
        }
    }

    // Top profiles display (top right)
    Column {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 30
        spacing: 8
        opacity: 0.85
        visible: showProfiles && topProfiles.length > 0 && !testMode

        Text {
            text: "Top Profiles"
            color: mapTexture === "bright" ? "#ffffff" : "#8899aa"
            font.pixelSize: 14
            font.bold: true
            font.family: Theme.bodyFont.family
        }

        Repeater {
            model: topProfiles

            Row {
                spacing: 8

                Text {
                    text: modelData.name || "Unknown"
                    color: mapTexture === "bright" ? "#dddddd" : "#667788"
                    font.pixelSize: 12
                    font.family: Theme.bodyFont.family
                    width: 150
                    elide: Text.ElideRight
                }

                Text {
                    text: "(" + (modelData.count || 0) + ")"
                    color: mapTexture === "bright" ? "#aaaaaa" : "#556677"
                    font.pixelSize: 11
                    font.family: Theme.bodyFont.family
                }
            }
        }
    }

    // Stats overlay (bottom left)
    Column {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.margins: 30
        spacing: 10
        opacity: 0.8
        visible: !widgetMode

        Text {
            text: shotCount + " shots in the last 24 hours"
            color: mapTexture === "bright" ? "#ffffff" : "#8899aa"
            font.pixelSize: 16
            font.family: Theme.bodyFont.family
        }

        Text {
            text: "decenza.coffee"
            color: mapTexture === "bright" ? "#aaaaaa" : "#556677"
            font.pixelSize: 12
            font.family: Theme.bodyFont.family
        }
    }

    // Touch to exit hint
    Text {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 30
        text: "Touch to exit"
        color: "#444455"
        font.pixelSize: 11
        font.family: Theme.bodyFont.family
        visible: !testMode && !widgetMode
    }
}
