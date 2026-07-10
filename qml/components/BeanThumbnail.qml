import QtQuick
import Decenza
import "."

// The bean/bag photo thumbnail — ONE implementation of the on-disk image
// cache pattern shared by BagCard, RecipeDrinkCard, and the recipe wizard's
// bag tiles: resolve the cached photo file for `imageKey`, ask the cache to
// backfill it when missing (ensureBagImage with the coffee name + product
// link), swap in the photo when bagImageReady fires, and keep a dimmed
// placeholder icon so imageless bags stay aligned in mixed lists.
Rectangle {
    id: thumb

    // Cache key: the canonical Bean Base id, or "bag-<rowid>" for a manual
    // bag's own photo. Empty = placeholder only, no cache traffic.
    property string imageKey: ""
    // Backfill inputs for ensureBagImage: coffee name (image search) and the
    // product-page link (og:image).
    property string fallbackName: ""
    property string link: ""
    // Legacy pre-removal blobs may still carry a CDN `image` URL — used
    // directly while no cached file exists (BagCard's fallback).
    property string legacyImageUrl: ""
    // Placeholder icon (tea bags pass the tea icon).
    property alias iconSource: placeholderIcon.source
    property real iconSize: Theme.scaled(22)
    // Decode resolution — never the full photo. Hosts with larger thumbs
    // (the wizard's bag tiles) raise it.
    property real imageSourceSize: Theme.scaled(88)

    property string cachedImagePath: ""
    function refreshImage() {
        if (imageKey.length === 0) {
            cachedImagePath = ""
            return
        }
        cachedImagePath = MainController.beanbase.bagImagePath(imageKey)
        if (cachedImagePath.length === 0)
            MainController.beanbase.ensureBagImage(imageKey, fallbackName, link)
    }
    Component.onCompleted: refreshImage()
    onImageKeyChanged: refreshImage()
    Connections {
        target: MainController.beanbase
        function onBagImageReady(id, path) {
            if (id === thumb.imageKey)
                thumb.cachedImagePath = path
        }
    }

    radius: Theme.scaled(6)
    color: Theme.backgroundColor
    border.color: Theme.borderColor
    border.width: 1

    ColoredIcon {
        id: placeholderIcon
        anchors.centerIn: parent
        visible: thumbImage.status !== Image.Ready
        source: "qrc:/icons/coffeebeans.svg"
        iconWidth: thumb.iconSize
        iconHeight: thumb.iconSize
        iconColor: Theme.textSecondaryColor
        opacity: 0.5
        Accessible.ignored: true
    }

    Image {
        id: thumbImage
        anchors.fill: parent
        anchors.margins: 1
        visible: status === Image.Ready
        source: thumb.cachedImagePath.length > 0
            ? "file:///" + thumb.cachedImagePath
            : thumb.legacyImageUrl
        sourceSize.width: thumb.imageSourceSize
        sourceSize.height: thumb.imageSourceSize
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        Accessible.ignored: true
    }
}
