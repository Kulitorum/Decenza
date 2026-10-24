import QtQuick

// Singleton-free accessible hit target for the graph's end marker. The host
// supplies its existing exploration state and handles announcements.
MouseArea {
    id: markerArea
    required property string accessibleName
    property var accessibilityItem: parent
    property bool accessibilityEnabled: false
    property bool previouslyAnnounced: false

    signal accessibleClicked()
    signal explorationRequested(string announcement, var item)
    signal explorationCleared(var item)

    cursorShape: Qt.PointingHandCursor
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.focusable: true
    Accessible.onPressAction: markerArea.accessibleClicked()
    Accessible.onToggleAction: markerArea.accessibleClicked()

    Component.onDestruction: {
        if (previouslyAnnounced)
            explorationCleared(accessibilityItem)
    }

    onClicked: {
        if (accessibilityEnabled && !previouslyAnnounced)
            explorationRequested(accessibleName, accessibilityItem)
        else
            accessibleClicked()
    }
}
