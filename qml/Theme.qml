pragma Singleton
import QtQuick

QtObject {
    // Reference design size (based on Android tablet in dp)
    readonly property real refWidth: 960
    readonly property real refHeight: 600

    // Scale factor - set from main.qml based on window size
    property real scale: 1.0

    // Actual window dimensions - set from main.qml for responsive sizing
    property real windowWidth: 960
    property real windowHeight: 600

    // Debug: scale multiplier (1.0 = auto, <1 = smaller, >1 = larger)
    property real scaleMultiplier: 1.0

    // Per-page scale multiplier (set by main.qml based on current page)
    property real pageScaleMultiplier: 1.0

    // Per-page scale configuration mode (set by main.qml)
    property bool configurePageScaleEnabled: false
    property string currentPageObjectName: ""

    // Convert emoji character to pre-rendered SVG image path.
    // Passes through qrc:/icons/... paths unchanged.
    function emojiToImage(emoji) {
        if (!emoji) return ""
        if (emoji.indexOf("qrc:") === 0) return emoji
        var cps = []
        for (var i = 0; i < emoji.length; ) {
            var cp = emoji.codePointAt(i)
            i += cp > 0xFFFF ? 2 : 1
            if (cp !== 0xFE0F) cps.push(cp.toString(16))
        }
        return "qrc:/emoji/" + cps.join("-") + ".svg"
    }

    // Helper function to scale values
    function scaled(value) { return Math.round(value * scale) }

    // Scale without page multiplier (for UI that should stay constant size across pages)
    function scaledBase(value) {
        return Math.round(value * scale / (pageScaleMultiplier || 1.0))
    }

    // Flash helper: returns red/black flash color when a color is being identified,
    // otherwise returns the normal color value. Called from web theme editor.
    // QML's binding engine tracks Settings.flashColorName and Settings.flashPhase
    // reads inside this function, so all color bindings re-evaluate on flash changes.
    function _c(name, value) {
        if (Settings.flashColorName === name && Settings.flashPhase > 0) {
            return Settings.flashPhase % 2 === 1 ? "#ff0000" : "#000000"
        }
        return value
    }

    // Dynamic colors - bind to Settings with fallback defaults
    // Wrapped in _c() for flash-to-identify from web theme editor
    property color backgroundColor: _c("backgroundColor", Settings.customThemeColors.backgroundColor || "#1a1a2e")
    property color surfaceColor: _c("surfaceColor", Settings.customThemeColors.surfaceColor || "#303048")
    property color primaryColor: _c("primaryColor", Settings.customThemeColors.primaryColor || "#4e85f4")
    property color secondaryColor: _c("secondaryColor", Settings.customThemeColors.secondaryColor || "#c0c5e3")
    property color textColor: _c("textColor", Settings.customThemeColors.textColor || "#ffffff")
    property color textSecondaryColor: _c("textSecondaryColor", Settings.customThemeColors.textSecondaryColor || "#a0a8b8")
    property color accentColor: _c("accentColor", Settings.customThemeColors.accentColor || "#e94560")
    property color successColor: _c("successColor", Settings.customThemeColors.successColor || "#00cc6d")
    property color warningColor: _c("warningColor", Settings.customThemeColors.warningColor || "#ffaa00")
    property color highlightColor: _c("highlightColor", Settings.customThemeColors.highlightColor || "#ffaa00")
    property color errorColor: _c("errorColor", Settings.customThemeColors.errorColor || "#ff4444")
    property color borderColor: _c("borderColor", Settings.customThemeColors.borderColor || "#3a3a4e")

    // Chart line colors
    property color pressureColor: _c("pressureColor", Settings.customThemeColors.pressureColor || "#18c37e")
    property color pressureGoalColor: _c("pressureGoalColor", Settings.customThemeColors.pressureGoalColor || "#69fdb3")
    property color flowColor: _c("flowColor", Settings.customThemeColors.flowColor || "#4e85f4")
    property color flowGoalColor: _c("flowGoalColor", Settings.customThemeColors.flowGoalColor || "#7aaaff")
    property color temperatureColor: _c("temperatureColor", Settings.customThemeColors.temperatureColor || "#e73249")
    property color temperatureGoalColor: _c("temperatureGoalColor", Settings.customThemeColors.temperatureGoalColor || "#ffa5a6")
    property color weightColor: _c("weightColor", Settings.customThemeColors.weightColor || "#a2693d")
    property color weightFlowColor: _c("weightFlowColor", Settings.customThemeColors.weightFlowColor || "#d4a574")
    property color resistanceColor: _c("resistanceColor", Settings.customThemeColors.resistanceColor || "#eae83d")

    // DYE measurement colors (Shot Info page)
    property color dyeDoseColor: _c("dyeDoseColor", Settings.customThemeColors.dyeDoseColor || "#6F4E37")
    property color dyeOutputColor: _c("dyeOutputColor", Settings.customThemeColors.dyeOutputColor || "#9C27B0")
    property color dyeTdsColor: _c("dyeTdsColor", Settings.customThemeColors.dyeTdsColor || "#FF9800")
    property color dyeEyColor: _c("dyeEyColor", Settings.customThemeColors.dyeEyColor || "#a2693d")

    // Scaled fonts (sizes customizable via Settings.customFontSizes)
    readonly property font headingFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.headingSize || 32), bold: true })
    readonly property font titleFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.titleSize || 24), bold: true })
    readonly property font subtitleFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.subtitleSize || 18), bold: true })
    readonly property font bodyFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.bodySize || 18) })
    readonly property font labelFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.labelSize || 14) })
    readonly property font captionFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.captionSize || 12) })
    readonly property font valueFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.valueSize || 48), bold: true })
    readonly property font timerFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.timerSize || 72), bold: true })

    // Scaled dimensions
    readonly property int buttonRadius: scaled(12)
    readonly property int cardRadius: scaled(16)
    readonly property int standardMargin: scaled(16)
    readonly property int smallMargin: scaled(8)
    readonly property int graphLineWidth: Math.max(1, scaled(1))

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

    // Dialogs — responsive: 40% of window width
    readonly property int dialogWidth: Math.max(scaled(280), windowWidth * 0.4)
    readonly property int dialogPadding: scaled(24)

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
    property color buttonDisabled: _c("buttonDisabled", Settings.customThemeColors.buttonDisabled || "#555555")

    // UI indicator colors
    property color stopMarkerColor: _c("stopMarkerColor", Settings.customThemeColors.stopMarkerColor || "#FF6B6B")
    property color frameMarkerColor: _c("frameMarkerColor", Settings.customThemeColors.frameMarkerColor || "#66ffffff")
    property color modifiedIndicatorColor: _c("modifiedIndicatorColor", Settings.customThemeColors.modifiedIndicatorColor || "#FFCC00")
    property color simulationIndicatorColor: _c("simulationIndicatorColor", Settings.customThemeColors.simulationIndicatorColor || "#E65100")
    property color warningButtonColor: _c("warningButtonColor", Settings.customThemeColors.warningButtonColor || "#FFA500")
    property color successButtonColor: _c("successButtonColor", Settings.customThemeColors.successButtonColor || "#2E7D32")

    // List/table colors
    property color rowAlternateColor: _c("rowAlternateColor", Settings.customThemeColors.rowAlternateColor || "#1a1a1a")
    property color rowAlternateLightColor: _c("rowAlternateLightColor", Settings.customThemeColors.rowAlternateLightColor || "#222222")

    // Source badge colors (profile/shot import pages)
    property color sourceBadgeBlueColor: _c("sourceBadgeBlueColor", Settings.customThemeColors.sourceBadgeBlueColor || "#4a90d9")
    property color sourceBadgeGreenColor: _c("sourceBadgeGreenColor", Settings.customThemeColors.sourceBadgeGreenColor || "#4ad94a")
    property color sourceBadgeOrangeColor: _c("sourceBadgeOrangeColor", Settings.customThemeColors.sourceBadgeOrangeColor || "#d9a04a")

    // Focus indicator styles
    readonly property color focusColor: primaryColor
    readonly property int focusBorderWidth: 3
    readonly property int focusMargin: 2
}
