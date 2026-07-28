import QtQuick
import Decenza

LayoutWidgetItem {
    id: root

    implicitWidth: Theme.scaled(1)
    implicitHeight: Theme.scaled(30)

    Rectangle {
        anchors.centerIn: parent
        width: Theme.scaled(1)
        height: Theme.scaled(30)
        color: Theme.textSecondaryColor
        opacity: 0.3
    }
}
