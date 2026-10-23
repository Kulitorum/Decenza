import QtQuick

QtObject {
    property real scaleFactor: 1
    property font bodyFont
    property color primaryColor: "#1769aa"
    property color primaryContrastColor: "white"
    property color insetBackgroundColor: "#dedede"
    property color textColor: "#202020"
    property color textSecondaryColor: "#707070"
    property string fallbackToggleLabel: "Toggle"
    function scaled(value: real): int { return Math.round(value * scaleFactor) }
}
