import QtQuick

// Resolved appearance tokens for service-free shared dialogs.
QtObject {
    property real scaleFactor: 1
    // Dialog's native Material defaults are fixed logical pixels, unlike the
    // header and content dimensions that callers commonly scale explicitly.
    property real padding: 24
    property real topPadding: 16
    property font bodyFont
    property font subtitleFont
    property color surfaceColor: "white"
    property color textColor: "#202020"
    property color textSecondaryColor: "#707070"
    property color borderColor: "#808080"
    property color primaryColor: "#1769aa"
    property color dialogDimColor: "#80000000"

    function scaled(value: real): int { return Math.round(value * scaleFactor) }
}
