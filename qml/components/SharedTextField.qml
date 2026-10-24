import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

TextField {
    id: control

    required property FormControlStyle controlStyle

    // Custom placeholder that disappears on focus/text (no floating animation).
    property string placeholder: ""
    // Explicit accessible name (overrides placeholder for screen readers).
    property string accessibleName: ""
    // Field fill — defaults to the host's inset background.
    property color fieldColor: controlStyle.insetBackgroundColor

    // The host decides whether screen-reader activation needs special handling.
    property bool accessibilityMode: false
    readonly property bool _accessibilityMode: accessibilityMode
    property bool _a11yActivated: false

    signal keyboardShowRequested()
    signal keyboardHideRequested()

    // Bind subproperties so existing native callers can override font.bold,
    // font.pixelSize or font.family without competing with a whole-font binding.
    font.family: controlStyle.font.family
    font.pixelSize: controlStyle.font.pixelSize
    font.bold: controlStyle.font.bold
    font.italic: controlStyle.font.italic
    font.letterSpacing: controlStyle.font.letterSpacing
    color: controlStyle.textColor
    placeholderText: ""  // Disable Material's floating placeholder.
    activeFocusOnTab: true

    // Accessibility: expose as editable text with label and current value.
    Accessible.role: Accessible.EditableText
    Accessible.name: accessibleName || placeholder || placeholderText
    // Never hand a masked password to a screen reader.
    Accessible.description: echoMode === TextInput.Normal ? text : ""
    Accessible.focusable: true

    // In accessibility mode, double-tap is the deliberate activation gesture.
    // Show the keyboard only then — not when TalkBack explore-by-touch gives focus.
    // Non-accessibility mode: still activate the field so VoiceOver/switch-access work.
    Accessible.onPressAction: {
        if (control._accessibilityMode)
            control._a11yActivated = true
        control.forceActiveFocus()
        control.keyboardShowRequested()
    }

    // Suppress the input method when screen-reader exploration moves focus here.
    // Connections lets a consumer keep its own onActiveFocusChanged handler.
    Connections {
        target: control
        function onActiveFocusChanged() {
            if (control.activeFocus && control._accessibilityMode && !control._a11yActivated)
                control.keyboardHideRequested()
            if (!control.activeFocus)
                control._a11yActivated = false
        }
    }

    // The Wasm accessibility bridge needs ValueChanged for programmatic text
    // changes as well as edits; Qt 6.12's own TextUpdated event is not handled
    // by its editable-text bridge. Wait for displayTextChanged: textChanged
    // precedes the accessible display-value update (qtdeclarative/src/quick/
    // items/qquicktextinput.cpp:3818-3824,3064-3077). Native is a no-op here.
    Connections {
        target: control
        function onDisplayTextChanged() {
            AccessibleValueChange.notifyTextValueChanged(control)
        }
    }

    // Disable Material's floating label completely.
    Material.containerStyle: Material.Outlined

    // Explicit padding. The 8px insets land the implicit height at ~36 to match
    // the app field standard (ValueInput / SuggestionField / StyledComboBox).
    leftPadding: controlStyle.scaled(12)
    rightPadding: controlStyle.scaled(12)
    topPadding: controlStyle.scaled(8)
    bottomPadding: controlStyle.scaled(8)

    // Default: dismiss keyboard on Enter (can be overridden with Keys handlers).
    Keys.onReturnPressed: function(event) {
        control.focus = false
        control.keyboardHideRequested()
    }
    Keys.onEnterPressed: function(event) {
        control.focus = false
        control.keyboardHideRequested()
    }

    background: Rectangle {
        color: control.fieldColor
        // Match ValueInput's rounding.
        radius: control.controlStyle.scaled(8)
        border.color: control.activeFocus ? control.controlStyle.primaryColor
                                          : control.controlStyle.textSecondaryColor
        border.width: 1

        // Decorative placeholder; accessible label is on the TextField itself.
        Text {
            Accessible.ignored: true
            anchors.fill: parent
            anchors.leftMargin: control.leftPadding
            anchors.rightMargin: control.rightPadding
            verticalAlignment: Text.AlignVCenter
            text: control.placeholder || control.placeholderText
            color: control.controlStyle.textSecondaryColor
            font: control.controlStyle.font
            elide: Text.ElideRight
            visible: !control.text && !control.activeFocus
        }
    }
}
