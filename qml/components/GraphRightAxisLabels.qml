// The tick-label Repeater delegate reads this file's `axisLabels` id; Bound makes it
// statically resolvable. The delegate declares its injected `index` role required, so
// Bound cannot break role injection here.
pragma ComponentBehavior: Bound

import QtQuick
import Decenza

// The right-hand axis label column, shared by the live and history shot graphs.
//
// Qt Graphs has no sanctioned second Y axis, so the right axis is drawn by hand: the traces
// mapped to it read plain value-holder QObjects for their own data→pixel mapping, and this
// column paints the matching tick labels at a fixed position beside the plot. Adding a real
// axis would resize the plot area and shift everything under it.
//
// It lived twice, copied verbatim into ShotGraph.qml and HistoryShotGraph.qml, differing only
// in a `visible` binding. Both copies would have needed the same third mode added by hand.
Item {
    id: axisLabels

    // "weight", "temperature" or "flow" — the vocabulary is SettingsGraph's.
    required property string mode

    // Value holders supplying min/max for each mode. Plain QtObjects, not ValueAxis.
    required property var weightAxis
    required property var tempAxis
    required property var flowAxis

    signal tapped()

    readonly property bool isWeight: mode === "weight"
    readonly property bool isFlow: mode === "flow"

    readonly property var activeAxis: isWeight ? weightAxis : (isFlow ? flowAxis : tempAxis)

    readonly property color labelColor: isWeight ? Theme.weightColor
                                                 : (isFlow ? Theme.flowColor : Theme.temperatureColor)

    readonly property string unitLabel: isWeight ? "g"
                                                 : (isFlow ? trFlowUnit.text : Theme.tempUnitSuffix())

    // Each mode names the mode a tap moves to, so the announcement matches what happens.
    // Distinct keys rather than reworded old ones: the previous two-state strings promised
    // "Tap for Weight" from temperature, which a third mode makes wrong, and an existing
    // translation would keep asserting it.
    readonly property string accessibleLabel: isWeight
        ? TranslationManager.translate("graph.rightAxisModeWeight", "Right axis: Weight. Tap for Temperature")
        : (isFlow ? TranslationManager.translate("graph.rightAxisModeFlow", "Right axis: Flow. Tap for Weight")
                  : TranslationManager.translate("graph.rightAxisModeTemperature", "Right axis: Temperature. Tap for Flow"))

    Tr { id: trFlowUnit; key: "graph.unit.flow"; fallback: "mL/s"; visible: false }

    Accessible.role: Accessible.Button
    Accessible.name: axisLabels.accessibleLabel
    Accessible.focusable: true
    Accessible.onPressAction: axisLabels.tapped()

    // Five evenly spaced labels, mirroring the original tickCount: 5.
    Repeater {
        model: 5
        Text {
            required property int index
            readonly property real value: {
                var axis = axisLabels.activeAxis
                if (!axis)
                    return 0
                return axis.max - index * (axis.max - axis.min) / 4
            }
            // Flow carries a decimal: at 3x the column runs 0 to 4.0, and whole numbers
            // alone would render four of the five ticks as "1, 2, 3, 4" against a scale
            // whose interesting differences are tenths.
            text: axisLabels.isWeight ? value.toFixed(0)
                                      : (axisLabels.isFlow ? value.toFixed(1)
                                                           : Theme.cToDisplay(value).toFixed(0))
            x: 0
            y: index / 4 * axisLabels.height - height / 2
            font: Theme.captionFont
            color: axisLabels.labelColor
            Accessible.ignored: true
        }
    }

    Text {
        text: axisLabels.unitLabel
        font: Theme.captionFont
        color: axisLabels.labelColor
        rotation: 90
        transformOrigin: Item.Center
        x: Theme.scaled(24)
        y: axisLabels.height / 2 - height / 2
        Accessible.ignored: true
    }

    MouseArea {
        anchors.fill: parent
        onClicked: axisLabels.tapped()
    }
}
