import QtQuick
import QtQuick.Effects
import Decenza

// Round "more below" button over the bottom of a scrolling area. Shown while content remains
// below the visible part of `flickable` (a ScrollView's contentItem is one); a tap scrolls on.
Rectangle {
    id: root

    required property Flickable flickable
    // Share of the visible height one tap scrolls, in (0, 1].
    property real step: 0.3

    width: Theme.scaled(28)
    height: Theme.scaled(28)
    radius: Theme.scaled(14)
    color: Theme.primaryColor
    border.color: Theme.primaryContrastColor
    border.width: Theme.scaled(2)
    opacity: 0.9
    visible: flickable.contentHeight > flickable.height && !flickable.atYEnd

    Image {
        anchors.centerIn: parent
        source: "qrc:/icons/ArrowLeft.svg"
        sourceSize.width: Theme.scaled(16)
        sourceSize.height: Theme.scaled(16)
        rotation: -90
        Accessible.ignored: true
        layer.enabled: true
        layer.smooth: true
        layer.effect: MultiEffect {
            colorization: 1.0
            colorizationColor: Theme.primaryContrastColor
        }
    }

    AccessibleMouseArea {
        anchors.fill: parent
        accessibleName: TranslationManager.translate("accessibility.scrolldown", "Scroll down")
        onAccessibleClicked: {
            var f = root.flickable
            var step = Math.min(1, Math.max(0.05, root.step))
            f.contentY = Math.min(f.originY + f.contentHeight - f.height, f.contentY + f.height * step)
        }
    }
}
