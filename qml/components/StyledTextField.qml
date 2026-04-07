import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Decenza

TextField {
    id: control

    // Custom placeholder that disappears on focus/text (no floating animation)
    property string placeholder: ""
    // Explicit accessible name (overrides placeholder for screen readers)
    property string accessibleName: ""

    // Track whether focus was granted via accessibility double-tap (onPressAction)
    property bool _a11yActivated: false
    readonly property bool _accessibilityMode: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

    font.pixelSize: Theme.labelFont.pixelSize
    color: Theme.textColor
    placeholderText: ""  // Disable Material's floating placeholder

    // Accessibility: expose as editable text with label and current value
    Accessible.role: Accessible.EditableText
    Accessible.name: accessibleName || placeholder || placeholderText
    Accessible.description: text
    Accessible.focusable: true

    // In accessibility mode, double-tap is the deliberate activation gesture.
    // Show the keyboard only then — not when TalkBack explore-by-touch gives focus.
    // Non-accessibility mode: still activate the field so VoiceOver/switch-access work.
    Accessible.onPressAction: {
        if (_accessibilityMode) {
            _a11yActivated = true
        }
        control.forceActiveFocus()
        Qt.inputMethod.show()
    }

    // When TalkBack cursor lands on the field, Qt gives it activeFocus and auto-shows
    // the keyboard. In accessibility mode, suppress this so the user can hear the
    // current value first. The keyboard will appear on deliberate double-tap above.
    // Uses Connections (not onActiveFocusChanged) so callers can add their own
    // onActiveFocusChanged without silently replacing this handler.
    Connections {
        target: control
        function onActiveFocusChanged() {
            if (control.activeFocus && control._accessibilityMode && !control._a11yActivated) {
                Qt.inputMethod.hide()
            }
            if (!control.activeFocus) {
                control._a11yActivated = false
            }
        }
    }

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
            font.pixelSize: Theme.labelFont.pixelSize
            elide: Text.ElideRight
            visible: !control.text && !control.activeFocus
        }
    }
}
