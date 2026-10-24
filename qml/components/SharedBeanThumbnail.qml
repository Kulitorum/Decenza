import QtQuick

// Service-free image presentation shared by native cache and browser HTTP
// adapters. The host supplies an already-resolved URL and placeholder icon.
Item {
    id: root

    required property RecipeDrinkCardStyle thumbnailStyle
    property url imageUrl: ""
    property real iconSize: thumbnailStyle.scaled(22)
    property real imageSourceSize: thumbnailStyle.scaled(88)
    property Component placeholderComponent: null

    Image {
        id: photo
        objectName: "sharedBeanThumbnailPhoto"
        anchors.fill: parent
        anchors.margins: 1
        visible: status === Image.Ready
        source: root.imageUrl
        sourceSize.width: root.imageSourceSize
        sourceSize.height: root.imageSourceSize
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        Accessible.ignored: true
    }

    Loader {
        id: placeholder
        objectName: "sharedBeanThumbnailPlaceholder"
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        active: photo.status !== Image.Ready
        sourceComponent: root.placeholderComponent
        Accessible.ignored: true
    }
}
