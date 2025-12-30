pragma Singleton
import QtQuick

QtObject {
    // Reference design size (based on Android tablet in dp)
    readonly property real refWidth: 960
    readonly property real refHeight: 600

    // Scale factor - set from main.qml based on window size
    property real scale: 1.0

    // Helper function to scale values
    function scaled(value) { return Math.round(value * scale) }

    // Dynamic colors - bind to Settings with fallback defaults
    property color backgroundColor: Settings.customThemeColors.backgroundColor || "#1a1a2e"
    property color surfaceColor: Settings.customThemeColors.surfaceColor || "#252538"
    property color primaryColor: Settings.customThemeColors.primaryColor || "#4e85f4"
    property color secondaryColor: Settings.customThemeColors.secondaryColor || "#c0c5e3"
    property color textColor: Settings.customThemeColors.textColor || "#ffffff"
    property color textSecondaryColor: Settings.customThemeColors.textSecondaryColor || "#a0a8b8"
    property color accentColor: Settings.customThemeColors.accentColor || "#e94560"
    property color successColor: Settings.customThemeColors.successColor || "#00cc6d"
    property color warningColor: Settings.customThemeColors.warningColor || "#ffaa00"
    property color errorColor: Settings.customThemeColors.errorColor || "#ff4444"
    property color borderColor: Settings.customThemeColors.borderColor || "#3a3a4e"

    // Chart line colors
    property color pressureColor: Settings.customThemeColors.pressureColor || "#18c37e"
    property color pressureGoalColor: Settings.customThemeColors.pressureGoalColor || "#69fdb3"
    property color flowColor: Settings.customThemeColors.flowColor || "#4e85f4"
    property color flowGoalColor: Settings.customThemeColors.flowGoalColor || "#7aaaff"
    property color temperatureColor: Settings.customThemeColors.temperatureColor || "#e73249"
    property color temperatureGoalColor: Settings.customThemeColors.temperatureGoalColor || "#ffa5a6"
    property color weightColor: Settings.customThemeColors.weightColor || "#a2693d"

    // DYE measurement colors (Shot Info page)
    property color dyeDoseColor: Settings.customThemeColors.dyeDoseColor || "#6F4E37"      // Coffee brown
    property color dyeOutputColor: Settings.customThemeColors.dyeOutputColor || "#9C27B0"  // Purple
    property color dyeTdsColor: Settings.customThemeColors.dyeTdsColor || "#FF9800"        // Orange
    property color dyeEyColor: Settings.customThemeColors.dyeEyColor || "#a2693d"          // Brown (same as weight)

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

    // Layout constants
    readonly property int statusBarHeight: scaled(70)
    readonly property int bottomBarHeight: scaled(70)
    readonly property int pageTopMargin: scaled(80)

    // Touch targets (44dp minimum per Apple/Google guidelines)
    readonly property int touchTargetMin: scaled(44)
    readonly property int touchTargetMedium: scaled(48)
    readonly property int touchTargetLarge: scaled(56)

    // Spacing
    readonly property int spacingSmall: scaled(8)
    readonly property int spacingMedium: scaled(16)
    readonly property int spacingLarge: scaled(24)

    // Dialogs
    readonly property int dialogWidth: scaled(380)

    // Settings columns
    readonly property int settingsColumnMin: scaled(280)
    readonly property int settingsColumnMax: scaled(400)

    // Gauges
    readonly property int gaugeSize: scaled(120)

    // Charts
    readonly property int chartMarginSmall: scaled(10)
    readonly property int chartMarginLarge: scaled(40)
    readonly property int chartFontSize: scaled(14)

    // Shadows
    readonly property color shadowColor: "#40000000"

    // Button states
    readonly property color buttonDefault: primaryColor
    readonly property color buttonHover: Qt.lighter(primaryColor, 1.1)
    readonly property color buttonPressed: Qt.darker(primaryColor, 1.1)
    readonly property color buttonDisabled: "#555555"

    // Focus indicator styles
    readonly property color focusColor: primaryColor
    readonly property int focusBorderWidth: 3
    readonly property int focusMargin: 2
}
