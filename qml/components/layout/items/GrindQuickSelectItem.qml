import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

// Layout widget: grinder dial-in quick-select (composable-brew-bar).
// Thin wrapper over the shared GrindField (replace-grind-inputs-with-picker):
// the label above a tappable pill showing the current dial-in, tapping opens
// the grind/RPM picker. All row generation, stepping and the picker itself
// live in GrindRowSource / GrindField / GrindPickerDialog — this file only
// binds them to the LIVE dial-in (Settings.dye) and applies commits through
// the same write-through path the Brew Settings controls use.
//
// This is one of the TWO surfaces whose grinder context IS the active grinder
// (Brew Settings is the other) — both edit the live dial-in. The record hosts
// inject the grinder that owns their value instead: the shot's in post-shot
// review, the bag's in the beans dialog, the recipe's package in the wizard.
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})
    property color zoneTextColor: Theme.textColor
    property bool zoneValueBold: false
    property string zoneStyle: "standard"
    // See LayoutItemDelegate.
    property color zoneFillOverride: "transparent"

    readonly property string labelText:
        TranslationManager.translate("grind.quickSelect.label", "Grind")

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

        GrindField {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            presentation: "pill"
            zoneTextColor: root.zoneTextColor
            zoneStyle: root.zoneStyle
            zoneFillOverride: root.zoneFillOverride
            grinderBrand: String(Settings.dye.dyeGrinderBrand || "")
            grinderModel: String(Settings.dye.dyeGrinderModel || "")
            grindSetting: String(Settings.dye.dyeGrinderSetting || "")
            rpmValue: Settings.dye.dyeGrinderRpm > 0 ? Settings.dye.dyeGrinderRpm : 0

            // Plain property writes: the SettingsDye setter write-through keeps
            // the active bag + package last-used dial-in current (coffee_bags),
            // matching the Brew Settings and Post-Shot Review write path.
            //
            // Explicit clears APPLY here too. An earlier draft made the pill
            // "the sole exception" that ignored them, reasoning that the live
            // dial-in had never been clearable. That was preserving an
            // ARTEFACT, not a decision: the old pill had no text entry, so a
            // clear was simply not expressible. Now it is — emptying the field
            // and pressing Done is as deliberate as typing a value, and
            // dropping it is the same silent swallow this change removed
            // everywhere else. Every host applies clears; there is no
            // exception.
            onGrindCommitted: function(v) { Settings.dye.dyeGrinderSetting = v }
            onRpmCommitted: function(rpm) { Settings.dye.dyeGrinderRpm = rpm }
        }
    }
}
