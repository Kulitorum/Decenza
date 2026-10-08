// The per-shot and per-curve Repeater delegates declare the roles they read as
// required; Bound lets them reach `root` statically.
pragma ComponentBehavior: Bound

import QtQuick
import Decenza

// The crosshair values under the plot: curve names once across the top, then a
// row per shot with its values in fixed columns, so shots read against each other.
// Under the plot rather than over it, so it never hides a curve, and a row per
// visible shot whether or not anything is inspected, so a tap never moves anything.
// The graph provides readoutCurves, readoutRowCount, readoutRowVisible(row) and
// readoutText(row, dataKey); a single-shot graph has one row and no swatches.
Flickable {
    id: root

    required property var graph

    readonly property real timeW: Theme.scaled(56)
    readonly property bool multiShot: graph.readoutRowCount > 1
    readonly property real swatchW: multiShot ? Theme.scaled(30) : 0
    readonly property int curveCount: graph.readoutCurves.length
    // Capped, so on a wide window the values stay together instead of spanning it.
    readonly property real colW: Math.min(Theme.scaled(72), Math.max(Theme.scaled(46),
        (width - timeW - swatchW) / Math.max(1, curveCount)))
    readonly property real rowH: Theme.captionFont.pixelSize + Theme.scaled(6)

    implicitHeight: grid.implicitHeight
    contentWidth: grid.implicitWidth
    contentHeight: grid.implicitHeight
    // Scrolls sideways only on a screen too narrow for every curve that is on.
    interactive: contentWidth > width
    flickableDirection: Flickable.HorizontalFlick
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    Accessible.ignored: true  // the graph's inspectAtPosition() announces the values

    Column {
        id: grid

        Row {
            height: root.rowH
            Text {
                width: root.timeW
                height: root.rowH
                verticalAlignment: Text.AlignVCenter
                text: root.graph.inspecting
                      ? root.graph.inspectTime.toFixed(1) + " " + TranslationManager.translate("common.unit.seconds", "s") : ""
                font.family: Theme.captionFont.family
                font.pixelSize: Theme.captionFont.pixelSize
                font.bold: true
                font.features: ({ "tnum": 1 })
                color: Theme.textColor
            }
            Item { width: root.swatchW; height: 1 }
            Repeater {
                model: root.graph.readoutCurves
                delegate: Text {
                    required property var modelData
                    width: root.colW
                    height: root.rowH
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    text: modelData.shortLabel
                    font: Theme.captionFont
                    color: modelData.sColor
                    elide: Text.ElideRight
                }
            }
        }

        Repeater {
            model: root.graph.readoutRowCount
            delegate: Row {
                id: shotRow
                required property int index
                visible: root.graph.readoutRowVisible(index)
                height: root.rowH

                Item { width: root.timeW; height: 1 }
                Item {
                    width: root.swatchW
                    height: root.rowH
                    ComparisonLineSwatch {
                        visible: root.multiShot
                        anchors.verticalCenter: parent.verticalCenter
                        shotIndex: shotRow.index
                        heavy: shotRow.index === 0
                    }
                }
                Repeater {
                    model: root.graph.readoutCurves
                    delegate: Text {
                        id: cell
                        required property var modelData
                        readonly property string value: root.graph.readoutText(shotRow.index, modelData.dataKey)
                        width: root.colW
                        height: root.rowH
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        // Blank until a tap; a shot that has ended keeps its row and its
                        // cells go quiet.
                        text: value.length > 0 ? value : root.graph.inspecting ? "–" : ""
                        font.family: Theme.captionFont.family
                        font.pixelSize: Theme.captionFont.pixelSize
                        font.features: ({ "tnum": 1 })
                        color: value.length > 0 ? Theme.textColor : Theme.textSecondaryColor
                        opacity: value.length > 0 ? 1 : 0.5
                    }
                }
            }
        }
    }
}
