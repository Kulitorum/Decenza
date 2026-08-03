// Delegates below declare their model roles with `required property`; ids from this file then
// resolve statically inside them. See PresetPillRow.qml for the full rationale.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza

// Displays inspect-crosshair values (time + colored dots) when user taps the graph.
// Uses opacity (not visible) to prevent layout shift.
Flow {
    id: inspectBar

    required property var graph

    Layout.fillWidth: true
    opacity: graph.inspecting ? 1 : 0
    spacing: Theme.spacingMedium

    Text {
        text: inspectBar.graph.inspectTime.toFixed(1) + "s"
        font.family: Theme.captionFont.family
        font.pixelSize: Theme.captionFont.pixelSize
        font.bold: true
        color: Theme.textColor
        Accessible.ignored: true
    }

    Repeater {
        // Visibility comes from Settings.graph, which is where it lives for every graph.
        // Reading each flag also makes it a binding dependency, so toggling a curve on
        // updates the bar without the user re-tapping the graph — the hand-rolled `_deps`
        // array that used to force that is unnecessary now these are real properties.
        //
        // The VALUES come from the graph's inspectValues, which is computed from the
        // unscaled source arrays — so these read true mL/s and g/s at any flow multiplier.
        model: {
            var g = inspectBar.graph
            var advanced = Settings.graph.advancedMode
            var vals = g.inspectValues
            // Order matches the legend: temperature pair, scale pair, resistance
            // pair, conductance pair.
            var entries = [
                { key: "pressure",        show: Settings.graph.showPressure },
                { key: "flow",            show: Settings.graph.showFlow },
                { key: "temperature",     show: Settings.graph.showTemperature },
                { key: "mixTemp",         show: Settings.graph.showTemperatureMix && advanced },
                { key: "mixTempGoal",     show: Settings.graph.showTemperatureMixGoal && advanced },
                { key: "weight",          show: Settings.graph.showWeight },
                { key: "weightFlow",      show: Settings.graph.showWeightFlow },
                { key: "resistance",      show: Settings.graph.showResistance && advanced },
                { key: "darcyResistance", show: Settings.graph.showDarcyResistance && advanced },
                { key: "conductance",     show: Settings.graph.showConductance && advanced },
                { key: "dCdt",            show: Settings.graph.showConductanceDerivative && advanced }
            ]
            var items = []
            for (var i = 0; i < entries.length; i++) {
                if (entries[i].show && vals[entries[i].key]) items.push(vals[entries[i].key])
            }
            return items
        }

        delegate: Row {
            id: inspectEntry

            required property var modelData
            spacing: Theme.scaled(4)
            Rectangle {
                width: Theme.scaled(8); height: Theme.scaled(8); radius: Theme.scaled(4)
                anchors.verticalCenter: parent.verticalCenter
                color: {
                    switch (inspectEntry.modelData.name) {
                        case "Pressure": return Theme.pressureColor
                        case "Flow": return Theme.flowColor
                        case "Temp": return Theme.temperatureColor
                        case "Weight": return Theme.weightColor
                        case "Weight flow": return Theme.weightFlowColor
                        case "Resistance": return Theme.resistanceColor
                        case "Conductance": return Theme.conductanceColor
                        case "Darcy R": return Theme.darcyResistanceColor
                        case "dC/dt": return Theme.conductanceDerivativeColor
                        case "Mix temp": return Theme.temperatureMixColor
                        case "Mix temp goal": return Theme.temperatureMixGoalColor
                        default: return Theme.textColor
                    }
                }
            }
            Text {
                text: inspectEntry.modelData.unit.length > 0
                    ? inspectEntry.modelData.value.toFixed(1) + " " + inspectEntry.modelData.unit
                    : inspectEntry.modelData.value.toFixed(1)
                font: Theme.captionFont
                color: Theme.textColor
                Accessible.ignored: true
            }
        }
    }
}
