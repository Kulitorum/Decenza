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
        // Rebuild the model whenever any show* flag changes — otherwise toggling
        // a curve on (e.g. Resistance) wouldn't update the bar until the user
        // taps the graph again. Each flag is read so QML tracks it as a binding
        // dependency.
        model: {
            var g = inspectBar.graph
            var _deps = [g.showPressure, g.showFlow, g.showTemperature, g.showWeight, g.showWeightFlow,
                         g.showResistance, g.showConductance, g.showDarcyResistance,
                         g.showConductanceDerivative, g.showTemperatureMix]
            var vals = g.inspectValues
            // Order matches the legend: temperature pair, scale pair, resistance
            // pair, conductance pair.
            var entries = [
                { key: "pressure",        show: g.showPressure },
                { key: "flow",            show: g.showFlow },
                { key: "temperature",     show: g.showTemperature },
                { key: "mixTemp",         show: g.showTemperatureMix },
                { key: "weight",          show: g.showWeight },
                { key: "weightFlow",      show: g.showWeightFlow },
                { key: "resistance",      show: g.showResistance },
                { key: "darcyResistance", show: g.showDarcyResistance },
                { key: "conductance",     show: g.showConductance },
                { key: "dCdt",            show: g.showConductanceDerivative }
            ]
            var items = []
            for (var i = 0; i < entries.length; i++) {
                if (entries[i].show && vals[entries[i].key]) items.push(vals[entries[i].key])
            }
            return items
        }

        delegate: Row {
            required property var modelData
            spacing: Theme.scaled(4)
            Rectangle {
                width: Theme.scaled(8); height: Theme.scaled(8); radius: Theme.scaled(4)
                anchors.verticalCenter: parent.verticalCenter
                color: {
                    switch (modelData.name) {
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
                        default: return Theme.textColor
                    }
                }
            }
            Text {
                text: modelData.unit.length > 0
                    ? modelData.value.toFixed(1) + " " + modelData.unit
                    : modelData.value.toFixed(1)
                font: Theme.captionFont
                color: Theme.textColor
                Accessible.ignored: true
            }
        }
    }
}
