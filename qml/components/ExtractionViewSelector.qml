// The option-card Repeater delegate reads this file's `selectorDialog` id; Bound makes
// it statically resolvable. The delegate declares its injected `model` role required in
// the same edit -- without that, Bound stops role injection and both option cards render
// blank at RUNTIME, silently. (The four `layer.effect` blocks read only `Theme`, a
// singleton, so they need nothing from the pragma.)
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

// Dialog for selecting espresso extraction view mode: Shot Chart or Cup Fill.
DecenzaDialog {
    id: selectorDialog

    property string currentMode: "chart"
    property bool showPhaseIndicator: true
    property bool showStats: true
    signal modeSelected(string mode)
    signal phaseIndicatorToggled(bool enabled)
    signal statsToggled(bool enabled)

    title: TranslationManager.translate("espresso.viewSelector.title", "Extraction View")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width * 0.85, Theme.scaled(360))
    // Let the Dialog auto-size vertically from content
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
        id: contentColumn
        spacing: Theme.spacingSmall

        // Title (moved from header to content so it's part of the measured layout)
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingMedium
            text: selectorDialog.title
            color: Theme.textColor
            font: Theme.subtitleFont
            Accessible.ignored: true  // Dialog.title already announces this
        }

        Repeater {
            model: ListModel {
                ListElement {
                    mode: "chart"
                    icon: "qrc:/icons/Graph.svg"
                    labelKey: "espresso.viewSelector.chart"
                    labelFallback: "Shot Chart"
                    descKey: "espresso.viewSelector.chartDesc"
                    descFallback: "Real-time pressure, flow, and weight curves"
                }
                ListElement {
                    mode: "cupFill"
                    icon: "qrc:/icons/espresso.svg"
                    labelKey: "espresso.viewSelector.cupFill"
                    labelFallback: "Cup Fill"
                    descKey: "espresso.viewSelector.cupFillDesc"
                    descFallback: "Animated cup filling with extraction progress"
                }
            }

            delegate: Rectangle {
                id: optionCard
                required property var model

                Layout.fillWidth: true
                implicitHeight: optionRow.implicitHeight + Theme.spacingMedium * 2
                radius: Theme.cardRadius
                color: Theme.backgroundColor
                border.color: selectorDialog.currentMode === model.mode
                    ? Theme.primaryColor : Theme.borderColor
                border.width: selectorDialog.currentMode === model.mode
                    ? Theme.scaled(2) : Theme.scaled(1)

                Accessible.ignored: true

                RowLayout {
                    id: optionRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    Image {
                        source: optionCard.model.icon
                        sourceSize.width: Theme.scaled(28)
                        sourceSize.height: Theme.scaled(28)
                        Layout.alignment: Qt.AlignVCenter

                        layer.enabled: true
                        layer.smooth: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: Theme.textColor
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Text {
                            text: TranslationManager.translate(optionCard.model.labelKey, optionCard.model.labelFallback)
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.weight: Font.Medium
                            Accessible.ignored: true
                        }

                        Text {
                            text: TranslationManager.translate(optionCard.model.descKey, optionCard.model.descFallback)
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }

                    // Selection indicator
                    Rectangle {
                        Layout.preferredWidth: Theme.scaled(20)
                        Layout.preferredHeight: Theme.scaled(20)
                        radius: Theme.scaled(10)
                        border.color: selectorDialog.currentMode === optionCard.model.mode
                            ? Theme.primaryColor : Theme.textSecondaryColor
                        border.width: Theme.scaled(2)
                        color: "transparent"
                        Layout.alignment: Qt.AlignVCenter

                        Rectangle {
                            anchors.centerIn: parent
                            width: Theme.scaled(10)
                            height: Theme.scaled(10)
                            radius: Theme.scaled(5)
                            color: Theme.primaryColor
                            visible: selectorDialog.currentMode === optionCard.model.mode
                        }
                    }
                }

                AccessibleMouseArea {
                    anchors.fill: parent
                    accessibleName: TranslationManager.translate(optionCard.model.labelKey, optionCard.model.labelFallback) + ". " +
                                    TranslationManager.translate(optionCard.model.descKey, optionCard.model.descFallback)
                    accessibleItem: optionCard
                    onAccessibleClicked: {
                        selectorDialog.modeSelected(optionCard.model.mode)
                        selectorDialog.close()
                    }
                }
            }
        }

        // Divider
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.topMargin: Theme.spacingSmall
            color: Theme.borderColor
        }

        // Display options. These three cards were written out longhand — 200 lines of
        // near-identical checkbox markup — before GraphOptionToggleCard existed.
        GraphOptionToggleCard {
            Layout.topMargin: Theme.spacingSmall
            label: TranslationManager.translate("espresso.viewSelector.showPhaseIndicator", "Show Phase Indicator")
            checked: selectorDialog.showPhaseIndicator
            onToggled: (enabled) => selectorDialog.phaseIndicatorToggled(enabled)
        }

        GraphOptionToggleCard {
            Layout.topMargin: Theme.spacingSmall
            label: TranslationManager.translate("espresso.viewSelector.showStats", "Show Stats")
            checked: selectorDialog.showStats
            onToggled: (enabled) => selectorDialog.statsToggled(enabled)
        }

        // Chart-only options. Both write settings every graph binds to, so the live graph
        // behind the dialog re-renders as they change.
        GraphOptionToggleCard {
            Layout.topMargin: Theme.spacingSmall
            visible: selectorDialog.currentMode === "chart"
            label: TranslationManager.translate("espresso.viewSelector.advancedCurves", "Advanced Curves")
            description: TranslationManager.translate("espresso.viewSelector.advancedCurvesDesc",
                                                      "Resistance, Conductance, Darcy R, Mix temp")
            checked: Settings.graph.advancedMode
            onToggled: (enabled) => Settings.graph.advancedMode = enabled
        }

        GraphFlowScaleCard {
            Layout.topMargin: Theme.spacingSmall
            Layout.bottomMargin: Theme.spacingMedium
            visible: selectorDialog.currentMode === "chart"
        }
    }
}
