import QtQuick
import QtQuick.Controls
import QtQuick.Window

Window {
    id: ghcWindow
    title: "GHC Simulator"
    width: 400
    height: 470
    minimumWidth: 300
    minimumHeight: 350
    visible: true
    color: "#1a1a1a"

    // Restore window position on load (not size - keep default to match real device)
    Component.onCompleted: {
        var savedX = Settings.value("ghcWindow/x", -1)
        var savedY = Settings.value("ghcWindow/y", -1)
        if (savedX >= 0 && savedY >= 0) {
            ghcWindow.x = savedX
            ghcWindow.y = savedY
        }
    }

    // Save window position on close
    onClosing: function(close) {
        Settings.setValue("ghcWindow/x", ghcWindow.x)
        Settings.setValue("ghcWindow/y", ghcWindow.y)
    }

    // Raise all application windows together when this window is activated
    onActiveChanged: {
        if (active) {
            GHCSimulator.ghcWindowActivated()
        }
    }

    // Listen for main window activation to raise ourselves
    Connections {
        target: GHCSimulator
        function onRaiseGhcWindow() {
            ghcWindow.raise()
        }
    }

    // Scale factor based on window size
    readonly property real scale: Math.min(width / 400, (height - 30) / 440)
    readonly property real dialCenterX: width / 2
    readonly property real dialCenterY: (height - 30) / 2
    readonly property real ledRingRadius: 160 * scale
    readonly property real ledSize: 16 * scale
    readonly property real buttonRadius: 45 * scale

    Image {
        id: ghcImage
        width: 400 * scale
        height: 440 * scale
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -15
        source: "qrc:/GHC_Small.png"
        fillMode: Image.PreserveAspectFit
    }

    // LED Ring - 12 LEDs positioned like a clock
    Repeater {
        model: 12
        delegate: Rectangle {
            id: led
            width: ledSize
            height: ledSize
            radius: ledSize / 2
            x: dialCenterX + ledRingRadius * Math.sin(index * Math.PI / 6) - width / 2
            y: dialCenterY - ledRingRadius * Math.cos(index * Math.PI / 6) - height / 2
            color: GHCSimulator ? GHCSimulator.ledColors[index] : "#1e1e1e"
            border.color: Qt.darker(color, 1.3)
            border.width: 1

            // Glow effect for lit LEDs
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 2
                height: parent.height * 2
                radius: width / 2
                color: parent.color
                opacity: 0.3
                visible: parent.color.toString() !== "#1e1e1e"
                z: -1
            }
        }
    }

    // Hot Water Button (top)
    MouseArea {
        x: dialCenterX - buttonRadius
        y: dialCenterY - ledRingRadius - buttonRadius / 2
        width: buttonRadius * 2
        height: buttonRadius
        onClicked: GHCSimulator.pressHotWater()
        cursorShape: Qt.PointingHandCursor

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: parent.pressed ? "#ffcc66" : "transparent"
            border.width: 2
            radius: 10
        }
    }

    // Steam Button (right)
    MouseArea {
        x: dialCenterX + ledRingRadius - buttonRadius / 2
        y: dialCenterY - buttonRadius
        width: buttonRadius
        height: buttonRadius * 2
        onClicked: GHCSimulator.pressSteam()
        cursorShape: Qt.PointingHandCursor

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: parent.pressed ? "#6699ff" : "transparent"
            border.width: 2
            radius: 10
        }
    }

    // Espresso Button (bottom)
    MouseArea {
        x: dialCenterX - buttonRadius
        y: dialCenterY + ledRingRadius - buttonRadius / 2
        width: buttonRadius * 2
        height: buttonRadius
        onClicked: GHCSimulator.pressEspresso()
        cursorShape: Qt.PointingHandCursor

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: parent.pressed ? "#66ff99" : "transparent"
            border.width: 2
            radius: 10
        }
    }

    // Flush Button (left)
    MouseArea {
        x: dialCenterX - ledRingRadius - buttonRadius / 2
        y: dialCenterY - buttonRadius
        width: buttonRadius
        height: buttonRadius * 2
        onClicked: GHCSimulator.pressFlush()
        cursorShape: Qt.PointingHandCursor

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: parent.pressed ? "#66ccff" : "transparent"
            border.width: 2
            radius: 10
        }
    }

    // Stop Button (center)
    MouseArea {
        x: dialCenterX - buttonRadius
        y: dialCenterY - buttonRadius
        width: buttonRadius * 2
        height: buttonRadius * 2
        onPressed: GHCSimulator.pressStop()
        onReleased: GHCSimulator.releaseStop()
        cursorShape: Qt.PointingHandCursor

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: parent.pressed ? "#ff6666" : "transparent"
            border.width: 2
            radius: buttonRadius
        }
    }

    // Status label
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.scaled(5)
        width: statusText.width + 20
        height: 24
        radius: 12
        color: "#333"

        Text {
            id: statusText
            anchors.centerIn: parent
            text: {
                if (!GHCSimulator) return "Not connected"
                switch (GHCSimulator.activeFunction) {
                    case 1: return "Espresso"
                    case 2: return "Steam"
                    case 3: return "Hot Water"
                    case 4: return "Flush"
                    default: return "Idle"
                }
            }
            color: "#ccc"
            font.pixelSize: 12
        }
    }
}
