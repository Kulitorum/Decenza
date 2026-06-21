import QtQuick
import QtQuick.Layouts
import Decenza

// Layout widget: current espresso profile name (composable-brew-bar).
// Label-over-value readout; picks up the zone's contrast color/emphasis when
// placed in a styled zone (e.g. accentBar).
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property color zoneTextColor: Theme.textColor
    property bool zoneValueBold: false

    readonly property string labelText: TranslationManager.translate("idle.status.profile", "Profile")
    readonly property string valueText: ProfileManager.currentProfileName || "—"

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
