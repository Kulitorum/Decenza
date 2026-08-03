import QtQuick
import QtQuick.Layouts
import Decenza

// Graph display options for the shot detail, post-shot review and shot comparison pages.
//
// Replaces the bare Advanced-curves toggle button those three pages each carried: the button
// now opens this, and advanced curves is an option inside it alongside the flow scale. The
// live espresso screen does not use this dialog — it already has ExtractionViewSelector, and
// the same two options are presented there rather than giving that screen a second menu.
//
// Every option writes Settings.graph directly. There is no state to hand back to the host
// page, because every graph binds to those properties.
DecenzaDialog {
    id: optionsDialog

    title: TranslationManager.translate("graph.options.title", "Graph Options")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width * 0.85, Theme.scaled(360))
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
            checked: Settings.graph.advancedMode
            onToggled: (enabled) => Settings.graph.advancedMode = enabled
        }

        GraphFlowScaleCard {
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            Layout.topMargin: Theme.spacingSmall
            Layout.bottomMargin: Theme.spacingMedium
        }
    }
}
