import QtQuick
import QtQuick.Effects
import Decenza

// Round "more below" button over the bottom of a scrolling area. Shown while content remains
// below the visible part of `flickable` (a ScrollView's contentItem is one); a tap scrolls on.
Rectangle {
    id: root

    required property Flickable flickable
    // Share of the visible height one tap scrolls.
    property real step: 0.3

    width: Theme.scaled(28)
    height: Theme.scaled(28)
    radius: Theme.scaled(14)
    color: Theme.primaryColor
    border.color: Theme.primaryContrastColor
    border.width: 2
    opacity: 0.9
    visible: flickable.contentHeight > flickable.height
             && flickable.contentY + flickable.height < flickable.contentHeight - 10

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
        onAccessibleClicked: root.flickable.contentY = Math.min(
            root.flickable.contentHeight - root.flickable.height,
            root.flickable.contentY + root.flickable.height * root.step)
    }
}
