import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../.."

// Layout widget: measured milk weight (composable-brew-bar).
// Shows the live in-session milk while steaming (sessionMeasuredMilkG on the
// window root) and falls back to the last committed session weight
// (Settings.brew.lastSteamMilkG); "—" when neither is available.
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})
    property color zoneTextColor: Theme.textColor
    property bool zoneValueBold: false

    // Per-instance display mode ("text" default | "icon": pitcher icon in place
    // of the label) and color override. See WidgetColor for the shared palette.
    readonly property string displayMode: (modelData && modelData.displayMode) ? modelData.displayMode : "text"
    readonly property string colorChoice: (modelData && modelData.color) ? modelData.color : "default"

    readonly property string labelText: TranslationManager.translate("idle.status.milk", "Milk")
    // Live in-session milk (set during steaming, reset to 0 at session end /
    // pitcher change). 0 when the window or property is unavailable — guarded so
    // the widget degrades to the committed value and never errors.
    readonly property double liveMilkG: {
        var win = root.Window.window
        return (win && win.sessionMeasuredMilkG > 0) ? win.sessionMeasuredMilkG : 0
    }
    readonly property double milkG: liveMilkG > 0 ? liveMilkG : Settings.brew.lastSteamMilkG
    readonly property string valueText: milkG > 0 ? milkG.toFixed(1) + " g" : "—"

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
            visible: root.displayMode !== "icon"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.labelText
            color: root.zoneTextColor
            font: Theme.labelFont
        }
        ThemedIcon {
            visible: root.displayMode === "icon"
            Layout.alignment: Qt.AlignHCenter
            source: "qrc:/icons/steam.svg"
            iconSize: Theme.scaled(20)
            color: WidgetColor.resolve(root.colorChoice, root.zoneTextColor)
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.valueText
            color: WidgetColor.resolve(root.colorChoice, root.zoneTextColor)
            font.pixelSize: Theme.scaled(21)
            font.bold: root.zoneValueBold
        }
    }
}
