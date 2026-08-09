import QtQuick
import Decenza

// The app's one bottom-centre transient message.
//
// main.qml carried three byte-identical copies of this block, each with its own
// Timer, its own z-order and its own idea of the duration. Nothing ever failed
// when they drifted because no two of them can render at the same moment — the
// exact shape the "centralize anything produced at more than one site" rule is
// about. Adding a fourth copy was what made extracting it unavoidable.
//
// Callers do `myToast.show(text)`; the fade-out is this component's business.
Rectangle {
    id: toast

    property string message: ""
    // UI auto-dismiss is one of the two sanctioned uses of a Timer.
    property int durationMs: 4000

    function show(text) {
        toast.message = text
        toast.opacity = 1
        hideTimer.restart()
    }

    anchors.bottom: parent ? parent.bottom : undefined
    anchors.bottomMargin: Theme.scaled(40)
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    width: label.implicitWidth + Theme.scaled(32)
    height: label.implicitHeight + Theme.scaled(16)
    radius: Theme.cardRadius
    color: Theme.surfaceColor
    opacity: 0
    visible: opacity > 0
    z: 600
    // The message is announced explicitly by the caller through
    // AccessibilityManager when it warrants it; the rectangle itself is chrome.
    Accessible.ignored: true

    Behavior on opacity {
        NumberAnimation { duration: 300 }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: toast.message
        color: Theme.textColor
        font.pixelSize: Theme.scaled(13)
        Accessible.ignored: true
    }

    Timer {
        id: hideTimer
        interval: toast.durationMs
        onTriggered: toast.opacity = 0
    }
}
