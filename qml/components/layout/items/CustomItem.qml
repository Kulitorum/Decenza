import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import DecenzaDE1
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

    readonly property int qtAlignment: {
        switch (textAlign) {
            case "left": return Text.AlignLeft
            case "right": return Text.AlignRight
            default: return Text.AlignHCenter
        }
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // Trigger counter to force re-evaluation of resolvedText
    property int _refreshTick: 0

    // Variable substitution - explicit dependency references so QML tracks changes
    readonly property string resolvedText: {
        // Reference dependencies to establish QML bindings
        var _tick = _refreshTick
        var _c = content
        if (typeof DE1Device !== "undefined") {
            void(DE1Device.temperature); void(DE1Device.steamTemperature)
            void(DE1Device.pressure); void(DE1Device.flow)
            void(DE1Device.waterLevel); void(DE1Device.waterLevelMl)
            void(DE1Device.stateString)
        }
        if (typeof MachineState !== "undefined") {
            void(MachineState.scaleWeight); void(MachineState.shotTime)
            void(MachineState.cumulativeVolume)
            void(MachineState.preinfusionVolume); void(MachineState.pourVolume)
        }
        if (typeof MainController !== "undefined") {
            void(MainController.targetWeight); void(MainController.currentProfileName)
            void(MainController.profileTargetTemperature)
            void(MainController.brewByRatio); void(MainController.brewByRatioDose)
        }
        if (typeof DE1Device !== "undefined") void(DE1Device.connected)
        if (typeof ScaleDevice !== "undefined" && ScaleDevice) { void(ScaleDevice.name); void(ScaleDevice.connected) }
        if (typeof Settings !== "undefined") { void(Settings.dyeGrinderSetting); void(Settings.dyeGrinderModel) }
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
        // Connection status
        var machineOn = typeof DE1Device !== "undefined" && DE1Device.connected
        var scaleOn = typeof ScaleDevice !== "undefined" && ScaleDevice && ScaleDevice.connected
        var flowScale = typeof ScaleDevice !== "undefined" && ScaleDevice && ScaleDevice.name === "Flow Scale"
        result = result.replace(/%CONNECTED%/g, machineOn ? "Online" : "Offline")
        if (result.indexOf("%CONNECTED_COLOR%") >= 0)
            result = result.replace(/%CONNECTED_COLOR%/g, machineOn ? Theme.successColor : Theme.errorColor)
        if (machineOn && scaleOn && !flowScale)
            result = result.replace(/%DEVICES%/g, "Machine + Scale")
        else if (machineOn && flowScale)
            result = result.replace(/%DEVICES%/g, "Machine + Simulated Scale")
        else
            result = result.replace(/%DEVICES%/g, "Machine")
        // Time
        var now = new Date()
        result = result.replace(/%TIME%/g, Qt.formatTime(now, "hh:mm"))
        result = result.replace(/%DATE%/g, Qt.formatDate(now, "yyyy-MM-dd"))
        return result
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
                "beaninfo": "BeanInfoPage.qml"
            }
            var page = pageMap[target]
            if (page && typeof pageStack !== "undefined") {
                pageStack.push(Qt.resolvedUrl("../../../pages/" + page))
            }
        } else if (category === "command") {
            switch (target) {
                case "sleep":
                    if (typeof DE1Device !== "undefined" && DE1Device.guiEnabled) {
                        if (typeof ScaleDevice !== "undefined" && ScaleDevice.connected)
                            ScaleDevice.disableLcd()
                        DE1Device.goToSleep()
                        var win = Window.window
                        if (win && typeof win.goToScreensaver === "function")
                            win.goToScreensaver()
                    }
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
                case "quit":
                    Qt.quit()
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
                color: (!root.hideBackground && (root.hasAction || root.bgColor !== "")) ? "white" : Theme.textColor
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
            accessibleName: root.resolvedText
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
            color: {
                var base = root.bgColor || (root.hasAction ? "#555555" : Theme.surfaceColor)
                return fullTap.isPressed ? Qt.darker(base, 1.2) : base
            }
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
            }

            Text {
                id: emojiText
                text: root.resolvedText
                textFormat: Text.RichText
                color: (!root.hideBackground && (root.hasAction || root.hasEmoji)) ? "white" : Theme.textColor
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
            accessibleName: root.resolvedText
            supportLongPress: root.longPressAction !== ""
            supportDoubleClick: root.doubleclickAction !== ""
            onAccessibleClicked: root.executeActionString(root.action)
            onAccessibleLongPressed: root.executeActionString(root.longPressAction)
            onAccessibleDoubleClicked: root.executeActionString(root.doubleclickAction)
        }
    }
}
