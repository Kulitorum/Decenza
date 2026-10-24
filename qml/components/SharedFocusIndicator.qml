pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T

Rectangle {
    id: root

    required property SharedButtonStyle buttonStyle
    required property SharedButtonInteraction interaction
    required property T.Control targetItem
    property real targetRadius: 0

    anchors.fill: parent
    anchors.margins: -buttonStyle.focusMargin
    visible: targetItem.visualFocus === true
             || (targetItem.activeFocus && interaction.accessibilityEnabled)
    color: "transparent"
    border.width: buttonStyle.focusBorderWidth
    border.color: buttonStyle.focusColor
    radius: targetRadius > 0 ? targetRadius + buttonStyle.focusMargin : 4

    SequentialAnimation on border.color {
        running: root.visible
        loops: Animation.Infinite
        ColorAnimation {
            to: root.buttonStyle.focusPulseColor
            duration: 500
        }
        ColorAnimation {
            to: root.buttonStyle.focusColor
            duration: 500
        }
    }
}
