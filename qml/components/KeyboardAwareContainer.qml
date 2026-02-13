import QtQuick
import QtQuick.Controls
import DecenzaDE1

// Container that shifts content up when a text field has focus.
// Uses focus state (not Qt.inputMethod which is unreliable on Android).
// When keyboard height is unknown, estimates 50% of screen height.
//
// Usage:
//   KeyboardAwareContainer {
//       textFields: [myTextField1, myTextField2]
//       YourContent { ... }
//   }

Item {
    id: root
    clip: true

    // List of text fields to track
    property var textFields: []

    // Current shift amount
    property real keyboardOffset: 0

    // True when a registered text field has focus
    property bool textFieldFocused: false

    // Content is the default property so children go inside automatically
    default property alias content: contentContainer.data

    function hasActiveFocus() {
        for (var i = 0; i < textFields.length; i++) {
            if (textFields[i] && textFields[i].activeFocus)
                return true
        }
        return false
    }

    function getActiveFocusField() {
        for (var i = 0; i < textFields.length; i++) {
            if (textFields[i] && textFields[i].activeFocus)
                return textFields[i]
        }
        return null
    }

    function updateKeyboardOffset() {
        var focusedField = getActiveFocusField()
        if (!focusedField) {
            keyboardOffset = 0
            return
        }

        // Use real keyboard height if available, otherwise estimate 50%
        var kbHeight = Qt.inputMethod.keyboardRectangle.height
        if (kbHeight <= 0)
            kbHeight = root.height * 0.5

        var visibleBottom = root.height - kbHeight

        // Get field's original position (undo current shift)
        var fieldPos = focusedField.mapToItem(root, 0, 0)
        var fieldBottomOriginal = fieldPos.y + focusedField.height + keyboardOffset
        var margin = root.height * 0.05

        keyboardOffset = Math.max(0, fieldBottomOriginal - visibleBottom + margin)
    }

    // Connect to each text field's focus signal
    onTextFieldsChanged: {
        for (var i = 0; i < textFields.length; i++) {
            textFields[i].activeFocusChanged.connect(_updateFocusState)
        }
    }

    function _updateFocusState() {
        var wasFocused = textFieldFocused
        textFieldFocused = hasActiveFocus()

        if (textFieldFocused) {
            updateKeyboardOffset()
        } else if (wasFocused) {
            keyboardOffset = 0
        }
    }

    // Tap outside to dismiss keyboard
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: {
            var focusedField = root.getActiveFocusField()
            if (focusedField) {
                focusedField.focus = false
                Qt.inputMethod.hide()
            }
        }
    }

    // Content container — shifts up when keyboard is showing
    Item {
        id: contentContainer
        width: parent.width
        height: parent.height
        y: -root.keyboardOffset

        Behavior on y {
            NumberAnimation { duration: 250; easing.type: Easing.OutQuad }
        }
    }
}
