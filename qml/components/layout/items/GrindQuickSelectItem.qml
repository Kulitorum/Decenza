import QtQuick
import QtQuick.Layouts
import Decenza

// Layout widget: grinder dial-in quick-select (composable-brew-bar).
// Thin wrapper over the shared GrindField (replace-grind-inputs-with-picker):
// the label above a tappable pill showing the current dial-in, tapping opens
// the grind/RPM picker. All row generation, stepping and the picker itself
// live in GrindRowSource / GrindField / GrindPickerDialog — this file only
// binds them to the LIVE dial-in in RecipeContext and applies commits through
// the same host-backed preference path used by shared pages.
//
// This is one of the TWO surfaces whose grinder context IS the active grinder
// (Brew Settings is the other) — both edit the live dial-in. The record hosts
// inject the grinder that owns their value instead: the shot's in post-shot
// review, the bag's in the beans dialog, the recipe's package in the wizard.
LayoutWidgetItem {
    id: root
    readonly property string labelText:
        TranslationManager.translate("grind.quickSelect.label", "Grind")
    property var _pendingPreferenceNames: ({})

    function setGrindPreference(fields: var) {
        if (!RecipeContext.ready) {
            HomePresetActions.failed(TranslationManager.translate(
                "idle.operationFailed", "The selection could not be changed. Please try again."))
            return
        }
        const requestId = RecipeContext.newRequestId()
        const pending = Object.assign({}, _pendingPreferenceNames)
        pending[requestId] = "grindDialIn"
        _pendingPreferenceNames = pending
        const values = { "dyeGrinderSetting": fields.grindSetting }
        if (Object.prototype.hasOwnProperty.call(fields, "rpm"))
            values.dyeGrinderRpm = fields.rpm
        RecipeContext.setPreference(requestId, "grindDialIn", values, RecipeContext.revision)
    }

    function finishGrindPreference(requestId: string, name: string, success: bool) {
        if (_pendingPreferenceNames[requestId] !== name)
            return
        const pending = Object.assign({}, _pendingPreferenceNames)
        delete pending[requestId]
        _pendingPreferenceNames = pending
        if (!success)
            HomePresetActions.failed(TranslationManager.translate(
                "idle.operationFailed", "The selection could not be changed. Please try again."))
    }

    Connections {
        target: RecipeContext
        function onPreferenceCompleted(requestId, name, success, preferences, reason) {
            root.finishGrindPreference(requestId, name, success)
        }
    }

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
            grinderBrand: String(RecipeContext.preferences.dyeGrinderBrand || "")
            grinderModel: String(RecipeContext.preferences.dyeGrinderModel || "")
            grindSetting: String(RecipeContext.preferences.dyeGrinderSetting || "")
            rpmValue: RecipeContext.preferences.dyeGrinderRpm > 0
                ? RecipeContext.preferences.dyeGrinderRpm : 0

            // RecipeContext's host setter delegates to SettingsDye, which keeps
            // the active bag and package last-used dial-in current.
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
            onDialInCommitted: function(fields) { root.setGrindPreference(fields) }
        }
    }
}
