// Delegates declare the one role they read as required; Bound lets them reach `row`.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza

// One row of the comparison: a label (with its unit) and a cell per shot, each a
// value plus an optional Δ pill against the base. The caller supplies how a cell
// reads, so inputs, metrics and badges share one layout.
ColumnLayout {
    id: row

    property real labelWidth: Theme.scaled(118)
    property real cellWidth: Theme.scaled(100)
    property string label
    property string unit: ""
    property var cells: []
    property var textFor: function(cell) { return "" }
    property var deltaFor: function(cell) { return "" }
    property var deltaSignFor: function(cell) { return 0 }
    property var mutedFor: function(cell) { return false }
    property var warnFor: function(cell) { return false }
    property font numberFont: Theme.bodyFont

    // Direction only: neither colour means better.
    readonly property color increaseColor: Theme.warningColor
    readonly property color decreaseColor: Theme.primaryColor

    spacing: 0

    Accessible.role: Accessible.StaticText
    Accessible.name: {
        let parts = [row.label + (row.unit ? " " + row.unit : "")]
        for (let i = 0; i < row.cells.length; i++) {
            const d = row.deltaFor(row.cells[i])
            parts.push(row.textFor(row.cells[i]) + (d ? " (" + d + ")" : ""))
        }
        return parts.join(", ")
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.borderColor
        opacity: 0.6
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.scaled(6)
        Layout.bottomMargin: Theme.scaled(6)
        spacing: Theme.spacingSmall

        Row {
            Layout.preferredWidth: row.labelWidth
            Layout.alignment: Qt.AlignBaseline
            spacing: Theme.scaled(4)
            Text {
                text: row.label
                font: Theme.labelFont
                color: Theme.textSecondaryColor
                Accessible.ignored: true
            }
            Text {
                visible: row.unit.length > 0
                text: row.unit
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                opacity: 0.7
                Accessible.ignored: true
            }
        }

        Repeater {
            model: row.cells
            delegate: RowLayout {
                id: cellItem
                required property var modelData
                readonly property string delta: row.deltaFor(modelData)
                readonly property int sign: row.deltaSignFor(modelData)
                readonly property color pillColor: sign > 0 ? row.increaseColor : row.decreaseColor
                Layout.preferredWidth: row.cellWidth
                Layout.alignment: Qt.AlignBaseline
                spacing: Theme.scaled(6)

                Text {
                    Layout.maximumWidth: row.cellWidth - (pill.visible ? pill.width + Theme.scaled(6) : 0)
                    text: row.textFor(cellItem.modelData)
                    font.family: row.numberFont.family
                    font.pixelSize: row.numberFont.pixelSize
                    // Equal-width digits, so a column of values and its Δs line up.
                    font.features: ({ "tnum": 1 })
                    color: row.warnFor(cellItem.modelData) ? Theme.warningColor
                         : row.mutedFor(cellItem.modelData) ? Theme.textSecondaryColor : Theme.textColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
                Rectangle {
                    id: pill
                    visible: cellItem.delta.length > 0
                    Layout.preferredWidth: deltaText.implicitWidth + Theme.scaled(12)
                    Layout.preferredHeight: deltaText.implicitHeight + Theme.scaled(2)
                    radius: height / 2
                    color: Qt.alpha(cellItem.pillColor, 0.16)
                    Text {
                        id: deltaText
                        anchors.centerIn: parent
                        text: (cellItem.sign > 0 ? "▲ " : cellItem.sign < 0 ? "▼ " : "") + cellItem.delta.replace(/^[+−]/, "")
                        font.family: row.numberFont.family
                        font.pixelSize: Theme.captionFont.pixelSize
                        font.features: ({ "tnum": 1 })
                        color: cellItem.pillColor
                        Accessible.ignored: true
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
}
