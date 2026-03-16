import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

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

    readonly property color _parsedBgColor: bgColor !== "" ? bgColor : (hasAction ? "#555555" : Theme.surfaceColor)
    readonly property color _effectiveBackground: _parsedBgColor
    // Content color for text and icon tinting on the button background
    readonly property color _contentColor: Theme.primaryContrastColor

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

    // Variable substitution - only tracks the live properties this item actually uses.
    // Items showing static values (e.g. %PROFILE%) no longer re-evaluate at 5 Hz.
    readonly property string resolvedText: {
        var _c = content  // direct dependency so content changes always trigger re-evaluation
        var _tick = _refreshTick
        if (_needsMachineData && typeof DE1Device !== "undefined") {
            void(DE1Device.temperature); void(DE1Device.steamTemperature)
            void(DE1Device.pressure); void(DE1Device.flow)
            void(DE1Device.waterLevel); void(DE1Device.waterLevelMl)
            void(DE1Device.stateString); void(DE1Device.connected)
        }
        if (_needsScaleData && typeof MachineState !== "undefined") {
            void(MachineState.scaleWeight); void(MachineState.shotTime)
            void(MachineState.cumulativeVolume)
            void(MachineState.preinfusionVolume); void(MachineState.pourVolume)
        }
        if ((_needsMachineData || _needsScaleData) && typeof MachineState !== "undefined") {
            void(MachineState.phase)
        }
        if (_needsControllerData && typeof MainController !== "undefined") {
            void(MainController.targetWeight); void(MainController.currentProfileName)
            void(MainController.profileTargetTemperature)
            void(MainController.brewByRatio); void(MainController.brewByRatioDose)
        }
        if (_needsScaleDevice && typeof ScaleDevice !== "undefined" && ScaleDevice) {
            void(ScaleDevice.name); void(ScaleDevice.connected)
        }
        if (_needsSettingsData && typeof Settings !== "undefined") {
            void(Settings.dyeGrinderSetting); void(Settings.dyeGrinderModel)
        }
        return substituteVariables(_c)
    }

    // Timer to update time/date variables (and any other periodic refresh)
    Timer {
        id: refreshTimer
        interval: 1000
        running: content.indexOf("%TIME%") >= 0 || content.indexOf("%DATE%") >= 0
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
                    console.warn("[CustomItem] Malformed HTML detected, stripping tags:", html.substring(0, 80))
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
        result = result.replace(/%TEMP%/g, typeof DE1Device !== "undefined" ? DE1Device.temperature.toFixed(1) : "—")
        result = result.replace(/%STEAM_TEMP%/g, typeof DE1Device !== "undefined" ? DE1Device.steamTemperature.toFixed(0) + "\u00B0" : "—")
        result = result.replace(/%PRESSURE%/g, typeof DE1Device !== "undefined" ? DE1Device.pressure.toFixed(1) : "—")
        result = result.replace(/%FLOW%/g, typeof DE1Device !== "undefined" ? DE1Device.flow.toFixed(1) : "—")
        result = result.replace(/%WATER%/g, typeof DE1Device !== "undefined" ? DE1Device.waterLevel.toFixed(0) : "—")
        result = result.replace(/%WATER_ML%/g, typeof DE1Device !== "undefined" ? DE1Device.waterLevelMl.toFixed(0) : "—")
        result = result.replace(/%STATE%/g, typeof DE1Device !== "undefined" ? DE1Device.stateString : "—")
        // Scale / Shot
        result = result.replace(/%WEIGHT%/g, typeof MachineState !== "undefined" ? MachineState.scaleWeight.toFixed(1) : "—")
        result = result.replace(/%SHOT_TIME%/g, typeof MachineState !== "undefined" ? MachineState.shotTime.toFixed(1) : "—")
        result = result.replace(/%VOLUME%/g, typeof MachineState !== "undefined" ? MachineState.cumulativeVolume.toFixed(0) : "—")
        result = result.replace(/%POUR_VOLUME%/g, typeof MachineState !== "undefined" ? MachineState.pourVolume.toFixed(0) : "—")
        result = result.replace(/%PREINFUSION_VOLUME%/g, typeof MachineState !== "undefined" ? MachineState.preinfusionVolume.toFixed(0) : "—")
        // Controller
        result = result.replace(/%TARGET_WEIGHT%/g, typeof MainController !== "undefined" ? MainController.targetWeight.toFixed(1) : "—")
        result = result.replace(/%PROFILE%/g, typeof MainController !== "undefined" ? MainController.currentProfileName : "—")
        result = result.replace(/%TARGET_TEMP%/g, typeof MainController !== "undefined" ? MainController.profileTargetTemperature.toFixed(1) : "—")
        result = result.replace(/%RATIO%/g, typeof MainController !== "undefined" ? MainController.brewByRatio.toFixed(1) : "—")
        result = result.replace(/%DOSE%/g, typeof MainController !== "undefined" ? MainController.brewByRatioDose.toFixed(1) : "—")
        // Scale device
        result = result.replace(/%SCALE%/g, typeof ScaleDevice !== "undefined" && ScaleDevice ? ScaleDevice.name : "—")
        // Grinder
        result = result.replace(/%GRIND%/g, typeof Settings !== "undefined" && Settings.dyeGrinderSetting ? Settings.dyeGrinderSetting : "—")
        result = result.replace(/%GRINDER%/g, typeof Settings !== "undefined" && Settings.dyeGrinderModel ? Settings.dyeGrinderModel : "—")
        // Machine ready status
        var machineReady = typeof MachineState !== "undefined" && MachineState.isReady
        result = result.replace(/%MACHINE_READY%/g, machineReady ? TranslationManager.translate("customitem.status.ready", "Ready") : TranslationManager.translate("customitem.status.notReady", "Not ready"))
        if (result.indexOf("%MACHINE_READY_COLOR%") >= 0)
            result = result.replace(/%MACHINE_READY_COLOR%/g, machineReady ? Theme.successColor : Theme.errorColor)
        // Connection status
        var machineOn = typeof DE1Device !== "undefined" && DE1Device.connected
        var scaleOn = typeof ScaleDevice !== "undefined" && ScaleDevice && ScaleDevice.connected
        var flowScale = typeof ScaleDevice !== "undefined" && ScaleDevice && ScaleDevice.isFlowScale
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
            return "<img src=\"" + src + "\" width=\"" + statusIconSize + "\" height=\"" + statusIconSize + "\" style=\"vertical-align: middle\">"
        }
        if (result.indexOf("%MACHINE_CONNECTED%") >= 0)
            result = result.replace(/%MACHINE_CONNECTED%/g,
                statusImg(machineOn ? statusConnected : statusDisconnected))
        if (result.indexOf("%SCALE_CONNECTED%") >= 0)
            result = result.replace(/%SCALE_CONNECTED%/g,
                statusImg((scaleOn && !flowScale) ? statusConnected : statusDisconnected))
        // Time
        var now = new Date()
        result = result.replace(/%TIME%/g, Qt.formatTime(now, Settings.use12HourTime ? "h:mmap" : "hh:mm"))
        result = result.replace(/%DATE%/g, Qt.formatDate(now, "yyyy-MM-dd"))
        // Convert any emoji Unicode in the result to <img> tags to avoid
        // CoreText/ImageIO crash from Apple Color Emoji PNG decoding on render thread
        return Theme.replaceEmojiWithImg(result, Theme.bodyFont.pixelSize)
    }

    function executeActionString(actionStr) {
        if (!actionStr) return
        var parts = actionStr.split(":")
        if (parts.length < 2) return
        var category = parts[0]
        var target = parts.slice(1).join(":")

        if (category === "togglePreset") {
            // Walk parent chain to find IdlePage (same pattern as EspressoItem)
            var p = root.parent
            while (p) {
                if (p.objectName === "idlePage") break
                p = p.parent
            }
            if (p && typeof p.activePresetFunction !== "undefined") {
                p.activePresetFunction = (p.activePresetFunction === target) ? "" : target
            }
        } else if (category === "navigate") {
            var pageMap = {
                "settings": "SettingsPage.qml",
                "history": "ShotHistoryPage.qml",
                "profiles": "ProfileSelectorPage.qml",
                "profileEditor": "ProfileEditorPage.qml",
                "recipes": "RecipeEditorPage.qml",
                "descaling": "DescalingPage.qml",
                "ai": "AISettingsPage.qml",
                "visualizer": "VisualizerBrowserPage.qml",
                "autofavorites": "AutoFavoritesPage.qml",
                "steam": "SteamPage.qml",
                "hotwater": "HotWaterPage.qml",
                "flush": "FlushPage.qml",
                "beaninfo": "BeanInfoPage.qml",
                "espresso": "EspressoPage.qml",
                "community": "CommunityBrowserPage.qml",
                "flowCalibration": "FlowCalibrationPage.qml",
                "profileImport": "ProfileImportPage.qml"
            }
            if (target === "shotReview") {
                var shotId = MainController.lastSavedShotId
                if (shotId > 0 && typeof pageStack !== "undefined")
                    pageStack.push(Qt.resolvedUrl("../../../pages/PostShotReviewPage.qml"), { editShotId: shotId })
                return
            }
            // Operation pages use replace (consistent with main.qml phase handler)
            var operationPages = ["espresso", "steam", "hotwater", "flush"]
            var page = pageMap[target]
            if (page && typeof pageStack !== "undefined") {
                if (operationPages.indexOf(target) >= 0)
                    pageStack.replace(null, Qt.resolvedUrl("../../../pages/" + page))
                else
                    pageStack.push(Qt.resolvedUrl("../../../pages/" + page))
            }
        } else if (category === "command") {
            switch (target) {
                case "sleep":
                    if (typeof ScaleDevice !== "undefined" && ScaleDevice && ScaleDevice.connected)
                        ScaleDevice.disableLcd()
                    if (typeof DE1Device !== "undefined")
                        DE1Device.goToSleep()
                    var win = Window.window
                    if (win && typeof win.goToScreensaver === "function")
                        win.goToScreensaver()
                    break
                case "startEspresso":
                    if (typeof DE1Device !== "undefined" && DE1Device.guiEnabled)
                        DE1Device.startEspresso()
                    break
                case "startSteam":
                    if (typeof DE1Device !== "undefined" && DE1Device.guiEnabled)
                        DE1Device.startSteam()
                    break
                case "startHotWater":
                    if (typeof DE1Device !== "undefined" && DE1Device.guiEnabled)
                        DE1Device.startHotWater()
                    break
                case "startFlush":
                    if (typeof DE1Device !== "undefined" && DE1Device.guiEnabled)
                        DE1Device.startFlush()
                    break
                case "idle":
                    if (typeof DE1Device !== "undefined")
                        DE1Device.requestIdle()
                    break
                case "tare":
                    if (typeof MachineState !== "undefined")
                        MachineState.tareScale()
                    break
                case "scanDE1":
                    if (typeof BLEManager !== "undefined")
                        BLEManager.startScan()
                    break
                case "scanScale":
                    if (typeof BLEManager !== "undefined")
                        BLEManager.scanForScales()
                    break
                case "brewSettings":
                    var bp = root.parent
                    while (bp) {
                        if (bp.objectName === "idlePage") break
                        bp = bp.parent
                    }
                    if (bp && bp.idleBrewDialog) bp.idleBrewDialog.open()
                    break
                case "toggleCharging":
                    if (typeof BatteryManager !== "undefined")
                        BatteryManager.chargingMode = (BatteryManager.chargingMode + 1) % 3
                    break
                case "tempToggleSteam":
                    if (typeof Settings !== "undefined" && typeof MainController !== "undefined") {
                        if (Settings.steamDisabled)
                            MainController.startSteamHeating()
                        else
                            MainController.turnOffSteamHeater()
                    }
                    break
                case "uploadVisualizer":
                    var lastId = MainController.lastSavedShotId
                    if (lastId > 0) {
                        var handler = function(shotId, data) {
                            if (shotId !== lastId) return
                            MainController.shotHistory.shotReady.disconnect(handler)
                            MainController.visualizer.uploadShotFromHistory(data)
                        }
                        MainController.shotHistory.shotReady.connect(handler)
                        MainController.shotHistory.requestShot(lastId)
                    }
                    break
                case "disconnectDE1":
                    if (typeof DE1Device !== "undefined")
                        DE1Device.disconnect()
                    break
                case "previousProfile":
                    var prevName = MainController.previousProfileName()
                    if (prevName)
                        MainController.loadProfile(prevName)
                    break
                case "quit":
                    Qt.quit()
                    break
                default:
                    // Handle parameterized commands like loadProfile:<name>
                    if (target.indexOf("loadProfile:") === 0) {
                        var profileName = target.substring("loadProfile:".length)
                        if (profileName)
                            MainController.loadProfile(profileName)
                    }
                    break
            }
        }
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
            color: {
                var base = root.bgColor || "#555555"
                return compactTap.isPressed ? Qt.darker(base, 1.2) : base
            }
            radius: Theme.cardRadius
            opacity: root.hasAction && typeof DE1Device !== "undefined" && !DE1Device.guiEnabled ? 0.5 : 1.0
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
                textFormat: Text.RichText
                color: Theme.textColor
                font: Theme.bodyFont
                horizontalAlignment: root.qtAlignment
                elide: Text.ElideRight
                maximumLineCount: 1
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            id: compactTap
            anchors.fill: parent
            accessibleName: Theme.stripEmoji(root.resolvedText)
            supportLongPress: root.longPressAction !== ""
            supportDoubleClick: root.doubleclickAction !== ""
            onAccessibleClicked: root.executeActionString(root.action)
            onAccessibleLongPressed: root.executeActionString(root.longPressAction)
            onAccessibleDoubleClicked: root.executeActionString(root.doubleclickAction)
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
            visible: !root.hideBackground && (root.hasAction || root.hasEmoji)
            anchors.fill: parent
            color: fullTap.isPressed ? Qt.darker(root._effectiveBackground, 1.2) : root._effectiveBackground
            radius: Theme.cardRadius
            opacity: root.hasAction && typeof DE1Device !== "undefined" && !DE1Device.guiEnabled ? 0.5 : 1.0
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
                opacity: root.hasAction && typeof DE1Device !== "undefined" && !DE1Device.guiEnabled ? 0.5 : 1.0
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
            accessibleName: Theme.stripEmoji(root.resolvedText)
            supportLongPress: root.longPressAction !== ""
            supportDoubleClick: root.doubleclickAction !== ""
            onAccessibleClicked: root.executeActionString(root.action)
            onAccessibleLongPressed: root.executeActionString(root.longPressAction)
            onAccessibleDoubleClicked: root.executeActionString(root.doubleclickAction)
        }

    }
}
