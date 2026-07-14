import QtQuick
import QtQuick.Window
import Decenza

// Shared `background:` for the 7 pages covered by the custom-background
// feature (Idle, Beans, Recipes, Steam, Hot Water, Flush, Equipment). Shows
// Settings.theme.backgroundImagePath when set, falling back to the flat
// Theme.backgroundColor a page always used before — including when the
// stored path no longer resolves to a readable file (deleted personal media,
// evicted cache entry), so a stale setting degrades gracefully instead of
// showing a broken image.
Item {
    id: root

    readonly property bool _hasImage: Settings.theme.backgroundImagePath.length > 0
    readonly property bool _imageFailed: bgImage.status === Image.Error

    Rectangle {
        anchors.fill: parent
        color: Theme.backgroundColor
        visible: !root._hasImage || root._imageFailed
    }

    Image {
        id: bgImage
        anchors.fill: parent
        visible: root._hasImage && status === Image.Ready
        source: root._hasImage ? "file:///" + Settings.theme.backgroundImagePath : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.width: Screen.width
        sourceSize.height: Screen.height
        Accessible.ignored: true
    }
}
