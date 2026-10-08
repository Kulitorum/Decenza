// The dash Repeater reads `swatch`; Bound resolves it. Its delegate takes no model roles.
pragma ComponentBehavior: Bound

import QtQuick
import Decenza

// The line pattern a comparison column is drawn with on the graph: solid, dashed,
// dash-dot — the same patterns ComparisonGraph._shotStyles uses — heavier for the base.
Item {
    id: swatch
    property int shotIndex: 0
    property bool heavy: false
    readonly property int lineHeight: heavy ? Theme.scaled(3) : Theme.scaled(2)

    implicitWidth: Theme.scaled(22)
    implicitHeight: Theme.scaled(12)
    Accessible.ignored: true

    Rectangle {
        visible: swatch.shotIndex % 3 === 0
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width; height: swatch.lineHeight; color: Theme.textColor
    }
    Row {
        visible: swatch.shotIndex % 3 === 1
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.scaled(2)
        Repeater { model: 4; Rectangle { width: Theme.scaled(4); height: swatch.lineHeight; color: Theme.textColor } }
    }
    Row {
        visible: swatch.shotIndex % 3 === 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.scaled(2)
        Rectangle { width: Theme.scaled(7); height: swatch.lineHeight; color: Theme.textColor }
        Rectangle { width: Theme.scaled(2); height: swatch.lineHeight; color: Theme.textColor }
        Rectangle { width: Theme.scaled(7); height: swatch.lineHeight; color: Theme.textColor }
    }
}
