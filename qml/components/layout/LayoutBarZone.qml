import QtQuick
import QtQuick.Layouts
import DecenzaDE1

Item {
    id: root

    required property string zoneName
    required property var items

    implicitHeight: Theme.bottomBarHeight
    implicitWidth: itemsRow.implicitWidth

    Component.onCompleted: {
    }

    RowLayout {
        id: itemsRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(implicitWidth, parent.width)
        spacing: Theme.spacingMedium

        Repeater {
            model: root.items
            delegate: LayoutItemDelegate {
                zoneName: root.zoneName
                Layout.fillWidth: modelData.type === "spacer"
            }
        }
    }
}
