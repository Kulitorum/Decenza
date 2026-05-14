// DecenzaGraphsTheme — Qt Graphs theme bound to Decenza's Theme.qml.
//
// Qt Graphs routes per-axis colors, plot-area background, and grid styling
// through `GraphsTheme` rather than the per-axis properties Qt Charts used
// (`labelsColor`, `gridLineColor`, `titleBrush`, etc.). This component
// centralises those values so every migrated graph shares the same look.
//
// Usage:
//   GraphsView {
//       theme: DecenzaGraphsTheme {}
//       ...
//   }
//
// Replaces: Qt Charts' per-axis color properties and ChartView.plotAreaColor.

import QtQuick
import QtGraphs
import Decenza

GraphsTheme {
    colorScheme: GraphsTheme.ColorScheme.Dark

    // Outer GraphsView background — let the page background show through.
    backgroundColor: "transparent"

    // Plot area background — visibly darker than the page so the chart reads
    // as a contained card the way Qt Charts' plotAreaColor did.
    plotAreaBackgroundColor: Qt.darker(Theme.surfaceColor, 1.6)

    // Grid lines — visible but low-contrast over the plot area.
    grid.mainColor: Qt.rgba(1, 1, 1, 0.35)
    grid.subColor: Qt.rgba(1, 1, 1, 0.18)
    grid.mainWidth: 1
    grid.subWidth: 1

    // Axis spines — clearly visible solid lines along the left and bottom,
    // and noticeably more prominent than the grid (grid alpha ≈ 0.35).
    axisX.mainColor: Qt.rgba(1, 1, 1, 0.85)
    axisY.mainColor: Qt.rgba(1, 1, 1, 0.85)

    // Thin axis spines + suppress the sub-feature. Qt 6.11 GraphsTheme has no
    // tickLength property; reducing mainWidth and zeroing subWidth is the
    // closest lever to dampen the long tick protrusions Qt Graphs draws by
    // default between the spine and the labels.
    axisX.mainWidth: 1
    axisY.mainWidth: 1
    axisX.subWidth: 0
    axisY.subWidth: 0

    // Labels and titles.
    labelTextColor: Theme.textSecondaryColor
    axisXLabelFont: Theme.captionFont
    axisYLabelFont: Theme.captionFont
}
