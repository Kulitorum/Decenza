pragma Singleton
import QtQuick

QtObject {
    // Colors
    readonly property color backgroundColor: "#1a1a2e"
    readonly property color surfaceColor: "#252538"
    readonly property color primaryColor: "#4e85f4"
    readonly property color secondaryColor: "#c0c5e3"
    readonly property color textColor: "#ffffff"
    readonly property color textSecondaryColor: "#7f879a"
    readonly property color accentColor: "#e94560"
    readonly property color successColor: "#00ff88"
    readonly property color warningColor: "#ffaa00"
    readonly property color errorColor: "#ff4444"

    // Gauge colors
    readonly property color pressureColor: "#4eb4f4"
    readonly property color flowColor: "#88dd88"
    readonly property color temperatureColor: "#f4a04e"
    readonly property color weightColor: "#dd88dd"

    // Fonts
    readonly property font headingFont: Qt.font({ pixelSize: 32, bold: true })
    readonly property font bodyFont: Qt.font({ pixelSize: 18 })
    readonly property font labelFont: Qt.font({ pixelSize: 14 })
    readonly property font valueFont: Qt.font({ pixelSize: 48, bold: true })
    readonly property font timerFont: Qt.font({ pixelSize: 72, bold: true })

    // Dimensions
    readonly property int buttonRadius: 12
    readonly property int cardRadius: 16
    readonly property int standardMargin: 16
    readonly property int smallMargin: 8
    readonly property int graphLineWidth: 3

    // Shadows
    readonly property color shadowColor: "#40000000"

    // Button states
    readonly property color buttonDefault: primaryColor
    readonly property color buttonHover: Qt.lighter(primaryColor, 1.1)
    readonly property color buttonPressed: Qt.darker(primaryColor, 1.1)
    readonly property color buttonDisabled: "#555555"
}
