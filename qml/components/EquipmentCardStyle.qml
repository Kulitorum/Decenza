import QtQuick

// Resolved card-shell values; native actions and browser actions remain host-owned.
QtObject {
    property font bodyFont
    property font labelFont
    property font captionFont
    property font subtitleFont
    property color textColor: "black"
    property color textSecondaryColor: "gray"
    property color warningColor: "orange"
    property real summaryLineSpacing: 2
    property int actionSize: 36
    property int removeHorizontalPadding: 10
    property color backgroundColor: "white"
    property color borderColor: "gray"
    property color selectedBorderColor: "blue"

    property int cardWidth: 360
    property int cardRadius: 16
    property int normalBorderWidth: 1
    property int selectedBorderWidth: 2
    property int outerVerticalPadding: 24
    property int contentPadding: 12
    property int contentSpacing: 6
}
