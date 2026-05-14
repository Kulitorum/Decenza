// CustomLegend — Qt Graphs bridge component.
//
// Qt Graphs has no built-in legend (unlike Qt Charts' `ChartView.legend`).
// This is a themed horizontal legend with tap-to-toggle visibility, styled
// via `Theme.qml`. Pass a model of `{ label, color, active }` entries.
//
// `label` (not `name`) and `active` (not `visible`) are used because `name`
// and `visible` collide with QML's built-in properties when carried in a JS
// model-data object (see docs/CLAUDE_MD/QML_GOTCHAS.md).
//
// Usage with a static model:
//   CustomLegend {
//       entries: [
//           { label: "Flow",   color: Theme.flowColor,   active: flowSeries.visible },
//           { label: "Weight", color: Theme.weightColor, active: weightSeries.visible }
//       ]
//       onEntryToggled: (index, nowActive) => {
//           if (index === 0) flowSeries.visible = nowActive
//           else             weightSeries.visible = nowActive
//       }
//   }
//
// Replaces: Qt Charts `ChartView.legend`.

import QtQuick
import QtQuick.Layouts
import Decenza

Item {
    id: legendRoot

    // Array of { label: string, color: color, active: bool } objects.
    property var entries: []

    // Whether tapping an entry should toggle its visibility.
    property bool toggleEnabled: true

    // Emitted when the user taps an entry; consumer applies the new state to the series.
    signal entryToggled(int index, bool nowActive)

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

                readonly property string entryLabel: modelData && modelData.label !== undefined ? modelData.label : ""
                readonly property color entryColor: modelData && modelData.color !== undefined ? modelData.color : Theme.textColor
                readonly property bool entryActive: modelData && modelData.active !== undefined ? modelData.active : true

                width: entryRow.implicitWidth + Theme.spacingMedium
                height: Math.max(Theme.scaled(32), entryRow.implicitHeight + Theme.scaled(12))
                radius: Theme.scaled(4)
                color: "transparent"
                opacity: entryActive ? 1.0 : 0.4

                Accessible.role: Accessible.CheckBox
                Accessible.name: entryLabel
                Accessible.checked: entryActive
                Accessible.focusable: true
                Accessible.onPressAction: _toggle()

                function _toggle() {
                    if (!legendRoot.toggleEnabled) return
                    legendRoot.entryToggled(entryDelegate.index, !entryDelegate.entryActive)
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
                        text: entryDelegate.entryLabel
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
