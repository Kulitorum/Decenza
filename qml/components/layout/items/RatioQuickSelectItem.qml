import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

// Layout widget: coffee:water ratio quick-select (composable-brew-bar).
// Shows the current ratio as a 1:X.X pill; tapping opens the ratio chooser.
// Selecting a preset sets ONLY Settings.brew.lastUsedRatio (never the persistent
// profile target / brewYieldOverride).
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property color zoneTextColor: Theme.textColor
    property bool zoneValueBold: false

    readonly property string labelText: TranslationManager.translate("idle.status.ratio", "Ratio")
    readonly property string ratioText: "1:" + Settings.brew.lastUsedRatio.toFixed(1)

    implicitWidth: col.implicitWidth
    implicitHeight: col.implicitHeight

    ColumnLayout {
        id: col
        anchors.centerIn: parent
        width: parent.width
        spacing: Theme.scaled(2)

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.labelText
            color: root.zoneTextColor
            font: Theme.labelFont
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: ratioValue.implicitWidth + Theme.spacingMedium * 2
            Layout.preferredHeight: Theme.scaled(32)
            radius: height / 2
            color: ratioMa.pressed ? Qt.darker(root.zoneTextColor, 1.15) : root.zoneTextColor

            Accessible.role: Accessible.Button
            Accessible.name: root.labelText + " " + root.ratioText + ". "
                             + TranslationManager.translate("idle.ratio.tapToChange", "Tap to change")
            Accessible.focusable: true
            Accessible.onPressAction: ratioMa.clicked(null)

            Text {
                id: ratioValue
                anchors.centerIn: parent
                text: root.ratioText
                // Pill fill is the zone text color (light on an accentBar zone);
                // accent-colored text reads against it in the intended placements.
                color: Theme.primaryColor
                font.pixelSize: Theme.scaled(20)
                font.bold: true
            }
            MouseArea { id: ratioMa; anchors.fill: parent; onClicked: ratioDialog.open() }
        }
    }

    RatioPresetDialog { id: ratioDialog }
}
