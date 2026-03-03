import QtQuick
import Decenza

Rectangle {
    id: focusIndicator

    // Parent must have focus-related properties
    property Item targetItem: parent

    anchors.fill: parent
    anchors.margins: -Theme.focusMargin

    visible: targetItem.activeFocus
    color: "transparent"
    border.width: Theme.focusBorderWidth
    border.color: Theme.focusColor
    radius: parent.radius ? parent.radius + Theme.focusMargin : 4

    // Pulsing animation when focused
    SequentialAnimation on border.color {
        running: focusIndicator.visible
        loops: Animation.Infinite

        ColorAnimation {
            to: Qt.lighter(Theme.primaryColor, 1.3)
            duration: 500
        }
        ColorAnimation {
            to: Theme.primaryColor
            duration: 500
        }
    }
}
