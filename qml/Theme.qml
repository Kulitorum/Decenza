pragma Singleton
import QtQuick

QtObject {
    // Reference design size
    readonly property real refWidth: 1280
    readonly property real refHeight: 800

    // Scale factor - set from main.qml based on window size
    property real scale: 1.0

    // Helper function to scale values
    function scaled(value) { return Math.round(value * scale) }

    // Colors
    readonly property color backgroundColor: "#1a1a2e"
    readonly property color surfaceColor: "#252538"
    readonly property color primaryColor: "#4e85f4"
    readonly property color secondaryColor: "#c0c5e3"
    readonly property color textColor: "#ffffff"
    readonly property color textSecondaryColor: "#a0a8b8"
    readonly property color accentColor: "#e94560"
    readonly property color successColor: "#00ff88"
    readonly property color warningColor: "#ffaa00"
    readonly property color errorColor: "#ff4444"
    readonly property color borderColor: "#3a3a4e"

    // Chart line colors (from DE1app dark theme)
    readonly property color pressureColor: "#18c37e"       // Green - actual pressure
    readonly property color pressureGoalColor: "#69fdb3"   // Light green - pressure goal
    readonly property color flowColor: "#4e85f4"           // Blue - actual flow
    readonly property color flowGoalColor: "#7aaaff"       // Light blue - flow goal
    readonly property color temperatureColor: "#e73249"    // Red - actual temperature
    readonly property color temperatureGoalColor: "#ffa5a6" // Light red - temp goal
    readonly property color weightColor: "#a2693d"         // Brown - weight

    // Scaled fonts
    readonly property font headingFont: Qt.font({ pixelSize: scaled(32), bold: true })
    readonly property font titleFont: Qt.font({ pixelSize: scaled(24), bold: true })
    readonly property font subtitleFont: Qt.font({ pixelSize: scaled(18), bold: true })
    readonly property font bodyFont: Qt.font({ pixelSize: scaled(18) })
    readonly property font labelFont: Qt.font({ pixelSize: scaled(14) })
    readonly property font captionFont: Qt.font({ pixelSize: scaled(12) })
    readonly property font valueFont: Qt.font({ pixelSize: scaled(48), bold: true })
    readonly property font timerFont: Qt.font({ pixelSize: scaled(72), bold: true })

    // Scaled dimensions
    readonly property int buttonRadius: scaled(12)
    readonly property int cardRadius: scaled(16)
    readonly property int standardMargin: scaled(16)
    readonly property int smallMargin: scaled(8)
    readonly property int graphLineWidth: Math.max(2, scaled(3))

    // Shadows
    readonly property color shadowColor: "#40000000"

    // Button states
    readonly property color buttonDefault: primaryColor
    readonly property color buttonHover: Qt.lighter(primaryColor, 1.1)
    readonly property color buttonPressed: Qt.darker(primaryColor, 1.1)
    readonly property color buttonDisabled: "#555555"
}
