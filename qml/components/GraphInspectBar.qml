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
        model: {
            var items = []
            var vals = inspectBar.graph.inspectValues
            var keys = ["pressure", "flow", "temperature", "weight", "weightFlow"]
            for (var i = 0; i < keys.length; i++) {
                if (vals[keys[i]]) items.push(vals[keys[i]])
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
                        default: return Theme.textColor
                    }
                }
            }
            Text {
                text: modelData.value.toFixed(1) + " " + modelData.unit
                font: Theme.captionFont
                color: Theme.textColor
                Accessible.ignored: true
            }
        }
    }
}
