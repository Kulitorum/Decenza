pragma ComponentBehavior: Bound

import QtQuick
import Decenza

// Shared thumbnail backed by the host image cache in both app surfaces.
Rectangle {
    id: thumb

    // Cache key: the canonical Bean Base id, or "bag-<rowid>" for a manual
    // bag's own photo. Empty = placeholder only, no cache traffic.
    property string imageKey: ""
    property string fallbackName: ""
    property string link: ""
    property string legacyImageUrl: ""
    property url iconSource: "qrc:/icons/coffeebeans.svg"
    property real iconSize: Theme.scaled(22)
    property real imageSourceSize: Theme.scaled(88)

    property string cachedImagePath: ""
    property int _imageVersion: 0

    function refreshImage() {
        if (imageKey.length === 0) {
            cachedImagePath = ""
            return
        }
        cachedImagePath = BeanUiService.bagImagePath(imageKey)
        if (cachedImagePath.length === 0)
            BeanUiService.ensureBagImage(imageKey, fallbackName, link)
    }
    Component.onCompleted: refreshImage()
    onImageKeyChanged: refreshImage()

    Connections {
        target: BeanUiService
        function onStateChanged() {
            if (BeanUiService.ready) thumb.refreshImage()
        }
        function onBagImageReady(id, path) {
            if (id !== thumb.imageKey)
                return
            thumb.cachedImagePath = path
            thumb._imageVersion++
        }
    }

    radius: Theme.scaled(6)
    color: Theme.backgroundColor
    border.color: Theme.borderColor
    border.width: 1

    RecipeDrinkCardStyle {
        id: nativeThumbnailStyle
        scale: Theme.scale
        secondaryTextColor: Theme.textSecondaryColor
    }

    readonly property string _resolvedImageUrl: cachedImagePath.length > 0
        ? cachedImagePath
          + (_imageVersion > 0 ? "?v=" + _imageVersion : "")
        : legacyImageUrl

    Component {
        id: nativePlaceholder
        ColoredIcon {
            source: thumb.iconSource
            iconWidth: thumb.iconSize
            iconHeight: thumb.iconSize
            iconColor: Theme.textSecondaryColor
            opacity: 0.5
            Accessible.ignored: true
        }
    }

    SharedBeanThumbnail {
        anchors.fill: parent
        thumbnailStyle: nativeThumbnailStyle
        imageUrl: thumb._resolvedImageUrl
        iconSize: thumb.iconSize
        imageSourceSize: thumb.imageSourceSize
        placeholderComponent: nativePlaceholder
    }
}
