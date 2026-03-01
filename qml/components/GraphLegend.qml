import QtQuick
import QtQuick.Layouts
import DecenzaDE1

// Tappable legend items that toggle curve visibility on the graph.
// Persists toggle state to Settings.
Item {
    id: legendRoot

    required property var graph

    Layout.fillWidth: true
    implicitHeight: legendFlow.childrenRect.height

    Flow {
        id: legendFlow
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width, legendFlow.childrenRect.width + spacing)
        spacing: Theme.spacingSmall

        Repeater {
            model: [
                { label: "Pressure", sColor: Theme.pressureColor, key: "showPressure" },
                { label: "Flow", sColor: Theme.flowColor, key: "showFlow" },
                { label: "Temp", sColor: Theme.temperatureColor, key: "showTemperature" },
                { label: "Weight", sColor: Theme.weightColor, key: "showWeight" },
                { label: "Wt flow", sColor: Theme.weightFlowColor, key: "showWeightFlow" },
                { label: "Resist(P/F)", sColor: Theme.resistanceColor, key: "showResistance" }
            ]

            delegate: Rectangle {
                required property var modelData
                width: legendItemRow.width + Theme.spacingSmall * 2
                height: legendItemRow.height + Theme.scaled(6)
                radius: Theme.scaled(4)
                color: "transparent"
                opacity: legendRoot.graph[modelData.key] ? 1.0 : 0.4

                Accessible.role: Accessible.CheckBox
                Accessible.name: modelData.label
                Accessible.checked: legendRoot.graph[modelData.key]
                Accessible.focusable: true
                Accessible.onPressAction: legendItemArea.clicked(null)

                Row {
                    id: legendItemRow
                    anchors.centerIn: parent
                    spacing: Theme.scaled(4)
                    Rectangle {
                        width: Theme.scaled(10); height: Theme.scaled(10); radius: Theme.scaled(5)
                        color: modelData.sColor; anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.label; font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                }

                MouseArea {
                    id: legendItemArea
                    anchors.fill: parent
                    onClicked: {
                        var newValue = !legendRoot.graph[modelData.key]
                        legendRoot.graph[modelData.key] = newValue
                        Settings.setValue("graph/" + modelData.key, newValue)
                    }
                }
            }
        }
    }
}
