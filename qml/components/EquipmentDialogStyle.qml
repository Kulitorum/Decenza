import QtQuick

// Explicit visual tokens consumed by the shared equipment picker/editor.
QtObject {
    required property SharedDialogStyle dialogStyle
    required property SharedButtonStyle buttonStyle
    required property SharedButtonInteraction buttonInteraction
    required property SharedKeyboardInteraction keyboardInteraction
    required property FormControlStyle formControlStyle
    required property SharedSwitchStyle switchStyle

    property real spacingSmall: 8
    property real spacingMedium: 16
    property real cardRadius: 10
    property font titleFont
    property font labelFont
    property font captionFont
    property color warningColor: "#a06000"
    property color errorColor: "#b00020"
}
