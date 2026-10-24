import QtQuick

// Shared presentation layer for the live shot's custom scene-graph traces.
// The native graph and the Wasm feasibility target use this same component; all
// data, ranges, visibility, and palette values enter through typed properties.
Item {
    id: traceLayer

    required property real plotX
    required property real plotY
    required property real plotWidth
    required property real plotHeight
    required property real minX
    required property real maxX

    required property real pressureMin
    required property real pressureMax
    required property real temperatureMin
    required property real temperatureMax
    required property real weightMin
    required property real weightMax
    required property real flowMin
    required property real flowMax

    required property color pressureColor
    required property color flowColor
    required property color temperatureColor
    required property color weightColor
    required property color weightFlowColor
    required property color resistanceColor
    required property color conductanceColor
    required property color darcyResistanceColor
    required property color temperatureMixColor
    required property real thickLineWidth
    required property real thinLineWidth

    required property bool showPressure
    required property bool showFlow
    required property bool showTemperature
    required property bool showWeight
    required property bool showWeightFlow
    required property bool showResistance
    required property bool showConductance
    required property bool showDarcyResistance
    required property bool showTemperatureMix
    required property bool advancedMode

    readonly property alias pressureRenderer: pressure
    readonly property alias flowRenderer: flow
    readonly property alias temperatureRenderer: temperature
    readonly property alias weightRenderer: weight
    readonly property alias weightFlowRenderer: weightFlow
    readonly property alias resistanceRenderer: resistance
    readonly property alias conductanceRenderer: conductance
    readonly property alias darcyResistanceRenderer: darcyResistance
    readonly property alias temperatureMixRenderer: temperatureMix

    FastLineRenderer {
        id: pressure
        x: traceLayer.plotX; y: traceLayer.plotY
        width: traceLayer.plotWidth; height: traceLayer.plotHeight
        color: traceLayer.pressureColor
        lineWidth: traceLayer.thickLineWidth
        minX: traceLayer.minX; maxX: traceLayer.maxX
        minY: traceLayer.pressureMin; maxY: traceLayer.pressureMax
        visible: traceLayer.showPressure
    }

    FastLineRenderer {
        id: flow
        x: traceLayer.plotX; y: traceLayer.plotY
        width: traceLayer.plotWidth; height: traceLayer.plotHeight
        color: traceLayer.flowColor
        lineWidth: traceLayer.thickLineWidth
        minX: traceLayer.minX; maxX: traceLayer.maxX
        // Flow-family traces intentionally use the caller's scaled range. Keep
        // clipping here because values above that range must not escape the plot.
        clip: true
        minY: traceLayer.flowMin; maxY: traceLayer.flowMax
        visible: traceLayer.showFlow
    }

    FastLineRenderer {
        id: temperature
        x: traceLayer.plotX; y: traceLayer.plotY
        width: traceLayer.plotWidth; height: traceLayer.plotHeight
        color: traceLayer.temperatureColor
        lineWidth: traceLayer.thickLineWidth
        minX: traceLayer.minX; maxX: traceLayer.maxX
        minY: traceLayer.temperatureMin; maxY: traceLayer.temperatureMax
        visible: traceLayer.showTemperature
    }

    FastLineRenderer {
        id: weightFlow
        x: traceLayer.plotX; y: traceLayer.plotY
        width: traceLayer.plotWidth; height: traceLayer.plotHeight
        color: traceLayer.weightFlowColor
        lineWidth: traceLayer.thinLineWidth
        minX: traceLayer.minX; maxX: traceLayer.maxX
        // Match the flow trace's range and clipping behavior.
        clip: true
        minY: traceLayer.flowMin; maxY: traceLayer.flowMax
        visible: traceLayer.showWeightFlow
    }

    FastLineRenderer {
        id: resistance
        x: traceLayer.plotX; y: traceLayer.plotY
        width: traceLayer.plotWidth; height: traceLayer.plotHeight
        color: traceLayer.resistanceColor
        lineWidth: traceLayer.thinLineWidth
        minX: traceLayer.minX; maxX: traceLayer.maxX
        minY: traceLayer.pressureMin; maxY: traceLayer.pressureMax
        visible: traceLayer.showResistance && traceLayer.advancedMode
    }

    FastLineRenderer {
        id: conductance
        x: traceLayer.plotX; y: traceLayer.plotY
        width: traceLayer.plotWidth; height: traceLayer.plotHeight
        color: traceLayer.conductanceColor
        lineWidth: traceLayer.thinLineWidth
        minX: traceLayer.minX; maxX: traceLayer.maxX
        minY: traceLayer.pressureMin; maxY: traceLayer.pressureMax
        visible: traceLayer.showConductance && traceLayer.advancedMode
    }

    FastLineRenderer {
        id: darcyResistance
        x: traceLayer.plotX; y: traceLayer.plotY
        width: traceLayer.plotWidth; height: traceLayer.plotHeight
        color: traceLayer.darcyResistanceColor
        lineWidth: traceLayer.thinLineWidth
        minX: traceLayer.minX; maxX: traceLayer.maxX
        minY: traceLayer.pressureMin; maxY: traceLayer.pressureMax
        visible: traceLayer.showDarcyResistance && traceLayer.advancedMode
    }

    FastLineRenderer {
        id: temperatureMix
        x: traceLayer.plotX; y: traceLayer.plotY
        width: traceLayer.plotWidth; height: traceLayer.plotHeight
        color: traceLayer.temperatureMixColor
        lineWidth: traceLayer.thinLineWidth
        minX: traceLayer.minX; maxX: traceLayer.maxX
        minY: traceLayer.temperatureMin; maxY: traceLayer.temperatureMax
        visible: traceLayer.showTemperatureMix && traceLayer.advancedMode
    }

    FastLineRenderer {
        id: weight
        x: traceLayer.plotX; y: traceLayer.plotY
        width: traceLayer.plotWidth; height: traceLayer.plotHeight
        color: traceLayer.weightColor
        lineWidth: traceLayer.thickLineWidth
        minX: traceLayer.minX; maxX: traceLayer.maxX
        minY: traceLayer.weightMin; maxY: traceLayer.weightMax
        visible: traceLayer.showWeight
    }
}
