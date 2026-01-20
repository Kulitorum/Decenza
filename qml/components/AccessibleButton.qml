import QtQuick
import QtQuick.Controls
import DecenzaDE1

// Button with required accessibility - enforces accessibleName at compile time
Button {
    id: root

    // Required property - will cause compile error if not provided
    required property string accessibleName

    // Optional description for additional context
    property string accessibleDescription: ""

    // Style variants (only one should be true)
    property bool primary: false      // Filled primary color background
    property bool subtle: false       // Glass-like semi-transparent (for dark backgrounds)
    property bool destructive: false  // Red/error color for delete actions
    property bool warning: false      // Orange/warning color for update actions

    // For AccessibleMouseArea to reference
    property Item accessibleItem: root

    implicitHeight: Theme.scaled(44)
    leftPadding: Theme.scaled(20)
    rightPadding: Theme.scaled(20)

    contentItem: Text {
        text: root.text
        font.pixelSize: Theme.scaled(16)
        font.family: Theme.bodyFont.family
        color: {
            if (!root.enabled) return Theme.textSecondaryColor
            if (root.primary || root.subtle || root.destructive || root.warning) return "white"
            return Theme.textColor
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitHeight: Theme.scaled(44)
        color: {
            if (root.subtle) {
                return root.down ? Qt.rgba(1, 1, 1, 0.3) : Qt.rgba(1, 1, 1, 0.2)
            }
            if (root.destructive) {
                return root.down ? Qt.darker(Theme.errorColor, 1.1) : Theme.errorColor
            }
            if (root.warning) {
                return root.down ? Qt.darker("#FFA500", 1.1) : "#FFA500"
            }
            if (root.primary) {
                return root.down ? Qt.darker(Theme.primaryColor, 1.1) : Theme.primaryColor
            }
            return root.down ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
        }
        border.width: (root.primary || root.subtle || root.destructive || root.warning) ? 0 : 1
        border.color: (root.primary || root.subtle || root.destructive || root.warning) ? "transparent" : (root.enabled ? Theme.borderColor : Qt.darker(Theme.borderColor, 1.2))
        radius: Theme.scaled(6)

        Behavior on color {
            ColorAnimation { duration: 100 }
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.description: accessibleDescription
    Accessible.focusable: true

    // Focus indicator
    FocusIndicator {
        targetItem: root
        visible: root.activeFocus
    }

    // Tap-to-announce, tap-again-to-activate for accessibility mode
    // Using TapHandler for better touch responsiveness
    AccessibleTapHandler {
        anchors.fill: parent
        accessibleName: root.accessibleName
        accessibleItem: root.accessibleItem

        onAccessibleClicked: {
            if (root.enabled) {
                root.clicked()
            }
        }
    }

    // Announce button name when focused via keyboard (for accessibility)
    onActiveFocusChanged: {
        if (activeFocus && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.lastAnnouncedItem = root
            AccessibilityManager.announce(root.accessibleName)
        }
    }
}
