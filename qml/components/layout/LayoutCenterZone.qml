import QtQuick
import QtQuick.Layouts

Item {
    id: root

    required property string zoneName
    required property var items

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

    // Calculate button sizing (auto-sized items don't count)
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
    readonly property real availableWidth: width - Theme.scaled(20) -
        (buttonCount > 1 ? (buttonCount - 1) * Theme.scaled(10) : 0)
    readonly property real buttonWidth: buttonCount > 0
        ? Math.min(Theme.scaled(150), availableWidth / buttonCount) : Theme.scaled(150)
    readonly property real buttonHeight: Theme.scaled(120)

    implicitHeight: contentRow.implicitHeight

    Component.onCompleted: {
    }

    RowLayout {
        id: contentRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.scaled(10)

        // Auto-centering spacer (hides when user has their own spacers)
        Item { Layout.fillWidth: true; visible: !root.hasSpacer }

        Repeater {
            model: root.items
            delegate: LayoutItemDelegate {
                zoneName: root.zoneName
                Layout.preferredWidth: root.isAutoSized(modelData.type) ? -1 : root.buttonWidth
                Layout.preferredHeight: modelData.type === "spacer" ? -1 : root.buttonHeight
                Layout.fillWidth: modelData.type === "spacer"
            }
        }

        // Auto-centering spacer (hides when user has their own spacers)
        Item { Layout.fillWidth: true; visible: !root.hasSpacer }
    }
}
