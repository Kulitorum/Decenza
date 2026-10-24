pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Shared recipe-name field and duplicate warning. The native page supplies
// NativeFormControlStyle and bridges the keyboard signals; browser hosts can
// supply their own style while reusing this exact editing/accessibility path.
ColumnLayout {
    id: root

    required property FormControlStyle controlStyle
    property string labelText: ""
    property string placeholderText: ""
    property string conflictText: ""
    property bool nameConflict: false
    property string fieldAccessibleName: ""
    property bool accessibilityMode: false
    property font captionFont: controlStyle.font
    property color conflictColor: controlStyle.textSecondaryColor

    property alias text: recipeInput.text
    readonly property alias inputField: recipeInput

    signal keyboardShowRequested()
    signal keyboardHideRequested()
    signal textEdited()

    spacing: controlStyle.scaled(4)

    function focusInput() {
        recipeInput.forceActiveFocus()
    }

    function selectAll() {
        recipeInput.selectAll()
    }

    Label {
        Layout.fillWidth: true
        text: root.labelText
        textFormat: Text.PlainText
        font: root.captionFont
        color: root.controlStyle.textSecondaryColor
        Accessible.ignored: true
    }

    SharedTextField {
        id: recipeInput
        Layout.fillWidth: true
        controlStyle: root.controlStyle
        placeholder: root.placeholderText
        accessibleName: root.fieldAccessibleName
        accessibilityMode: root.accessibilityMode
        onTextEdited: root.textEdited()

        onKeyboardShowRequested: root.keyboardShowRequested()
        onKeyboardHideRequested: root.keyboardHideRequested()
    }

    Label {
        id: conflictLabel
        objectName: "recipeNameConflict"
        Layout.fillWidth: true
        visible: root.nameConflict
        text: root.conflictText
        textFormat: Text.PlainText
        font: root.captionFont
        color: root.conflictColor
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
}
