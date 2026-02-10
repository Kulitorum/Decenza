import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    // Extract screensaver subtype from the layout item type string
    // e.g. "screensaverFlipClock" -> "flipclock"
    readonly property string screensaverSubtype: {
        var type = modelData.type || ""
        if (type === "screensaverFlipClock") return "flipclock"
        if (type === "screensaverPipes") return "pipes"
        if (type === "screensaverAttractor") return "attractor"
        if (type === "screensaverShotMap") return "shotmap"
        return ""
    }

    readonly property bool requiresQuick3D:
        screensaverSubtype === "pipes" || screensaverSubtype === "shotmap"

    readonly property bool canRender: !requiresQuick3D || Settings.hasQuick3D

    readonly property string displayLabel: {
        switch (screensaverSubtype) {
            case "flipclock": return "Flip Clock"
            case "pipes":     return "3D Pipes"
            case "attractor": return "Attractors"
            case "shotmap":   return "Shot Map"
            default:          return "Screensaver"
        }
    }

    readonly property string displayIcon: {
        switch (screensaverSubtype) {
            case "flipclock": return "qrc:/icons/sleep.svg"
            case "pipes":     return "qrc:/icons/sparkle.svg"
            case "attractor": return "qrc:/icons/sparkle.svg"
            case "shotmap":   return "qrc:/icons/wifi.svg"
            default:          return "qrc:/icons/sleep.svg"
        }
    }

    // Flip clock scale: 0.0 = small (fit width), 1.0 = large (fit height)
    readonly property real clockScale: typeof modelData.clockScale === "number" ? modelData.clockScale : 1.0
    // Shot map width: 1.0 = standard, 1.7 = wide
    readonly property real mapScale: typeof modelData.mapScale === "number" ? modelData.mapScale : 1.0
    // Shot map background: "" = use global, "dark", "bright", "satellite"
    readonly property string mapTexture: typeof modelData.mapTexture === "string" ? modelData.mapTexture : ""
    readonly property bool isFlipClock: screensaverSubtype === "flipclock"

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    function activateScreensaver() {
        ScreensaverManager.screensaverType = root.screensaverSubtype
        if (ScaleDevice && ScaleDevice.connected) {
            ScaleDevice.disableLcd()
        }
        if (DE1Device && DE1Device.connected) {
            DE1Device.goToSleep()
        }
        var win = Window.window
        if (win && typeof win.goToScreensaver === "function") {
            win.goToScreensaver()
        }
    }

    // --- COMPACT MODE (bar zones) ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactPreview.implicitWidth
        implicitHeight: Theme.bottomBarHeight

        Item {
            id: compactPreview
            anchors.fill: parent
            anchors.topMargin: Theme.spacingSmall
            anchors.bottomMargin: Theme.spacingSmall
            clip: true
            implicitWidth: {
                // Width based on the clock's aspect ratio scaled to bar height
                var h = Theme.bottomBarHeight - Theme.spacingSmall * 2
                if (root.isFlipClock) return h * 2.5
                if (root.screensaverSubtype === "shotmap") return h * 1.5 * root.mapScale
                return h * 1.5
            }

            // Flip Clock preview
            FlipClockScreensaver {
                anchors.centerIn: parent
                width: parent.width / 0.756
                height: parent.width / 1.764
                visible: root.isFlipClock
                running: visible && compactContent.visible
                backgroundColor: "transparent"
            }

            // Attractor preview
            StrangeAttractorScreensaver {
                anchors.fill: parent
                visible: root.screensaverSubtype === "attractor"
                running: visible && compactContent.visible
            }

            // Pipes preview (Quick3D) — only create when compact to avoid duplicate View3D
            Loader {
                anchors.fill: parent
                active: Settings.hasQuick3D && root.screensaverSubtype === "pipes" && root.isCompact
                visible: root.screensaverSubtype === "pipes"
                source: "qrc:/qt/qml/DecenzaDE1/qml/components/PipesScreensaver.qml"
                onLoaded: item.running = Qt.binding(function() {
                    return visible && compactContent.visible
                })
            }

            // Shot Map preview (Quick3D) — only create when compact to avoid duplicate View3D
            Loader {
                anchors.fill: parent
                active: Settings.hasQuick3D && root.screensaverSubtype === "shotmap" && root.isCompact
                visible: root.screensaverSubtype === "shotmap"
                source: "qrc:/qt/qml/DecenzaDE1/qml/components/ShotMapScreensaver.qml"
                onLoaded: {
                    item.running = Qt.binding(function() {
                        return visible && compactContent.visible
                    })
                    item.widgetMode = true
                    item.mapTexture = Qt.binding(function() {
                        return root.mapTexture !== "" ? root.mapTexture : ScreensaverManager.shotMapTexture
                    })
                }
            }

            // Fallback when Quick3D not available
            Rectangle {
                anchors.fill: parent
                color: "#333333"
                radius: Theme.cardRadius
                visible: !root.canRender

                Text {
                    anchors.centerIn: parent
                    text: root.displayLabel
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.activateScreensaver()
        }
    }

    // --- FULL MODE (center zones) ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: Theme.scaled(200)
        implicitHeight: Theme.scaled(120)

        // Black background (hidden for flip clock which is transparent)
        Rectangle {
            anchors.fill: parent
            color: "#000000"
            radius: Theme.cardRadius
            visible: !root.isFlipClock
        }

        // Live screensaver preview
        Item {
            anchors.fill: parent
            anchors.margins: root.isFlipClock ? 0 : 1
            clip: true

            // Flip Clock — always sized to fill widget width; widget width controlled by clockScale
            FlipClockScreensaver {
                anchors.centerIn: parent
                // Virtual size makes the clock exactly fill the widget width
                // cardWidth = parent.width / 5.04; virtual dims ensure that's what min() returns
                width: parent.width / 0.756
                height: parent.width / 1.764
                visible: root.isFlipClock
                running: visible && fullContent.visible
                backgroundColor: "transparent"
            }

            // Strange Attractor — direct component (always available, no Quick3D)
            StrangeAttractorScreensaver {
                anchors.fill: parent
                visible: root.screensaverSubtype === "attractor"
                running: visible && fullContent.visible
            }

            // 3D Pipes — Loader with qrc path (requires Quick3D)
            Loader {
                anchors.fill: parent
                active: Settings.hasQuick3D && root.screensaverSubtype === "pipes" && !root.isCompact
                visible: root.screensaverSubtype === "pipes"
                source: "qrc:/qt/qml/DecenzaDE1/qml/components/PipesScreensaver.qml"
                onLoaded: item.running = Qt.binding(function() {
                    return visible && fullContent.visible
                })
            }

            // Shot Map — Loader with qrc path (requires Quick3D)
            Loader {
                anchors.fill: parent
                active: Settings.hasQuick3D && root.screensaverSubtype === "shotmap" && !root.isCompact
                visible: root.screensaverSubtype === "shotmap"
                source: "qrc:/qt/qml/DecenzaDE1/qml/components/ShotMapScreensaver.qml"
                onLoaded: {
                    item.running = Qt.binding(function() {
                        return visible && fullContent.visible
                    })
                    item.widgetMode = true
                    item.mapTexture = Qt.binding(function() {
                        return root.mapTexture !== "" ? root.mapTexture : ScreensaverManager.shotMapTexture
                    })
                }
            }

            // Fallback when Quick3D not available
            Text {
                anchors.centerIn: parent
                visible: !root.canRender
                text: root.displayLabel + "\n(requires 3D)"
                color: Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                font: Theme.captionFont
            }
        }

        // Tap overlay to activate full-screen screensaver
        MouseArea {
            anchors.fill: parent
            onClicked: root.activateScreensaver()
        }
    }
}
