import QtQuick

// Resolved tokens for the app's two shared button presentations. Native and
// browser hosts provide their own appearance values and local scale factor.
QtObject {
    property real scaleFactor: 1
    property font bodyFont

    property color primaryColor: "#1769aa"
    property color primaryContrastColor: "white"
    property color warningButtonColor: "#f0a000"
    property color warningForegroundColor: "black"
    property color errorColor: "#b00020"
    property color surfaceColor: "#ffffff"
    property color cardBackgroundColor: "#ffffff"
    property color textColor: "#202020"
    property color textSecondaryColor: "#707070"
    property color borderColor: "#808080"

    property int focusMargin: 3
    property int focusBorderWidth: 2
    property color focusColor: "#1769aa"
    property color focusPulseColor: "#4288c4"

    function scaled(value: real): int {
        return Math.round(value * scaleFactor)
    }
}
