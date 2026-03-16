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

    // Check if a Unicode code point is an emoji that would trigger Apple Color Emoji
    // font rendering (sbix PNG decoding on macOS, CBDT/CBLC on Android).
    // Returns true for characters that need to be rendered as images, not text glyphs.
    function _isEmoji(cp) {
        // Emoticons
        if (cp >= 0x1F600 && cp <= 0x1F64F) return true
        // Misc Symbols & Pictographs
        if (cp >= 0x1F300 && cp <= 0x1F5FF) return true
        // Transport & Map Symbols
        if (cp >= 0x1F680 && cp <= 0x1F6FF) return true
        // Supplemental Symbols & Pictographs
        if (cp >= 0x1F900 && cp <= 0x1F9FF) return true
        // Symbols & Pictographs Extended-A
        if (cp >= 0x1FA00 && cp <= 0x1FA6F) return true
        // Symbols & Pictographs Extended-B
        if (cp >= 0x1FA70 && cp <= 0x1FAFF) return true
        // Dingbats (✂✈✉✌ etc. — many rendered as color emoji by macOS)
        if (cp >= 0x2702 && cp <= 0x27B0) return true
        // Misc Symbols (☀☁☂☃☄★☎☮☯ etc.)
        if (cp >= 0x2600 && cp <= 0x26FF) return true
        // CJK/enclosed ideographic supplement (🈁🈚🉐 etc.)
        if (cp >= 0x1F200 && cp <= 0x1F2FF) return true
        // Enclosed alphanumeric supplement (Ⓜ🅰🅱 etc.)
        if (cp >= 0x1F100 && cp <= 0x1F1FF) return true
        // Skin tone modifiers (not standalone but may appear)
        if (cp >= 0x1F3FB && cp <= 0x1F3FF) return true
        // Variation selector 16 (emoji presentation) — skip, handled separately
        // ZWJ (U+200D) — skip, only a joiner
        return false
    }

    // Replace emoji Unicode characters in a string with RichText <img> tags
    // pointing to pre-rendered SVGs. This prevents CoreText/ImageIO crashes
    // caused by Apple Color Emoji font PNG decoding on the render thread.
    // Only use on strings bound to Text elements with textFormat: Text.RichText.
    function replaceEmojiWithImg(text, pixelSize) {
        if (!text) return ""
        var size = pixelSize || 16
        var result = ""
        var i = 0
        while (i < text.length) {
            var cp = text.codePointAt(i)
            var charLen = cp > 0xFFFF ? 2 : 1
            if (_isEmoji(cp)) {
                // Collect full emoji sequence (multi-codepoint with ZWJ, modifiers)
                var emojiCps = [cp]
                var j = i + charLen
                while (j < text.length) {
                    var next = text.codePointAt(j)
                    var nextLen = next > 0xFFFF ? 2 : 1
                    if (next === 0xFE0F) {
                        // Variation selector 16 — skip (emojiToImage strips it)
                        j += nextLen
                        continue
                    }
                    if (next === 0x200D) {
                        // ZWJ — consume it only if followed by an emoji
                        var zjPos = j + nextLen
                        if (zjPos < text.length) {
                            var after = text.codePointAt(zjPos)
                            if (_isEmoji(after)) {
                                j = zjPos
                                emojiCps.push(after)
                                j += after > 0xFFFF ? 2 : 1
                                continue
                            }
                        }
                        break  // ZWJ not followed by emoji — end sequence
                    }
                    if (_isEmoji(next) || (next >= 0x1F3FB && next <= 0x1F3FF)) {
                        // Skin tone modifier or continuation
                        emojiCps.push(next)
                        j += nextLen
                        continue
                    }
                    break
                }
                var src = "qrc:/emoji/" + emojiCps.map(function(c) { return c.toString(16) }).join("-") + ".svg"
                result += "<img src=\"" + src + "\" width=\"" + size + "\" height=\"" + size
                    + "\" style=\"vertical-align: middle\">"
                i = j
            } else if (cp === 0xFE0F) {
                // Stray variation selector — skip
                i += charLen
            } else {
                result += text.substring(i, i + charLen)
                i += charLen
            }
        }
        return result
    }

    // Strip emoji Unicode characters from a string entirely.
    // Use for plain-text Text elements where <img> tags aren't supported.
    function stripEmoji(text) {
        if (!text) return ""
        var result = ""
        var i = 0
        while (i < text.length) {
            var cp = text.codePointAt(i)
            var charLen = cp > 0xFFFF ? 2 : 1
            if (!_isEmoji(cp) && cp !== 0xFE0F && cp !== 0x200D) {
                result += text.substring(i, i + charLen)
            }
            i += charLen
        }
        return result
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
    property color primaryContrastColor: _c("primaryContrastColor", Settings.customThemeColors.primaryContrastColor || "#ffffff")
    property color iconColor: _c("iconColor", Settings.customThemeColors.iconColor || "#ffffff")
    property color bottomBarColor: _c("bottomBarColor", Settings.customThemeColors.bottomBarColor || "#4e85f4")
    property color actionButtonContentColor: _c("actionButtonContentColor", Settings.customThemeColors.actionButtonContentColor || "#ffffff")

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
    property color waterLevelColor: _c("waterLevelColor", Settings.customThemeColors.waterLevelColor || "#4e85f4")

    // Tracking status colors (profile goal vs actual)
    property color trackOnTargetColor: _c("trackOnTargetColor", Settings.customThemeColors.trackOnTargetColor || "#00cc6d")
    property color trackDriftingColor: _c("trackDriftingColor", Settings.customThemeColors.trackDriftingColor || "#f0ad4e")
    property color trackOffTargetColor: _c("trackOffTargetColor", Settings.customThemeColors.trackOffTargetColor || "#e94560")

    // Shared tracking color logic: proportional thresholds with floor values
    // so low goals (e.g. 0.5 mL/s flow) don't trigger red on tiny deltas.
    // isPressure: true for pressure tracking, false for flow tracking.
    function trackingColor(delta, goal, isPressure) {
        var floorGood = isPressure ? 0.8 : 0.4
        var floorWarn = isPressure ? 1.8 : 0.8
        var threshGood = Math.max(floorGood, goal * 0.25)
        var threshWarn = Math.max(floorWarn, goal * 0.50)
        if (delta < threshGood) return trackOnTargetColor
        if (delta < threshWarn) return trackDriftingColor
        return trackOffTargetColor
    }

    // Translucent, pastel-tinted overlay text color derived from a tracking color.
    // Lightens toward white for readability over dark backgrounds.
    function tintedOverlayColor(baseColor, alpha) {
        return Qt.rgba(0.7 + baseColor.r * 0.3, 0.7 + baseColor.g * 0.3, 0.7 + baseColor.b * 0.3, alpha)
    }

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
