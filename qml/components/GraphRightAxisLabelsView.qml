pragma ComponentBehavior: Bound

import QtQuick

// Singleton-free right-axis labels used by the native wrapper and shared graph.
Item {
    id: axisLabels

    required property string mode
    required property real weightMin
    required property real weightMax
    required property real tempMin
    required property real tempMax
    required property real flowMin
    required property real flowMax
    required property ShotGraphStyle style

    readonly property bool isWeight: mode === "weight"
    readonly property bool isFlow: mode === "flow"
    readonly property real activeMin: isWeight ? weightMin : (isFlow ? flowMin : tempMin)
    readonly property real activeMax: isWeight ? weightMax : (isFlow ? flowMax : tempMax)
    readonly property color labelColor: isWeight ? style.weightColor
                                                 : (isFlow ? style.flowColor : style.temperatureColor)
    readonly property string unitLabel: isWeight ? "g" : (isFlow ? style.flowUnit : style.tempUnitSuffix())
    readonly property string accessibleLabel: isWeight ? style.weightAxisAnnouncement
        : (isFlow ? style.flowAxisAnnouncement : style.temperatureAxisAnnouncement)

    signal tapped()

    Accessible.role: Accessible.Button
    Accessible.name: axisLabels.accessibleLabel
    Accessible.focusable: true
    Accessible.onPressAction: axisLabels.tapped()

    Repeater {
        model: 5
        Text {
            required property int index
            readonly property real value: axisLabels.activeMax
                - index * (axisLabels.activeMax - axisLabels.activeMin) / 4
            text: axisLabels.isWeight ? value.toFixed(0)
                                      : (axisLabels.isFlow ? value.toFixed(1)
                                                           : axisLabels.style.cToDisplay(value).toFixed(0))
            x: 0
            y: index / 4 * axisLabels.height - height / 2
            font: axisLabels.style.captionFont
            color: axisLabels.labelColor
            Accessible.ignored: true
        }
    }

    Text {
        text: axisLabels.unitLabel
        font: axisLabels.style.captionFont
        color: axisLabels.labelColor
        rotation: 90
        transformOrigin: Item.Center
        x: axisLabels.style.scaled(24)
        y: axisLabels.height / 2 - height / 2
        Accessible.ignored: true
    }

    MouseArea {
        anchors.fill: parent
        onClicked: axisLabels.tapped()
    }
}
