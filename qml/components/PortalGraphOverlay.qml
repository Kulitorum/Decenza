pragma ComponentBehavior: Bound
import QtQuick
import Decenza

Item {
    id: portalGraph
    property var graphsView: null
    property var axisX: null
    property var samples: []
    property bool showLabels: true
    readonly property bool hasData: samples.length > 0
    readonly property bool hasVisibleData: hasData && (Settings.graph.showPortalEc || Settings.graph.showPortalTemperature)
    readonly property real labelWidth: hasVisibleData && showLabels ? Theme.scaled(125) : 0
    readonly property real lastTime: hasData ? samples[samples.length - 1].time : 0

    function segments(field) {
        var result = []
        var current = []
        for (var i = 0; i < samples.length; i++) {
            var sample = samples[i]
            if (sample.breakBefore && current.length > 0) {
                result.push(current)
                current = []
            }
            current.push(Qt.point(sample.time, sample[field]))
        }
        if (current.length) result.push(current)
        return result
    }
    readonly property var ecSegments: segments("ecRaw")
    readonly property var temperatureSegments: segments("temperatureC")
    readonly property var ecRange: {
        var lo = 0
        var hi = 0.1
        for (var i = 0; i < samples.length; i++) {
            lo = Math.min(lo, samples[i].ecRaw)
            hi = Math.max(hi, samples[i].ecRaw)
        }
        return [lo < 0 ? lo * 1.1 : 0, hi * 1.1]
    }
    QtObject {
        id: ecAxis
        property real min: portalGraph.ecRange[0]
        property real max: portalGraph.ecRange[1]
    }
    QtObject {
        id: outletAxis
        property real min: 0
        property real max: 100
    }
    Repeater {
        model: portalGraph.ecSegments
        delegate: DashedLineSeries {
            required property var modelData
            graphsView: portalGraph.graphsView
            axisX: portalGraph.axisX
            axisY: ecAxis
            points: modelData
            dashed: false
            strokeColor: Theme.portalEcColor
            strokeWidth: Theme.graphLineWidth
            visible: Settings.graph.showPortalEc && points.length >= 2
        }
    }
    Repeater {
        model: portalGraph.temperatureSegments
        delegate: DashedLineSeries {
            required property var modelData
            graphsView: portalGraph.graphsView
            axisX: portalGraph.axisX
            axisY: outletAxis
            points: modelData
            dashed: false
            strokeColor: Theme.portalTemperatureColor
            strokeWidth: Theme.graphLineWidth
            visible: Settings.graph.showPortalTemperature && points.length >= 2
        }
    }
    Item {
        id: axes
        visible: portalGraph.hasVisibleData && portalGraph.showLabels
        x: portalGraph.graphsView ? portalGraph.graphsView.plotArea.x + portalGraph.graphsView.plotArea.width + Theme.scaled(55) : 0
        y: portalGraph.graphsView ? portalGraph.graphsView.plotArea.y : 0
        width: Theme.scaled(120)
        height: portalGraph.graphsView ? portalGraph.graphsView.plotArea.height : 0
        Repeater {
            model: 3
            delegate: Item {
                id: tickRow
                required property int index
                width: axes.width
                y: index * Math.max(0, axes.height - Theme.scaled(16)) / 2
                Text {
                    width: Theme.scaled(60)
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.portalEcColor
                    font.pixelSize: Theme.scaled(11)
                    visible: Settings.graph.showPortalEc
                    text: (ecAxis.max - tickRow.index * (ecAxis.max - ecAxis.min) / 2).toFixed(ecAxis.max < 1 ? 3 : 2)
                }
                Text {
                    x: Theme.scaled(60)
                    width: Theme.scaled(60)
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.portalTemperatureColor
                    font.pixelSize: Theme.scaled(11)
                    visible: Settings.graph.showPortalTemperature
                    text: Theme.cToDisplay(100 - tickRow.index * 50).toFixed(0)
                }
            }
        }
        Text {
            y: -Theme.scaled(19)
            width: Theme.scaled(60)
            horizontalAlignment: Text.AlignHCenter
            text: TranslationManager.translate("portal.ecRaw", "EC (raw)")
            color: Theme.portalEcColor
            font.pixelSize: Theme.scaled(10)
            visible: Settings.graph.showPortalEc
        }
        Text {
            x: Theme.scaled(60)
            y: -Theme.scaled(19)
            width: Theme.scaled(60)
            horizontalAlignment: Text.AlignHCenter
            text: "PORTAL " + Theme.tempUnitSuffix()
            color: Theme.portalTemperatureColor
            font.pixelSize: Theme.scaled(10)
            visible: Settings.graph.showPortalTemperature
        }
    }
}
