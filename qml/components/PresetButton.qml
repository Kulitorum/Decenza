import QtQuick
import QtQuick.Controls
import Decenza

/**
 * PresetButton - A styled button for recipe presets
 */
Button {
    id: root

    property bool selected: false

    implicitWidth: Theme.scaled(90)
    implicitHeight: Theme.scaled(36)

    background: Rectangle {
        radius: Theme.scaled(8)
        color: {
            if (root.selected) return Theme.primaryColor
            if (root.down) return Qt.rgba(255, 255, 255, 0.15)
            if (root.hovered) return Qt.rgba(255, 255, 255, 0.1)
            return Qt.rgba(255, 255, 255, 0.05)
        }
        border.width: root.selected ? 0 : 1
        border.color: Theme.textSecondaryColor

        Behavior on color {
            ColorAnimation { duration: 100 }
        }
    }

    contentItem: Text {
        text: root.text
        font: Theme.captionFont
        color: root.selected ? Theme.primaryContrastColor : Theme.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
