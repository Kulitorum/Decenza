import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

/**
 * RecipeSection - A styled section container for the Recipe Editor
 * Shows a horizontal line with centered title, optional enable checkbox
 */
Item {
    id: root

    property string title: ""
    property bool canEnable: false      // Show checkbox in header
    property bool sectionEnabled: true  // Section enabled state (when canEnable is true)
    default property alias content: contentColumn.children

    Accessible.role: Accessible.Section
    Accessible.name: root.title

    signal sectionToggled(bool enabled)

    implicitHeight: contentColumn.implicitHeight + (title ? headerRow.height + Theme.scaled(8) : 0)

    // Section header with centered title and lines
    Row {
        id: headerRow
        width: parent.width
        height: title ? Theme.scaled(24) : 0
        visible: title !== ""
        spacing: Theme.scaled(8)

        // Left line
        Rectangle {
            width: Theme.scaled(20)
            height: 1
            color: Theme.textSecondaryColor
            opacity: 0.5
            anchors.verticalCenter: parent.verticalCenter
        }

        // Checkbox (if canEnable)
        CheckBox {
            id: enableCheckbox
            visible: root.canEnable
            checked: root.sectionEnabled
            onToggled: root.sectionToggled(checked)
            width: visible ? implicitWidth : 0
            anchors.verticalCenter: parent.verticalCenter
            padding: 0
            indicator: Rectangle {
                implicitWidth: Theme.scaled(16)
                implicitHeight: Theme.scaled(16)
                radius: Theme.scaled(3)
                color: "transparent"
                border.color: enableCheckbox.checked ? Theme.primaryColor : Theme.textSecondaryColor
                border.width: 1

                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.scaled(10)
                    height: Theme.scaled(10)
                    radius: Theme.scaled(2)
                    color: Theme.primaryColor
                    visible: enableCheckbox.checked
                }
            }
        }

        // Title text
        Text {
            id: titleText
            text: root.title
            font.family: Theme.captionFont.family
            font.pixelSize: Theme.captionFont.pixelSize
            font.bold: true
            color: (root.canEnable && !root.sectionEnabled) ? Theme.textSecondaryColor : Theme.textColor
            anchors.verticalCenter: parent.verticalCenter
        }

        // Right line (fills remaining space)
        Rectangle {
            width: parent.width - Theme.scaled(20) - titleText.width - Theme.scaled(16)
                   - (enableCheckbox.visible ? enableCheckbox.width + Theme.scaled(8) : 0)
            height: 1
            color: Theme.textSecondaryColor
            opacity: 0.5
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // Content area
    ColumnLayout {
        id: contentColumn
        anchors.top: headerRow.bottom
        anchors.topMargin: title ? Theme.scaled(8) : 0
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.scaled(10)
        opacity: (root.canEnable && !root.sectionEnabled) ? 0.4 : 1.0
        enabled: !root.canEnable || root.sectionEnabled
    }
}
