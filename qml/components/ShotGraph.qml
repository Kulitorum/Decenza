pragma ComponentBehavior: Bound

import QtQuick
import Decenza

// The native and browser views share the same graph adapter and live service.
Item {
    id: chart

    readonly property alias graphsViewRef: graphView.graphsViewRef
    readonly property rect plotArea: graphView.plotArea
    readonly property bool advancedMode: options.advancedMode
    readonly property string rightAxisMode: options.rightAxisMode
    readonly property int flowMultiplier: options.flowMultiplier

    PreferenceWrite { id: preferenceWrite; onFailed: message => writeError.show(message) }
    StatusToast { id: writeError; error: true }

    // Public action used by AutoFavoriteInfoPage when the user taps the right axis.
    function toggleRightAxis() {
        const current = RecipeContext.preferences.rightAxisMode || "weight"
        const next = current === "weight" ? "temperature"
            : current === "temperature" ? "flow" : "weight"
        preferenceWrite.submit("rightAxisMode", next)
    }

    // Live samples and the PORTAL overlay use one host-owned stream in both builds.
    PortalGraphLiveSource {
        id: livePortalSource
        function readSamples(): var { return LiveShotGraph.portalSamples }
        sampleCount: LiveShotGraph.portalSampleCount
        rawTime: LiveShotGraph.rawTime
        ecMin: LiveShotGraph.portalEcMin
        ecMax: LiveShotGraph.portalEcMax
    }
    Connections {
        target: LiveShotGraph
        function onPortalSampleAdded(time, ecRaw, temperatureC, breakBefore) {
            livePortalSource.sampleAdded(time, ecRaw, temperatureC, breakBefore)
        }
        function onCleared() { livePortalSource.cleared() }
    }

    ShotGraphData {
        id: liveGraphData
        rawTime: LiveShotGraph.rawTime
        phaseMarkers: LiveShotGraph.phaseMarkers
        pressureGoalPoints: LiveShotGraph.pressureGoalPoints
        flowGoalPoints: LiveShotGraph.flowGoalPoints
        temperatureGoalPoints: LiveShotGraph.temperatureGoalPoints
        temperatureMixGoalPoints: LiveShotGraph.temperatureMixGoalPoints
        profileTargetWeight: LiveShotGraph.profileTargetWeight
        machineTargetWeight: LiveShotGraph.machineTargetWeight
        weightAtStop: LiveShotGraph.weightAtStop
        finalWeight: LiveShotGraph.finalWeight
    }

    ShotGraphOptions {
        id: options
        advancedMode: Settings.graph.advancedMode
        rightAxisMode: Settings.graph.rightAxisMode
        flowMultiplier: Settings.graph.flowMultiplier
        showPressure: Settings.graph.showPressure
        showFlow: Settings.graph.showFlow
        showTemperature: Settings.graph.showTemperature
        showTemperatureMixGoal: Settings.graph.showTemperatureMixGoal
        showResistance: Settings.graph.showResistance
        showConductance: Settings.graph.showConductance
        showDarcyResistance: Settings.graph.showDarcyResistance
        showWeightFlow: Settings.graph.showWeightFlow
        showTemperatureMix: Settings.graph.showTemperatureMix
        showWeight: Settings.graph.showWeight
    }

    ShotGraphStyle {
        id: sharedGraphStyle
        scale: Theme.scale
        darkMode: Settings.theme.isDarkMode
        fahrenheit: Theme.tempIsFahrenheit()
        captionFont: Theme.captionFont
        textSecondaryColor: Theme.textSecondaryColor
        surfaceColor: Theme.surfaceColor
        pressureColor: Theme.pressureColor
        pressureGoalColor: Theme.pressureGoalColor
        flowColor: Theme.flowColor
        flowGoalColor: Theme.flowGoalColor
        temperatureColor: Theme.temperatureColor
        temperatureGoalColor: Theme.temperatureGoalColor
        weightColor: Theme.weightColor
        weightFlowColor: Theme.weightFlowColor
        resistanceColor: Theme.resistanceColor
        conductanceColor: Theme.conductanceColor
        darcyResistanceColor: Theme.darcyResistanceColor
        temperatureMixColor: Theme.temperatureMixColor
        temperatureMixGoalColor: Theme.temperatureMixGoalColor
        accentColor: Theme.accentColor
        stopMarkerColor: Theme.stopMarkerColor
        frameMarkerColor: Theme.frameMarkerColor
        portalEcColor: Theme.portalEcColor
        portalTemperatureColor: Theme.portalTemperatureColor
        timeAxisTitle: TranslationManager.translate("graph.axis.time", "Time (s)")
        weightAxisAnnouncement: TranslationManager.translate(
            "graph.rightAxisModeWeight", "Right axis: Weight. Tap for Temperature")
        flowAxisAnnouncement: TranslationManager.translate(
            "graph.rightAxisModeFlow", "Right axis: Flow. Tap for Weight")
        temperatureAxisAnnouncement: TranslationManager.translate(
            "graph.rightAxisModeTemperature", "Right axis: Temperature. Tap for Flow")
        flowUnit: TranslationManager.translate("graph.unit.flow", "mL/s")
        temperatureUnit: Theme.tempUnitSuffix()
    }

    // The shared view reserves the portal-label column using the same condition
    // as PortalGraphOverlay. The overlay itself remains host-owned and optional.
    readonly property real portalLabelWidth: options.advancedMode
        && (Settings.graph.showPortalEc || Settings.graph.showPortalTemperature)
        && LiveShotGraph.portalSampleCount > 0 ? Theme.scaled(125) : 0

    ShotGraphView {
        id: graphView
        anchors.fill: parent
        graphData: liveGraphData
        graphOptions: options
        graphStyle: sharedGraphStyle
        portalLabelWidth: chart.portalLabelWidth
        accessibilityEnabled: AccessibilityManager.enabled
        lastAnnouncedItem: AccessibilityManager.lastAnnouncedItem

        portalOverlayComponent: Component {
            PortalGraphOverlay {
                anchors.fill: parent
                graphsView: graphView.graphsViewRef
                axisX: graphView.timeAxisRef
                live: true
                liveSource: livePortalSource
                advancedMode: options.advancedMode
            }
        }

        onFastSeriesReady: function(pressure, flow, temperature, weight,
                                    weightFlow, resistance, conductance,
                                    darcyResistance, temperatureMix) {
            LiveShotGraph.registerFastSeries(pressure, flow, temperature, weight,
                weightFlow, resistance, conductance, darcyResistance, temperatureMix)
        }
        onRightAxisTapped: chart.toggleRightAxis()
        onAccessibilityAnnouncementRequested: function(announcement) {
            AccessibilityManager.announce(announcement)
        }
        onAccessibilityExplorationRequested: function(announcement, item) {
            AccessibilityManager.lastAnnouncedItem = item
            AccessibilityManager.announce(announcement)
        }
        onAccessibilityExplorationCleared: function(item) {
            if (AccessibilityManager.lastAnnouncedItem === item)
                AccessibilityManager.lastAnnouncedItem = null
        }
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - Theme.spacingLarge * 2
        visible: !LiveShotGraph.ready
        text: LiveShotGraph.errorMessage || TranslationManager.translate("common.loading", "Loading…")
        color: Theme.textColor
        font: Theme.bodyFont
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

}
