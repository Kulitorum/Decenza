import QtQuick
import QtQuick.Controls
import Decenza

// Tab bar for StyledTabButton tabs: no Material highlight indicator, a bottom
// border the active tab merges into, and a scrim under glass chrome.
TabBar {
    id: root

    contentItem: ListView {
        model: root.contentModel
        currentIndex: root.currentIndex
        spacing: root.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        snapMode: ListView.SnapToItem
        highlightMoveDuration: 0
        highlight: Item {}
    }

    background: Rectangle {
        // Unselected labels would otherwise sit directly on a background image.
        color: Theme.glassChrome ? Theme.cardBackgroundColor : "transparent"

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.borderColor
        }
    }
}
