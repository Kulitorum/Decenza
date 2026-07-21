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
    // Returns "" when no asset is bundled — see _emojiAssetPath.
    function emojiToImage(emoji) {
        if (!emoji) return ""
        if (emoji.indexOf("qrc:") === 0) return emoji
        var cps = []
        for (var i = 0; i < emoji.length; ) {
            var cp = emoji.codePointAt(i)
            i += cp > 0xFFFF ? 2 : 1
            if (cp !== 0xFE0F) cps.push(cp.toString(16))
        }
        return _emojiAssetPath(cps)
    }

    // Asset path for a codepoint list, or "" when nothing is bundled for it.
    //
    // The app ships the complete Twemoji set, but "complete" is relative to a pinned
    // upstream: a codepoint from a newer Unicode revision, or a sequence upstream does not
    // draw, has no asset. Emitting the path anyway produces an image reference nothing can
    // resolve, which renders as a broken-image artefact — worse than either showing the
    // emoji or dropping it. So callers get "" and drop it.
    //
    // EmojiAssets.has() is an invokable, and non-reactive on purpose: the bundled set is
    // fixed at build time, so unlike Settings.theme.effectiveFontSizes there is nothing for
    // a binding to re-evaluate. See src/core/emojiassets.h.
    property bool _warnedNoEmojiAssets: false
    function _emojiAssetPath(cps) {
        if (!cps || cps.length === 0) return ""
        if (typeof EmojiAssets === "undefined") {
            // Distinct from "this emoji isn't bundled". If the context property is missing,
            // EVERY emoji in this QML engine silently vanishes — and an app with no emoji
            // looks deliberate. The C++ warning in emojiassets.cpp cannot fire here, because
            // has() is never reached. Secondary engines are the real risk: main.cpp creates
            // one for the GHC window that does not set this property.
            if (!_warnedNoEmojiAssets) {
                _warnedNoEmojiAssets = true
                console.warn("[Emoji] EmojiAssets context property missing — every emoji in "
                           + "this QML engine will be stripped. main.cpp must call "
                           + "setContextProperty(\"EmojiAssets\", ...) on this engine.")
            }
            return ""
        }
        var key = cps.join("-")
        if (!EmojiAssets.has(key)) return ""
        return "qrc:/emoji/" + key + ".svg"
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
        // Combining enclosing keycap — the tail of "1️⃣" (U+0031 U+FE0F U+20E3). Without
        // this the whole sequence falls through every range above (the base is ASCII "1")
        // and reaches CoreText as a colour glyph, which is the crash this file prevents.
        // See _isEmojiPresentation for the base character.
        if (cp === 0x20E3) return true
        // Variation selector 16 (emoji presentation) — skip, handled separately
        // ZWJ (U+200D) — skip, only a joiner
        return false
    }

    // True when `cp` is a character that this text is asking to render in EMOJI
    // presentation — i.e. it is followed by U+FE0F.
    //
    // Range checks alone cannot catch these: "©️" is U+00A9 (Latin-1 copyright sign) and
    // "1️⃣" starts at U+0031 (ASCII digit one). Both are ordinary text codepoints that macOS
    // renders from Apple Color Emoji ONLY because of the trailing U+FE0F — which is exactly
    // the colour-glyph path that crashes the render thread. The variation selector is the
    // signal, so use it rather than trying to enumerate every base character.
    function _isEmojiPresentation(text, i, cp) {
        var next = i + (cp > 0xFFFF ? 2 : 1)
        if (next >= text.length || text.codePointAt(next) !== 0xFE0F) return false
        // Bound it, but be honest about how loose the bound is: `cp >= 0xA9` is EVERY
        // character above U+00A9, not just symbols — é, Cyrillic, Greek, Hebrew, Arabic and
        // CJK all qualify. So a stray U+FE0F after CJK text makes that character resolve to
        // a nonexistent asset and get DROPPED (see _emojiAssetPath). ASCII letters are
        // excluded, which covers the common accidental case.
        //
        // Enumerating Unicode's real Emoji_Presentation property would be exact but large;
        // this is deliberately the cheap approximation. A stray FE0F after non-emoji text is
        // rare, and the cost is one lost character rather than a crash.
        if (cp >= 0x30 && cp <= 0x39) return true   // keycap digits
        if (cp === 0x23 || cp === 0x2A) return true // keycap # and *
        return cp >= 0xA9
    }

    // Replace emoji Unicode characters in a string with RichText <img> tags
    // pointing to pre-rendered SVGs. This prevents CoreText/ImageIO crashes
    // caused by Apple Color Emoji font PNG decoding on the render thread.
    // Bind to a Text element — prefer Text.StyledText (elide works), though the
    // output also renders under Text.RichText. The emoji <img> carries both
    // align="middle" (honored by StyledText) and style="vertical-align" (honored
    // by RichText) so it stays centered either way. See QML_GOTCHAS.md — RichText
    // silently disables elide.
    // allowMarkup: pass true ONLY when the caller has already escaped its input and
    // is deliberately supplying markup (links, formatting). It defaults to FALSE so
    // that untrusted text -- bean names from Bean Base, AI replies, community
    // screensaver authors, GitHub release notes -- cannot inject tags into the
    // RichText/StyledText renderer. Getting this wrong should fail visibly (raw
    // tags on screen), not silently.
    function replaceEmojiWithImg(text, pixelSize, allowMarkup) {
        if (!text) return ""
        var size = pixelSize || 16
        var result = ""
        var i = 0
        while (i < text.length) {
            var cp = text.codePointAt(i)
            var charLen = cp > 0xFFFF ? 2 : 1
            if (_isEmoji(cp) || _isEmojiPresentation(text, i, cp)) {
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
                var src = _emojiAssetPath(emojiCps.map(function(c) { return c.toString(16) }))
                if (src === "") {
                    // Nothing bundled for this sequence — drop it rather than emitting a
                    // path that resolves to nothing. The surrounding text is unaffected.
                    i = j
                    continue
                }
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
                var chunk = text.substring(i, i + charLen)
                result += allowMarkup ? chunk : escapeHtml(chunk)
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
        // Decode entities as well as stripping tags. Callers now pass MarkdownRenderer output,
        // where QTextDocument::toHtml() has escaped & < > " and emitted &nbsp; — without this a
        // screen reader announces "Fixes R amp semicolon D sync". &amp; must be decoded LAST or
        // it re-creates the others.
        return stripEmoji(html.replace(/<[^>]*>/g, ""))
            .replace(/&nbsp;/g, " ")
            .replace(/&lt;/g, "<")
            .replace(/&gt;/g, ">")
            .replace(/&quot;/g, "\"")
            .replace(/&#39;/g, "'")
            .replace(/&amp;/g, "&")
            .trim()
    }

    // Escape user-supplied text for embedding as CONTENT in a StyledText, RichText or
    // MarkdownText string.
    //
    // Escapes & and < only — deliberately NOT >. Injection requires OPENING a tag, and
    // only < does that; once < is &lt; a stray > is just a greater-than sign with no tag
    // to close. Escaping > is a convention inherited from ATTRIBUTE contexts, where >
    // can terminate a tag early. Nothing here puts untrusted text in an attribute: the
    // only attributes we generate are src/width on <img>, built from hex codepoints,
    // and the <a href> in ExpandableTextArea, whose URL regex excludes quotes.
    //
    // Leaving > raw is what makes one policy correct for all three formats: escaping it
    // breaks Markdown blockquotes ("> quoted" renders indented, "&gt; quoted" renders as
    // literal text), which AI replies use routinely. Verified against Qt 6.11.1.
    //
    // CONTRACT: content only. Never interpolate untrusted text into an attribute value —
    // that needs quote escaping, which escaping > never provided anyway.
    function escapeHtml(s) {
        return String(s)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
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

    // Should the chrome render as translucent "glass" — scrimmed cards, bars, dialogs,
    // inset controls and action tiles — rather than the opaque fills the app uses on a
    // flat page?
    //
    // On when a background image is set (chrome must go translucent or it reads as a slab
    // on the photo), or when the user turns the glass option on for any theme. This is ONE
    // named predicate rather than the expression repeated at every call site, which is
    // what it used to be — `Settings.theme.backgroundImagePath.length > 0`, spelled out 28
    // times across 19 files. Every copy is another chance for the next trigger to be missed
    // at one of them, and none of those sites ever cared that it was an *image*: only that
    // the chrome should go translucent.
    //
    // This is NOT "is a background active" — it is true with the switch on and no
    // background at all, and false with a colour set and the switch off. For that question
    // use hasBackgroundPreset or hasBackgroundImage.
    readonly property bool glassChrome: hasBackgroundImage
                                        || Settings.theme.glassChrome

    // --- Background colour derivation ----------------------------------------
    //
    // A background colour is a KNOWN value, so unlike a photo everything that has to sit
    // on it can be computed from it. That is what lets every colour be offered under any
    // theme: a pale background under a dark theme would otherwise leave white text on a
    // white page, and the previous design avoided that only by hiding the light half of
    // the catalogue whenever you were in dark mode — so the chooser never showed a light
    // option at all.
    //
    // The ARITHMETIC LIVES IN C++ (BackgroundPresets::derive), not here. It used to live
    // in this file with a hand-kept copy in the test, which meant the contrast floors
    // measured the copy: changing a constant here left the suite green. Reading the
    // derived values means the tests measure what ships.
    //
    // While a colour is active these REPLACE the palette's background, text,
    // secondary-text, surface, border and icon colours. Accents, chart series and status
    // colours still come from the user's theme. A custom text colour is deliberately
    // overridden: the background the user just picked decides light-on-dark or
    // dark-on-light, and no stored preference can be right across the whole ramp.
    //
    // NOTE ON WHICH FLAG TO USE — the two are not interchangeable, and confusing them made
    // the glass switch fail to turn things back off:
    //
    //   hasBackgroundPreset — "the page is a known colour, so derive readable values from
    //       it". Legibility, NOT optional. Text, icons, borders and the recessed inset fill
    //       must follow the page whatever the user has toggled.
    //   glassChrome         — "the user asked for the translucent, neutral look". A user
    //       preference, and it must toggle cleanly: everything it turns on it must turn off.
    //
    // A fill that exists to LOOK a certain way belongs to glassChrome. A colour that exists
    // to stay READABLE belongs to hasBackgroundPreset.
    readonly property bool hasBackgroundPreset: Settings.theme.backgroundPreset.length > 0
    readonly property var _derived: Settings.theme.derivedBackgroundColors

    // Diagnostic-only wrapper around _derived reads. hasBackgroundPreset and _derived are
    // sibling bindings that both key off the same C++ NOTIFY (Settings.theme's
    // backgroundPresetChanged) with no ordering between them — a suspected, unconfirmed
    // theory is that on a background-preset transition, a binding that reads both can see
    // hasBackgroundPreset already flipped true while _derived is still its stale prior value
    // (empty, coming from "none") for one evaluation, producing the engine's own "Unable to
    // assign [undefined] to QColor" warning with no context to confirm or refute that theory.
    // This does NOT fix or mask anything: it returns exactly what _derived[key] already
    // returns (still undefined if it is), so that engine warning still fires unchanged right
    // after this one. Purely additive, so real occurrences can be monitored before any fix is
    // attempted. Remove once the theory is confirmed/refuted and, if confirmed, fixed.
    function _derivedGet(key) {
        var v = _derived[key]
        if (v === undefined) {
            console.warn("[Theme] _derived." + key + " undefined at read — hasBackgroundPreset="
                + hasBackgroundPreset + " backgroundPreset="
                + JSON.stringify(Settings.theme.backgroundPreset)
                + " derivedKeys=" + Object.keys(_derived).length)
        }
        return v
    }

    function _mix(a: color, b: color, t: real): color {
        return Qt.rgba(a.r + (b.r - a.r) * t,
                       a.g + (b.g - a.g) * t,
                       a.b + (b.b - a.b) * t,
                       1.0)
    }

    // Keep an accent fill only while the derived text can be read on it; otherwise fall
    // back to the derived surface, which is guaranteed to work. The bottom bar needs this:
    // the light palette's bar is #ffffff, and a dark background colour derives text to
    // white, so the accent would have been white on white.
    function _fillCarrying(preferred: color): color {
        return _contrastRatio(textColor, preferred) >= 3.0 ? preferred : _derivedGet("surface")
    }

    // Dynamic colors - bind to Settings with fallback defaults
    // Wrapped in _c() for flash-to-identify from web theme editor
    //
    // A background preset overrides the palette's own background colour — that is what a
    // preset IS. Resolution order: preset > custom theme colour > built-in default. The
    // preset's `color` field arrives already resolved for the current light/dark mode.
    property color backgroundColor: _c("backgroundColor",
        Settings.theme.activeBackgroundPreset.value
            || Settings.theme.customThemeColors.backgroundColor
            || "#1a1a2e")
    // Derived while a preset is active, like backgroundColor. This is the one that makes
    // the BARS follow: StatusBar and the bottom bars fill with surfaceColor (or a scrim of
    // it), so leaving it at the palette value left them a different hue from the page they
    // sat on — the theme's navy over a grey page. Deriving it here fixes every consumer at
    // once instead of special-casing each bar.
    property color surfaceColor: _c("surfaceColor", hasBackgroundPreset
        ? _derivedGet("surface")
        : (Settings.theme.customThemeColors.surfaceColor || "#303048"))

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

    // True when there is a PICTURE behind the chrome — a photo, or the last shot's chart —
    // which is the only case where translucency has anything to show through. The name
    // predates the shot chart and is kept because ~70 call sites read it; what it means is
    // "something with structure is back there", not "a file on disk is set".
    //
    // The shot case additionally requires a RENDER to exist. "shot" is believed as stored
    // (it has no parameter), so on a fresh install with no shots the source says shot while
    // BackgroundSurface paints a flat colour — and scrimming chrome over a flat colour is
    // the exact elevation-cancelling failure chromeFill()'s own comment documents.
    readonly property bool hasBackgroundImage: Settings.theme.backgroundSource === "image"
                                               || (Settings.theme.backgroundSource === "shot"
                                                   && LastShotChartSource.imageSource.length > 0)

    // The fill a piece of chrome should actually paint.
    //
    // Over an IMAGE, translucency is the whole point: the photo shows through and that is
    // what makes the chrome read as glass.
    //
    // Over a FLAT colour there is nothing behind to show through, so a scrim is not
    // translucency — it is just a smaller step away from the page, and it silently undoes
    // the elevation the fill was given. That is what left the action tiles nearly
    // invisible: they were lifted 14 L* and then scrimmed back to 5.6. On a flat page the
    // fill stays opaque and the elevation does the work.
    function chromeFill(base: color): color {
        return hasBackgroundImage ? scrimColor(base) : base
    }

    // Scrim a color for use over a background IMAGE: same hue, reduced
    // opacity so the wallpaper shows through. Use this instead of hand-rolling
    // Qt.rgba(...) at each call site — keeps every scrim in the app at the same
    // translucency level tuned by backgroundScrimAlpha above.
    function scrimColor(baseColor) {
        return Qt.rgba(baseColor.r, baseColor.g, baseColor.b, backgroundScrimAlpha)
    }

    // Black or white, whichever is actually more readable on fillColor.
    //
    // Picks by comparing the two real WCAG contrast ratios rather than thresholding a
    // brightness value. Brightness and contrast are different measures and they disagree
    // for mid-range fills — thresholding brightness picks white for #ff4444 (3.4:1) when
    // black would give 6.2:1. A threshold also leaves any fill sitting near it one
    // imperceptible tweak away from flipping every button that uses it; comparing ratios
    // has no threshold to sit near.
    //
    // Opaque fills only: alpha is ignored, so a translucent fill derives from its
    // unblended colour rather than what shows through it.
    function contrastColorFor(fillColor: color): color {
        var l = _relativeLuminance(fillColor)
        var ratioOnBlack = (l + 0.05) / 0.05
        var ratioOnWhite = 1.05 / (l + 0.05)
        return ratioOnBlack >= ratioOnWhite ? "#000000" : "#FFFFFF"
    }

    // WCAG 2.x relative luminance: sRGB channels linearised, then weighted. Not the same
    // as the cheaper BT.601 brightness weights — see contrastColorFor.
    function _relativeLuminance(c) {
        function linearise(channel) {
            return channel <= 0.03928 ? channel / 12.92
                                      : Math.pow((channel + 0.055) / 1.055, 2.4)
        }
        return 0.2126 * linearise(c.r) + 0.7152 * linearise(c.g) + 0.0722 * linearise(c.b)
    }

    // Card fill for page-level content cards. Dialogs/popups use
    // dialogBackgroundColor below (same value, separately documented).
    // Opaque surfaceColor when no background image is set — zero visual change.
    readonly property color cardBackgroundColor: glassChrome
        ? chromeFill(surfaceColor)
        : surfaceColor

    // Frame fill for content dialogs/popups (Brew Settings, Grind Setting, Brew
    // Ratio, etc.). With the glass chrome on these use the same
    // tinted-glass scrim as the idle action tiles (Steam/Recipes/Beans) so the
    // wallpaper shows through and the dialog matches the rest of the chrome
    // rather than reading as an out-of-place opaque slab; opaque surfaceColor
    // otherwise, so nothing changes with no background set. The modal Overlay
    // dim behind the dialog keeps the glass legible over busy photos.
    readonly property color dialogBackgroundColor: glassChrome
        ? chromeFill(surfaceColor)
        : surfaceColor

    // Recessed/inset fill for controls that use flat backgroundColor to "blend
    // into" the page rather than stand out as a surface — text field boxes,
    // switch tracks, tab-button active states, unselected pills. Opaque
    // backgroundColor otherwise, so nothing changes with no background set.
    // Over a photo this is a dimmed patch: the image shows through at a different
    // brightness, so a 40% wash of backgroundColor reads as a recess. Over a FLAT page —
    // a preset, or Glass with no background — the same expression is 40% of a colour
    // composited over itself, which is that colour exactly, and the control disappears.
    // (Most inset controls sit on a card, where the scrim lands on surfaceColor and reads
    // fine either way; this fixes the minority drawn straight onto the page, which is why
    // it went unnoticed.) On a flat page we scrim toward the contrast direction instead,
    // so the recessed step survives.
    readonly property color insetBackgroundColor: hasBackgroundPreset || glassChrome
        ? (hasBackgroundImage ? scrimColor(backgroundColor) : _flatInsetTint)
        : backgroundColor

    // A translucent white (dark mode) or black (light mode) wash — a step away from
    // whatever is behind, rather than a wash of that same colour. Alpha is deliberately
    // well under backgroundScrimAlpha: this marks a recess, it does not fill a surface.
    // Keyed on what is actually behind the control — the preset colour when there is one,
    // the theme's polarity otherwise. A preset can be pale under a dark theme, so
    // isDarkMode is the wrong question once a preset is active.
    readonly property color _flatInsetTint: (hasBackgroundPreset ? Qt.colorEqual(_derivedGet("text"), "#000000") : !isDarkMode)
        ? Qt.rgba(0, 0, 0, 0.06)
        : Qt.rgba(1, 1, 1, 0.10)
    // The semantic palette — primary, accent, success, warning, error — kept readable on
    // the page a background colour produces.
    //
    // Unlike text, borders and card fills, these are NOT derived: their hue is the meaning,
    // so amber has to stay amber. But they are authored against a dark page and nine of the
    // catalogue's colours are pale, where they collapse — on Cortado, warning measures
    // 1.3:1, error 1.4:1 and success 1.2:1 against 4.5:1 needed. That is not a dim caption,
    // it is a warning banner you cannot see, and it predates the presets: a light theme with
    // a background photo has always had it. Each colour is nudged along the axis it is
    // already on, by the smallest step that clears the floor, so a dark page (where they
    // measure 5:1 to 9:1) is untouched and a pale one gets a deeper version of the same hue.
    //
    // Gated on hasBackgroundPreset, not glassChrome: this is the READABILITY branch, and the
    // page whose luminance we are correcting against is the preset's. See the note at
    // hasBackgroundPreset for why those two gates are not interchangeable.
    function _readableOnPage(base: color): color {
        return hasBackgroundPreset
            ? Settings.theme.adjustedForContrast(base, _derivedGet("background"))
            : base
    }
    property color primaryColor: _c("primaryColor", _readableOnPage(Settings.theme.customThemeColors.primaryColor || "#4e85f4"))
    // Fill for idle-screen action tiles (Recipes/Beans/Steam/etc.). Over a custom
    // background image they use the neutral surfaceColor so they match the bars and
    // cards (CustomItem scrims it); otherwise the standard primaryColor accent. The
    // blue accent reads as out of place once the rest of the chrome is a neutral scrim.
    // Gated on the SWITCH, not on the preset. It was gated on the preset, which meant a
    // preset pinned the tiles to a neutral fill and turning glass off could not bring the
    // accent back — the switch appeared to work only for the bottom bar.
    readonly property color actionTileColor: !glassChrome
        ? primaryColor
        : (hasBackgroundPreset ? _derivedGet("actionTile") : surfaceColor)

    // Colour for text and icons sitting ON a chrome fill.
    //
    // With a background preset the fill is derived from the page, so a stored palette
    // colour need not suit it: white is right on the usual blue action tile and invisible
    // on one derived from Cortado or Porcelain. But blanket-deriving would also repaint
    // content on fills that are still the theme's own accent, so the palette colour is kept
    // whenever it is actually readable on the fill and replaced only when it is not.
    //
    // The threshold is 3:1, WCAG's large-text floor, because this is button and tile
    // labelling. It is deliberately below the 4.5:1 used for body text: white on the
    // primary blue measures 3.5:1, an existing and intentional choice, and a 4.5 gate
    // would flip every accent button in the app to black content.
    function contentColorOn(fill: color, fallback: color): color {
        // Also when the glass switch is on: it changes actionTileColor and
        // actionButtonFill() with no colour preset present, and gating only on the preset
        // left a light theme + glass on painting white content on a white surface.
        if (!hasBackgroundPreset && !glassChrome)
            return fallback
        return _contrastRatio(fallback, fill) >= 3.0 ? fallback : contrastColorFor(fill)
    }

    function _contrastRatio(a: color, b: color): real {
        var la = _relativeLuminance(a)
        var lb = _relativeLuminance(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }

    // Fill for idle-screen action buttons that render their OWN ActionButton/
    // Rectangle (Sleep/Quit/History/Favorites/Discuss) rather than as a scrimmed
    // CustomItem tile (Recipes/Beans/Steam). With the glass chrome on they
    // scrim to the same neutral glass as those tiles — so they read as buttons
    // like Steam/Hot Water rather than an opaque colour slab — while keeping
    // their given accent/grey fill when no image is set (zero change). Applies to
    // both the full-mode ActionButton and the compact-mode Rectangle (Sleep/Quit).
    // The image-case fill equals cardBackgroundColor (scrimColor(surfaceColor)).
    function actionButtonFill(baseColor: color): color {
        // Also the switch, not the preset — same reason as actionTileColor.
        return glassChrome ? cardBackgroundColor : baseColor
    }

    // actionButtonFill for a host that knows the chrome fill to use: the background
    // chooser's preview, drawing a candidate colour whose derived fill is not Theme's
    // (Theme still describes the APPLIED background until Apply is pressed). Needed
    // because the glass branch above discards baseColor entirely — passing the
    // candidate in as baseColor does nothing, which is why the preview's Ratio, Grind
    // and Sleep chips stayed on the applied theme's navy while the page around them
    // followed the candidate. Transparent override = no host opinion, behave exactly
    // as actionButtonFill.
    function actionButtonFillOn(baseColor: color, overrideFill: color): color {
        return (glassChrome && overrideFill.a > 0) ? overrideFill : actionButtonFill(baseColor)
    }
    property color secondaryColor: _c("secondaryColor", Settings.theme.customThemeColors.secondaryColor || "#c0c5e3")
    // Derived from the background while a preset is active — see the derivation block
    // above for why a stored preference cannot be right across the whole ramp.
    property color textColor: _c("textColor", hasBackgroundPreset
        ? _derivedGet("text")
        : (Settings.theme.customThemeColors.textColor || "#ffffff"))
    // Pushed AWAY from the page whenever the glass chrome is active. Originally tried
    // scoping this to only bare-background text (Settings tab bar) on the theory that
    // text already sitting on a cardBackgroundColor/insetBackgroundColor scrim had
    // enough contrast from that scrim alone — wrong in practice: bean inventory
    // cards (roaster/origin/tasting-note text on a scrimmed card, still over a
    // busy photo) were just as hard to read. Applies everywhere textSecondaryColor
    // is read, uniformly. Unchanged when the glass chrome is off.
    //
    // The direction is per-mode, and that is a fix, not a refinement: this was
    // unconditionally `Qt.lighter(..., 1.4)`, which is right against a dark page and
    // exactly backwards against a light one — it pushed secondary text TOWARD the
    // background and cut its contrast. Any light theme with a photo has had that since
    // the background feature shipped; presets ship a full light set, so it stops being
    // rare. Lighten in dark mode, darken in light mode.
    property color textSecondaryColor: _c("textSecondaryColor", hasBackgroundPreset
        // Softened toward the page from the derived text colour, not from the palette.
        // Derived in C++, where the softening fraction and its justification live.
        ? _derivedGet("textSecondary")
        : (glassChrome
            ? (isDarkMode ? Qt.lighter(Settings.theme.customThemeColors.textSecondaryColor || "#a0a8b8", 1.4)
                          : Qt.darker(Settings.theme.customThemeColors.textSecondaryColor || "#a0a8b8", 1.4))
            : (Settings.theme.customThemeColors.textSecondaryColor || "#a0a8b8")))

    // Kept as an alias so call sites that already migrated to the more specific
    // name don't need to churn back — both now resolve to the same brightened value.
    readonly property color textSecondaryOnBackgroundColor: textSecondaryColor
    // All five run through _readableOnPage — see the note above primaryColor.
    property color accentColor: _c("accentColor", _readableOnPage(Settings.theme.customThemeColors.accentColor || "#e94560"))
    property color successColor: _c("successColor", _readableOnPage(Settings.theme.customThemeColors.successColor || "#00cc6d"))
    property color warningColor: _c("warningColor", _readableOnPage(Settings.theme.customThemeColors.warningColor || "#ffaa00"))
    property color highlightColor: _c("highlightColor", _readableOnPage(Settings.theme.customThemeColors.highlightColor || "#ffaa00"))
    property color errorColor: _c("errorColor", _readableOnPage(Settings.theme.customThemeColors.errorColor || "#ff4444"))
    // Derived while a colour is active so a border is visible on a pale page as well as a
    // dark one: a stored dark border vanishes on Porcelain, a stored light one on French
    // Roast.
    property color borderColor: _c("borderColor", hasBackgroundPreset
        ? _derivedGet("border")
        : (Settings.theme.customThemeColors.borderColor || "#3a3a4e"))
    property color primaryContrastColor: _c("primaryContrastColor", Settings.theme.customThemeColors.primaryContrastColor || "#ffffff")
    // Icons are monochrome and sit on the page or on a card, both derived from the preset,
    // so they follow the derived text colour rather than a stored one.
    property color iconColor: _c("iconColor", hasBackgroundPreset
        ? _derivedGet("text")
        : (Settings.theme.customThemeColors.iconColor || "#ffffff"))
    property color bottomBarColor: _c("bottomBarColor", hasBackgroundPreset
        ? _fillCarrying(Settings.theme.customThemeColors.bottomBarColor || "#4e85f4")
        : (Settings.theme.customThemeColors.bottomBarColor || "#4e85f4"))
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
    // Fill for a small tappable value chip (the Ratio/Grind pills) sitting in a
    // zone of the given style, chosen to contrast with that zone's OWN background
    // (zoneBackgroundColor): a light capsule on the blue accentBar, a recessed
    // inset chip on a surfaceColor zone, and a raised surface chip on the
    // transparent standard zone (where a bare zoneTextColor fill would otherwise
    // be a jarring white capsule in dark mode).
    function zoneChipColor(style) {
        if (style === "accentBar") return primaryContrastColor
        if (style === "surface")   return insetBackgroundColor
        return surfaceColor
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
    // Deliberately a DEEPER violet, not the washed-out twin the other goal colors
    // are. Both goal lines are dashed pastels sitting within ~1°C of each other in
    // the same band, so a paler mix goal reads as a second temperatureGoalColor
    // rather than as the goal for the violet mix line. Visualizer separates the
    // same pair the same way (#EE3377 basket goal vs the deeper #AA3477 mix goal).
    property color temperatureMixGoalColor: _c("temperatureMixGoalColor", Settings.theme.customThemeColors.temperatureMixGoalColor || "#a678b8")
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

    // The bundled UI family ("Decenza Sans"), or empty when registration failed — an empty
    // family in Qt.font() falls back to the application default, preserving the graceful
    // degradation main.cpp already provides. Stated explicitly on every role rather than
    // relying on inheritance, so a Quick Controls style default cannot quietly displace it
    // (#1537).
    readonly property string fontFamily: Settings.theme.bundledFontFamily

    // The UI family plus the bundled symbol face, in priority order. Qt consults the
    // second family only for codepoints the first lacks, so this changes nothing about
    // how text renders — Decenza Sans still draws every letter — while arrows and
    // geometric shapes (→ ← ↗ ↕ ▶ ◀ ⧉) come from the bundle instead of whatever the host
    // happened to offer. That is the whole fix: no QML edits, no icons, no emoji, and the
    // symbols stay monochrome so they take the element's colour like the text around them.
    //
    // Each entry is dropped when empty. A "" in this list is NOT inert — Qt resolves it to
    // the application default, which would silently reinstate the platform fallback.
    readonly property var fontFamilies: {
        var f = []
        if (fontFamily) f.push(fontFamily)
        if (Settings.theme.symbolFontFamily) f.push(Settings.theme.symbolFontFamily)
        return f
    }

    // Scaled fonts. Sizes come from Settings.theme.effectiveFontSizes, which merges the
    // user's overrides over the canonical defaults declared once in SettingsTheme — never
    // re-hardcode a default here, that duplication is what this replaced.
    readonly property font headingFont: Qt.font({ families: fontFamilies, pixelSize: scaled(Settings.theme.effectiveFontSizes.headingSize), bold: true })
    readonly property font titleFont: Qt.font({ families: fontFamilies, pixelSize: scaled(Settings.theme.effectiveFontSizes.titleSize), bold: true })
    readonly property font subtitleFont: Qt.font({ families: fontFamilies, pixelSize: scaled(Settings.theme.effectiveFontSizes.subtitleSize), bold: true })
    readonly property font bodyFont: Qt.font({ families: fontFamilies, pixelSize: scaled(Settings.theme.effectiveFontSizes.bodySize) })
    readonly property font labelFont: Qt.font({ families: fontFamilies, pixelSize: scaled(Settings.theme.effectiveFontSizes.labelSize) })
    readonly property font captionFont: Qt.font({ families: fontFamilies, pixelSize: scaled(Settings.theme.effectiveFontSizes.captionSize) })
    readonly property font valueFont: Qt.font({ families: fontFamilies, pixelSize: scaled(Settings.theme.effectiveFontSizes.valueSize), bold: true })
    readonly property font timerFont: Qt.font({ families: fontFamilies, pixelSize: scaled(Settings.theme.effectiveFontSizes.timerSize), bold: true })

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
    // These two are drawn as text ON THE PAGE, so they take the same treatment as the
    // semantic palette — see _readableOnPage. The markers above do not: they sit on a
    // chart's own surface rather than the page, and frameMarkerColor carries an alpha the
    // adjustment would flatten.
    property color modifiedIndicatorColor: _c("modifiedIndicatorColor", _readableOnPage(Settings.theme.customThemeColors.modifiedIndicatorColor || "#FFCC00"))
    property color simulationIndicatorColor: _c("simulationIndicatorColor", _readableOnPage(Settings.theme.customThemeColors.simulationIndicatorColor || "#E65100"))
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
