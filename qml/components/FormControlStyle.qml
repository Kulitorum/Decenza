import QtQuick

// Visual values supplied by a host application to shared form controls.
QtObject {
    property real scale: 1.0
    property font font: Qt.font({ pointSize: 14 })
    property color textColor: "black"
    property color textSecondaryColor: "gray"
    property color primaryColor: "blue"
    property color surfaceColor: "white"
    property color insetBackgroundColor: "white"
    property color borderColor: "gray"

    function scaled(value: real): int {
        return Math.round(value * scale)
    }
}
