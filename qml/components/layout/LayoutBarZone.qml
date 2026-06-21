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

    // A user spacer also wants the row to span the full width so it can expand
    // (e.g. the status bar's pageTitle-then-spacer layout). Fill the row for fill
    // modes or when a spacer is present; otherwise shrink-to-content and align.
    readonly property bool hasSpacer: {
        for (var i = 0; i < items.length; i++)
            if (items[i].type === "spacer") return true
        return false
    }
    readonly property bool fillRow: fillWidthMode || hasSpacer

    implicitHeight: Theme.bottomBarHeight
    implicitWidth: itemsRow.implicitWidth

    RowLayout {
        id: itemsRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: (root.fillRow || root.alignment === "left") ? parent.left : undefined
        anchors.right: (root.fillRow || root.alignment === "right") ? parent.right : undefined
        anchors.horizontalCenter: (!root.fillRow && root.alignment === "center") ? parent.horizontalCenter : undefined
        width: root.fillRow ? parent.width : Math.min(implicitWidth, parent.width)
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
