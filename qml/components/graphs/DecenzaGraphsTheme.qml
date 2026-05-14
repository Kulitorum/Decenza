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
    grid.mainColor: Qt.rgba(1, 1, 1, 0.18)
    grid.subColor: Qt.rgba(1, 1, 1, 0.08)
    grid.mainWidth: 1
    grid.subWidth: 1

    // Axis spines — a touch brighter than grid lines.
    axisX.mainColor: Qt.rgba(1, 1, 1, 0.28)
    axisY.mainColor: Qt.rgba(1, 1, 1, 0.28)

    // Labels and titles.
    labelTextColor: Theme.textSecondaryColor
    axisXLabelFont: Theme.captionFont
    axisYLabelFont: Theme.captionFont
}
