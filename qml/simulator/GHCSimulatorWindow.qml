// The LED-ring Repeater delegate reads this window's geometry properties; Bound makes
// them statically resolvable. It declares its one injected role, `index`, required in
// the same edit -- without that, Bound stops role injection and all twelve LEDs stack
// on one position at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import Decenza

Window {
    id: ghcWindow
    // Both GHCSimulator and GHCSimulatorService expose the same controller API.
    property var controller: null
    property var translator: null
    property bool controllerAvailable: false
    property bool useSharedHidePreference: false
    property bool sharedHidden: false
    property bool localHidden: false
    property var settingsApp: null
    property var positionStore: null
    property real initialX: -1
    property real initialY: -1
    property bool stopHeld: false

    title: translate("ghcSimulator.title", "GHC Simulator")
    width: 400
    height: 470
    minimumWidth: 300
    minimumHeight: 350
    readonly property bool simulatorAvailable: controllerAvailable
    visibility: simulatorAvailable
            && !(settingsApp ? settingsApp.hideGhcSimulator : sharedHidden)
            && !localHidden
        ? Window.Windowed : Window.Hidden
    color: "#1a1a1a"

    function translate(key, fallback) {
        // Read the version so translated bindings refresh when the language changes.
        const version = translator ? translator.translationVersion : 0
        return translator ? translator.translateString(key, fallback) : fallback
    }

    function pressStop() {
        if (!stopHeld && controllerAvailable && controller) {
            stopHeld = true
            controller.pressStop()
        }
    }

    function releaseStop() {
        if (stopHeld) {
            stopHeld = false
            if (controller) controller.releaseStop()
        }
    }

    // Restore position supplied by the creator; the browser keeps this local to
    // its window, while native wiring can preserve the existing saved position.
    Component.onCompleted: {
        if (initialX >= 0 && initialY >= 0) {
            ghcWindow.x = initialX
            ghcWindow.y = initialY
        }
    }

    // Window lifecycle is client-local. The creator decides whether hiding is
    // also persisted through an existing native shared preference.
    onClosing: function(close) {
        close.accepted = false
        if (useSharedHidePreference && settingsApp) settingsApp.hideGhcSimulator = true
        else if (useSharedHidePreference) hideRequested()
        else localHidden = true
    }
    onVisibleChanged: {
        if (!visible) {
            releaseStop()
            if (x >= 0 && y >= 0) {
                if (positionStore) {
                    positionStore.setValue("ghcWindow/x", ghcWindow.x)
                    positionStore.setValue("ghcWindow/y", ghcWindow.y)
                }
                positionSaved(ghcWindow.x, ghcWindow.y)
            }
        }
    }

    onActiveChanged: {
        if (active && controller) controller.ghcWindowActivated()
        else releaseStop()
    }
    onControllerAvailableChanged: if (!controllerAvailable) releaseStop()
    onSharedHiddenChanged: if (!sharedHidden) localHidden = false

    Connections {
        target: ghcWindow.controller
        function onRaiseGhcWindow() {
            if (ghcWindow.visible) ghcWindow.raise()
        }
    }

    // Scale factor based on window size
    readonly property real scale: Math.min(width / 400, (height - 30) / 440)
    readonly property real dialCenterX: width / 2
    readonly property real dialCenterY: (height - 30) / 2
    readonly property real ledRingRadius: 160 * scale
    readonly property real ledSize: 16 * scale
    readonly property real buttonRadius: 45 * scale

    signal hideRequested()
    signal positionSaved(real savedX, real savedY)

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
            required property int index

            width: ghcWindow.ledSize
            height: ghcWindow.ledSize
            radius: ghcWindow.ledSize / 2
            x: ghcWindow.dialCenterX + ghcWindow.ledRingRadius * Math.sin(led.index * Math.PI / 6) - width / 2
            y: ghcWindow.dialCenterY - ghcWindow.ledRingRadius * Math.cos(led.index * Math.PI / 6) - height / 2
            color: {
                const colors = ghcWindow.controller ? ghcWindow.controller.ledColors : []
                return colors.length > led.index ? colors[led.index] : "#1e1e1e"
            }
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
        id: hotWaterControl
        x: ghcWindow.dialCenterX - ghcWindow.buttonRadius
        y: ghcWindow.dialCenterY - ghcWindow.ledRingRadius - ghcWindow.buttonRadius / 2
        width: ghcWindow.buttonRadius * 2
        height: ghcWindow.buttonRadius
        onClicked: if (ghcWindow.controllerAvailable) ghcWindow.controller.pressHotWater()
        cursorShape: Qt.PointingHandCursor
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: ghcWindow.translate("ghcSimulator.hotWater", "Hot Water")
        Accessible.focusable: true
        Accessible.onPressAction: hotWaterControl.clicked(null)
        Keys.onReturnPressed: function(event) { hotWaterControl.clicked(null); event.accepted = true }
        Keys.onSpacePressed: function(event) { hotWaterControl.clicked(null); event.accepted = true }

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
        id: steamControl
        x: ghcWindow.dialCenterX + ghcWindow.ledRingRadius - ghcWindow.buttonRadius / 2
        y: ghcWindow.dialCenterY - ghcWindow.buttonRadius
        width: ghcWindow.buttonRadius
        height: ghcWindow.buttonRadius * 2
        onClicked: if (ghcWindow.controllerAvailable) ghcWindow.controller.pressSteam()
        cursorShape: Qt.PointingHandCursor
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: ghcWindow.translate("ghcSimulator.steam", "Steam")
        Accessible.focusable: true
        Accessible.onPressAction: steamControl.clicked(null)
        Keys.onReturnPressed: function(event) { steamControl.clicked(null); event.accepted = true }
        Keys.onSpacePressed: function(event) { steamControl.clicked(null); event.accepted = true }

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
        id: espressoControl
        x: ghcWindow.dialCenterX - ghcWindow.buttonRadius
        y: ghcWindow.dialCenterY + ghcWindow.ledRingRadius - ghcWindow.buttonRadius / 2
        width: ghcWindow.buttonRadius * 2
        height: ghcWindow.buttonRadius
        onClicked: if (ghcWindow.controllerAvailable) ghcWindow.controller.pressEspresso()
        cursorShape: Qt.PointingHandCursor
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: ghcWindow.translate("ghcSimulator.espresso", "Espresso")
        Accessible.focusable: true
        Accessible.onPressAction: espressoControl.clicked(null)
        Keys.onReturnPressed: function(event) { espressoControl.clicked(null); event.accepted = true }
        Keys.onSpacePressed: function(event) { espressoControl.clicked(null); event.accepted = true }

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
        id: flushControl
        x: ghcWindow.dialCenterX - ghcWindow.ledRingRadius - ghcWindow.buttonRadius / 2
        y: ghcWindow.dialCenterY - ghcWindow.buttonRadius
        width: ghcWindow.buttonRadius
        height: ghcWindow.buttonRadius * 2
        onClicked: if (ghcWindow.controllerAvailable) ghcWindow.controller.pressFlush()
        cursorShape: Qt.PointingHandCursor
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: ghcWindow.translate("ghcSimulator.flush", "Flush")
        Accessible.focusable: true
        Accessible.onPressAction: flushControl.clicked(null)
        Keys.onReturnPressed: function(event) { flushControl.clicked(null); event.accepted = true }
        Keys.onSpacePressed: function(event) { flushControl.clicked(null); event.accepted = true }

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
        id: stopControl
        x: ghcWindow.dialCenterX - ghcWindow.buttonRadius
        y: ghcWindow.dialCenterY - ghcWindow.buttonRadius
        width: ghcWindow.buttonRadius * 2
        height: ghcWindow.buttonRadius * 2
        onPressed: ghcWindow.pressStop()
        onReleased: ghcWindow.releaseStop()
        onCanceled: ghcWindow.releaseStop()
        cursorShape: Qt.PointingHandCursor
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: ghcWindow.translate("ghcSimulator.stop", "Stop")
        Accessible.focusable: true
        Accessible.onPressAction: {
            ghcWindow.pressStop()
            ghcWindow.releaseStop()
        }
        Keys.onReturnPressed: function(event) { ghcWindow.pressStop(); ghcWindow.releaseStop(); event.accepted = true }
        Keys.onSpacePressed: function(event) { ghcWindow.pressStop(); ghcWindow.releaseStop(); event.accepted = true }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: parent.pressed ? "#ff6666" : "transparent"
            border.width: 2
            radius: ghcWindow.buttonRadius
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
                if (!ghcWindow.simulatorAvailable) return ghcWindow.translate("ghcSimulator.unavailable", "Simulator unavailable")
                switch (ghcWindow.controller ? ghcWindow.controller.activeFunction : 0) {
                    case 1: return ghcWindow.translate("ghcSimulator.espresso", "Espresso")
                    case 2: return ghcWindow.translate("ghcSimulator.steam", "Steam")
                    case 3: return ghcWindow.translate("ghcSimulator.hotWater", "Hot Water")
                    case 4: return ghcWindow.translate("ghcSimulator.flush", "Flush")
                    default: return ghcWindow.translate("ghcSimulator.idle", "Idle")
                }
            }
            color: "#ccc"
            font.pixelSize: Theme.scaled(12)
        }
    }
}
