import QtQuick
import QtQuick.Layouts
import Decenza

Item {
    id: root

    required property string zoneName
    required property var items

    // Per-zone options (composable-brew-bar). Defaults preserve today's behaviour.
    property string distribution: "packed"   // packed | equalWidth | spaced
    property string alignment: "center"       // left | center | right
    property string zoneStyle: "standard"     // standard | surface | accentBar

    // Fill modes occupy the full width, so alignment has no effect on them.
    readonly property bool fillWidthMode: distribution === "equalWidth" || distribution === "spaced"

    implicitHeight: Theme.bottomBarHeight
    implicitWidth: itemsRow.implicitWidth

    RowLayout {
        id: itemsRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: (root.fillWidthMode || root.alignment === "left") ? parent.left : undefined
        anchors.right: (root.fillWidthMode || root.alignment === "right") ? parent.right : undefined
        anchors.horizontalCenter: (!root.fillWidthMode && root.alignment === "center") ? parent.horizontalCenter : undefined
        width: root.fillWidthMode ? parent.width : Math.min(implicitWidth, parent.width)
        spacing: Theme.spacingMedium

        Repeater {
            model: root.items
            delegate: LayoutItemDelegate {
                zoneName: root.zoneName
                zoneTextColor: Theme.zoneTextColor(root.zoneStyle)
                zoneValueBold: Theme.zoneValueBold(root.zoneStyle)
                // equalWidth/spaced: every item gets an equal share of the width,
                // independent of content width. packed: only spacers grow.
                Layout.fillWidth: modelData.type === "spacer" || root.fillWidthMode
                Layout.preferredWidth: root.fillWidthMode ? 1 : -1
            }
        }
    }
}
