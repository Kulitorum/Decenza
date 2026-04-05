import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Tappable legend items that toggle curve visibility on the graph.
// Persists toggle state to Settings.
Item {
    id: legendRoot

    required property var graph
    property bool advancedMode: false
    property bool liveMode: false  // true = live shot graph (hides post-shot-only curves like dC/dt)

    Layout.fillWidth: true
    implicitHeight: legendRow.height

    Flow {
        id: legendRow
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.scaled(2)

        Repeater {
            model: [
                { label: TranslationManager.translate("graph.pressure", "Pressure"), sColor: Theme.pressureColor, key: "showPressure",
                  tip: TranslationManager.translate("graph.tip.pressure", "Pump pressure in bar. Shows the machine's intent — what it's trying to do.") },
                { label: TranslationManager.translate("graph.flow", "Flow"), sColor: Theme.flowColor, key: "showFlow",
                  tip: TranslationManager.translate("graph.tip.flow", "Water flow rate in mL/s. Shows the coffee's response — how easily water passes through the puck.") },
                { label: TranslationManager.translate("graph.temp", "Temp"), sColor: Theme.temperatureColor, key: "showTemperature",
                  tip: TranslationManager.translate("graph.tip.temp", "Basket temperature in \u00B0C. The temperature at the group head thermocouple.") },
                { label: TranslationManager.translate("graph.mixTemp", "Mix temp"), sColor: Theme.temperatureMixColor, key: "showTemperatureMix", advanced: true,
                  tip: TranslationManager.translate("graph.tip.mixTemp", "Mix temperature in \u00B0C. The actual water temperature reaching the puck. Difference from basket temp reveals group head thermal stability.") },
                { label: TranslationManager.translate("graph.weight", "Weight"), sColor: Theme.weightColor, key: "showWeight",
                  tip: TranslationManager.translate("graph.tip.weight", "Cumulative beverage weight in grams from the scale.") },
                { label: TranslationManager.translate("graph.wtFlow", "Wt flow"), sColor: Theme.weightFlowColor, key: "showWeightFlow",
                  tip: TranslationManager.translate("graph.tip.wtFlow", "Weight-based flow rate in g/s from the scale. More accurate than pump flow for measuring actual output.") },
                { label: TranslationManager.translate("graph.resistance", "Resist(P/F)"), sColor: Theme.resistanceColor, key: "showResistance", advanced: true,
                  tip: TranslationManager.translate("graph.tip.resistance", "Puck resistance (P/F). Rising = puck tightening. Falling = puck opening. Erratic = channeling.") },
                { label: TranslationManager.translate("graph.darcyResistance", "Resist(P/F\u00B2)"), sColor: Theme.darcyResistanceColor, key: "showDarcyResistance", advanced: true,
                  tip: TranslationManager.translate("graph.tip.darcyResistance", "Darcy resistance (P/F\u00B2). Physics-based puck resistance for laminar flow. Inverse of conductance.") },
                { label: TranslationManager.translate("graph.conductance", "Conduct(F\u00B2/P)"), sColor: Theme.conductanceColor, key: "showConductance", advanced: true,
                  tip: TranslationManager.translate("graph.tip.conductance", "Conductance (F\u00B2/P, Darcy's law). Rising = puck opening up. Stable = consistent extraction. Spike = channeling.") },
                { label: TranslationManager.translate("graph.dCdt", "dC/dt"), sColor: Theme.conductanceDerivativeColor, key: "showConductanceDerivative", advanced: true, postShotOnly: true,
                  tip: TranslationManager.translate("graph.tip.dCdt", "Rate of change of conductance. The best channeling detector — spikes reveal transient channels that are invisible in other curves.") }
            ]

            delegate: Rectangle {
                required property var modelData
                visible: (!modelData.advanced || legendRoot.advancedMode) && (!modelData.postShotOnly || !legendRoot.liveMode)
                width: visible ? legendItemRow.width + Theme.spacingMedium * 2 : 0
                height: visible ? Math.max(Theme.scaled(44), legendItemRow.height + Theme.scaled(24)) : 0
                radius: Theme.scaled(4)
                color: "transparent"
                opacity: legendRoot.graph[modelData.key] ? 1.0 : 0.4

                Accessible.role: Accessible.CheckBox
                Accessible.name: modelData.label
                Accessible.checked: legendRoot.graph[modelData.key] ?? false
                Accessible.focusable: true
                Accessible.description: TranslationManager.translate("graph.tip.longPressHint", "Long-press to view description.")
                Accessible.onPressAction: toggleVisibility()

                function toggleVisibility() {
                    var newValue = !legendRoot.graph[modelData.key]
                    legendRoot.graph[modelData.key] = newValue
                    Settings.setValue("graph/" + modelData.key, newValue)
                }

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

                property bool longPressShowing: false

                MouseArea {
                    id: legendItemArea
                    anchors.fill: parent
                    preventStealing: true
                    hoverEnabled: true
                    onClicked: toggleVisibility()
                    onPressAndHold: {
                        parent.longPressShowing = true
                        longPressHideTimer.restart()
                    }
                }

                Timer {
                    id: longPressHideTimer
                    interval: 4000
                    onTriggered: parent.longPressShowing = false
                }

                ToolTip {
                    id: legendTip
                    text: modelData.tip ?? ""
                    visible: ((legendItemArea.containsMouse && legendItemArea.pressedButtons === 0) || parent.longPressShowing) && text !== ""
                    delay: parent.longPressShowing ? 0 : 500
                    width: Math.min(Theme.scaled(280), Theme.windowWidth * 0.7)

                    contentItem: Text {
                        text: legendTip.text
                        font: Theme.captionFont
                        color: Theme.textColor
                        wrapMode: Text.Wrap
                    }

                    background: Rectangle {
                        color: Theme.surfaceColor
                        border.color: Theme.borderColor
                        border.width: Theme.scaled(1)
                        radius: Theme.cardRadius
                    }
                }
            }
        }
    }
}
