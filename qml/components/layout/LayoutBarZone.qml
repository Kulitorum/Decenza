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
    // Overrides the colour the zone's items draw their text and icons in. Set by
    // LayoutPreview when it is previewing a background colour that has not been applied,
    // where Theme's own values still describe the CURRENT background. Transparent = unset.
    property color contentColorOverride: "transparent"
    // Companion to contentColorOverride for the fills widgets draw — see LayoutItemDelegate.
    property color fillColorOverride: "transparent"
    // "auto" (bar = compact) or "large" to render widgets in the big center style.
    property string itemSize: "auto"          // auto | compact | large
    // Visual scale of the bar's content (matches center zones' scale control).
    property real zoneScale: 1.0

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

    // Grow to fit large-style / scaled items; bar height otherwise.
    implicitHeight: Math.max(Theme.bottomBarHeight, itemsRow.implicitHeight * root.zoneScale)
    implicitWidth: itemsRow.implicitWidth * root.zoneScale

    RowLayout {
        id: itemsRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacingMedium

        // Scale the content visually; pin the transform to the alignment edge so
        // scaled content doesn't drift away from where it's aligned.
        scale: root.zoneScale
        transformOrigin: root.alignment === "left" ? Item.Left
            : (root.alignment === "right" ? Item.Right : Item.Center)

        // Alignment via flexible end-fillers (packed mode only). When the zone is
        // wider than its content, the visible filler(s) absorb the slack and push
        // the content group left / center / right. In fill modes (equalWidth /
        // spaced) or when the user added their own spacer, both fillers are hidden
        // and the delegates' own fill behaviour governs the row, so alignment has
        // no effect — matching the documented behaviour.
        // Leading filler: present for center + right.
        Item {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Layout.minimumWidth: 0
            visible: !root.fillRow && (root.alignment === "center" || root.alignment === "right")
        }

        Repeater {
            model: root.items
            delegate: LayoutItemDelegate {
                zoneName: root.zoneName
                itemSize: root.itemSize
                zoneTextColor: root.contentColorOverride.a > 0 ? root.contentColorOverride
                                                              : Theme.zoneTextColor(root.zoneStyle)
                zoneValueBold: Theme.zoneValueBold(root.zoneStyle)
                zoneFillOverride: root.fillColorOverride
                zoneStyle: root.zoneStyle
                // equalWidth/spaced: every item gets an equal share of the width,
                // independent of content width. packed: only spacers grow.
                Layout.fillWidth: modelData.type === "spacer" || root.fillWidthMode
                Layout.preferredWidth: root.fillWidthMode ? 1 : -1
                // Cap the shot plan at the bar width — RowLayout does not shrink
                // items below implicit width, and the plan's text elides once its
                // delegate is width-bound (single line in bars).
                Layout.maximumWidth: modelData.type === "shotPlan"
                    ? root.width : Number.POSITIVE_INFINITY
            }
        }

        // Trailing filler: present for center + left.
        Item {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Layout.minimumWidth: 0
            visible: !root.fillRow && (root.alignment === "center" || root.alignment === "left")
        }
    }
}
