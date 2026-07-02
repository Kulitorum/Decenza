import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza

// Layout widget: measured milk weight (composable-brew-bar).
// Shows the live in-session milk while steaming (sessionMeasuredMilkG on the
// window root) and falls back to the last committed session weight
// (Settings.brew.lastSteamMilkG); "—" when neither is available.
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property color zoneTextColor: Theme.textColor
    property bool zoneValueBold: false

    readonly property string labelText: TranslationManager.translate("idle.status.milk", "Milk")

    property bool scaleConnected: ScaleDevice && ScaleDevice.connected

    // Pitcher tare for the selected steam preset (0 when none saved/disabled).
    function _pitcherWeight() {
        var p = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
        return (p && !p.disabled) ? (p.pitcherWeightG ?? 0) : 0
    }

    // Captured/committed milk for this session: the auto-captured value held on
    // the window root (set at capture, reset to 0 at session end / pitcher change).
    readonly property double sessionMilkG: {
        var win = root.Window.window
        return (win && win.sessionMeasuredMilkG > 0) ? win.sessionMeasuredMilkG : 0
    }
    // Live net milk on the scale right now (pitcher tare subtracted). Needs a saved
    // pitcher weight to be meaningful; 0 otherwise.
    readonly property double liveNetMilkG: {
        var p = root._pitcherWeight()
        // Gate on the steam phase: without it, a cup heavier than the saved pitcher
        // tare sitting on the scale in a non-steam context would show a phantom
        // "milk" reading (mirrors the steam-phase gate ScaleWeightItem's
        // contextAware mode already uses).
        return (root.scaleConnected && p > 0
                && MachineState.phase === MachineStateType.Phase.Steaming)
               ? Math.max(0, MachineState.scaleWeight - p) : 0
    }
    // Show the captured value once it lands (held), else the live net milk while
    // the pitcher is on the scale, else the last committed session weight.
    readonly property double milkG: root.sessionMilkG > 0 ? root.sessionMilkG
                                    : (root.liveNetMilkG > 0.3 ? root.liveNetMilkG
                                    : Settings.brew.lastSteamMilkG)
    readonly property string valueText: root.milkG > 0 ? root.milkG.toFixed(1) + " g" : "—"

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
