import QtQuick
import QtQuick.Layouts
import DecenzaDE1
import "layout"

Rectangle {
    color: Theme.surfaceColor

    // Parse statusBar zone from layout config
    property var statusBarItems: {
        var raw = Settings.layoutConfiguration
        try {
            var parsed = JSON.parse(raw)
            return (parsed.zones && parsed.zones.statusBar) || []
        } catch(e) {
            return []
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.chartMarginSmall
        anchors.rightMargin: Theme.spacingLarge
        spacing: Theme.spacingMedium

        Repeater {
            model: statusBarItems
            delegate: LayoutItemDelegate {
                zoneName: "statusBar"
                Layout.fillWidth: modelData.type === "spacer"
            }
        }
    }
}
