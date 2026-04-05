import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import Decenza

// Collapsible panel showing per-phase metrics in a compact table.
// Collapsed by default; state persisted to Settings.
Item {
    id: root

    required property var phaseSummaries  // Array of {name, duration, avgPressure, avgFlow, weightGained, isFlowMode}

    Layout.fillWidth: true
    implicitHeight: panelColumn.height
    visible: phaseSummaries && phaseSummaries.length > 0

    property bool expanded: Settings.value("shotReview/phaseSummaryExpanded", false) === true

    ColumnLayout {
        id: panelColumn
        width: parent.width
        spacing: 0

        // Header — tap to expand/collapse
        Rectangle {
            Layout.fillWidth: true
            height: Theme.scaled(40)
            color: "transparent"

            Accessible.role: Accessible.Button
            Accessible.name: headerLabel.text + (root.expanded ? ", " + TranslationManager.translate("common.expanded", "expanded") : ", " + TranslationManager.translate("common.collapsed", "collapsed"))
            Accessible.focusable: true
            Accessible.onPressAction: headerArea.clicked(null)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingSmall
                anchors.rightMargin: Theme.spacingSmall

                Text {
                    id: headerLabel
                    text: TranslationManager.translate("phaseSummary.title", "Phase Summary")
                    font: Theme.subtitleFont
                    color: Theme.textSecondaryColor
                    Accessible.ignored: true
                }

                Item { Layout.fillWidth: true }

                Image {
                    source: "qrc:/icons/ArrowLeft.svg"
                    sourceSize.width: Theme.scaled(12)
                    sourceSize.height: Theme.scaled(12)
                    rotation: root.expanded ? -90 : 180
                    Accessible.ignored: true

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.textSecondaryColor
                    }
                }
            }

            MouseArea {
                id: headerArea
                anchors.fill: parent
                onClicked: {
                    root.expanded = !root.expanded
                    Settings.setValue("shotReview/phaseSummaryExpanded", root.expanded)
                }
            }
        }

        // Table content (visible when expanded)
        ColumnLayout {
            visible: root.expanded
            Layout.fillWidth: true
            spacing: 0
            Layout.leftMargin: Theme.spacingSmall
            Layout.rightMargin: Theme.spacingSmall

            // Column headers
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                Text {
                    Layout.preferredWidth: root.width * 0.28
                    text: TranslationManager.translate("phaseSummary.phase", "Phase")
                    font.family: Theme.captionFont.family
                    font.pixelSize: Theme.captionFont.pixelSize
                    font.bold: true
                    color: Theme.textSecondaryColor
                    Accessible.ignored: true
                }
                Text {
                    Layout.preferredWidth: root.width * 0.16
                    text: TranslationManager.translate("phaseSummary.duration", "Duration")
                    font.family: Theme.captionFont.family
                    font.pixelSize: Theme.captionFont.pixelSize
                    font.bold: true
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignRight
                    Accessible.ignored: true
                }
                Text {
                    Layout.preferredWidth: root.width * 0.18
                    text: TranslationManager.translate("phaseSummary.pressure", "Avg Press")
                    font.family: Theme.captionFont.family
                    font.pixelSize: Theme.captionFont.pixelSize
                    font.bold: true
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignRight
                    Accessible.ignored: true
                }
                Text {
                    Layout.preferredWidth: root.width * 0.18
                    text: TranslationManager.translate("phaseSummary.flow", "Avg Flow")
                    font.family: Theme.captionFont.family
                    font.pixelSize: Theme.captionFont.pixelSize
                    font.bold: true
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignRight
                    Accessible.ignored: true
                }
                Text {
                    Layout.preferredWidth: root.width * 0.18
                    text: TranslationManager.translate("phaseSummary.weight", "Weight")
                    font.family: Theme.captionFont.family
                    font.pixelSize: Theme.captionFont.pixelSize
                    font.bold: true
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignRight
                    Accessible.ignored: true
                }
            }

            // Divider
            Rectangle {
                Layout.fillWidth: true
                height: Theme.scaled(1)
                color: Theme.borderColor
                Layout.topMargin: Theme.scaled(2)
                Layout.bottomMargin: Theme.scaled(2)
            }

            // Phase rows
            Repeater {
                model: root.phaseSummaries

                delegate: RowLayout {
                    required property var modelData
                    required property int index

                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)

                    Accessible.role: Accessible.StaticText
                    Accessible.name: modelData.name + ": " +
                        (modelData.duration ?? 0).toFixed(1) + " seconds, " +
                        (modelData.avgPressure ?? 0).toFixed(1) + " bar, " +
                        (modelData.avgFlow ?? 0).toFixed(1) + " mL per second, " +
                        (modelData.weightGained ?? 0).toFixed(1) + " grams"

                    // Phase name with pump mode indicator
                    RowLayout {
                        Layout.preferredWidth: root.width * 0.28
                        spacing: Theme.scaled(3)

                        Rectangle {
                            width: Theme.scaled(6); height: Theme.scaled(6); radius: Theme.scaled(3)
                            color: modelData.isFlowMode ? Theme.flowColor : Theme.pressureColor
                            Accessible.ignored: true
                        }

                        Text {
                            text: modelData.name ?? ""
                            font: Theme.captionFont
                            color: Theme.textColor
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                    }

                    Text {
                        Layout.preferredWidth: root.width * 0.16
                        text: (modelData.duration ?? 0).toFixed(1) + "s"
                        font: Theme.captionFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignRight
                        Accessible.ignored: true
                    }
                    Text {
                        Layout.preferredWidth: root.width * 0.18
                        text: (modelData.avgPressure ?? 0).toFixed(1) + " bar"
                        font: Theme.captionFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignRight
                        Accessible.ignored: true
                    }
                    Text {
                        Layout.preferredWidth: root.width * 0.18
                        text: (modelData.avgFlow ?? 0).toFixed(1) + " mL/s"
                        font: Theme.captionFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignRight
                        Accessible.ignored: true
                    }
                    Text {
                        Layout.preferredWidth: root.width * 0.18
                        text: (modelData.weightGained ?? 0).toFixed(1) + "g"
                        font: Theme.captionFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignRight
                        Accessible.ignored: true
                    }
                }
            }
        }
    }
}
