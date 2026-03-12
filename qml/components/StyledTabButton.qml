import QtQuick
import QtQuick.Controls
import Decenza

// Classic tab button - active tab has top-rounded corners and merges with content
TabButton {
    id: root

    // For accessibility - builds accessible name from text and selection state
    property string tabLabel: ""  // Set for translated accessibility
    property string accessibleName: {
        var label = tabLabel !== "" ? tabLabel : root.text
        var tabStr = TranslationManager.translate("common.tab", "tab")
        var selectedStr = root.checked ? ", " + TranslationManager.translate("common.selected", "selected") : ""
        return label + " " + tabStr + selectedStr
    }

    width: implicitWidth
    leftPadding: Theme.scaled(8)
    rightPadding: Theme.scaled(8)
    topPadding: Theme.scaled(6)
    bottomPadding: Theme.scaled(6)
    font.pixelSize: Theme.scaled(13)
    font.bold: root.checked

    contentItem: Text {
        text: root.text
        font: root.font
        color: root.checked ? Theme.textColor : Theme.textSecondaryColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        color: "transparent"

        // Active tab shape
        Rectangle {
            visible: root.checked
            anchors.fill: parent
            anchors.bottomMargin: -1  // extend below to cover tab bar border
            color: Theme.backgroundColor
            border.color: Theme.borderColor
            border.width: 1
            radius: Theme.scaled(4)

            // Cover bottom rounded corners and bottom border
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 1
                anchors.rightMargin: 1
                height: Theme.scaled(5)
                color: Theme.backgroundColor
            }
        }
    }

    Accessible.role: Accessible.PageTab
    Accessible.name: root.accessibleName
    Accessible.focusable: true

    // Focus indicator - only for keyboard navigation, not mouse clicks
    FocusIndicator {
        targetItem: root
        visible: root.visualFocus
    }
}
