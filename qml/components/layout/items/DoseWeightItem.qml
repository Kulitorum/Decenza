import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza

// Layout widget: measured dose weight (composable-brew-bar).
// Shows Settings.dye.dyeBeanWeight; "—" when no dose recorded. While the idle
// page's bean auto-capture is weighing a dose it shows the engine's live
// virtual-zero net instead (doseLiveNetG on the window root, -1 when not
// weighing) in the secondary color, and flashes the accent color at capture —
// the same numbers and timing as the espresso panel's readout.
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property color zoneTextColor: Theme.textColor
    property bool zoneValueBold: false

    readonly property string labelText: TranslationManager.translate("idle.status.beans", "Beans")
    // Live dose state published on the window root by IdlePage (see main.qml).
    // Guarded so the widget degrades to the recorded dose when the window or
    // property is unavailable.
    readonly property real liveNetG: {
        var win = root.Window.window
        return (win && win.doseLiveNetG !== undefined) ? win.doseLiveNetG : -1
    }
    readonly property bool captureFlash: {
        var win = root.Window.window
        return win ? (win.doseCaptureFlash === true) : false
    }
    readonly property bool isLive: liveNetG >= 0
    readonly property string valueText: root.isLive
                                        ? root.liveNetG.toFixed(1) + " g"
                                        : Settings.dye.dyeBeanWeight > 0
                                          ? Settings.dye.dyeBeanWeight.toFixed(1) + " g"
                                          : "—"

    implicitWidth: col.implicitWidth
    implicitHeight: col.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: root.labelText + ": " + root.valueText
    Accessible.focusable: true

    ColumnLayout {
        id: col
        anchors.centerIn: parent
        width: parent.width
        spacing: 0
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.labelText
            color: root.zoneTextColor
            font: Theme.labelFont
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.valueText
            // Secondary while a live (unsettled) weight is showing, accent flash
            // at capture, else the zone's configured color.
            color: root.captureFlash ? Theme.primaryColor
                 : root.isLive ? Theme.textSecondaryColor
                 : root.zoneTextColor
            font.pixelSize: Theme.scaled(21)
            font.bold: root.zoneValueBold
        }
    }
}
