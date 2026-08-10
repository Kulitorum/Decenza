// `layer.effect` declares an inline component, so ids from this file are not statically
// resolvable inside it without this pragma. No Repeater/delegate in this file, so no
// `required property` is needed — see PresetPillRow.qml for the case that does.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

LayoutWidgetItem {
    id: root

    // %STEAM_TEMP% resolves to "Off" from the RESOLVED heater state, not from
    // the measured temperature — a boiler switched off minutes ago still reads
    // hot on its way down.
    //
    // Hoisted to a property so the read happens on steamHeaterStateChanged and
    // nowhere else. Read inline it would run inside `resolvedText`, which
    // re-evaluates on every DE1 sample (~10 Hz while the machine runs) for every
    // Custom widget on the layout — and each read is a full policy resolve:
    // QSettings, a JSON parse of the pitcher blob, and a second parse of the
    // active recipe when Let the recipe decide is on. A widget with no
    // %STEAM_TEMP% in it paid that too.
    readonly property bool _steamHeaterOff:
        typeof MainController !== "undefined" && MainController !== null && !MainController.steamHeaterOn

    readonly property string content: modelData.content || "Text"
    readonly property string textAlign: modelData.align || "center"
    readonly property string action: modelData.action || ""
    readonly property string longPressAction: modelData.longPressAction || ""
    readonly property string doubleclickAction: modelData.doubleclickAction || ""
    readonly property string emoji: modelData.emoji || ""
    readonly property string bgColor: modelData.backgroundColor || ""
    readonly property bool hasAction: action !== "" || longPressAction !== "" || doubleclickAction !== ""
    readonly property bool hideBackground: modelData.hideBackground === true
    readonly property bool hasEmoji: emoji !== ""
    readonly property bool emojiIsSvg: hasEmoji && emoji.indexOf("qrc:") === 0

    // Accessibility hint describing configured secondary actions (for TalkBack/VoiceOver)
    // Action labels are intentionally generic because action strings (e.g. "navigate:settings") have no associated human-readable label.
    readonly property string _accessibleHint: {
        var _ = TranslationManager.translationVersion  // re-evaluate on language change
        var hasLP = root.longPressAction !== ""
        var hasDC = root.doubleclickAction !== ""
        if (hasLP && hasDC)
            return TranslationManager.translate("customitem.accessible.hint.both", "Long-press or double-tap for additional actions.")
        if (hasLP)
            return TranslationManager.translate("customitem.accessible.hint.longpress", "Long-press for additional action.")
        if (hasDC)
            return TranslationManager.translate("customitem.accessible.hint.doubletap", "Double-tap for additional action.")
        return ""
    }

    // Action tiles use Theme.actionTileColor (neutral over a custom background
    // image so they match the bars/cards, standard accent otherwise); an explicit
    // per-widget bgColor still wins. (zoneFillOverride is inherited from LayoutWidgetItem;
    // see there for who sets it.)
    readonly property color _themeTileColor: hasAction ? Theme.actionTileColor : Theme.surfaceColor
    readonly property color _parsedBgColor: bgColor !== "" ? bgColor
        : (zoneFillOverride.a > 0 ? zoneFillOverride : _themeTileColor)

    // A brew-settings widget highlights (Theme.highlightColor) whenever a real
    // brew override is in effect — temperature or target yield deviating from the
    // ACTIVE baseline. When a recipe is active that baseline is the recipe's own
    // yield/temp, so a recipe's designed values don't light this up
    // (recipe-baseline-not-override, #1485); the shared MainController flags fold
    // in the recipe-vs-profile choice, matching Brew Settings and the Shot Plan.
    readonly property bool _isBrewSettingsWidget: action === "brewSettings"
        || longPressAction === "brewSettings" || doubleclickAction === "brewSettings"
    readonly property bool _brewOverrideActive:
        MainController.temperatureIsRealOverride || MainController.yieldIsRealOverride
    readonly property color _baseBackground:
        (_isBrewSettingsWidget && _brewOverrideActive) ? Theme.highlightColor : _parsedBgColor
    // Idle-screen action tiles (Recipes/Beans/Steam/Hot Water/Flush/Equipment/
    // etc. — all compiled to CustomItem, see LayoutItemDelegate.compileToCustom)
    // and user-authored Custom widgets share this rendering path; scrim
    // uniformly like every other fill in the app when the glass chrome is on.
    readonly property color _effectiveBackground:
        Theme.glassChrome ? Theme.chromeFill(_baseBackground) : _baseBackground
    // Content color for text and icon tinting on the button background
    readonly property color _contentColor: Theme.contentColorOn(_effectiveBackground, Theme.primaryContrastColor)

    // Active-mode highlight: a togglePreset:<mode> button shows a contrasting ring
    // while its preset row is expanded, so you can see which mode is selected. In
    // practice these are the compiled action buttons (Espresso/Steam/Hot Water/
    // Flush/Beans/Equipment) — neither the in-app nor the web widget editor exposes
    // togglePreset for hand-made custom widgets, though a raw action string in a
    // layout config is honored the same way.
    readonly property string _toggleMode:
        action.indexOf("togglePreset:") === 0 ? action.substring("togglePreset:".length) : ""
    property var idlePage: {
        var p = root.parent
        while (p) {
            if (p.objectName === "idlePage") return p
            p = p.parent
        }
        // Hosts outside IdlePage (the persistent status bar lives beside the page
        // stack in main.qml): fall back to the stack's current page. togglePreset
        // and the active ring then work exactly while the home screen is showing —
        // the only time the preset row exists — and stay inert elsewhere.
        if (AppShell.currentPage && AppShell.currentPage.objectName === "idlePage")
            return AppShell.currentPage
        return null
    }
    readonly property bool isActive: _toggleMode !== ""
        && idlePage !== null && idlePage.activePresetFunction === _toggleMode
    // Ring must contrast with both the button fill and the page background: a darker
    // shade vanishes against a dark background, a lighter one against a light background.
    readonly property color _activeRingColor: Settings.theme.isDarkMode
        ? Qt.lighter(_effectiveBackground, 1.6) : Qt.darker(_effectiveBackground, 1.5)

    readonly property int qtAlignment: {
        switch (textAlign) {
            case "left": return Text.AlignLeft
            case "right": return Text.AlignRight
            default: return Text.AlignHCenter
        }
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // Trigger counter to force re-evaluation of resolvedText (only used for %TIME%/%DATE%)
    property int _refreshTick: 0

    // Precomputed flags: which live data categories does this item's template use?
    // These depend only on `content` so they evaluate once at layout load, not at 5 Hz.
    readonly property bool _needsMachineData: content.indexOf("%TEMP%") >= 0
        || content.indexOf("%STEAM_TEMP%") >= 0
        || content.indexOf("%PRESSURE%") >= 0
        || content.indexOf("%FLOW%") >= 0
        || content.indexOf("%WATER%") >= 0
        || content.indexOf("%STATE%") >= 0
        || content.indexOf("%CONNECTED%") >= 0
        || content.indexOf("%MACHINE_CONNECTED%") >= 0
        || content.indexOf("%MACHINE_READY%") >= 0
        || content.indexOf("%DEVICES%") >= 0

    readonly property bool _needsScaleData: content.indexOf("%WEIGHT%") >= 0
        || content.indexOf("%SHOT_TIME%") >= 0
        || content.indexOf("%VOLUME%") >= 0
        || content.indexOf("%POUR_VOLUME%") >= 0
        || content.indexOf("%PREINFUSION_VOLUME%") >= 0
        || content.indexOf("%DEVICES%") >= 0

    readonly property bool _needsControllerData: content.indexOf("%TARGET_WEIGHT%") >= 0
        || content.indexOf("%PROFILE%") >= 0
        || content.indexOf("%TARGET_TEMP%") >= 0
        || content.indexOf("%RATIO%") >= 0
        || content.indexOf("%DOSE%") >= 0

    readonly property bool _needsScaleDevice: content.indexOf("%SCALE%") >= 0
        || content.indexOf("%SCALE_CONNECTED%") >= 0
        || content.indexOf("%DEVICES%") >= 0

    readonly property bool _needsSettingsData: content.indexOf("%GRIND%") >= 0
        || content.indexOf("%GRINDER%") >= 0
        || content.indexOf("%RPM%") >= 0

    // Variable substitution - only tracks the live properties this item actually uses.
    // Items showing static values (e.g. %PROFILE%) no longer re-evaluate at 5 Hz.
    readonly property string resolvedText: {
        var _c = content  // direct dependency so content changes always trigger re-evaluation
        var _tick = _refreshTick
        if (_needsMachineData && typeof DE1Device !== "undefined" && DE1Device !== null) {
            void(DE1Device.temperature); void(DE1Device.steamTemperature)
            void(DE1Device.pressure); void(DE1Device.flow)
            void(DE1Device.waterLevel); void(DE1Device.waterLevelMl)
            void(DE1Device.stateString); void(DE1Device.connected)
        }
        if (_needsScaleData && typeof MachineState !== "undefined" && MachineState !== null) {
            void(MachineState.scaleWeight); void(MachineState.shotTime)
            void(MachineState.cumulativeVolume)
            void(MachineState.preinfusionVolume); void(MachineState.pourVolume)
        }
        if ((_needsMachineData || _needsScaleData) && typeof MachineState !== "undefined" && MachineState !== null) {
            void(MachineState.phase)
        }
        if (_needsControllerData && typeof ProfileManager !== "undefined" && ProfileManager !== null) {
            void(ProfileManager.targetWeight); void(ProfileManager.currentProfileName)
            void(ProfileManager.profileTargetTemperature)
            void(ProfileManager.brewByRatio); void(ProfileManager.brewByRatioDose)
            // %TARGET_TEMP% shows the effective brew temp (per-brew override when set)
            if (typeof Settings !== "undefined" && Settings !== null) void(Settings.brew.temperatureOverride)
        }
        if (_needsScaleDevice) {
            void(ScaleDevice.name); void(ScaleDevice.connected)
        }
        if (_needsSettingsData && typeof Settings !== "undefined" && Settings !== null) {
            void(Settings.dye.dyeGrinderSetting); void(Settings.dye.dyeGrinderModel)
            void(Settings.dye.dyeGrinderRpm)
        }
        return substituteVariables(_c)
    }

    // Timer to update time/date variables (and any other periodic refresh)
    Timer {
        id: refreshTimer
        interval: 1000
        running: root.content.indexOf("%TIME%") >= 0 || root.content.indexOf("%DATE%") >= 0
        repeat: true
        onTriggered: root._refreshTick++
    }

    // Detect malformed HTML (e.g. tags inside attribute values) and strip to plain text
    function sanitizeHtml(html) {
        if (!html || html.indexOf("<") < 0) return html
        var inTag = false
        var inQuote = false
        for (var i = 0; i < html.length; i++) {
            var ch = html[i]
            if (inQuote) {
                if (ch === '"') inQuote = false
                else if (ch === '<') {
                    // Tag inside a quoted attribute — HTML is broken, strip all tags
                    root._warn("Malformed HTML detected, stripping tags: " + html.substring(0, 80))
                    return html.replace(/<[^>]*>/g, "")
                }
            } else if (inTag) {
                if (ch === '"') inQuote = true
                else if (ch === '>') inTag = false
            } else {
                if (ch === '<') inTag = true
            }
        }
        return html
    }

    function substituteVariables(text) {
        if (!text) return ""
        var result = sanitizeHtml(text)
        // Machine
        result = result.replace(/%TEMP%/g, typeof DE1Device !== "undefined" && DE1Device !== null ? Theme.cToDisplay(DE1Device.temperature).toFixed(1) : "—")
        // "Off" when the heater is off — same rule as SteamTemperatureItem, and for
        // the same reason: the measured boiler temperature cannot tell a hot
        // boiler from one that is cooling because the heater was switched off.
        result = result.replace(/%STEAM_TEMP%/g, typeof DE1Device !== "undefined" && DE1Device !== null
            ? (root._steamHeaterOff
                ? SteamLabels.offReadout
                : Theme.cToDisplay(DE1Device.steamTemperature).toFixed(0) + "\u00B0")
            : "—")
        result = result.replace(/%PRESSURE%/g, typeof DE1Device !== "undefined" && DE1Device !== null ? DE1Device.pressure.toFixed(1) : "—")
        result = result.replace(/%FLOW%/g, typeof DE1Device !== "undefined" && DE1Device !== null ? DE1Device.flow.toFixed(1) : "—")
        result = result.replace(/%WATER%/g, typeof DE1Device !== "undefined" && DE1Device !== null ? DE1Device.waterLevel.toFixed(0) : "—")
        result = result.replace(/%WATER_ML%/g, typeof DE1Device !== "undefined" && DE1Device !== null ? DE1Device.waterLevelMl.toFixed(0) : "—")
        result = result.replace(/%STATE%/g, typeof DE1Device !== "undefined" && DE1Device !== null ? DE1Device.stateString : "—")
        // Scale / Shot
        result = result.replace(/%WEIGHT%/g, typeof MachineState !== "undefined" && MachineState !== null ? MachineState.scaleWeight.toFixed(1) : "—")
        result = result.replace(/%SHOT_TIME%/g, typeof MachineState !== "undefined" && MachineState !== null ? MachineState.shotTime.toFixed(1) : "—")
        result = result.replace(/%VOLUME%/g, typeof MachineState !== "undefined" && MachineState !== null ? MachineState.cumulativeVolume.toFixed(0) : "—")
        result = result.replace(/%POUR_VOLUME%/g, typeof MachineState !== "undefined" && MachineState !== null ? MachineState.pourVolume.toFixed(0) : "—")
        result = result.replace(/%PREINFUSION_VOLUME%/g, typeof MachineState !== "undefined" && MachineState !== null ? MachineState.preinfusionVolume.toFixed(0) : "—")
        // Profile (ProfileManager)
        result = result.replace(/%TARGET_WEIGHT%/g, typeof ProfileManager !== "undefined" && ProfileManager !== null ? ProfileManager.targetWeight.toFixed(1) : "—")
        result = result.replace(/%PROFILE%/g, typeof ProfileManager !== "undefined" && ProfileManager !== null ? ProfileManager.currentProfileName : "—")
        // The EFFECTIVE brew temp — the per-brew override when set (which, with a
        // recipe active, is the recipe's own temp), else the profile default. This
        // matches %TARGET_WEIGHT% (which reads the effective ProfileManager.targetWeight)
        // so temp and yield stay aligned (recipe-baseline-not-override, #1485).
        result = result.replace(/%TARGET_TEMP%/g, typeof Settings !== "undefined" && Settings !== null
            ? Theme.cToDisplay(Settings.brew.hasTemperatureOverride ? Settings.brew.temperatureOverride : ProfileManager.profileTargetTemperature).toFixed(1)
            : "—")
        result = result.replace(/%RATIO%/g, typeof ProfileManager !== "undefined" && ProfileManager !== null ? ProfileManager.brewByRatio.toFixed(1) : "—")
        result = result.replace(/%DOSE%/g, typeof ProfileManager !== "undefined" && ProfileManager !== null ? ProfileManager.brewByRatioDose.toFixed(1) : "—")
        // Scale device
        result = result.replace(/%SCALE%/g, ScaleDevice.name || "—")
        // Grinder
        result = result.replace(/%GRIND%/g, typeof Settings !== "undefined" && Settings !== null && Settings.dye.dyeGrinderSetting ? Settings.dye.dyeGrinderSetting : "—")
        result = result.replace(/%RPM%/g, typeof Settings !== "undefined" && Settings !== null && Settings.dye.dyeGrinderRpm > 0 ? String(Settings.dye.dyeGrinderRpm) : "—")
        result = result.replace(/%GRINDER%/g, typeof Settings !== "undefined" && Settings !== null && Settings.dye.dyeGrinderModel ? Settings.dye.dyeGrinderModel : "—")
        // Machine ready status
        var machineReady = typeof MachineState !== "undefined" && MachineState !== null && MachineState.isReady
        result = result.replace(/%MACHINE_READY%/g, machineReady ? TranslationManager.translate("customitem.status.ready", "Ready") : TranslationManager.translate("customitem.status.notReady", "Not ready"))
        if (result.indexOf("%MACHINE_READY_COLOR%") >= 0)
            result = result.replace(/%MACHINE_READY_COLOR%/g, machineReady ? Theme.successColor : Theme.errorColor)
        // Connection status
        var machineOn = typeof DE1Device !== "undefined" && DE1Device !== null && DE1Device.connected
        var scaleOn = ScaleDevice.connected
        var flowScale = ScaleDevice.isFlowScale
        result = result.replace(/%CONNECTED%/g, machineOn ? TranslationManager.translate("customitem.status.online", "Online") : TranslationManager.translate("customitem.status.offline", "Offline"))
        if (result.indexOf("%CONNECTED_COLOR%") >= 0)
            result = result.replace(/%CONNECTED_COLOR%/g, machineOn ? Theme.successColor : Theme.errorColor)
        if (machineOn && scaleOn && !flowScale)
            result = result.replace(/%DEVICES%/g, TranslationManager.translate("customitem.devices.machineScale", "Machine + Scale"))
        else if (machineOn && flowScale)
            result = result.replace(/%DEVICES%/g, TranslationManager.translate("customitem.devices.machineSimScale", "Machine + Simulated Scale"))
        else
            result = result.replace(/%DEVICES%/g, TranslationManager.translate("customitem.devices.machine", "Machine"))
        // Individual connection indicators (✅ = emoji/2705, ❌ = icons/cross-filled)
        var statusIconSize = Theme.bodyFont.pixelSize
        var statusConnected = "qrc:/emoji/2705.svg"
        var statusDisconnected = "qrc:/icons/cross-filled.svg"
        var statusImg = function(src) {
            // align="middle" centres the icon in Text.StyledText (which ignores the
            // CSS style= attribute); style keeps it centred under any RichText caller.
            return "<img src=\"" + src + "\" width=\"" + statusIconSize + "\" height=\"" + statusIconSize + "\" align=\"middle\" style=\"vertical-align: middle\">"
        }
        if (result.indexOf("%MACHINE_CONNECTED%") >= 0)
            result = result.replace(/%MACHINE_CONNECTED%/g,
                statusImg(machineOn ? statusConnected : statusDisconnected))
        if (result.indexOf("%SCALE_CONNECTED%") >= 0)
            result = result.replace(/%SCALE_CONNECTED%/g,
                statusImg((scaleOn && !flowScale) ? statusConnected : statusDisconnected))
        // Time
        var now = new Date()
        result = result.replace(/%TIME%/g, Qt.formatTime(now, Settings.app.use12HourTime ? "h:mmap" : "hh:mm"))
        result = result.replace(/%DATE%/g, Qt.formatDate(now, "yyyy-MM-dd"))
        // Convert any emoji Unicode in the result to <img> tags to avoid
        // CoreText/ImageIO crash from Apple Color Emoji PNG decoding on render thread
        // allowMarkup: user-authored widget templates may deliberately contain formatting.
        return Theme.replaceEmojiWithImg(result, Theme.bodyFont.pixelSize, true)
    }


    // Dispatch lives in the LayoutActions singleton, not here: the built-in action
    // widgets whose gestures a user can override need the same switch, and they
    // cannot reach a function that lives on this component.
    //
    // The gesture handlers below go through LayoutActions.runGestureOrReserved, the
    // same entry point the ten built-ins use — one read path for all eleven widget
    // types. "custom" reserves no destination, so for this widget it is exactly
    // "run the stored action if there is one", which is what it always did.
    function executeActionString(actionStr) {
        LayoutActions.execute(actionStr, { idlePage: root.idlePage })
    }

    function _runGesture(gestureKey) {
        LayoutActions.runGestureOrReserved(root.modelData, gestureKey, root.modelData.type || "custom",
                                           { idlePage: root.idlePage })
    }

    // The malformed-HTML path below is the one diagnostic this file still owns.
    function _warn(msg) {
        console.warn("CustomItem: " + msg)
    }

    // --- COMPACT MODE (bar rendering) ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth + (root.hasAction || root.bgColor !== "" ? Theme.scaled(16) : 0)
        implicitHeight: Theme.bottomBarHeight

        Rectangle {
            visible: !root.hideBackground && (root.hasAction || root.bgColor !== "")
            anchors.fill: parent
            anchors.topMargin: Theme.spacingSmall
            anchors.bottomMargin: Theme.spacingSmall
            color: compactTap.isPressed ? Qt.darker(root._effectiveBackground, 1.2) : root._effectiveBackground
            radius: Theme.cardRadius
            opacity: root.hasAction && typeof DE1Device !== "undefined" && DE1Device !== null && !DE1Device.guiEnabled ? 0.5 : 1.0
            // A hairline edge whenever the page is a flat preset colour. Fill contrast
            // alone is not enough there: the tile is a lifted shade of the very colour
            // behind it, so on the lighter colours it reads as a smudge rather than a
            // button. Over an image the scrim and the photo already give the edge away.
            border.width: root.isActive ? Theme.scaled(3) : (Theme.hasBackgroundPreset ? 1 : 0)
            border.color: root.isActive ? root._activeRingColor : Theme.borderColor
        }

        RowLayout {
            id: compactRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            // Emoji/icon in compact mode
            Image {
                visible: root.hasEmoji
                source: visible ? Theme.emojiToImage(root.emoji) : ""
                sourceSize.width: Theme.scaled(28)
                sourceSize.height: Theme.scaled(28)
                Layout.alignment: Qt.AlignVCenter
                Accessible.ignored: true
            }

            Text {
                text: root.resolvedText
                // RichText, because the editor saves per-range styling as CSS spans
                // (`<span style="color:…; font-size:…px">`, documentformatter.cpp:400-411) and
                // StyledText has no `<span>` handler and never reads a
                // `style=` attribute. The only tag whose attributes reach the character format
                // is `<font>` (qquickstyledtext.cpp:421-422); `<a>`, `<img>`, `<ol>` and `<ul>`
                // attributes are parsed too but carry no styling — and `<img>` is why emoji
                // (Theme.replaceEmojiWithImg) rendered correctly here all along.
                // So the span was dropped and every custom widget rendered at the default
                // colour and size no matter what was saved.
                //
                // Qt ignores BOTH `elide` and `maximumLineCount` on RichText — the rich path
                // in QQuickTextPrivate::updateSize() never reaches setupTextLayout(), which is
                // the only place either is consulted. So neither is declared here: a property
                // that provably does nothing is the thing this branch is removing elsewhere.
                // `clip` bounds the paint instead, and content with a line break grows the row
                // rather than being clamped — the trade for rendering the styling the editor
                // promises.
                textFormat: Text.RichText
                clip: true
                color: Theme.textColor
                font: Theme.bodyFont
                horizontalAlignment: root.qtAlignment
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            id: compactTap
            anchors.fill: parent
            accessibleName: Theme.toAccessibleText(root.resolvedText) + (root.isActive ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
            accessibleDescription: root._accessibleHint
            supportLongPress: root.longPressAction !== ""
            supportDoubleClick: true
            onAccessibleClicked: root._runGesture("action")
            onAccessibleLongPressed: root._runGesture("longPressAction")
            onAccessibleDoubleClicked: root._runGesture("doubleclickAction")
        }
    }

    // --- FULL MODE (center rendering) ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: root.hasEmoji ? Math.max(Theme.scaled(150), emojiText.implicitWidth + Theme.scaled(24)) : (fullText.implicitWidth + Theme.scaled(16) + (root.hasAction ? Theme.scaled(16) : 0))
        implicitHeight: root.hasEmoji ? Theme.scaled(120) : (fullText.implicitHeight + Theme.scaled(16) + (root.hasAction ? Theme.scaled(8) : 0))

        Rectangle {
            id: fullBgRect
            visible: !root.hideBackground && (root.hasAction || root.hasEmoji)
            anchors.fill: parent
            color: fullTap.isPressed ? Qt.darker(root._effectiveBackground, 1.2) : root._effectiveBackground
            radius: Theme.cardRadius
            opacity: root.hasAction && typeof DE1Device !== "undefined" && DE1Device !== null && !DE1Device.guiEnabled ? 0.5 : 1.0
            // A hairline edge whenever the page is a flat preset colour. Fill contrast
            // alone is not enough there: the tile is a lifted shade of the very colour
            // behind it, so on the lighter colours it reads as a smudge rather than a
            // button. Over an image the scrim and the photo already give the edge away.
            border.width: root.isActive ? Theme.scaled(3) : (Theme.hasBackgroundPreset ? 1 : 0)
            border.color: root.isActive ? root._activeRingColor : Theme.borderColor
        }

        // Layout with emoji: icon above text (like ActionButton)
        Column {
            visible: root.hasEmoji
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            // Emoji/icon
            Image {
                source: Theme.emojiToImage(root.emoji)
                sourceSize.width: Theme.scaled(48)
                sourceSize.height: Theme.scaled(48)
                anchors.horizontalCenter: parent.horizontalCenter
                opacity: root.hasAction && typeof DE1Device !== "undefined" && DE1Device !== null && !DE1Device.guiEnabled ? 0.5 : 1.0
                Accessible.ignored: true
                // Tint SVG icons to match text color in both modes
                layer.enabled: root.emojiIsSvg
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: root._contentColor
                }
            }

            Text {
                id: emojiText
                text: root.resolvedText
                // RichText — see the compact-mode Text above. Neither of these two centre
                // renderings elides, so nothing is given up here.
                textFormat: Text.RichText
                color: root._contentColor
                font: Theme.bodyFont
                horizontalAlignment: Text.AlignHCenter
                anchors.horizontalCenter: parent.horizontalCenter
                Accessible.ignored: true
            }
        }

        // Layout without emoji: text only (original behavior)
        Text {
            id: fullText
            visible: !root.hasEmoji
            anchors.centerIn: parent
            width: Math.max(0, parent.width - (root.hasAction ? Theme.scaled(24) : 0))
            text: root.resolvedText
            textFormat: Text.RichText
            color: Theme.textColor
            font: Theme.bodyFont
            horizontalAlignment: root.qtAlignment
            wrapMode: Text.Wrap
            Accessible.ignored: true
        }

        AccessibleTapHandler {
            id: fullTap
            anchors.fill: parent
            accessibleName: Theme.toAccessibleText(root.resolvedText) + (root.isActive ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
            accessibleDescription: root._accessibleHint
            supportLongPress: root.longPressAction !== ""
            supportDoubleClick: true
            onAccessibleClicked: root._runGesture("action")
            onAccessibleLongPressed: root._runGesture("longPressAction")
            onAccessibleDoubleClicked: root._runGesture("doubleclickAction")
        }

    }
}
