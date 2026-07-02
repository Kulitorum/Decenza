import QtQuick
import QtQuick.Layouts
import Decenza

// Layout widget: measured dose weight (composable-brew-bar).
// Shows Settings.dye.dyeBeanWeight; "—" when no dose recorded.
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property color zoneTextColor: Theme.textColor
    property bool zoneValueBold: false

    readonly property string labelText: TranslationManager.translate("idle.status.beans", "Beans")

    // Live-then-hold dose: show the live net-bean weight while a dose is being
    // weighed, freeze the captured dose once the stable-capture "beep" sets
    // Settings.dye.dyeBeanWeight, and hold it until a fresh dose is placed on the
    // scale. Falls back to the last recorded dose when idle. Event-based — no timers.
    property bool scaleConnected: ScaleDevice && ScaleDevice.connected
    property real _held: 0
    property bool _wasEmpty: true
    function _liveNet() { return Math.max(0, MachineState.scaleWeight - Settings.brew.doseCupTareWeight) }
    readonly property bool _loaded: root.scaleConnected
        && MachineState.scaleWeight > (Settings.brew.doseCupTareWeight + 0.3)

    Connections {
        target: MachineState
        function onScaleWeightChanged() {
            if (MachineState.scaleWeight < 1.0) {
                root._wasEmpty = true            // cup lifted off
            } else if (root._wasEmpty && root._liveNet() > 0.3) {
                root._held = 0                   // a fresh dose is being placed -> go live
                root._wasEmpty = false
            }
        }
    }
    Connections {
        target: Settings.dye
        function onDyeBeanWeightChanged() {
            // Stable-capture "beep" landed while a dose is on the scale -> hold it.
            if (Settings.dye.dyeBeanWeight > 0 && root._loaded)
                root._held = Settings.dye.dyeBeanWeight
        }
    }

    readonly property string valueText: {
        if (root._held > 0) return root._held.toFixed(1) + " g"          // held captured dose
        if (root._loaded)   return root._liveNet().toFixed(1) + " g"     // live while weighing
        return Settings.dye.dyeBeanWeight > 0                            // idle: last recorded
               ? Settings.dye.dyeBeanWeight.toFixed(1) + " g" : "—"
    }

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
            color: root.zoneTextColor
            font.pixelSize: Theme.scaled(21)
            font.bold: root.zoneValueBold
        }
    }
}
