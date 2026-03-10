import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "."

// Dialog for selecting espresso extraction view mode: Shot Chart or Cup Fill.
Dialog {
    id: selectorDialog

    property string currentMode: "chart"
    property bool showPhaseIndicator: true
    signal modeSelected(string mode)
    signal phaseIndicatorToggled(bool enabled)

    title: TranslationManager.translate("espresso.viewSelector.title", "Extraction View")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width * 0.85, Theme.scaled(360))

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    header: Item {
        height: Theme.scaled(48)
        Text {
            anchors.centerIn: parent
            text: selectorDialog.title
            color: Theme.textColor
            font: Theme.subtitleFont
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingSmall

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
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(72)
                radius: Theme.cardRadius
                color: Theme.backgroundColor
                border.color: selectorDialog.currentMode === model.mode
                    ? Theme.primaryColor : Theme.borderColor
                border.width: selectorDialog.currentMode === model.mode
                    ? Theme.scaled(2) : Theme.scaled(1)

                Accessible.ignored: true

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    Image {
                        source: model.icon
                        sourceSize.width: Theme.scaled(28)
                        sourceSize.height: Theme.scaled(28)
                        Layout.alignment: Qt.AlignVCenter
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Text {
                            text: TranslationManager.translate(model.labelKey, model.labelFallback)
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.weight: Font.Medium
                            Accessible.ignored: true
                        }

                        Text {
                            text: TranslationManager.translate(model.descKey, model.descFallback)
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                    }

                    // Selection indicator
                    Rectangle {
                        width: Theme.scaled(20)
                        height: Theme.scaled(20)
                        radius: Theme.scaled(10)
                        border.color: selectorDialog.currentMode === model.mode
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
                            visible: selectorDialog.currentMode === model.mode
                        }
                    }
                }

                AccessibleMouseArea {
                    anchors.fill: parent
                    accessibleName: TranslationManager.translate(model.labelKey, model.labelFallback) + ". " +
                                    TranslationManager.translate(model.descKey, model.descFallback)
                    accessibleItem: optionCard
                    onAccessibleClicked: {
                        selectorDialog.modeSelected(model.mode)
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

        // Phase indicator toggle
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(48)
            Layout.topMargin: Theme.spacingSmall
            radius: Theme.cardRadius
            color: Theme.backgroundColor

            Accessible.ignored: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium
                spacing: Theme.spacingMedium

                Text {
                    text: TranslationManager.translate("espresso.viewSelector.showPhaseIndicator", "Show Phase Indicator")
                    color: Theme.textColor
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.bodyFont.pixelSize
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }

                CheckBox {
                    id: phaseIndicatorCheck
                    checked: selectorDialog.showPhaseIndicator
                    Accessible.name: TranslationManager.translate("espresso.viewSelector.showPhaseIndicator", "Show Phase Indicator")
                    Accessible.focusable: true
                    onToggled: {
                        selectorDialog.phaseIndicatorToggled(checked)
                    }
                }
            }
        }
    }

    footer: null
}
