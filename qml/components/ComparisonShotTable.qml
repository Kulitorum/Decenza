import QtQuick
import QtQuick.Layouts
import DecenzaDE1

// Transposed comparison table: rows = metrics + phases, columns = shots.
// Phase rows are tappable toggles that show/hide vertical phase lines on the graph.
ColumnLayout {
    id: root

    required property var graph
    required property var comparisonModel

    Layout.fillWidth: true
    spacing: 0

    readonly property real labelColW: Theme.scaled(72)

    function metricValue(key, info) {
        switch (key) {
            case "profile": {
                var name = info.profileName || "\u2014"
                var t = info.temperatureOverride
                return (t !== undefined && t !== null && t > 0) ? name + " (" + Math.round(t) + "\u00B0C)" : name
            }
            case "duration":  return (info.duration || 0).toFixed(1) + "s"
            case "dose":      return (info.doseWeight || 0).toFixed(1) + "g"
            case "output": {
                var actual = (info.finalWeight || 0).toFixed(1) + "g"
                var y = info.yieldOverride
                return (y !== undefined && y !== null && y > 0 && Math.abs(y - info.finalWeight) > 0.5)
                       ? actual + " (" + Math.round(y) + "g)" : actual
            }
            case "ratio":     return info.ratio || "\u2014"
            case "rating":    return (info.enjoyment || 0) + "%"
            case "bean": {
                var bean = (info.beanBrand || "") + (info.beanType ? " " + info.beanType : "")
                var grind = info.grinderSetting || ""
                if (bean && grind) return bean + " (" + grind + ")"
                return bean || (grind ? "(" + grind + ")" : "\u2014")
            }
            case "roast": {
                var parts = []
                if (info.roastLevel) parts.push(info.roastLevel)
                if (info.roastDate)  parts.push(info.roastDate)
                return parts.length > 0 ? parts.join(", ") : "\u2014"
            }
            case "tdsEy": {
                var p = []
                if (info.drinkTds > 0) p.push(info.drinkTds.toFixed(2) + "%")
                if (info.drinkEy  > 0) p.push(info.drinkEy.toFixed(1)  + "%")
                return p.join(" / ") || "\u2014"
            }
            case "barista": return info.barista || "\u2014"
            case "notes":   return info.notes   || "\u2014"
        }
        return "\u2014"
    }

    // Returns true if at least one shot has data for this metric key
    function metricRowVisible(key) {
        for (var i = 0; i < comparisonModel.shotCount; i++) {
            var info = comparisonModel.getShotInfo(i)
            switch (key) {
                case "tdsEy":   if (info.drinkTds > 0 || info.drinkEy  > 0) return true; break
                case "barista": if (info.barista !== "")                      return true; break
                case "notes":   if (info.notes   !== "")                      return true; break
                default: return true
            }
        }
        return false
    }

    readonly property var metricDefs: [
        { key: "duration", label: "Duration"  },
        { key: "dose",     label: "Dose"      },
        { key: "output",   label: "Output"    },
        { key: "ratio",    label: "Ratio"     },
        { key: "rating",   label: "Rating"    },
        { key: "bean",     label: "Bean"      },
        { key: "roast",    label: "Roast"     },
        { key: "tdsEy",    label: "TDS/EY"   },
        { key: "barista",  label: "Barista"   },
        { key: "notes",    label: "Notes"     },
    ]

    // ── Shot header row ──────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall
        Layout.bottomMargin: Theme.spacingSmall

        Item { Layout.preferredWidth: root.labelColW; height: Theme.scaled(44) }

        Repeater {
            model: comparisonModel.shots

            ColumnLayout {
                required property int index
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                // Line indicator + profile name on one row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)

                    Item {
                        width: Theme.scaled(20); height: Theme.scaled(12)
                        Rectangle {
                            visible: index % 3 === 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width; height: Theme.scaled(2); color: Theme.textColor
                        }
                        Row {
                            visible: index % 3 === 1
                            anchors.verticalCenter: parent.verticalCenter; spacing: Theme.scaled(2)
                            Repeater { model: 4; Rectangle { width: Theme.scaled(3); height: Theme.scaled(2); color: Theme.textColor } }
                        }
                        Row {
                            visible: index % 3 === 2
                            anchors.verticalCenter: parent.verticalCenter; spacing: Theme.scaled(2)
                            Rectangle { width: Theme.scaled(5); height: Theme.scaled(2); color: Theme.textColor }
                            Rectangle { width: Theme.scaled(2); height: Theme.scaled(2); color: Theme.textColor }
                            Rectangle { width: Theme.scaled(3); height: Theme.scaled(2); color: Theme.textColor }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.metricValue("profile", root.comparisonModel.getShotInfo(index))
                        font: Theme.labelFont
                        color: Theme.textColor
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                }

                // Date as subtext, indented to align under profile name
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.scaled(26)
                    text: root.comparisonModel.getShotInfo(index).dateTime || ""
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
            }
        }
    }

    // Separator
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor; Layout.bottomMargin: Theme.scaled(4) }

    // ── Metric rows ──────────────────────────────────────────────────────────
    Repeater {
        model: root.metricDefs

        RowLayout {
            id: metricRowItem
            required property var modelData
            visible: root.metricRowVisible(modelData.key)
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Layout.topMargin: Theme.scaled(3)

            Text {
                Layout.preferredWidth: root.labelColW
                text: metricRowItem.modelData.label
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                elide: Text.ElideRight
                Accessible.ignored: true
            }

            Repeater {
                model: root.comparisonModel.shots
                Text {
                    required property int index
                    Layout.fillWidth: true
                    text: root.metricValue(metricRowItem.modelData.key, root.comparisonModel.getShotInfo(index))
                    horizontalAlignment: Text.AlignLeft
                    font: Theme.labelFont
                    color: metricRowItem.modelData.key === "rating" ? Theme.warningColor : Theme.textColor
                    elide: metricRowItem.modelData.key === "notes" ? Text.ElideNone : Text.ElideRight
                    wrapMode: metricRowItem.modelData.key === "notes" ? Text.Wrap : Text.NoWrap
                    Accessible.ignored: true
                }
            }
        }
    }

}
