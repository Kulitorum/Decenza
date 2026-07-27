import QtQuick
import Decenza

// A text label that announces itself when tapped (for accessibility)
Text {
    id: root

    property string announcement: text  // Override for custom announcement

    MouseArea {
        anchors.fill: parent
        onClicked: {
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                AccessibilityManager.announce(root.announcement, true)
            }
        }
    }
}
