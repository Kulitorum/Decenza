import QtQuick
import QtQuick.Layouts
import Decenza

// Unified shot comparison table: header row (column toggles) + one row per shot (row toggle).
// Replaces the separate legend, curve toggle buttons, and ComparisonInspectBar.
//
// Column headers: tap to toggle that curve type on/off in the graph.
// Row headers:    tap to toggle that shot's lines on/off.
// Data cells:     show crosshair values when graph.inspecting, "—" otherwise.
ColumnLayout {
    id: root

    required property var graph
    required property var comparisonModel
    property bool advancedMode: false

    Layout.fillWidth: true
    spacing: Theme.spacingSmall

    // Fixed width for the shot-label column; data columns are narrow fixed width.
    // Advanced mode packs 4 extra columns in, so cells shrink to keep the table
    // inside the page width on mobile.
    readonly property real shotColW: Theme.scaled(advancedMode ? 96 : 120)
    readonly property real dataColW: Theme.scaled(advancedMode ? 40 : 56)

    // Whether a shot slot is currently visible
    function shotVisible(i) {
        if (i === 0) return graph.showShot0
        if (i === 1) return graph.showShot1
        return graph.showShot2
    }
    function toggleShot(i) {
        if      (i === 0) graph.showShot0 = !graph.showShot0
        else if (i === 1) graph.showShot1 = !graph.showShot1
        else              graph.showShot2 = !graph.showShot2
    }

    // Formatted value for a data cell (returns "—" when not inspecting or no data)
    function cellText(shotIdx, key) {
        if (!graph.inspecting || shotIdx >= graph.inspectShotValues.length) return "\u2014"
        var sv = graph.inspectShotValues[shotIdx]
        // In narrow advanced mode cells we drop units to keep numbers readable.
        var compact = root.advancedMode
        switch (key) {
            case "pressure":    return sv.hasPressure    ? sv.pressure.toFixed(1)    + (compact ? "" : " bar")  : "\u2014"
            case "flow":        return sv.hasFlow        ? sv.flow.toFixed(1)        + (compact ? "" : " mL/s") : "\u2014"
            case "temp":        return sv.hasTemperature ? sv.temperature.toFixed(1) + (compact ? "" : " \u00B0C") : "\u2014"
            case "weight":      return sv.hasWeight      ? sv.weight.toFixed(1)      + (compact ? "" : " g")    : "\u2014"
            case "weightFlow":  return sv.hasWeightFlow  ? sv.weightFlow.toFixed(1)  + (compact ? "" : " g/s")  : "\u2014"
            case "resistance":  return sv.hasResistance  ? sv.resistance.toFixed(1)           : "\u2014"
            case "conductance": return sv.hasConductance ? sv.conductance.toFixed(1)          : "\u2014"
            case "dCdt":        return sv.hasConductanceDerivative ? sv.conductanceDerivative.toFixed(1) : "\u2014"
            case "darcyR":      return sv.hasDarcyResistance       ? sv.darcyResistance.toFixed(1)       : "\u2014"
            case "mixTemp":     return sv.hasTemperatureMix        ? sv.temperatureMix.toFixed(1) + (compact ? "" : " \u00B0C") : "\u2014"
        }
        return "\u2014"
    }

    // Settings keys corresponding to each graph property (for persistence)
    readonly property var settingsKeys: ({
        "showPressure":    "graph/showPressure",
        "showFlow":        "graph/showFlow",
        "showTemperature": "graph/showTemperature",
        "showWeight":      "graph/showWeight",
        "showWeightFlow":  "graph/showWeightFlow",
        "showResistance":  "graph/showResistance",
        "showConductance": "graph/showConductance",
        "showConductanceDerivative": "graph/showConductanceDerivative",
        "showDarcyResistance":       "graph/showDarcyResistance",
        "showTemperatureMix":        "graph/showTemperatureMix"
    })

    function toggleCurve(key) {
        var newVal = !graph[key]
        graph[key] = newVal
        var sKey = settingsKeys[key]
        if (sKey) Settings.setValue(sKey, newVal)
    }

    // Column definitions (order matches data cells in each shot row).
    // Columns flagged `advanced` only render when advancedMode is on.
    readonly property var allColumns: [
        { key: "showPressure",    dataKey: "pressure",    label: "P",    dotColor: Theme.pressureColor,             advanced: false },
        { key: "showFlow",        dataKey: "flow",        label: "F",    dotColor: Theme.flowColor,                 advanced: false },
        { key: "showTemperature", dataKey: "temp",        label: "T",    dotColor: Theme.temperatureColor,          advanced: false },
        { key: "showWeight",      dataKey: "weight",      label: "W",    dotColor: Theme.weightColor,               advanced: false },
        { key: "showWeightFlow",  dataKey: "weightFlow",  label: "WF",   dotColor: Theme.weightFlowColor,           advanced: false },
        { key: "showResistance",  dataKey: "resistance",  label: "R",    dotColor: Theme.resistanceColor,           advanced: true  },
        { key: "showConductance", dataKey: "conductance", label: "C",    dotColor: Theme.conductanceColor,          advanced: true  },
        { key: "showConductanceDerivative", dataKey: "dCdt",   label: "dC/dt", dotColor: Theme.conductanceDerivativeColor, advanced: true },
        { key: "showDarcyResistance",       dataKey: "darcyR", label: "dR",    dotColor: Theme.darcyResistanceColor,        advanced: true },
        { key: "showTemperatureMix",        dataKey: "mixTemp",label: "Tmix",  dotColor: Theme.temperatureMixColor,         advanced: true }
    ]
    readonly property var columns: {
        var out = []
        for (var i = 0; i < allColumns.length; i++) {
            if (!allColumns[i].advanced || advancedMode) out.push(allColumns[i])
        }
        return out
    }

    // ── Header row ─────────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        // Corner: shows crosshair time when inspecting
        Text {
            Layout.preferredWidth: root.shotColW
            text: graph.inspecting ? graph.inspectTime.toFixed(1) + "s" : ""
            font.family: Theme.captionFont.family
            font.pixelSize: Theme.captionFont.pixelSize
            font.bold: true
            color: Theme.textColor
            Accessible.ignored: true
        }

        // Column toggle headers (one per curve type)
        Repeater {
            model: root.columns

            Rectangle {
                required property var modelData
                required property int index

                Layout.preferredWidth: root.dataColW
                height: Theme.scaled(28)
                radius: Theme.scaled(14)
                color: graph[modelData.key] ? Theme.surfaceColor : "transparent"
                border.color: graph[modelData.key] ? Theme.primaryColor : Theme.borderColor
                border.width: 1
                opacity: graph[modelData.key] ? 1.0 : 0.5

                Accessible.role: Accessible.CheckBox
                Accessible.name: modelData.label
                Accessible.checked: graph[modelData.key]
                Accessible.focusable: true
                Accessible.onPressAction: root.toggleCurve(modelData.key)

                RowLayout {
                    anchors.centerIn: parent
                    spacing: Theme.scaled(3)
                    Rectangle {
                        width: Theme.scaled(6); height: Theme.scaled(6); radius: Theme.scaled(3)
                        color: modelData.dotColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: modelData.label
                        font: Theme.captionFont
                        color: Theme.textColor
                        Accessible.ignored: true
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.toggleCurve(modelData.key)
                }
            }
        }

    }

    // ── Shot rows (one per visible shot in window) ──────────────────────────────
    Repeater {
        model: root.comparisonModel.shots

        RowLayout {
            id: shotRow
            required property int index
            // Expose for inner use (inner delegates would shadow `index`)
            property int shotIdx: index

            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            opacity: root.shotVisible(shotIdx) ? 1.0 : 0.4

            // Row header: line-style indicator + date, tap to toggle shot visibility
            Rectangle {
                Layout.preferredWidth: root.shotColW
                height: Theme.scaled(44)
                radius: Theme.scaled(10)
                color: Theme.surfaceColor
                border.color: root.shotVisible(shotRow.shotIdx) ? Theme.primaryColor : Theme.borderColor
                border.width: 1

                Accessible.role: Accessible.CheckBox
                Accessible.name: {
                    var info = root.comparisonModel.getShotInfo(shotRow.shotIdx)
                    return "Shot " + (shotRow.shotIdx + 1) + " " + (info.dateTime || "")
                }
                Accessible.checked: root.shotVisible(shotRow.shotIdx)
                Accessible.focusable: true
                Accessible.onPressAction: root.toggleShot(shotRow.shotIdx)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.scaled(6)
                    anchors.rightMargin: Theme.scaled(4)
                    spacing: Theme.scaled(4)

                    // Line-style indicator (solid / dashed / dash-dot)
                    Item {
                        width: Theme.scaled(16)
                        height: parent.height
                        Accessible.ignored: true

                        // Shot 0: solid
                        Rectangle {
                            visible: shotRow.shotIdx === 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width; height: Theme.scaled(2)
                            color: Theme.textColor
                        }
                        // Shot 1: dashed
                        Row {
                            visible: shotRow.shotIdx === 1
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.scaled(2)
                            Repeater {
                                model: 3
                                Rectangle { width: Theme.scaled(3); height: Theme.scaled(2); color: Theme.textColor }
                            }
                        }
                        // Shot 2: dash-dot
                        Row {
                            visible: shotRow.shotIdx === 2
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.scaled(2)
                            Rectangle { width: Theme.scaled(5); height: Theme.scaled(2); color: Theme.textColor }
                            Rectangle { width: Theme.scaled(2); height: Theme.scaled(2); color: Theme.textColor }
                            Rectangle { width: Theme.scaled(3); height: Theme.scaled(2); color: Theme.textColor }
                        }
                    }

                    // Profile name + date subtext
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(1)

                        Text {
                            Layout.fillWidth: true
                            text: root.comparisonModel.getShotInfo(shotRow.shotIdx).profileName || ""
                            font.family: Theme.captionFont.family
                            font.pixelSize: Theme.captionFont.pixelSize
                            color: Theme.textColor
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.comparisonModel.getShotInfo(shotRow.shotIdx).dateTime || ""
                            font.family: Theme.captionFont.family
                            font.pixelSize: Math.round(Theme.captionFont.pixelSize * 0.85)
                            color: Theme.textSecondaryColor
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.toggleShot(shotRow.shotIdx)
                }
            }

            // Data cells — one per column, driven by root.columns so basic/advanced
            // columns stay aligned with the header Repeater above.
            Repeater {
                model: root.columns
                Text {
                    required property var modelData
                    Layout.preferredWidth: root.dataColW
                    text: root.cellText(shotRow.shotIdx, modelData.dataKey)
                    horizontalAlignment: Text.AlignHCenter
                    font: Theme.captionFont
                    color: modelData.dotColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
            }
        }
    }
}
