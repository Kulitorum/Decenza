// CustomLegend — Qt Graphs bridge component.
//
// Qt Graphs has no built-in legend (unlike Qt Charts' `ChartView.legend`).
// This is a themed horizontal legend with tap-to-toggle visibility, styled
// via `Theme.qml`. Pass either a model of `{ name, color, visible }` entries
// or bind series directly via `entries`.
//
// Usage with a static model:
//   CustomLegend {
//       entries: [
//           { name: "Flow",   color: Theme.flowColor,   visible: flowSeries.visible },
//           { name: "Weight", color: Theme.weightColor, visible: weightSeries.visible }
//       ]
//       onEntryToggled: (index, nowVisible) => {
//           if (index === 0) flowSeries.visible = nowVisible
//           else             weightSeries.visible = nowVisible
//       }
//   }
//
// Replaces: Qt Charts `ChartView.legend`.

import QtQuick
import QtQuick.Layouts
import Decenza

Item {
    id: legendRoot

    // Array of { name: string, color: color, visible: bool } objects.
    property var entries: []

    // Whether tapping an entry should toggle its visibility.
    property bool toggleEnabled: true

    // Emitted when the user taps an entry; consumer applies the new state to the series.
    signal entryToggled(int index, bool nowVisible)

    implicitHeight: legendFlow.implicitHeight
    Layout.fillWidth: true

    Flow {
        id: legendFlow
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.spacingSmall

        Repeater {
            model: legendRoot.entries

            delegate: Rectangle {
                id: entryDelegate
                required property int index
                required property var modelData

                readonly property string entryName: modelData && modelData.name !== undefined ? modelData.name : ""
                readonly property color entryColor: modelData && modelData.color !== undefined ? modelData.color : Theme.textColor
                readonly property bool entryVisible: modelData && modelData.visible !== undefined ? modelData.visible : true

                width: entryRow.implicitWidth + Theme.spacingMedium
                height: Math.max(Theme.scaled(32), entryRow.implicitHeight + Theme.scaled(12))
                radius: Theme.scaled(4)
                color: "transparent"
                opacity: entryVisible ? 1.0 : 0.4

                Accessible.role: Accessible.CheckBox
                Accessible.name: entryName
                Accessible.checked: entryVisible
                Accessible.focusable: true
                Accessible.onPressAction: _toggle()

                function _toggle() {
                    if (!legendRoot.toggleEnabled) return
                    legendRoot.entryToggled(entryDelegate.index, !entryDelegate.entryVisible)
                }

                Row {
                    id: entryRow
                    anchors.centerIn: parent
                    spacing: Theme.scaled(6)

                    Rectangle {
                        width: Theme.scaled(10)
                        height: Theme.scaled(10)
                        radius: Theme.scaled(1)
                        color: entryDelegate.entryColor
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: entryDelegate.entryName
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.ignored: true
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: legendRoot.toggleEnabled
                    onClicked: entryDelegate._toggle()
                }
            }
        }
    }
}
