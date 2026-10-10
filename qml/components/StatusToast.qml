import QtQuick
import Decenza

// The app's one bottom-centre transient message. Callers do `myToast.show(text)`;
// the fade-out is this component's business. A toast that should stay up while a
// condition holds binds `message` and `opacity` instead of calling show().
Rectangle {
    id: toast

    property string message: ""
    // UI auto-dismiss is one of the two sanctioned uses of a Timer.
    property int durationMs: 4000
    property bool error: false

    function show(text) {
        toast.message = text
        toast.opacity = 1
        hideTimer.restart()
    }

    anchors.bottom: parent ? parent.bottom : undefined
    anchors.bottomMargin: Theme.scaled(40)
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    width: Math.min(label.implicitWidth + Theme.scaled(32),
                    parent ? parent.width - Theme.scaled(48) : label.implicitWidth + Theme.scaled(32))
    height: label.implicitHeight + Theme.scaled(16)
    radius: Theme.cardRadius
    color: Theme.surfaceColor
    border.color: Theme.errorColor
    border.width: toast.error ? 1 : 0
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
        width: Math.min(implicitWidth, toast.width - Theme.scaled(32))
        text: toast.message
        color: toast.error ? Theme.errorColor : Theme.textColor
        font.pixelSize: Theme.scaled(13)
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        Accessible.ignored: true
    }

    Timer {
        id: hideTimer
        interval: toast.durationMs
        onTriggered: toast.opacity = 0
    }
}
