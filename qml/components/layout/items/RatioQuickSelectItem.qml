import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

// Layout widget: coffee:water ratio quick-select (composable-brew-bar).
// Shows the current ratio as a 1:X.X pill; tapping opens the ratio chooser.
// Selecting a preset arms a RATIO ANCHOR on the session (add-yield-ratio-
// anchor): the target then derives from the dose and follows it.
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property color zoneTextColor: Theme.textColor
    property bool zoneValueBold: false

    readonly property string labelText: TranslationManager.translate("idle.status.ratio", "Ratio")
    // The ACTUAL active ratio: the stored anchor when ratio-anchored, else
    // derived target ÷ dose — ProfileManager.brewByRatio folds both, the same
    // source the scale widget and Brew Settings use. With no dose recorded a
    // derived ratio is unknowable — show a dash rather than a stand-in.
    readonly property double activeRatio: ProfileManager.brewByRatio > 0
        ? ProfileManager.brewByRatio
        : (ProfileManager.brewByRatioDose > 0
           ? ProfileManager.targetWeight / ProfileManager.brewByRatioDose : 0)
    readonly property string ratioText: activeRatio > 0 ? "1:" + activeRatio.toFixed(1) : "1:—"
    // Override state against the active anchor: the session deviates from
    // the recipe's/bag's stored spec (the shared brew-baseline rule).
    readonly property bool overridden: MainController.yieldIsRealOverride

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
                // Override-highlight when the session deviates from the active
                // recipe's/bag's stored spec — consistent with the Brew
                // Settings rows and the Shot Plan.
                color: root.overridden ? Theme.highlightColor : Theme.primaryColor
                font.pixelSize: Theme.scaled(20)
                font.bold: true
            }
            MouseArea { id: ratioMa; anchors.fill: parent; onClicked: ratioDialog.open() }
        }
    }

    RatioPresetDialog { id: ratioDialog }
}
