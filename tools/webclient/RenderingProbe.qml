import QtQuick
import QtQuick.Controls
import Decenza

Column {
    id: probe
    spacing: 12
    property alias trace: replay

    RecordedTrace { id: replay }

    Label {
        width: parent.width
        text: "Shared production graph"
        font.bold: true
        wrapMode: Text.WordWrap
    }
    Label {
        width: parent.width
        text: "Recorded graph fixture. The graph view is shared with native Decenza. Portal overlay is not included."
        wrapMode: Text.WordWrap
    }

    Rectangle {
        width: parent.width
        height: 320
        color: "#161b20"

        ShotGraphView {
            id: graphView
            anchors.fill: parent
            portalLabelWidth: 0
            graphData: ShotGraphData {
                rawTime: replay.elapsed
                pressureGoalPoints: replay.pressureGoalPoints
                flowGoalPoints: replay.flowGoalPoints
                temperatureGoalPoints: replay.temperatureGoalPoints
                profileTargetWeight: 36
                phaseMarkers: replay.elapsed >= replay.duration
                    ? [{time: 0, label: "Start"}, {time: replay.duration, label: "End"}]
                    : [{time: 0, label: "Start"}]
            }
            graphOptions: ShotGraphOptions {
                id: graphOptionsValue
                advancedMode: true
                showResistance: false
                showConductance: false
                showDarcyResistance: false
                showTemperatureMixGoal: false
            }
            graphStyle: ShotGraphStyle {
                captionFont: Qt.font({family: "Decenza Sans", pixelSize: 12})
            }
            onFastSeriesReady: function(pressure, flow, temperature, weight,
                                       weightFlow, resistance, conductance,
                                       darcyResistance, temperatureMix) {
                replay.attach(pressure, flow, temperature, weight, weightFlow, temperatureMix)
            }
            onRightAxisTapped: {
                graphOptionsValue.rightAxisMode = graphOptionsValue.rightAxisMode === "weight" ? "temperature"
                    : (graphOptionsValue.rightAxisMode === "temperature" ? "flow" : "weight")
            }
        }
    }
    Label {
        width: parent.width
        text: "Fixture: blooming_aborted.json · " + replay.sampleCount + " samples · "
            + replay.elapsed.toFixed(1) + " / " + replay.duration.toFixed(1) + " s"
        wrapMode: Text.WordWrap
    }
    Row {
        spacing: 8
        Button {
            text: "Replay 63 seconds"
            enabled: !replay.playing
            Accessible.name: text
            Accessible.role: Accessible.Button
            Accessible.focusable: true
            Accessible.onPressAction: replay.start()
            onClicked: replay.start()
        }
        Button {
            text: "Show all"
            Accessible.name: text
            Accessible.role: Accessible.Button
            Accessible.focusable: true
            Accessible.onPressAction: replay.showAll()
            onClicked: replay.showAll()
        }
    }
    Label {
        width: parent.width
        text: replay.error || (replay.playing ? "Measuring presented frames…"
            : "Last replay: " + replay.frameCount + " presented-frame intervals, p95 "
                + replay.frameP95Ms.toFixed(1) + " ms (this viewport and browser only)")
        wrapMode: Text.WordWrap
    }
}
