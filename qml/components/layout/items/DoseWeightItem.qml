import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../.."

// Layout widget: measured dose weight (composable-brew-bar).
// Shows Settings.dye.dyeBeanWeight; "—" when no dose recorded. While the idle
// page's bean auto-capture is weighing a dose it shows the engine's live
// virtual-zero net instead (doseLiveNetG on the window root, -1 when not
// weighing) in the secondary color, and flashes the accent color at capture —
// driven by the same capture engine and flash timing as the espresso panel's
// readout (clamped at 0; reverts to the recorded dose once captured, where the
// panel keeps ticking live).
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})
    property color zoneTextColor: Theme.textColor
    property bool zoneValueBold: false

    // Per-instance display mode ("text" default | "icon": beans icon in place
    // of the label) and color override. See WidgetColor for the shared palette.
    readonly property string displayMode: (modelData && modelData.displayMode) ? modelData.displayMode : "text"
    readonly property string colorChoice: (modelData && modelData.color) ? modelData.color : "default"

    readonly property string labelText: TranslationManager.translate("idle.status.beans", "Beans")
    // Live dose state published on the window root by IdlePage (see main.qml).
    // Guarded so the widget degrades to the recorded dose when the window or
    // property is unavailable — but a window WITHOUT the property is a one-sided
    // rename (a wiring bug, not a legitimate state), so warn once to keep that
    // failure greppable (same pattern as SteamPlanText).
    property bool _warnedMissingProp: false
    readonly property real liveNetG: {
        var win = root.Window.window
        if (win && win.doseLiveNetG === undefined && !root._warnedMissingProp) {
            root._warnedMissingProp = true
            console.warn("DoseWeightItem: window root has no doseLiveNetG — live dose readout disabled")
        }
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
            source: "qrc:/icons/coffeebeans.svg"
            iconSize: Theme.scaled(20)
            color: WidgetColor.resolve(root.colorChoice, root.zoneTextColor)
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.valueText
            // A named color override is static in all states; otherwise secondary
            // while a live (unsettled) weight is showing, accent flash at capture,
            // else the zone's configured color.
            color: WidgetColor.resolve(root.colorChoice,
                       root.captureFlash ? Theme.primaryColor
                     : root.isLive ? Theme.textSecondaryColor
                     : root.zoneTextColor)
            font.pixelSize: Theme.scaled(21)
            font.bold: root.zoneValueBold
        }
    }
}
