import QtQuick
import QtCharts
import Decenza
import "."  // For AccessibleMouseArea

ChartView {
    id: chart
    antialiasing: true
    backgroundColor: "transparent"
    plotAreaColor: Qt.darker(Theme.surfaceColor, 1.3)
    legend.visible: false

    // Persisted visibility toggles (tappable legend)
    property bool showPressure: Settings.value("steamGraph/showPressure", true)
    property bool showFlow: Settings.value("steamGraph/showFlow", true)
    property bool showTemperature: Settings.value("steamGraph/showTemperature", true)

    margins.top: Theme.scaled(10)
    margins.bottom: 0
    margins.left: Theme.scaled(40)
    margins.right: Theme.scaled(55)

    Component.onCompleted: {
        SteamDataModel.registerFastSeries(pressureRenderer, flowRenderer, temperatureRenderer)
        SteamDataModel.registerGoalSeries(flowGoalSeries)
        recalcMax()
    }

    // Auto-expanding time axis (same pattern as ShotGraph)
    property double minTime: 5.0
    property double paddingPixels: Theme.scaled(5)
    property double cachedPlotWidth: 1
    property double _lastAxisMax: 5.0

    function recalcMax() {
        var raw = SteamDataModel.rawTime * cachedPlotWidth / Math.max(1, cachedPlotWidth - paddingPixels)
        var newMax = Math.max(minTime, raw)
        if (newMax !== _lastAxisMax) {
            _lastAxisMax = newMax
            timeAxis.max = newMax
            timeAxis.tickCount = Math.min(7, Math.max(3, Math.floor(newMax / 10) + 2))
        }
    }

    Connections {
        target: SteamDataModel
        function onRawTimeChanged() { chart.recalcMax() }
    }

    onPlotAreaChanged: {
        var w = Math.max(1, chart.plotArea.width)
        if (Math.abs(w - cachedPlotWidth) > 1) {
            cachedPlotWidth = w
            recalcMax()
        }
    }

    // Sync visibility toggles from Settings (e.g., changed on another page)
    Connections {
        target: Settings
        function onValueChanged(key) {
            if (key === "steamGraph/showPressure") chart.showPressure = Settings.value("steamGraph/showPressure", true)
            if (key === "steamGraph/showFlow") chart.showFlow = Settings.value("steamGraph/showFlow", true)
            if (key === "steamGraph/showTemperature") chart.showTemperature = Settings.value("steamGraph/showTemperature", true)
        }
    }

    // Time axis (X)
    ValueAxis {
        id: timeAxis
        min: 0
        max: chart.minTime
        tickCount: 3
        labelFormat: "%.0f"
        labelsColor: Theme.textSecondaryColor
        gridLineColor: Qt.rgba(255, 255, 255, 0.1)
    }

    // Pressure/Flow axis (left Y) — steam pressure is typically 0–4 bar
    ValueAxis {
        id: pressureAxis
        min: 0
        max: 6
        tickCount: 4
        labelFormat: "%.0f"
        labelsColor: Theme.textSecondaryColor
        gridLineColor: Qt.rgba(255, 255, 255, 0.1)
        titleText: "bar / mL/s"
        titleBrush: Theme.textSecondaryColor
    }

    // Temperature axis (right Y) — hidden; labels drawn manually
    ValueAxis {
        id: tempAxis
        min: 100
        max: 180
        tickCount: 5
        visible: false
    }

    // Flow goal (dashed line)
    LineSeries {
        id: flowGoalSeries
        name: ""
        color: Theme.flowGoalColor
        width: Theme.scaled(2)
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showFlow
    }

    // Empty anchor series to keep tempAxis registered with ChartView
    LineSeries {
        name: ""
        axisX: timeAxis
        axisYRight: tempAxis
    }

    // === LIVE DATA — FastLineRenderer (pre-allocated VBO) ===

    FastLineRenderer {
        id: pressureRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.pressureColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showPressure
    }

    FastLineRenderer {
        id: flowRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.flowColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showFlow
    }

    FastLineRenderer {
        id: temperatureRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.temperatureColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: tempAxis.min; maxY: tempAxis.max
        visible: chart.showTemperature
    }

    // Time axis label — inside graph at bottom right
    Text {
        x: chart.plotArea.x + chart.plotArea.width - width - Theme.spacingSmall
        y: chart.plotArea.y + chart.plotArea.height - height - Theme.scaled(12)
        text: "Time (s)"
        color: Theme.textSecondaryColor
        font: Theme.captionFont
        opacity: 0.7
        Accessible.ignored: true
    }

    // Manual right-axis labels for temperature
    Item {
        id: rightAxisLabels
        x: chart.plotArea.x + chart.plotArea.width + Theme.scaled(4)
        y: chart.plotArea.y
        width: chart.width - x
        height: chart.plotArea.height

        Accessible.role: Accessible.StaticText
        Accessible.name: TranslationManager.translate("steamGraph.rightAxis", "Temperature axis")
        Accessible.ignored: true

        Repeater {
            model: 5
            Text {
                required property int index
                property real value: tempAxis.max - index * (tempAxis.max - tempAxis.min) / 4
                text: value.toFixed(0)
                x: 0
                y: index / 4 * rightAxisLabels.height - height / 2
                font: Theme.captionFont
                color: Theme.temperatureColor
                Accessible.ignored: true
            }
        }

        Text {
            text: "°C"
            font: Theme.captionFont
            color: Theme.temperatureColor
            rotation: 90
            transformOrigin: Item.Center
            x: Theme.scaled(24)
            y: rightAxisLabels.height / 2 - height / 2
            Accessible.ignored: true
        }
    }

    // === TAPPABLE LEGEND ===

    Row {
        id: legendRow
        x: chart.plotArea.x
        y: chart.plotArea.y + Theme.scaled(4)
        spacing: Theme.spacingMedium

        Repeater {
            model: [
                { label: TranslationManager.translate("steamGraph.legend.pressure", "Pressure"), color: Theme.pressureColor, key: "steamGraph/showPressure", shown: chart.showPressure },
                { label: TranslationManager.translate("steamGraph.legend.flow", "Flow"), color: Theme.flowColor, key: "steamGraph/showFlow", shown: chart.showFlow },
                { label: TranslationManager.translate("steamGraph.legend.temperature", "Temperature"), color: Theme.temperatureColor, key: "steamGraph/showTemperature", shown: chart.showTemperature }
            ]

            delegate: Rectangle {
                id: legendItem
                width: legendItemRow.implicitWidth + Theme.spacingSmall * 2
                height: legendItemRow.implicitHeight + Theme.scaled(4)
                radius: Theme.scaled(4)
                color: "transparent"
                opacity: modelData.shown ? 1.0 : 0.4

                Accessible.ignored: true

                Row {
                    id: legendItemRow
                    anchors.centerIn: parent
                    spacing: Theme.scaled(4)

                    Rectangle {
                        width: Theme.scaled(12)
                        height: Theme.scaled(3)
                        radius: Theme.scaled(1)
                        color: modelData.color
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: modelData.label
                        color: Theme.textColor
                        font: Theme.captionFont
                        Accessible.ignored: true
                    }
                }

                AccessibleMouseArea {
                    anchors.fill: parent
                    accessibleName: modelData.label + (modelData.shown ? "" : " " + TranslationManager.translate("common.hidden", "hidden"))
                    accessibleItem: legendItem
                    onAccessibleClicked: {
                        var newVal = !modelData.shown
                        Settings.setValue(modelData.key, newVal)
                        // Direct update for immediate feedback
                        if (modelData.key === "steamGraph/showPressure") chart.showPressure = newVal
                        else if (modelData.key === "steamGraph/showFlow") chart.showFlow = newVal
                        else if (modelData.key === "steamGraph/showTemperature") chart.showTemperature = newVal
                    }
                }
            }
        }
    }
}
