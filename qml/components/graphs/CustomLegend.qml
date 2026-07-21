// CustomLegend — the shared graph-legend component for every Decenza graph.
//
// Qt Graphs has no built-in legend (unlike Qt Charts' `ChartView.legend`).
// This is a themed, WRAPPING, tap-to-toggle legend styled via `Theme.qml`.
// It is the single legend renderer used by the shot graph (via `GraphLegend`),
// the steam graph, the flow-calibration graph, and the profile-editor graph —
// so wrapping/centering/hit-target/tooltip behavior lives in exactly one place.
//
// Pass a model of entry objects. Recognized fields:
//   label  (string) — the entry text                                (required)
//   color  (color)  — the swatch color                              (required)
//   active (bool)   — whether the series is visible (drives opacity) (default true)
//   tip    (string) — optional long-press / hover tooltip text      (optional)
//
// `label` (not `name`) and `active` (not `visible`) are used because `name`
// and `visible` collide with QML's built-in properties when carried in a JS
// model-data object (see docs/CLAUDE_MD/QML_GOTCHAS.md).
//
// Component-level options:
//   toggleEnabled (bool)   — whether tapping an entry emits entryToggled (default true)
//   swatchStyle   (string) — "dot" (default) or "line" (for goal curves)
//
// Layout: entries stay on one centered line when they fit the width the caller
// gives the component, and WRAP onto additional lines when they don't — no entry
// is ever clipped off an edge. The caller MUST give the component a bounded width
// (Layout.fillWidth, or an explicit width) for wrapping to engage.
//
// Usage with a static, non-toggling model (e.g. profile editor):
//   CustomLegend {
//       toggleEnabled: false
//       swatchStyle: "line"
//       entries: [
//           { label: "Pressure", color: Theme.pressureGoalColor },
//           { label: "Flow",     color: Theme.flowGoalColor }
//       ]
//   }
//
// Usage with toggling + persistence (consumer applies the new state to the series):
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
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

Item {
    id: legendRoot

    // Array of { label: string, color: color, active: bool, tip?: string } objects.
    property var entries: []

    // Whether tapping an entry should toggle its visibility.
    property bool toggleEnabled: true

    // "dot" (default round swatch) or "line" (thin horizontal bar, for goal curves).
    property string swatchStyle: "dot"

    // Emitted when the user taps an entry; consumer applies the new state to the series.
    signal entryToggled(int index, bool nowActive)

    // Swatch dimensions per style.
    readonly property real _swatchWidth: Theme.scaled(swatchStyle === "line" ? 16 : 10)
    readonly property real _swatchHeight: Theme.scaled(swatchStyle === "line" ? 3 : 10)

    // Unwrapped single-line content width, computed loop-free from the entry model
    // (NOT from the now-width-bounded Flow.implicitWidth, which would form a
    // width -> implicitWidth -> width binding loop). Must mirror the delegate's
    // own width formula: entryRow.implicitWidth + Theme.spacingMedium, where
    // entryRow.implicitWidth = swatch + Theme.scaled(6) + advanceWidth(label).
    readonly property real _unwrappedContentWidth: {
        var n = legendRoot.entries.length
        if (n === 0)
            return 0
        var total = 0
        for (var i = 0; i < n; i++) {
            var e = legendRoot.entries[i]
            var label = (e && e.label !== undefined) ? e.label : ""
            var entryRowW = legendRoot._swatchWidth + Theme.scaled(6) + legendMetrics.advanceWidth(label)
            total += entryRowW + Theme.spacingMedium
        }
        total += Theme.spacingSmall * (n - 1)
        return total
    }

    implicitWidth: _unwrappedContentWidth
    implicitHeight: legendFlow.implicitHeight
    Layout.fillWidth: true

    FontMetrics {
        id: legendMetrics
        font: Theme.captionFont
    }

    Flow {
        id: legendFlow
        anchors.horizontalCenter: parent.horizontalCenter
        // Bounded width is what makes Flow wrap. Fit-to-content (centered) when it
        // fits; fill the available width (and wrap) when it doesn't. Guard the
        // pre-layout width==0 transient by falling back to the content width.
        width: legendRoot.width > 0
               ? Math.min(legendRoot.width, legendRoot._unwrappedContentWidth)
               : legendRoot._unwrappedContentWidth
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
                readonly property string entryTip: modelData && modelData.tip !== undefined ? modelData.tip : ""

                property bool longPressShowing: false

                width: entryRow.implicitWidth + Theme.spacingMedium
                // Touch-friendly min height only matters for tappable entries;
                // a display-only legend (toggleEnabled: false) stays compact.
                height: legendRoot.toggleEnabled
                        ? Math.max(Theme.scaled(44), entryRow.implicitHeight + Theme.scaled(12))
                        : entryRow.implicitHeight + Theme.scaled(6)
                radius: Theme.scaled(4)
                color: "transparent"
                opacity: entryActive ? 1.0 : 0.4

                Accessible.role: Accessible.CheckBox
                Accessible.name: entryLabel
                Accessible.checked: entryActive
                Accessible.focusable: true
                Accessible.description: entryTip !== "" ? TranslationManager.translate("graph.tip.longPressHint", "Long-press to view description.") : ""
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
                        width: legendRoot._swatchWidth
                        height: legendRoot._swatchHeight
                        radius: legendRoot.swatchStyle === "line" ? Theme.scaled(1) : width / 2
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
                    id: entryArea
                    anchors.fill: parent
                    enabled: legendRoot.toggleEnabled || entryDelegate.entryTip !== ""
                    hoverEnabled: entryDelegate.entryTip !== ""
                    preventStealing: true
                    onClicked: entryDelegate._toggle()
                    onPressAndHold: {
                        if (entryDelegate.entryTip === "") return
                        entryDelegate.longPressShowing = true
                        longPressHideTimer.restart()
                    }
                }

                Timer {
                    id: longPressHideTimer
                    interval: 4000
                    onTriggered: entryDelegate.longPressShowing = false
                }

                ToolTip {
                    id: entryTipPopup
                    text: entryDelegate.entryTip
                    visible: entryDelegate.entryTip !== ""
                             && ((entryArea.containsMouse && entryArea.pressedButtons === 0) || entryDelegate.longPressShowing)
                    delay: entryDelegate.longPressShowing ? 0 : 500
                    width: Math.min(Theme.scaled(280), Theme.windowWidth * 0.7)

                    contentItem: Text {
                        text: entryTipPopup.text
                        font: Theme.captionFont
                        color: Theme.textColor
                        wrapMode: Text.Wrap
                    }

                    background: Rectangle {
                        color: Theme.surfaceColor
                        border.color: Theme.borderColor
                        border.width: Theme.scaled(1)
                        radius: Theme.cardRadius
                    }
                }
            }
        }
    }
}
