import QtQuick
import Decenza

// Native singleton adapter around the reusable, singleton-free label view.
Item {
    id: axisLabels

    required property string mode
    required property var weightAxis
    required property var tempAxis
    required property var flowAxis

    signal tapped()

    ShotGraphStyle {
        id: axisStyle
        scale: Theme.scale
        captionFont: Theme.captionFont
        weightColor: Theme.weightColor
        flowColor: Theme.flowColor
        temperatureColor: Theme.temperatureColor
        flowUnit: TranslationManager.translate("graph.unit.flow", "mL/s")
        temperatureUnit: Theme.tempUnitSuffix()
        weightAxisAnnouncement: TranslationManager.translate(
            "graph.rightAxisModeWeight", "Right axis: Weight. Tap for Temperature")
        flowAxisAnnouncement: TranslationManager.translate(
            "graph.rightAxisModeFlow", "Right axis: Flow. Tap for Weight")
        temperatureAxisAnnouncement: TranslationManager.translate(
            "graph.rightAxisModeTemperature", "Right axis: Temperature. Tap for Flow")
    }

    GraphRightAxisLabelsView {
        anchors.fill: parent
        mode: axisLabels.mode
        weightMin: axisLabels.weightAxis ? axisLabels.weightAxis.min : 0
        weightMax: axisLabels.weightAxis ? axisLabels.weightAxis.max : 1
        tempMin: axisLabels.tempAxis ? axisLabels.tempAxis.min : 0
        tempMax: axisLabels.tempAxis ? axisLabels.tempAxis.max : 1
        flowMin: axisLabels.flowAxis ? axisLabels.flowAxis.min : 0
        flowMax: axisLabels.flowAxis ? axisLabels.flowAxis.max : 1
        style: axisStyle
        onTapped: axisLabels.tapped()
    }
}
