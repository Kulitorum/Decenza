pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

// Service-free rendering for flat backgrounds, tinted patterns and photos.
// Hosts resolve catalogue entries, URLs, and colors before passing them here.
Item {
    id: root

    property color baseColor: "black"
    property bool patternEnabled: false
    property url patternSource: ""
    property color patternInkColor: "white"
    property int patternTileSize: 0
    property real patternOpacity: 0
    property bool photoEnabled: false
    property url photoSource: ""
    property int photoDecodeWidth: 0
    property int photoDecodeHeight: 0

    readonly property int photoStatus: backgroundPhoto.status

    signal photoLoadFailed(url source)

    Rectangle {
        objectName: "sharedBackgroundBase"
        anchors.fill: parent
        color: root.baseColor
        // Keep the base under decoding or failed photos so an invalid image source
        // degrades to the same plain background as an empty source.
        visible: !root.photoEnabled || backgroundPhoto.status !== Image.Ready
    }

    Image {
        id: patternTile
        objectName: "sharedBackgroundPattern"
        anchors.fill: parent
        visible: root.patternEnabled && !root.photoEnabled
        source: visible ? root.patternSource : ""
        fillMode: Image.Tile
        sourceSize.width: visible ? root.patternTileSize : 0
        sourceSize.height: visible ? root.patternTileSize : 0
        opacity: visible ? root.patternOpacity : 0
        asynchronous: true
        cache: true
        Accessible.ignored: true

        layer.enabled: visible
        layer.effect: MultiEffect {
            colorization: 1.0
            colorizationColor: root.patternInkColor
        }
    }

    Image {
        id: backgroundPhoto
        objectName: "sharedBackgroundPhoto"
        anchors.fill: parent
        visible: root.photoEnabled && status === Image.Ready
        source: root.photoEnabled ? root.photoSource : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.width: root.photoDecodeWidth
        sourceSize.height: root.photoDecodeHeight
        Accessible.ignored: true
        onStatusChanged: if (status === Image.Error)
            root.photoLoadFailed(backgroundPhoto.source)
    }
}
