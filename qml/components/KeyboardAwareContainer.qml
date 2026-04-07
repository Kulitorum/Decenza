import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Decenza

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

    // Optional Flickable to scroll on Android (adjustPan can't scroll inside Flickables)
    property Flickable targetFlickable: null

    // Set true when inside a Dialog/Popup overlay where Android adjustPan
    // doesn't work. Uses keyboard-height-aware scrolling instead.
    property bool inOverlay: false

    // Current shift amount
    property real keyboardOffset: 0

    // Estimated keyboard height for use in Flickable contentHeight bindings.
    // Updated when a text field gains focus in overlay mode.
    property real estimatedKeyboardHeight: 0

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

        // Android uses adjustPan which handles keyboard avoidance at the OS level.
        // Skip container shift to avoid double-shifting, but scroll the Flickable
        // since adjustPan can't scroll inside Qt Flickables.
        // Exception: adjustPan doesn't work for Dialogs/Popups in the Qt overlay,
        // so inOverlay mode falls through to the keyboard-height-aware path below.
        if (Qt.platform.os === "android" && !inOverlay) {
            if (targetFlickable)
                ensureFieldVisibleInFlickable(focusedField)
            keyboardOffset = 0
            return
        }

        // Use real keyboard height if available; estimate only on mobile
        var kbHeight = Qt.inputMethod.keyboardRectangle.height
        if (kbHeight <= 0) {
            if (Qt.platform.os === "android" || Qt.platform.os === "ios")
                kbHeight = root.height * 0.5
            else
                kbHeight = 0  // Desktop — no on-screen keyboard
        }

        if (kbHeight <= 0) {
            keyboardOffset = 0
            return
        }

        // For overlays with a Flickable, scroll instead of shifting the container.
        // This keeps the dialog in place and scrolls content within it.
        if (inOverlay && targetFlickable) {
            keyboardOffset = 0
            estimatedKeyboardHeight = kbHeight
            // Use mapToItem for both top and bottom to handle scaled parents correctly
            var overlayFieldTop = focusedField.mapToItem(targetFlickable.contentItem, 0, 0)
            var overlayFieldBottom = focusedField.mapToItem(targetFlickable.contentItem, 0, focusedField.height)
            var overlayMargin = 20
            var overlayVisibleHeight = root.height - kbHeight
            var overlayMaxContentY = Math.max(0, targetFlickable.contentHeight - targetFlickable.height)
            if (overlayFieldBottom.y + overlayMargin > targetFlickable.contentY + overlayVisibleHeight) {
                targetFlickable.contentY = Math.min(
                    overlayFieldBottom.y + overlayMargin - overlayVisibleHeight, overlayMaxContentY)
            }
            return
        }

        var visibleBottom = root.height - kbHeight

        // Get field's original position (undo current shift)
        var fieldPos = focusedField.mapToItem(root, 0, 0)
        var fieldBottomOriginal = fieldPos.y + focusedField.height + keyboardOffset
        var margin = root.height * 0.05

        keyboardOffset = Math.max(0, fieldBottomOriginal - visibleBottom + margin)
    }

    function ensureFieldVisibleInFlickable(field) {
        if (!targetFlickable) return

        var fieldPos = field.mapToItem(targetFlickable.contentItem, 0, 0)
        var fieldBottom = fieldPos.y + field.height

        // This function is only called on Android where adjustPan handles
        // keyboard avoidance at the OS level (shifts the window up).
        // Only scroll the Flickable when the field is outside its normal
        // viewport — don't subtract keyboard height since adjustPan handles that.
        var visibleHeight = targetFlickable.height
        var margin = 20
        var maxContentY = Math.max(0, targetFlickable.contentHeight - targetFlickable.height)

        // Scroll up if field is above visible area
        if (fieldPos.y < targetFlickable.contentY + margin) {
            targetFlickable.contentY = Math.max(0, fieldPos.y - margin)
        }
        // Scroll down if field is below visible area
        else if (fieldBottom + margin > targetFlickable.contentY + visibleHeight) {
            targetFlickable.contentY = Math.min(
                fieldBottom + margin - visibleHeight, maxContentY)
        }
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
            // Defer reset: Qt fires activeFocusChanged(false) on the old field
            // before activeFocusChanged(true) on the new field. Without deferral,
            // estimatedKeyboardHeight resets to 0 between fields, causing the
            // Flickable contentHeight to shrink and contentY to clamp.
            Qt.callLater(function() {
                if (!hasActiveFocus()) {
                    keyboardOffset = 0
                    estimatedKeyboardHeight = 0
                }
            })
        }
    }

    // Tap outside to dismiss keyboard
    MouseArea {
        anchors.fill: parent
        z: -1
        Accessible.ignored: true
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
