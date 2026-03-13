import QtQuick
import QtQuick.Controls
import Decenza

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

    // For external reference
    property Item accessibleItem: root

    implicitHeight: Theme.scaled(44)
    leftPadding: Theme.scaled(20)
    rightPadding: Theme.scaled(20)

    // Icon styling — set icon.source to show an icon before the text
    icon.width: Theme.scaled(16)
    icon.height: Theme.scaled(16)
    icon.color: {
        if (!root.enabled) {
            // Disabled primary/colored buttons: use semi-transparent contrast color
            if (root.primary || root.destructive || root.warning) return Qt.rgba(Theme.primaryContrastColor.r, Theme.primaryContrastColor.g, Theme.primaryContrastColor.b, 0.5)
            if (root.subtle) return Qt.rgba(Theme.primaryContrastColor.r, Theme.primaryContrastColor.g, Theme.primaryContrastColor.b, 0.4)
            return Theme.textSecondaryColor
        }
        if (root.primary || root.subtle || root.destructive || root.warning) return Theme.primaryContrastColor
        return Theme.textColor
    }

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight
        Accessible.ignored: true

        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: root.icon.source.toString() !== "" && root.text !== "" ? Theme.scaled(6) : 0
            Accessible.ignored: true

            Image {
                source: root.icon.source
                sourceSize.width: root.icon.width
                sourceSize.height: root.icon.height
                anchors.verticalCenter: parent.verticalCenter
                visible: root.icon.source.toString() !== ""
                opacity: root.enabled ? 1.0 : 0.5
                Accessible.ignored: true
            }

            Text {
                text: root.text
                font.pixelSize: root.font.pixelSize > 0 ? root.font.pixelSize : Theme.scaled(16)
                font.family: root.font.family || Theme.bodyFont.family
                font.weight: root.font.weight
                color: root.icon.color
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                visible: root.text !== ""
                Accessible.ignored: true
            }
        }
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
                return root.down ? Qt.darker(Theme.warningButtonColor, 1.1) : Theme.warningButtonColor
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

    // Accessibility on the Button itself (not delegated to a child)
    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleDescription ? (root.accessibleName + ". " + root.accessibleDescription) : root.accessibleName
    Accessible.focusable: true
    Accessible.onPressAction: {
        if (root.enabled) {
            root.clicked()
        }
    }

    // Focus indicator
    FocusIndicator {
        targetItem: root
        visible: root.activeFocus
    }

    // Clear lastAnnouncedItem when destroyed to prevent dangling pointer crash
    Component.onDestruction: {
        if (typeof AccessibilityManager !== "undefined" &&
            AccessibilityManager.lastAnnouncedItem === root) {
            AccessibilityManager.lastAnnouncedItem = null
        }
    }

    // Plain MouseArea for tap-to-announce in accessibility mode (no Accessible.* — Button handles that)
    MouseArea {
        id: touchArea
        anchors.fill: parent
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor

        onPressed: function(mouse) {
            mouse.accepted = true
        }

        onClicked: {
            var accessibilityMode = typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

            if (accessibilityMode) {
                if (AccessibilityManager.lastAnnouncedItem === root) {
                    // Second tap on same item = activate
                    root.clicked()
                } else {
                    // First tap = announce only
                    AccessibilityManager.lastAnnouncedItem = root
                    AccessibilityManager.announce(root.accessibleName)
                }
            } else {
                // Normal mode: activate immediately
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
