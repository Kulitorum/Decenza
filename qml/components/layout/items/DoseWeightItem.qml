import QtQuick
import QtQuick.Layouts
import Decenza

// Layout widget: measured dose weight (composable-brew-bar).
// Live-then-hold: shows the live net-bean weight while weighing, freezes the
// captured dose at the beep, holds it, then falls back to the last recorded
// dose (Settings.dye.dyeBeanWeight), or "—" when none.
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
    // Latched true when a cup sits on the scale during an operation, cleared when the
    // cup is lifted — suppresses a post-shot brew cup reading as live beans in Idle
    // (the _operating phase gate alone doesn't cover the return-to-Idle window).
    property bool _postExtraction: false
    function _liveNet() { return Math.max(0, MachineState.scaleWeight - Settings.brew.doseCupTareWeight) }
    // Live net-bean display is valid only with a saved dose-cup tare (else scale − 0 = gross, mislabelled
    // "beans") AND when the net sits in a plausible dose range — a brew cup's net (≫ any real dose) must not
    // read as beans. When either fails, valueText falls through to the recorded dyeBeanWeight.
    readonly property bool _loaded: root.scaleConnected
        && Settings.brew.doseCupTareWeight > 0
        && MachineState.scaleWeight > (Settings.brew.doseCupTareWeight + 0.3)
        && _liveNet() <= 55

    // Live net-bean display only makes sense while dosing. During espresso preheat or
    // an active operation (preinfusion/pour/ending/steam/water/flush) the scale is
    // measuring the brew cup / milk / water, so a "beans = scale − cup tare" reading
    // there is a phantom; suppress the live path in those phases (the held/recorded
    // dose still shows). Preheat is included because the brew cup goes on the scale
    // then, before Preinfusion flips.
    readonly property bool _operating:
        MachineState.phase === MachineStateType.Phase.EspressoPreheating
        || MachineState.phase === MachineStateType.Phase.Preinfusion
        || MachineState.phase === MachineStateType.Phase.Pouring
        || MachineState.phase === MachineStateType.Phase.Ending
        || MachineState.phase === MachineStateType.Phase.Steaming
        || MachineState.phase === MachineStateType.Phase.HotWater
        || MachineState.phase === MachineStateType.Phase.Flushing

    Connections {
        target: MachineState
        function onScaleWeightChanged() {
            if (MachineState.scaleWeight < 1.0) {
                root._wasEmpty = true            // cup lifted off
                root._held = 0                   // drop the hold so the idle fallback (dyeBeanWeight) stays fresh
                root._postExtraction = false     // cup gone -> clear the post-operation latch
            } else {
                // A cup on the scale during an operation (pour/steam/…): latch until it's
                // lifted, so a brew cup left on after the shot doesn't read as live beans.
                if (root._operating) root._postExtraction = true
                if (root._wasEmpty && root._liveNet() > 0.3) {
                    root._held = 0               // a fresh dose is being placed -> go live
                    root._wasEmpty = false
                }
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
        if (root._loaded && !root._operating && !root._postExtraction)   // live while weighing (not mid/post-operation)
            return root._liveNet().toFixed(1) + " g"
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
