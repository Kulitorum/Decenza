import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import DecenzaDE1

TextField {
    id: control

    // Custom placeholder that disappears on focus/text (no floating animation)
    property string placeholder: ""
    // Explicit accessible name (overrides placeholder for screen readers)
    property string accessibleName: ""

    font.pixelSize: Theme.scaled(18)
    color: Theme.textColor
    placeholderText: ""  // Disable Material's floating placeholder

    // Accessibility: expose as editable text with label and current value
    Accessible.role: Accessible.EditableText
    Accessible.name: accessibleName || placeholder || placeholderText
    Accessible.description: text
    Accessible.focusable: true

    // Disable Material's floating label completely
    Material.containerStyle: Material.Outlined

    // Explicit padding
    leftPadding: Theme.scaled(12)
    rightPadding: Theme.scaled(12)
    topPadding: Theme.scaled(12)
    bottomPadding: Theme.scaled(12)

    // Default: dismiss keyboard on Enter (can be overridden with Keys.onReturnPressed)
    Keys.onReturnPressed: function(event) {
        control.focus = false
        Qt.inputMethod.hide()
    }
    Keys.onEnterPressed: function(event) {
        control.focus = false
        Qt.inputMethod.hide()
    }

    background: Rectangle {
        color: Theme.backgroundColor
        radius: Theme.scaled(4)
        border.color: control.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
        border.width: 1

        // Custom placeholder text that simply disappears (decorative — label is in Accessible.name)
        Text {
            Accessible.ignored: true
            anchors.fill: parent
            anchors.leftMargin: control.leftPadding
            anchors.rightMargin: control.rightPadding
            verticalAlignment: Text.AlignVCenter
            text: control.placeholder || control.placeholderText
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(18)
            elide: Text.ElideRight
            visible: !control.text && !control.activeFocus
        }
    }
}
