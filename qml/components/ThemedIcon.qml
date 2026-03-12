import QtQuick
import QtQuick.Effects
import Decenza

// Icon that adapts to the current theme by tinting the source image.
// SVG icons in this project use hardcoded white strokes, which become
// invisible on light backgrounds. This component replaces the icon color
// with the specified tint color using MultiEffect's colorization.
//
// Usage:
//   ThemedIcon {
//       source: "qrc:/icons/settings.svg"
//       iconSize: Theme.scaled(24)
//       color: Theme.textColor  // optional, defaults to textColor
//   }

Item {
    id: root

    property alias source: img.source
    property int iconSize: Theme.scaled(24)
    property color color: Theme.textColor

    implicitWidth: iconSize
    implicitHeight: iconSize

    Image {
        id: img
        anchors.centerIn: parent
        sourceSize.width: root.iconSize
        sourceSize.height: root.iconSize
        layer.enabled: !Theme.isDarkMode
        layer.smooth: true
        layer.effect: MultiEffect {
            colorization: 1.0
            colorizationColor: root.color
        }
    }
}
