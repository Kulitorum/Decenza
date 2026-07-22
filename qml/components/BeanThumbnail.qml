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
    // Bumped when the cache reports new bytes for this key. A refresh writes
    // to the SAME path, so `cachedImagePath` is reassigned an identical string
    // — no change signal, no re-read — and even a rebuilt delegate would be
    // served the old pixels from Qt's pixmap cache, which is keyed by URL and
    // never checks mtime. Editing a bag's product URL would then show the old
    // roaster's photo until the app restarted. Same fix as LibraryItemCard's
    // _thumbVersion, and it keeps caching on for the common no-change case.
    property int _imageVersion: 0
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
            if (id !== thumb.imageKey)
                return
            thumb.cachedImagePath = path
            thumb._imageVersion++   // new bytes at the same path: bust the cache
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
              + (thumb._imageVersion > 0 ? "?v=" + thumb._imageVersion : "")
            : thumb.legacyImageUrl
        sourceSize.width: thumb.imageSourceSize
        sourceSize.height: thumb.imageSourceSize
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        Accessible.ignored: true
    }
}
