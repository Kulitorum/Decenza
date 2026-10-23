import QtQuick

// Resolved layout and palette tokens for the shared bottom navigation bar.
// Hosts provide their own theme projection; this type contains no app services.
QtObject {
    property real scaleFactor: 1
    property int barHeight: 70
    property int chartMarginSmall: 10
    property int spacingLarge: 24
    property int spacingMedium: 16
    property int titleFontSize: 20
    property int backIconSize: 28
    property int focusRadius: 4
    property font bodyFont
    property color defaultBarColor: "#ffffff"
    property color iconColor: "#202020"
    property color glassBarColor: "#ffffff"
    property color borderColor: "#808080"
    property int focusMargin: 2
    property int focusBorderWidth: 3
    property color focusColor: "#1769aa"
    property bool glassChrome: false

    function scaled(value: real): int {
        return Math.round(value * scaleFactor)
    }
}
