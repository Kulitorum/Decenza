import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Graph display options shared by the shot detail, review and comparison pages.
DecenzaDialog {
    id: optionsDialog

    PreferenceWrite {
        id: graphPreferenceWrite
        domain: "graph"
        onFailed: function(message) { preferenceError.show(message) }
    }
    StatusToast { id: preferenceError; parent: Overlay.overlay; error: true }

    title: TranslationManager.translate("graph.options.title", "Graph Options")
    modal: true
    anchors.centerIn: parent
    // parent is Overlay.overlay, assigned by GraphOptionsButton — null until the
    // declaring item is in a window, so this is guarded like SelectionDialog,
    // EquipmentInfoDialog and ChangeBeansDialog all guard theirs.
    width: parent ? Math.min(parent.width * 0.85, Theme.scaled(360)) : Theme.scaled(360)
    padding: 0

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    header: null
    footer: null

    contentItem: ColumnLayout {
        spacing: Theme.spacingSmall

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingMedium
            text: optionsDialog.title
            color: Theme.textColor
            font: Theme.subtitleFont
            Accessible.ignored: true  // Dialog.title already announces this
        }

        GraphOptionToggleCard {
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            label: TranslationManager.translate("espresso.viewSelector.advancedCurves", "Advanced Curves")
            description: TranslationManager.translate("espresso.viewSelector.advancedCurvesDesc",
                                                      "Resistance, Conductance, Darcy R, Mix temp")
            enabled: graphPreferenceWrite.ready
            checked: graphPreferenceWrite.displayedValue("advancedMode",
                SettingsPreferencesService.preferences["graph.advancedMode"] === true)
            onToggled: (enabled) => graphPreferenceWrite.submit("advancedMode", enabled)
        }

        GraphFlowScaleCard {
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            Layout.topMargin: Theme.spacingSmall
            Layout.bottomMargin: Theme.spacingMedium
        }
    }
}
