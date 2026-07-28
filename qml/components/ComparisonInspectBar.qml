// Delegates below declare their model roles with `required property`; ids from this file then
// resolve statically inside them. See PresetPillRow.qml for the full rationale.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza

// Displays crosshair values for shot comparison (up to 3 shots).
// One row per visible shot: date+time label followed by colored-dot metric pairs.
// Mirrors GraphInspectBar visual style — uses opacity (not visible) to prevent layout shift.
ColumnLayout {
    id: bar

    required property var graph

    Layout.fillWidth: true
    opacity: graph.inspecting ? 1 : 0
    spacing: Theme.spacingSmall

    // Crosshair time — shown once above all shot rows
    Text {
        text: bar.graph.inspectTime.toFixed(1) + "s"
        font.family: Theme.captionFont.family
        font.pixelSize: Theme.captionFont.pixelSize
        font.bold: true
        color: Theme.textColor
        Accessible.ignored: true
    }

    // One row per shot in the current window
    Repeater {
        model: bar.graph.inspectShotValues

        // Each row: date label + temp dot + flow dot + weight dot
        Flow {
            id: shotRow

            required property var modelData
            required property int index

            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            // Shot date+time identifier
            Text {
                text: shotRow.modelData.dateTime || ""
                font.family: Theme.captionFont.family
                font.pixelSize: Theme.captionFont.pixelSize
                font.bold: true
                color: Theme.textColor
                Accessible.ignored: true
            }

            // Temperature
            Row {
                spacing: Theme.scaled(4)
                Rectangle {
                    width: Theme.scaled(8)
                    height: Theme.scaled(8)
                    radius: Theme.scaled(4)
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.temperatureColor
                }
                Text {
                    text: shotRow.modelData.hasTemperature
                          ? Theme.cToDisplay(shotRow.modelData.temperature).toFixed(1) + " " + Theme.tempUnitSuffix()
                          : "\u2014"
                    font: Theme.captionFont
                    color: Theme.textColor
                    Accessible.ignored: true
                }
            }

            // Flow
            Row {
                spacing: Theme.scaled(4)
                Rectangle {
                    width: Theme.scaled(8)
                    height: Theme.scaled(8)
                    radius: Theme.scaled(4)
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.flowColor
                }
                Text {
                    text: shotRow.modelData.hasFlow
                          ? shotRow.modelData.flow.toFixed(1) + " mL/s"
                          : "\u2014"
                    font: Theme.captionFont
                    color: Theme.textColor
                    Accessible.ignored: true
                }
            }

            // Weight
            Row {
                spacing: Theme.scaled(4)
                Rectangle {
                    width: Theme.scaled(8)
                    height: Theme.scaled(8)
                    radius: Theme.scaled(4)
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.weightColor
                }
                Text {
                    text: shotRow.modelData.hasWeight
                          ? shotRow.modelData.weight.toFixed(1) + " g"
                          : "\u2014"
                    font: Theme.captionFont
                    color: Theme.textColor
                    Accessible.ignored: true
                }
            }
        }
    }
}
