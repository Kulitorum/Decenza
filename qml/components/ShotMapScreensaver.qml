import QtQuick
import QtQuick.Controls
import DecenzaDE1

Item {
    id: root

    property bool running: true
    property string mapStyle: ScreensaverManager.shotMapStyle  // "dark", "globe", "bright"

    // Shot data from API
    property var shots: []
    property int shotCount: 0

    // Map bounds (Mercator projection)
    readonly property real mapMinLat: -60
    readonly property real mapMaxLat: 75
    readonly property real mapMinLon: -180
    readonly property real mapMaxLon: 180

    // Globe rotation
    property real globeRotation: 0

    Component.onCompleted: {
        console.log("[ShotMapScreensaver] Started, style:", mapStyle)
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
        running: root.running && root.visible && mapStyle === "globe"
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
                        shotCount = data.count || 0
                        console.log("[ShotMapScreensaver] Loaded", shots.length, "shots")
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

    // Convert lat/lon to screen coordinates (Mercator projection)
    function latLonToXY(lat, lon) {
        // Adjust longitude for globe rotation
        if (mapStyle === "globe") {
            lon = lon - globeRotation
            // Wrap around
            while (lon < -180) lon += 360
            while (lon > 180) lon -= 360
        }

        var x = (lon - mapMinLon) / (mapMaxLon - mapMinLon) * width
        var y = (mapMaxLat - lat) / (mapMaxLat - mapMinLat) * height
        return { x: x, y: y, visible: true }
    }

    // Calculate opacity based on shot age (fade over 24 hours)
    function getOpacity(timestamp) {
        var shotTime = new Date(timestamp).getTime()
        var now = Date.now()
        var ageHours = (now - shotTime) / (1000 * 60 * 60)
        return Math.max(0.2, 1 - (ageHours / 24))
    }

    // Background
    Rectangle {
        anchors.fill: parent
        color: mapStyle === "bright" ? "#1a1a2e" : "#0a0a12"
    }

    // World map outline (simplified)
    Canvas {
        id: mapCanvas
        anchors.fill: parent
        visible: mapStyle !== "bright"

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()

            // Draw grid lines
            ctx.strokeStyle = mapStyle === "globe" ? "#1a2a3a" : "#1a1a2a"
            ctx.lineWidth = 1

            // Longitude lines
            for (var lon = -180; lon <= 180; lon += 30) {
                var pos = latLonToXY(0, lon)
                ctx.beginPath()
                ctx.moveTo(pos.x, 0)
                ctx.lineTo(pos.x, height)
                ctx.stroke()
            }

            // Latitude lines
            for (var lat = -60; lat <= 75; lat += 15) {
                var pos = latLonToXY(lat, 0)
                ctx.beginPath()
                ctx.moveTo(0, pos.y)
                ctx.lineTo(width, pos.y)
                ctx.stroke()
            }
        }

        // Redraw when globe rotates
        Connections {
            target: root
            function onGlobeRotationChanged() {
                if (mapStyle === "globe") {
                    mapCanvas.requestPaint()
                }
            }
        }
    }

    // Globe overlay (circular mask effect)
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 0.9
        height: width
        radius: width / 2
        color: "transparent"
        border.color: mapStyle === "globe" ? "#2a4a6a" : "transparent"
        border.width: mapStyle === "globe" ? 3 : 0
        visible: mapStyle === "globe"

        // Glow effect
        Rectangle {
            anchors.fill: parent
            anchors.margins: -10
            radius: width / 2
            color: "transparent"
            border.color: "#1a3a5a"
            border.width: 20
            opacity: 0.3
        }
    }

    // Shot markers
    Repeater {
        model: shots

        Item {
            id: shotMarker
            property var pos: latLonToXY(modelData.lat, modelData.lon)
            property real shotOpacity: getOpacity(modelData.ts)
            property bool isNew: {
                var shotTime = new Date(modelData.ts).getTime()
                var now = Date.now()
                return (now - shotTime) < 60000  // Less than 1 minute old
            }

            x: pos.x - width / 2
            y: pos.y - height / 2
            width: 20
            height: 20
            visible: pos.visible
            opacity: shotOpacity

            // Outer glow
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 2
                height: width
                radius: width / 2
                color: mapStyle === "bright" ? "#ff6b35" : "#4a9eff"
                opacity: 0.3

                // Pulse animation for new shots
                SequentialAnimation on scale {
                    running: shotMarker.isNew
                    loops: 3
                    NumberAnimation { from: 1; to: 2; duration: 500; easing.type: Easing.OutQuad }
                    NumberAnimation { from: 2; to: 1; duration: 500; easing.type: Easing.InQuad }
                }
            }

            // Inner dot
            Rectangle {
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: mapStyle === "bright" ? "#ff8c5a" : "#7abdff"
            }
        }
    }

    // Stats overlay
    Column {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.margins: 30
        spacing: 10
        opacity: 0.8

        Text {
            text: shotCount + " shots in the last 24 hours"
            color: mapStyle === "bright" ? "#ffffff" : "#8899aa"
            font.pixelSize: 16
            font.family: Theme.bodyFont.family
        }

        Text {
            text: "decenza.coffee"
            color: mapStyle === "bright" ? "#aaaaaa" : "#556677"
            font.pixelSize: 12
            font.family: Theme.bodyFont.family
        }
    }

    // Recent shots ticker
    Column {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 30
        spacing: 8
        opacity: 0.9

        Text {
            text: "Recent Shots"
            color: mapStyle === "bright" ? "#ffffff" : "#8899aa"
            font.pixelSize: 14
            font.bold: true
            font.family: Theme.bodyFont.family
        }

        Repeater {
            model: shots.slice(0, 5)  // Show last 5 shots

            Text {
                property real shotOpacity: getOpacity(modelData.ts)
                text: modelData.city + (modelData.profile ? " - " + modelData.profile : "")
                color: mapStyle === "bright" ? "#cccccc" : "#667788"
                font.pixelSize: 12
                font.family: Theme.bodyFont.family
                opacity: shotOpacity
            }
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
    }
}
