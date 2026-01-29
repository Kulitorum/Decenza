import QtQuick
import QtQuick.Layouts

Item {
    id: root

    required property string zoneName
    required property var items

    implicitHeight: Theme.bottomBarHeight
    implicitWidth: itemsRow.implicitWidth

    Component.onCompleted: {
        console.log("[IdlePage] LayoutBarZone", zoneName, "items count:", items ? items.length : "null", "size:", width, "x", height, "implicitSize:", implicitWidth, "x", implicitHeight)
    }

    RowLayout {
        id: itemsRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(implicitWidth, parent.width)
        spacing: Theme.spacingMedium

        Repeater {
            model: root.items
            onCountChanged: console.log("[IdlePage] LayoutBarZone", root.zoneName, "Repeater count:", count)
            delegate: LayoutItemDelegate {
                zoneName: root.zoneName
                Layout.fillWidth: modelData.type === "spacer"
            }
        }
    }
}
