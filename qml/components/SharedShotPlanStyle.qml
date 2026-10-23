import QtQuick

// Visual and geometry tokens for the native/browser shared Shot Plan view.
QtObject {
    property font bodyFont: Qt.font({ pointSize: 14 })
    property color textColor: "black"
    property color pressedColor: "blue"
    property color warningColor: "red"
    property real spacing: 8
    property real iconSize: 20
    property real hitInset: 8
}
