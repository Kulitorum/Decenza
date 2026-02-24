import QtQuick
import QtQuick.Layouts
import DecenzaDE1

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

    Layout.fillWidth: true
    spacing: Theme.spacingSmall

    // Fixed width for the shot-label column; data columns are narrow fixed width.
    readonly property real shotColW: Theme.scaled(120)
    readonly property real dataColW: Theme.scaled(56)

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
        switch (key) {
            case "pressure":    return sv.hasPressure    ? sv.pressure.toFixed(1)    + " bar"  : "\u2014"
            case "flow":        return sv.hasFlow        ? sv.flow.toFixed(1)        + " mL/s" : "\u2014"
            case "temp":        return sv.hasTemperature ? sv.temperature.toFixed(1) + " \u00B0C" : "\u2014"
            case "weight":      return sv.hasWeight      ? sv.weight.toFixed(1)      + " g"    : "\u2014"
            case "weightFlow":  return sv.hasWeightFlow  ? sv.weightFlow.toFixed(1)  + " g/s"  : "\u2014"
            case "resistance":  return sv.hasResistance  ? sv.resistance.toFixed(1)           : "\u2014"
        }
        return "\u2014"
    }

    // Column definitions (order matches data cells in each shot row)
    readonly property var columns: [
        { key: "showPressure",    dataKey: "pressure",   label: "P",  unit: "bar",  dotColor: Theme.pressureColor    },
        { key: "showFlow",        dataKey: "flow",       label: "F",  unit: "mL/s", dotColor: Theme.flowColor        },
        { key: "showTemperature", dataKey: "temp",       label: "T",  unit: "°C",   dotColor: Theme.temperatureColor },
        { key: "showWeight",      dataKey: "weight",     label: "W",  unit: "g",    dotColor: Theme.weightColor      },
        { key: "showWeightFlow",  dataKey: "weightFlow", label: "WF", unit: "g/s",  dotColor: Theme.weightFlowColor  },
        { key: "showResistance",  dataKey: "resistance", label: "R",  unit: "",     dotColor: Theme.resistanceColor  }
    ]

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
                Accessible.onPressAction: graph[modelData.key] = !graph[modelData.key]

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
                    onClicked: graph[modelData.key] = !graph[modelData.key]
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

            // Data cells — one per column, aligned with header
            Text {
                Layout.preferredWidth: root.dataColW
                text: root.cellText(shotRow.shotIdx, "pressure")
                horizontalAlignment: Text.AlignHCenter
                font: Theme.captionFont
                color: Theme.pressureColor
                Accessible.ignored: true
            }
            Text {
                Layout.preferredWidth: root.dataColW
                text: root.cellText(shotRow.shotIdx, "flow")
                horizontalAlignment: Text.AlignHCenter
                font: Theme.captionFont
                color: Theme.flowColor
                Accessible.ignored: true
            }
            Text {
                Layout.preferredWidth: root.dataColW
                text: root.cellText(shotRow.shotIdx, "temp")
                horizontalAlignment: Text.AlignHCenter
                font: Theme.captionFont
                color: Theme.temperatureColor
                Accessible.ignored: true
            }
            Text {
                Layout.preferredWidth: root.dataColW
                text: root.cellText(shotRow.shotIdx, "weight")
                horizontalAlignment: Text.AlignHCenter
                font: Theme.captionFont
                color: Theme.weightColor
                Accessible.ignored: true
            }
            Text {
                Layout.preferredWidth: root.dataColW
                text: root.cellText(shotRow.shotIdx, "weightFlow")
                horizontalAlignment: Text.AlignHCenter
                font: Theme.captionFont
                color: Theme.weightFlowColor
                Accessible.ignored: true
            }
            Text {
                Layout.preferredWidth: root.dataColW
                text: root.cellText(shotRow.shotIdx, "resistance")
                horizontalAlignment: Text.AlignHCenter
                font: Theme.captionFont
                color: Theme.resistanceColor
                Accessible.ignored: true
            }

        }
    }
}
