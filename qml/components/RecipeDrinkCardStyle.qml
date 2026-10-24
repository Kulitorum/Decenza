import QtQuick

// Explicit presentation tokens for the shared recipe-card view. Hosts own the
// values; this type has no dependency on Decenza's Theme singleton.
QtObject {
    property real scale: 1.0
    property real cardPadding: 16
    property real smallSpacing: 8
    property real mediumSpacing: 16
    property real thumbnailSize: 44
    property real iconSize: 16
    property real cardRadius: 16

    property color cardBackgroundColor: "white"
    property color borderColor: "gray"
    property color accentColor: "blue"
    property color warningColor: "#9a4139"
    property color textColor: "black"
    property color secondaryTextColor: "gray"
    // Qt.font() requires a valid font-map property. Native and remote hosts
    // override these baseline sizes with their shared theme tokens.
    property font titleFont: Qt.font({ pointSize: 14 })
    property font captionFont: Qt.font({ pointSize: 12 })

    function scaled(value: real): int {
        return Math.round(value * scale)
    }
}
