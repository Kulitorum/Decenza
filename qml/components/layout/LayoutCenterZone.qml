import QtQuick
import QtQuick.Layouts
import DecenzaDE1

Item {
    id: root

    required property string zoneName
    required property var items
    property real zoneScale: 1.0

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
            case "steamTemperature":
            case "scaleWeight":
            case "weather":
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

    Component.onCompleted: {
    }

    RowLayout {
        id: contentRow
        width: root.width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.scaledSpacing

        // Auto-centering spacer (hides when user has their own spacers)
        Item { Layout.fillWidth: true; visible: !root.hasSpacer }

        Repeater {
            model: root.items
            delegate: LayoutItemDelegate {
                zoneName: root.zoneName
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
                Layout.preferredHeight: modelData.type === "spacer" ? -1 : root.buttonHeight
                Layout.fillWidth: modelData.type === "spacer"
            }
        }

        // Auto-centering spacer (hides when user has their own spacers)
        Item { Layout.fillWidth: true; visible: !root.hasSpacer }
    }
}
