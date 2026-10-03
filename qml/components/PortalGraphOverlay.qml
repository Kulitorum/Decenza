pragma ComponentBehavior: Bound
import QtQuick
import QtGraphs
import Decenza

Item {
    id: portalGraph
    property GraphsView graphsView: null
    property var axisX: null
    property var samples: []
    property bool showLabels: true
    property bool live: false
    // PORTAL curves are advanced-only; the host chart says whether it is in advanced mode.
    property bool advancedMode: false
    readonly property bool ecVisible: advancedMode && Settings.graph.showPortalEc
    readonly property bool temperatureVisible: advancedMode && Settings.graph.showPortalTemperature
    readonly property bool hasData: live ? ShotDataModel.portalSampleCount > 0 : samples.length > 0
    readonly property bool hasVisibleData: hasData && (ecVisible || temperatureVisible)
    readonly property real labelWidth: hasVisibleData && showLabels ? Theme.scaled(125) : 0
    // Plot origin in this item's coordinates (it fills the graph's outer item, where the
    // GraphsView may sit below a margin); plotArea alone is in the view's coordinates.
    readonly property real plotX: graphsView ? graphsView.x + graphsView.plotArea.x : 0
    readonly property real plotY: graphsView ? graphsView.y + graphsView.plotArea.y : 0
    readonly property real lastTime: live ? ShotDataModel.rawTime : (hasData ? samples[samples.length - 1].time : 0)

    function segments(field) {
        var result = []
        var current = []
        for (let i = 0; i < samples.length; i++) {
            let sample = samples[i]
            if (sample.breakBefore && current.length > 0) {
                result.push(current)
                current = []
            }
            current.push(Qt.point(sample.time, sample[field]))
        }
        if (current.length) result.push(current)
        return result
    }
    readonly property var ecSegments: live ? [] : segments("ecRaw")
    readonly property var temperatureSegments: live ? [] : segments("temperatureC")
    readonly property var ecRange: {
        var bounds = live ? [ShotDataModel.portalEcMin, ShotDataModel.portalEcMax]
                          : ShotDataModel.portalEcBounds(samples)
        return [bounds[0] * ShotDataModel.portalEcAxisPadding, bounds[1] * ShotDataModel.portalEcAxisPadding]
    }
    // Hidden axes: the saved-shot series map through them, the live renderers and the
    // label column read their min/max. A hidden axis reserves no plot space
    // (axisrenderer.cpp:1082).
    ValueAxis {
        id: ecAxis
        visible: false
        min: portalGraph.ecRange[0]
        max: portalGraph.ecRange[1]
    }
    ValueAxis {
        id: outletAxis
        visible: false
        min: 0
        max: 100
    }
    GraphSeriesInstantiator {
        graphsView: portalGraph.graphsView
        model: portalGraph.ecSegments
        delegate: LineSeries {
            required property var modelData
            axisY: ecAxis
            values: modelData
            color: Theme.portalEcColor
            width: Theme.graphLineWidth
            visible: portalGraph.ecVisible
        }
    }
    GraphSeriesInstantiator {
        graphsView: portalGraph.graphsView
        model: portalGraph.temperatureSegments
        delegate: LineSeries {
            required property var modelData
            axisY: outletAxis
            values: modelData
            color: Theme.portalTemperatureColor
            width: Theme.graphLineWidth
            visible: portalGraph.temperatureVisible
        }
    }
    // Live capture appends directly to persistent native renderers. Only page entry
    // replays the saved list; packet arrivals never copy/re-segment that list.
    property var liveSegments: []
    function clearLive() {
        for (let i = 0; i < liveSegments.length; i++) liveSegments[i].destroy()
        liveSegments = []
    }
    function appendLive(time, ecRaw, temperatureC, breakBefore) {
        if (!live) return
        if (breakBefore || liveSegments.length === 0) {
            let segment = liveSegment.createObject(portalGraph)
            if (!segment) return
            liveSegments.push(segment)
        }
        var current = liveSegments[liveSegments.length - 1]
        current.ec.appendPoint(time, ecRaw)
        current.temperature.appendPoint(time, temperatureC)
    }
    Component.onCompleted: {
        if (!live) return
        var initial = ShotDataModel.portalSamples
        for (let i = 0; i < initial.length; i++) {
            let sample = initial[i]
            appendLive(sample.time, sample.ecRaw, sample.temperatureC, sample.breakBefore)
        }
    }
    Connections {
        target: portalGraph.live ? ShotDataModel : null
        function onPortalSampleAdded(time, ecRaw, temperatureC, breakBefore) {
            portalGraph.appendLive(time, ecRaw, temperatureC, breakBefore)
        }
        function onCleared() { portalGraph.clearLive() }
    }
    Component {
        id: liveSegment
        Item {
            readonly property alias ec: liveEc
            readonly property alias temperature: liveTemperature
            x: portalGraph.plotX
            y: portalGraph.plotY
            width: portalGraph.graphsView ? portalGraph.graphsView.plotArea.width : 0
            height: portalGraph.graphsView ? portalGraph.graphsView.plotArea.height : 0
            clip: true
            FastLineRenderer {
                id: liveEc
                anchors.fill: parent
                minX: portalGraph.axisX ? portalGraph.axisX.min : 0
                maxX: portalGraph.axisX ? portalGraph.axisX.max : 1
                minY: ecAxis.min
                maxY: ecAxis.max
                color: Theme.portalEcColor
                lineWidth: Theme.graphLineWidth
                visible: portalGraph.ecVisible
            }
            FastLineRenderer {
                id: liveTemperature
                anchors.fill: parent
                minX: portalGraph.axisX ? portalGraph.axisX.min : 0
                maxX: portalGraph.axisX ? portalGraph.axisX.max : 1
                minY: outletAxis.min
                maxY: outletAxis.max
                color: Theme.portalTemperatureColor
                lineWidth: Theme.graphLineWidth
                visible: portalGraph.temperatureVisible
            }
        }
    }
    Item {
        id: axes
        visible: portalGraph.hasVisibleData && portalGraph.showLabels
        x: portalGraph.graphsView ? portalGraph.plotX + portalGraph.graphsView.plotArea.width + Theme.scaled(55) : 0
        y: portalGraph.plotY
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
                    visible: portalGraph.ecVisible
                    text: (ecAxis.max - tickRow.index * (ecAxis.max - ecAxis.min) / 2).toFixed(ecAxis.max < 1 ? 3 : 2)
                }
                Text {
                    x: Theme.scaled(60)
                    width: Theme.scaled(60)
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.portalTemperatureColor
                    font.pixelSize: Theme.scaled(11)
                    visible: portalGraph.temperatureVisible
                    text: Theme.cToDisplay(outletAxis.max - tickRow.index * (outletAxis.max - outletAxis.min) / 2).toFixed(0)
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
            visible: portalGraph.ecVisible
        }
        Text {
            x: Theme.scaled(60)
            y: -Theme.scaled(19)
            width: Theme.scaled(60)
            horizontalAlignment: Text.AlignHCenter
            text: "PORTAL " + Theme.tempUnitSuffix()
            color: Theme.portalTemperatureColor
            font.pixelSize: Theme.scaled(10)
            visible: portalGraph.temperatureVisible
        }
    }
}
