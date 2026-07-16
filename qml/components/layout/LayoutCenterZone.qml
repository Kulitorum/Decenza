import QtQuick
import QtQuick.Layouts
import Decenza

Item {
    id: root

    required property string zoneName
    required property var items
    property real zoneScale: 1.0
    // Horizontal alignment of the row within the zone: "center" (default) | "left" | "right".
    // Reads the same per-zone "alignment" option the bar zones read (Settings.network zone
    // options), but the gating differs: a center zone has no distribution, so alignment is
    // suppressed only by a user spacer (hasSpacer) — not by LayoutBarZone's wider fillRow
    // (fillWidthMode || hasSpacer). Default "center" = unchanged behavior.
    property string alignment: "center"
    // Zone style preset: "standard" (default) | "surface" | "accentBar". Applies to item
    // text only. Like the top/bottom bar zones, a center zone paints no background of its
    // own — only statusBar and lowerMidBar do. Default "standard" = unchanged behavior.
    property string zoneStyle: "standard"

    // No "distribution" option by design. A center zone sizes every item from a fixed cell
    // (buttonWidth, capped at Theme.scaled(150) * zoneScale) so action buttons never stretch,
    // while equalWidth and spaced both require items to expand to fill the zone. With the cap
    // kept, the two would collapse into each other and do nothing on a button-only row, so the
    // editors hide the control here rather than offer a value that cannot apply.

    // Items that size to their content instead of using fixed button width
    // Action buttons (espresso, steam, etc.) use fixed buttonWidth; readouts auto-size
    function isAutoSized(type) {
        switch (type) {
            case "spacer":
            case "custom":
            case "shotPlan":
            case "temperature":
            case "waterLevel":
            case "connectionStatus":
            case "machineStatus":
            case "steamTemperature":
            case "batteryLevel":
            case "scaleBattery":
            case "scaleWeight":
            case "weather":
            case "ghcSimulator":
            case "clock":
                return true
            default:
                return false
        }
    }

    // Calculate button sizing with zoneScale baked in (renders at display resolution)
    readonly property int buttonCount: {
        if (!items) return 0
        var count = 0
        for (var i = 0; i < items.length; i++) {
            if (!isAutoSized(items[i].type)) count++
        }
        return count
    }
    readonly property bool hasSpacer: {
        if (!items) return false
        for (var i = 0; i < items.length; i++) {
            if (items[i].type === "spacer") return true
        }
        return false
    }
    readonly property real scaledSpacing: Theme.scaled(10) * zoneScale
    readonly property real availableWidth: width - Theme.scaled(20) * zoneScale -
        (buttonCount > 1 ? (buttonCount - 1) * scaledSpacing : 0)
    readonly property real buttonWidth: buttonCount > 0
        ? Math.min(Theme.scaled(150) * zoneScale, availableWidth / buttonCount) : Theme.scaled(150) * zoneScale
    readonly property real buttonHeight: Theme.scaled(120) * zoneScale

    implicitHeight: contentRow.implicitHeight

    RowLayout {
        id: contentRow
        width: root.width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.scaledSpacing

        // Leading auto-spacer: present for center + right alignment (absent for left → row hugs the left).
        // Hides when the user has their own spacers (they position manually).
        Item { Layout.fillWidth: true; visible: !root.hasSpacer && (root.alignment === "center" || root.alignment === "right") }

        Repeater {
            model: root.items
            delegate: LayoutItemDelegate {
                zoneName: root.zoneName
                zoneTextColor: Theme.zoneTextColor(root.zoneStyle)
                zoneValueBold: Theme.zoneValueBold(root.zoneStyle)
                Layout.preferredWidth: {
                    // Flip clock: interpolate between buttonWidth and wide based on clockScale
                    if (modelData.type === "screensaverFlipClock") {
                        var s = typeof modelData.clockScale === "number" ? modelData.clockScale : 1.0
                        return root.buttonWidth + s * (root.buttonHeight * 3.7 - root.buttonWidth)
                    }
                    // Shot map: scale width from 1x to 1.7x buttonWidth
                    if (modelData.type === "screensaverShotMap") {
                        var m = typeof modelData.mapScale === "number" ? modelData.mapScale : 1.0
                        return root.buttonWidth * m
                    }
                    // Last shot: scale width from 1x to 2.5x buttonWidth
                    if (modelData.type === "lastShot") {
                        var ls = typeof modelData.shotScale === "number" ? modelData.shotScale : 1.0
                        return root.buttonWidth * ls
                    }
                    if (root.isAutoSized(modelData.type)) return -1
                    return root.buttonWidth
                }
                // The shot plan sizes to its content, which can exceed the zone
                // (long bean names, many items). RowLayout does NOT shrink items
                // below their implicit width, so without a cap the plan paints
                // past the screen edge; capped, the text wraps and elides inside.
                Layout.maximumWidth: modelData.type === "shotPlan"
                    ? root.availableWidth : Number.POSITIVE_INFINITY
                Layout.preferredHeight: modelData.type === "spacer" ? -1 : root.buttonHeight
                Layout.fillWidth: modelData.type === "spacer"
            }
        }

        // Trailing auto-spacer: present for center + left alignment (absent for right → row hugs the right).
        Item { Layout.fillWidth: true; visible: !root.hasSpacer && (root.alignment === "center" || root.alignment === "left") }
    }
}
