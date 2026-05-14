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
    backgroundVisible: false
    backgroundColor: "transparent"

    // Plot area background — matches the slightly-darker card style of Charts.
    plotAreaBackgroundVisible: true
    plotAreaBackgroundColor: Qt.darker(Theme.surfaceColor, 1.3)

    // Grid lines — subtle, low-contrast over the plot area.
    gridVisible: true
    grid.mainColor: Qt.rgba(1, 1, 1, 0.1)
    grid.subColor: Qt.rgba(1, 1, 1, 0.04)
    grid.mainWidth: 1
    grid.subWidth: 1

    // Axis lines.
    axisX.mainColor: Qt.rgba(1, 1, 1, 0.2)
    axisX.subColor: Qt.rgba(1, 1, 1, 0.1)
    axisY.mainColor: Qt.rgba(1, 1, 1, 0.2)
    axisY.subColor: Qt.rgba(1, 1, 1, 0.1)

    // Labels and titles — secondary text colour from the app theme.
    labelTextColor: Theme.textSecondaryColor
    axisXLabelFont: Theme.captionFont
    axisYLabelFont: Theme.captionFont
}
