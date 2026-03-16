import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "."

// Dialog for selecting espresso extraction view mode: Shot Chart or Cup Fill.
Dialog {
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
    topPadding: 0
    bottomPadding: 0

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
                        source: model.icon
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
                            wrapMode: Text.WordWrap
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
            id: phaseToggleCard
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(48)
            Layout.topMargin: Theme.spacingSmall
            radius: Theme.cardRadius
            color: Theme.backgroundColor

            property bool isChecked: selectorDialog.showPhaseIndicator

            Accessible.role: Accessible.CheckBox
            Accessible.name: TranslationManager.translate("espresso.viewSelector.showPhaseIndicator", "Show Phase Indicator")
            Accessible.checked: isChecked
            Accessible.focusable: true
            Accessible.onPressAction: phaseToggleArea.clicked(null)

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

                Rectangle {
                    width: Theme.scaled(20)
                    height: Theme.scaled(20)
                    radius: Theme.scaled(4)
                    color: phaseToggleCard.isChecked ? Theme.primaryColor : "transparent"
                    border.color: phaseToggleCard.isChecked ? Theme.primaryColor : Theme.textSecondaryColor
                    border.width: Theme.scaled(2)
                    Layout.alignment: Qt.AlignVCenter

                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/icons/tick.svg"
                        sourceSize.width: Theme.scaled(14)
                        sourceSize.height: Theme.scaled(14)
                        visible: phaseToggleCard.isChecked
                        Accessible.ignored: true

                        layer.enabled: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: Theme.surfaceColor
                        }
                    }
                }
            }

            MouseArea {
                id: phaseToggleArea
                anchors.fill: parent
                onClicked: selectorDialog.phaseIndicatorToggled(!phaseToggleCard.isChecked)
            }
        }

        // Stats toggle
        Rectangle {
            id: statsToggleCard
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(48)
            Layout.topMargin: Theme.spacingSmall
            Layout.bottomMargin: Theme.spacingMedium
            radius: Theme.cardRadius
            color: Theme.backgroundColor

            property bool isChecked: selectorDialog.showStats

            Accessible.role: Accessible.CheckBox
            Accessible.name: TranslationManager.translate("espresso.viewSelector.showStats", "Show Stats")
            Accessible.checked: isChecked
            Accessible.focusable: true
            Accessible.onPressAction: statsToggleArea.clicked(null)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium
                spacing: Theme.spacingMedium

                Text {
                    text: TranslationManager.translate("espresso.viewSelector.showStats", "Show Stats")
                    color: Theme.textColor
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.bodyFont.pixelSize
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }

                Rectangle {
                    width: Theme.scaled(20)
                    height: Theme.scaled(20)
                    radius: Theme.scaled(4)
                    color: statsToggleCard.isChecked ? Theme.primaryColor : "transparent"
                    border.color: statsToggleCard.isChecked ? Theme.primaryColor : Theme.textSecondaryColor
                    border.width: Theme.scaled(2)
                    Layout.alignment: Qt.AlignVCenter

                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/icons/tick.svg"
                        sourceSize.width: Theme.scaled(14)
                        sourceSize.height: Theme.scaled(14)
                        visible: statsToggleCard.isChecked
                        Accessible.ignored: true

                        layer.enabled: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: Theme.surfaceColor
                        }
                    }
                }
            }

            MouseArea {
                id: statsToggleArea
                anchors.fill: parent
                onClicked: selectorDialog.statsToggled(!statsToggleCard.isChecked)
            }
        }
    }
}
