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

    // Suppress the sub-feature on axis spines — Qt Graphs draws long tick
    // protrusions between the spine and the labels by default; Charts had
    // either none or much shorter ones.
    axisX.subWidth: 0
    axisY.subWidth: 0

    // Labels and titles.
    labelTextColor: Theme.textSecondaryColor
    axisXLabelFont: Theme.captionFont
    axisYLabelFont: Theme.captionFont
}
