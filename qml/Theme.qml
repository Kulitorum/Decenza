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
    // The operation selected on the idle screen ("espresso"/"steam"/…; "" when the idle
    // page isn't active OR it is active but nothing is selected — don't read "" as
    // "not on the idle page"). Published by IdlePage._publishOperationMode(); read by the
    // page-aware Plan widget. Lives here beside currentPageObjectName so page/mode state
    // has ONE reactive home — don't add a second copy on the window root.
    property string currentOperationMode: ""

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
    // Bind to a Text element — prefer Text.StyledText (elide works), though the
    // output also renders under Text.RichText. The emoji <img> carries both
    // align="middle" (honored by StyledText) and style="vertical-align" (honored
    // by RichText) so it stays centered either way. See QML_GOTCHAS.md — RichText
    // silently disables elide.
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
                                emojiCps.push(0x200D)
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
                // align="middle" centres the emoji in Text.StyledText (which ignores
                // the CSS style= attribute); style="vertical-align" keeps it centred in
                // any label still using Text.RichText. Prefer StyledText — RichText
                // silently disables elide (see QML_GOTCHAS.md).
                result += "<img src=\"" + src + "\" width=\"" + size + "\" height=\"" + size
                    + "\" align=\"middle\" style=\"vertical-align: middle\">"
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

    // Strip HTML tags and emoji from a string for accessible names.
    // Use on text that has been through replaceEmojiWithImg() to get
    // a clean plain-text string for TalkBack/VoiceOver.
    function toAccessibleText(html) {
        if (!html) return ""
        return stripEmoji(html.replace(/<[^>]*>/g, "")).trim()
    }

    // Escape user-supplied text so it can be safely embedded in a StyledText/RichText
    // string (a bean or roaster name containing & < > would otherwise be mis-parsed).
    function escapeHtml(s) {
        return String(s)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
    }

    // 6-digit hex (#rrggbb) for StyledText/RichText <font color> spans. The
    // point is STRIPPING ALPHA (Qt parses #AARRGGBB fine): a user-customized
    // translucent theme color would otherwise render the span see-through.
    function colorToHex(c) {
        function h(x) { var s = Math.round(x * 255).toString(16); return s.length < 2 ? "0" + s : s }
        return "#" + h(c.r) + h(c.g) + h(c.b)
    }

    // The styled separator dot used between joined text fragments — slightly larger and
    // bolder than the surrounding text. StyledText HTML (relative sizing — no hardcoded
    // sizes); hosts must set textFormat: Text.StyledText. Shared by joinWithBullet and
    // the plan components (which can't use joinWithBullet — they bold per-value).
    readonly property string bulletSep: " <font size=\"+1\"><b>·</b></font> "

    // Join already-computed text parts with bulletSep, HTML-escaping each part.
    function joinWithBullet(parts) {
        var sep = bulletSep
        var out = []
        for (var i = 0; i < parts.length; i++) {
            var p = String(parts[i])
            if (p.length > 0) out.push(escapeHtml(p))
        }
        return out.join(sep)
    }

    // Truncate a UTF-16 string at `cap` code units and append an ellipsis,
    // backing off one unit if cap would split a surrogate pair so we
    // don't emit an orphaned high surrogate to the accessibility tree.
    function _truncateWithEllipsis(s, cap) {
        if (s.length <= cap) return s
        var c = s.charCodeAt(cap - 1)
        var cut = (c >= 0xD800 && c <= 0xDBFF) ? cap - 1 : cap
        return s.substring(0, cut) + "\u2026"
    }

    // Strip Markdown syntax so screen readers don't read formatting
    // characters (e.g. "star star bold star star" for **bold**). Also
    // caps length so large transcripts don't flood the accessibility tree.
    // Pass to Accessible.description on read-only TextAreas that render
    // Text.MarkdownText. Default cap: 2000 characters.
    // Note: the regex chain is a best-effort cleanup and does NOT handle
    // nested or malformed spans (e.g. ***bold-italic***, unclosed **bold) —
    // those pass through partially un-stripped, which is acceptable for an
    // accessibility hint.
    function stripMarkdown(text, maxLen) {
        if (!text) return ""
        var cap = maxLen ?? 2000
        var s = text
            .replace(/```[\s\S]*?```/g, " ")
            .replace(/`([^`]+)`/g, "$1")
            .replace(/!\[[^\]]*\]\([^)]*\)/g, "")
            .replace(/\[([^\]]+)\]\([^)]+\)/g, "$1")
            .replace(/\*\*([^*]+)\*\*/g, "$1")
            .replace(/__([^_]+)__/g, "$1")
            .replace(/\*([^*]+)\*/g, "$1")
            .replace(/_([^_]+)_/g, "$1")
            .replace(/~~([^~]+)~~/g, "$1")
            .replace(/^#{1,6}\s+/gm, "")
            .replace(/^\s*[-*+]\s+/gm, "")
            .replace(/^\s*\d+\.\s+/gm, "")
            .replace(/^\s*>\s?/gm, "")
            .replace(/^\s*-{3,}\s*$/gm, "")
            .replace(/\n{3,}/g, "\n\n")
            .trim()
        return _truncateWithEllipsis(s, cap)
    }

    // Cap plain text length for Accessible.description so very large
    // strings (log output, crash dumps) don't flood the accessibility
    // tree. Default cap: 2000 characters. For Markdown-formatted content,
    // use stripMarkdown instead so formatting chars aren't read literally.
    function capAccessibleText(text, maxLen) {
        if (!text) return ""
        var cap = maxLen ?? 2000
        return _truncateWithEllipsis(text, cap)
    }

    // --- Temperature unit (display/entry only; storage stays Celsius) ---
    // The conversion math lives in C++ (TemperatureDisplay bridge, unit-tested in
    // tst_temperaturedisplay). These wrappers read Settings.app.temperatureUnit in
    // JS — that read is what makes bindings re-evaluate on a unit toggle (property
    // reads inside C++ methods are invisible to the QML binding engine).
    function tempIsFahrenheit() { return Settings.app.temperatureUnit === "fahrenheit" }
    function tempUnitSuffix() { return TemperatureDisplay.unitSuffix(tempIsFahrenheit()) }
    function cToDisplay(celsius) { return TemperatureDisplay.cToDisplay(celsius, tempIsFahrenheit()) }
    function displayToC(value) { return TemperatureDisplay.displayToC(value, tempIsFahrenheit()) }
    // A temperature DELTA/offset scales only (no +32 origin shift): +4°C = +7.2°F.
    function cDeltaToDisplay(deltaCelsius) { return TemperatureDisplay.cDeltaToDisplay(deltaCelsius, tempIsFahrenheit()) }
    function displayToCDelta(deltaValue) { return TemperatureDisplay.displayToCDelta(deltaValue, tempIsFahrenheit()) }
    function formatTemperature(celsius, decimals) {
        var d = (decimals === undefined) ? 0 : decimals
        return cToDisplay(celsius).toFixed(d) + tempUnitSuffix()
    }

    // Helper function to scale values
    function scaled(value) { return Math.round(value * scale) }

    // Scale without page multiplier (for UI that should stay constant size across pages)
    function scaledBase(value) {
        return Math.round(value * scale / (pageScaleMultiplier || 1.0))
    }

    // Flash helper: returns red/black flash color when a color is being identified,
    // otherwise returns the normal color value. Called from web theme editor.
    // QML's binding engine tracks Settings.theme.flashColorName and Settings.theme.flashPhase
    // reads inside this function, so all color bindings re-evaluate on flash changes.
    function _c(name, value) {
        if (Settings.theme.flashColorName === name && Settings.theme.flashPhase > 0) {
            return Settings.theme.flashPhase % 2 === 1 ? "#ff0000" : "#000000"
        }
        return value
    }

    // Dark-vs-light mode of the active theme. Mirror of Settings.theme.isDarkMode:
    // the canonical spelling is Settings.theme.isDarkMode (CLAUDE.md settings rule),
    // but a bare `Theme.isDarkMode` read otherwise resolves to undefined — falsy —
    // and fails silently, which has already caused real bugs. The mirror makes
    // either spelling correct.
    readonly property bool isDarkMode: Settings.theme.isDarkMode

    // Dynamic colors - bind to Settings with fallback defaults
    // Wrapped in _c() for flash-to-identify from web theme editor
    property color backgroundColor: _c("backgroundColor", Settings.theme.customThemeColors.backgroundColor || "#1a1a2e")
    property color surfaceColor: _c("surfaceColor", Settings.theme.customThemeColors.surfaceColor || "#303048")

    // Single translucency level for every "scrim" used when a custom background
    // image is active (cards, bars, inset controls, action-button tiles) — a
    // tinted-glass wash, not a heavy dimmer. Started at 0.25 to match the AI
    // Provider tab's pre-existing "configured" chip look, but that left
    // secondary text (textSecondaryColor) illegible against busy/bright photo
    // regions — bold primary text had enough weight to survive, lighter
    // secondary text didn't. Bumped once to 0.4: still visibly lighter than a
    // heavy dimmer, but enough to give secondary text real contrast. Fixing
    // contrast here (uniformly, for all text) rather than special-casing
    // textSecondaryColor to match primary when a background is active — the
    // latter would erase the app's text-hierarchy convention specifically in
    // this one state instead of just fixing the underlying contrast problem.
    readonly property real backgroundScrimAlpha: 0.4

    // Scrim a color for use over a custom background image: same hue, reduced
    // opacity so the wallpaper shows through. Use this instead of hand-rolling
    // Qt.rgba(...) at each call site — keeps every scrim in the app at the same
    // translucency level tuned by backgroundScrimAlpha above.
    function scrimColor(baseColor) {
        return Qt.rgba(baseColor.r, baseColor.g, baseColor.b, backgroundScrimAlpha)
    }

    // Card fill for page-level content cards (NOT dialogs/popups, which already
    // sit above a dim Overlay and don't need the wallpaper showing through them).
    // Opaque surfaceColor when no background image is set — zero visual change.
    readonly property color cardBackgroundColor: Settings.theme.backgroundImagePath.length > 0
        ? scrimColor(surfaceColor)
        : surfaceColor

    // Recessed/inset fill for controls that use flat backgroundColor to "blend
    // into" the page rather than stand out as a surface — text field boxes,
    // switch tracks, tab-button active states, unselected pills. Opaque
    // backgroundColor otherwise, so nothing changes with no background set.
    readonly property color insetBackgroundColor: Settings.theme.backgroundImagePath.length > 0
        ? scrimColor(backgroundColor)
        : backgroundColor
    property color primaryColor: _c("primaryColor", Settings.theme.customThemeColors.primaryColor || "#4e85f4")
    // Fill for idle-screen action tiles (Recipes/Beans/Steam/etc.). Over a custom
    // background image they use the neutral surfaceColor so they match the bars and
    // cards (CustomItem scrims it); otherwise the standard primaryColor accent. The
    // blue accent reads as out of place once the rest of the chrome is a neutral scrim.
    readonly property color actionTileColor: Settings.theme.backgroundImagePath.length > 0
        ? surfaceColor
        : primaryColor
    property color secondaryColor: _c("secondaryColor", Settings.theme.customThemeColors.secondaryColor || "#c0c5e3")
    property color textColor: _c("textColor", Settings.theme.customThemeColors.textColor || "#ffffff")
    // Brightened whenever a background image is active. Originally tried scoping
    // this to only bare-background text (Settings tab bar) on the theory that text
    // already sitting on a cardBackgroundColor/insetBackgroundColor scrim had
    // enough contrast from that scrim alone — wrong in practice: bean inventory
    // cards (roaster/origin/tasting-note text on a scrimmed card, still over a
    // busy photo) were just as hard to read. Applies everywhere textSecondaryColor
    // is read, uniformly. Unchanged with no background image set.
    property color textSecondaryColor: _c("textSecondaryColor", Settings.theme.backgroundImagePath.length > 0
        ? Qt.lighter(Settings.theme.customThemeColors.textSecondaryColor || "#a0a8b8", 1.4)
        : (Settings.theme.customThemeColors.textSecondaryColor || "#a0a8b8"))

    // Kept as an alias so call sites that already migrated to the more specific
    // name don't need to churn back — both now resolve to the same brightened value.
    readonly property color textSecondaryOnBackgroundColor: textSecondaryColor
    property color accentColor: _c("accentColor", Settings.theme.customThemeColors.accentColor || "#e94560")
    property color successColor: _c("successColor", Settings.theme.customThemeColors.successColor || "#00cc6d")
    property color warningColor: _c("warningColor", Settings.theme.customThemeColors.warningColor || "#ffaa00")
    property color highlightColor: _c("highlightColor", Settings.theme.customThemeColors.highlightColor || "#ffaa00")
    property color errorColor: _c("errorColor", Settings.theme.customThemeColors.errorColor || "#ff4444")
    property color borderColor: _c("borderColor", Settings.theme.customThemeColors.borderColor || "#3a3a4e")
    property color primaryContrastColor: _c("primaryContrastColor", Settings.theme.customThemeColors.primaryContrastColor || "#ffffff")
    property color iconColor: _c("iconColor", Settings.theme.customThemeColors.iconColor || "#ffffff")
    property color bottomBarColor: _c("bottomBarColor", Settings.theme.customThemeColors.bottomBarColor || "#4e85f4")
    property color actionButtonContentColor: _c("actionButtonContentColor", Settings.theme.customThemeColors.actionButtonContentColor || "#ffffff")

    // --- Layout zone style presets (composable-brew-bar) -----------------
    // A zone's "style" option picks a named preset bundling background fill,
    // text/value color, and value emphasis. Presets resolve to existing theme
    // tokens so they track light/dark/custom palettes (no hardcoded colors).
    //   "standard"  - transparent background, normal text (default, today's look)
    //   "surface"   - surface fill, normal text
    //   "accentBar" - accent fill + contrast text + bold values (the PR #1364 look)
    function zoneBackgroundColor(style) {
        if (style === "accentBar") return primaryColor
        if (style === "surface")   return surfaceColor
        return "transparent"
    }
    function zoneTextColor(style) {
        if (style === "accentBar") return primaryContrastColor
        return textColor
    }
    function zoneValueBold(style) {
        return style === "accentBar"
    }

    // Chart line colors
    property color pressureColor: _c("pressureColor", Settings.theme.customThemeColors.pressureColor || "#18c37e")
    property color pressureGoalColor: _c("pressureGoalColor", Settings.theme.customThemeColors.pressureGoalColor || "#69fdb3")
    property color flowColor: _c("flowColor", Settings.theme.customThemeColors.flowColor || "#4e85f4")
    property color flowGoalColor: _c("flowGoalColor", Settings.theme.customThemeColors.flowGoalColor || "#7aaaff")
    property color temperatureColor: _c("temperatureColor", Settings.theme.customThemeColors.temperatureColor || "#e73249")
    property color temperatureGoalColor: _c("temperatureGoalColor", Settings.theme.customThemeColors.temperatureGoalColor || "#ffa5a6")
    property color weightColor: _c("weightColor", Settings.theme.customThemeColors.weightColor || "#a2693d")
    property color weightFlowColor: _c("weightFlowColor", Settings.theme.customThemeColors.weightFlowColor || "#d4a574")
    property color resistanceColor: _c("resistanceColor", Settings.theme.customThemeColors.resistanceColor || "#eae83d")
    property color conductanceColor: _c("conductanceColor", Settings.theme.customThemeColors.conductanceColor || "#00d2d3")
    property color conductanceDerivativeColor: _c("conductanceDerivativeColor", Settings.theme.customThemeColors.conductanceDerivativeColor || "#e056a0")
    property color darcyResistanceColor: _c("darcyResistanceColor", Settings.theme.customThemeColors.darcyResistanceColor || "#f0a500")
    property color temperatureMixColor: _c("temperatureMixColor", Settings.theme.customThemeColors.temperatureMixColor || "#ce93d8")
    // Washed-out twin of temperatureMixColor, mirroring how temperatureGoalColor
    // relates to temperatureColor: lighter and desaturated, not darker.
    property color temperatureMixGoalColor: _c("temperatureMixGoalColor", Settings.theme.customThemeColors.temperatureMixGoalColor || "#e6c3ec")
    property color waterLevelColor: _c("waterLevelColor", Settings.theme.customThemeColors.waterLevelColor || "#4e85f4")

    // Tracking status colors (profile goal vs actual)
    property color trackOnTargetColor: _c("trackOnTargetColor", Settings.theme.customThemeColors.trackOnTargetColor || "#00cc6d")
    property color trackDriftingColor: _c("trackDriftingColor", Settings.theme.customThemeColors.trackDriftingColor || "#f0ad4e")
    property color trackOffTargetColor: _c("trackOffTargetColor", Settings.theme.customThemeColors.trackOffTargetColor || "#e94560")

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
    property color dyeDoseColor: _c("dyeDoseColor", Settings.theme.customThemeColors.dyeDoseColor || "#6F4E37")
    property color dyeOutputColor: _c("dyeOutputColor", Settings.theme.customThemeColors.dyeOutputColor || "#9C27B0")
    property color dyeTdsColor: _c("dyeTdsColor", Settings.theme.customThemeColors.dyeTdsColor || "#FF9800")
    property color dyeEyColor: _c("dyeEyColor", Settings.theme.customThemeColors.dyeEyColor || "#a2693d")

    // Scaled fonts (sizes customizable via Settings.theme.customFontSizes)
    readonly property font headingFont: Qt.font({ pixelSize: scaled(Settings.theme.customFontSizes.headingSize || 32), bold: true })
    readonly property font titleFont: Qt.font({ pixelSize: scaled(Settings.theme.customFontSizes.titleSize || 24), bold: true })
    readonly property font subtitleFont: Qt.font({ pixelSize: scaled(Settings.theme.customFontSizes.subtitleSize || 18), bold: true })
    readonly property font bodyFont: Qt.font({ pixelSize: scaled(Settings.theme.customFontSizes.bodySize || 18) })
    readonly property font labelFont: Qt.font({ pixelSize: scaled(Settings.theme.customFontSizes.labelSize || 14) })
    readonly property font captionFont: Qt.font({ pixelSize: scaled(Settings.theme.customFontSizes.captionSize || 12) })
    readonly property font valueFont: Qt.font({ pixelSize: scaled(Settings.theme.customFontSizes.valueSize || 48), bold: true })
    readonly property font timerFont: Qt.font({ pixelSize: scaled(Settings.theme.customFontSizes.timerSize || 72), bold: true })

    // Real monospace family per platform. "monospace" is NOT a registered font
    // family on macOS/iOS/Windows — using it triggers a slow font-alias scan and
    // a Qt warning at startup. Resolve to a family that actually exists. Always
    // use Theme.monoFontFamily for monospaced text, never the literal "monospace".
    readonly property string monoFontFamily: {
        switch (Qt.platform.os) {
            case "osx":
            case "ios": return "Menlo"
            case "windows": return "Consolas"
            default: return "monospace"  // android / linux: the generic resolves
        }
    }

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
    property color buttonDisabled: _c("buttonDisabled", Settings.theme.customThemeColors.buttonDisabled || "#555555")

    // UI indicator colors
    property color stopMarkerColor: _c("stopMarkerColor", Settings.theme.customThemeColors.stopMarkerColor || "#FF6B6B")
    property color frameMarkerColor: _c("frameMarkerColor", Settings.theme.customThemeColors.frameMarkerColor || "#66ffffff")
    property color modifiedIndicatorColor: _c("modifiedIndicatorColor", Settings.theme.customThemeColors.modifiedIndicatorColor || "#FFCC00")
    property color simulationIndicatorColor: _c("simulationIndicatorColor", Settings.theme.customThemeColors.simulationIndicatorColor || "#E65100")
    property color warningButtonColor: _c("warningButtonColor", Settings.theme.customThemeColors.warningButtonColor || "#FFA500")
    property color successButtonColor: _c("successButtonColor", Settings.theme.customThemeColors.successButtonColor || "#2E7D32")

    // List/table colors
    property color rowAlternateColor: _c("rowAlternateColor", Settings.theme.customThemeColors.rowAlternateColor || "#1a1a1a")
    property color rowAlternateLightColor: _c("rowAlternateLightColor", Settings.theme.customThemeColors.rowAlternateLightColor || "#222222")

    // Source badge colors (profile/shot import pages)
    property color sourceBadgeBlueColor: _c("sourceBadgeBlueColor", Settings.theme.customThemeColors.sourceBadgeBlueColor || "#4a90d9")
    property color sourceBadgeGreenColor: _c("sourceBadgeGreenColor", Settings.theme.customThemeColors.sourceBadgeGreenColor || "#4ad94a")
    property color sourceBadgeOrangeColor: _c("sourceBadgeOrangeColor", Settings.theme.customThemeColors.sourceBadgeOrangeColor || "#d9a04a")

    // Focus indicator styles
    readonly property color focusColor: primaryColor
    readonly property int focusBorderWidth: 3
    readonly property int focusMargin: 2
}
