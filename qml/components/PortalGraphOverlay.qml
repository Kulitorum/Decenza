pragma ComponentBehavior: Bound
import QtQuick
import Decenza

Item {
    id: portalGraph
    property var graphsView: null
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
    readonly property real lastTime: live ? ShotDataModel.rawTime : (hasData ? samples[samples.length - 1].time : 0)

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
    readonly property var ecSegments: live ? [] : segments("ecRaw")
    readonly property var temperatureSegments: live ? [] : segments("temperatureC")
    readonly property var ecRange: {
        var bounds = live ? [ShotDataModel.portalEcMin, ShotDataModel.portalEcMax]
                          : ShotDataModel.portalEcBounds(samples)
        return [bounds[0] * ShotDataModel.portalEcAxisPadding, bounds[1] * ShotDataModel.portalEcAxisPadding]
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
            visible: portalGraph.ecVisible && points.length >= 2
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
            visible: portalGraph.temperatureVisible && points.length >= 2
        }
    }
    // Live capture appends directly to persistent native renderers. Only page entry
    // replays the saved list; packet arrivals never copy/re-segment that list.
    property var liveSegments: []
    function clearLive() {
        for (var i = 0; i < liveSegments.length; i++) liveSegments[i].destroy()
        liveSegments = []
    }
    function appendLive(time, ecRaw, temperatureC, breakBefore) {
        if (!live) return
        if (breakBefore || liveSegments.length === 0) {
            var segment = liveSegment.createObject(portalGraph)
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
        for (var i = 0; i < initial.length; i++) {
            var sample = initial[i]
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
            x: portalGraph.graphsView ? portalGraph.graphsView.plotArea.x : 0
            y: portalGraph.graphsView ? portalGraph.graphsView.plotArea.y : 0
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
