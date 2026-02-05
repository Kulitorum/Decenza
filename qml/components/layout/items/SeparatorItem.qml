import QtQuick
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

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
