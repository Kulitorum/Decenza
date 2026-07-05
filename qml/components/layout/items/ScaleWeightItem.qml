import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    // Per-instance data mode (composable-brew-bar): "" / "gross" (raw weight,
    // default), "netBeans" (minus dose-cup tare), "netMilk" (minus pitcher
    // weight), "contextAware" (net milk while steaming, else net beans),
    // "beansHold" (live net beans, then hold the captured dose after the beep),
    // "expectedYield" (the active stop-at-weight target = ProfileManager.targetWeight: the
    //   brew-yield override when set — which equals dose × ratio in brew-by-ratio mode — else
    //   the profile's fixed target weight. Purely computed, so shown even without a scale).
    readonly property string dataMode: (modelData && modelData.dataMode) ? modelData.dataMode : ""

    // Per-instance display mode (composable-status-bar): "text" (default, a
    // connected-dot + value) or "icon" (a scale icon ahead of the value).
    readonly property string displayMode: (modelData && modelData.displayMode) ? modelData.displayMode : "text"

    // Per-instance "show ratio" toggle: when brew-by-ratio is active the weight
    // is suffixed with "1:X.X". Defaults to true (today's behaviour); set false
    // to suppress where the ratio is already shown elsewhere (ratio pill / status
    // bar).
    readonly property bool showRatio: (modelData && modelData.showRatio !== undefined) ? modelData.showRatio : true

    function _pitcherWeight() {
        var p = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
        return (p && !p.disabled) ? (p.pitcherWeightG ?? 0) : 0
    }

    // "beansHold": show LIVE net beans while dosing, then freeze the captured
    // dose once a stable capture has set Settings.dye.dyeBeanWeight, holding it
    // until the cup is lifted (scale returns to ~0), then go live again for the
    // next dose. Event-based — no timers.
    property real _heldDose: 0
    // Latched true when a cup sits on the scale during an operation, cleared when the
    // cup is lifted — suppresses a post-shot brew cup reading as live beans in Idle.
    property bool _postExtraction: false
    Connections {
        target: Settings.dye
        function onDyeBeanWeightChanged() {
            if (root.dataMode === "beansHold" && Settings.dye.dyeBeanWeight > 0
                    && MachineState.scaleWeight > Settings.brew.doseCupTareWeight)
                root._heldDose = Settings.dye.dyeBeanWeight
        }
    }
    Connections {
        target: MachineState
        function onScaleWeightChanged() {
            if (root.dataMode !== "beansHold") return
            if (MachineState.scaleWeight < 1.0) {
                root._heldDose = 0            // cup lifted -> next dose reads live
                root._postExtraction = false  // and clear the post-operation latch
            } else if (root._operating) {
                root._postExtraction = true   // cup on scale during an op -> latch until lifted
            }
        }
    }

    // Apply the per-instance data mode to the raw scale reading.
    // Operating phases where a live net-beans reading would be a phantom (the scale is
    // measuring the brew cup / milk / water, not beans). Mirrors DoseWeightItem._operating.
    readonly property bool _operating:
        MachineState.phase === MachineStateType.Phase.EspressoPreheating
        || MachineState.phase === MachineStateType.Phase.Preinfusion
        || MachineState.phase === MachineStateType.Phase.Pouring
        || MachineState.phase === MachineStateType.Phase.Ending
        || MachineState.phase === MachineStateType.Phase.Steaming
        || MachineState.phase === MachineStateType.Phase.HotWater
        || MachineState.phase === MachineStateType.Phase.Flushing

    function displayedWeight() {
        var w = MachineState.scaleWeight
        if (root.dataMode === "netBeans")
            return Math.max(0, w - Settings.brew.doseCupTareWeight)
        if (root.dataMode === "netMilk")
            return Math.max(0, w - root._pitcherWeight())
        if (root.dataMode === "contextAware") {
            var steaming = MachineState.phase === MachineStateType.Phase.Steaming
            return Math.max(0, w - (steaming ? root._pitcherWeight() : Settings.brew.doseCupTareWeight))
        }
        if (root.dataMode === "beansHold") {
            // Mirrors DoseWeightItem's live→hold→recorded chain: the frozen captured
            // dose if we have one this cycle; else the live net beans while actually
            // dosing; else (mid/post-operation, cup off, no saved tare, or an implausible
            // net) the last recorded dose — never the phantom/gross live reading.
            if (root._heldDose > 0) return root._heldDose
            var live = (root._operating || root._postExtraction || Settings.brew.doseCupTareWeight <= 0)
                       ? 0 : Math.max(0, w - Settings.brew.doseCupTareWeight)
            // net > 55 g is a brew cup, not beans; fall through to the recorded dose.
            return (live > 0.3 && live <= 55) ? live : Settings.dye.dyeBeanWeight
        }
        if (root.dataMode === "expectedYield") {
            // The active stop-at-weight target (ProfileManager.targetWeight): the brew-yield
            // override when one is set — which equals dose × ratio in brew-by-ratio mode —
            // else the profile's fixed target weight. Purely computed; needs no scale.
            return ProfileManager.targetWeight
        }
        return w
    }

    property bool isFlowScale: ScaleDevice && ScaleDevice.isFlowScale
    property bool scaleConnected: ScaleDevice && ScaleDevice.connected
    property bool accessibilityEnabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

    // Open the single global Brew Settings dialog (hosted at the app root) via the
    // window, so this works wherever the tile is placed — including the persistent
    // status bar, which is not a descendant of IdlePage.
    function openBrewSettings() {
        var win = root.Window.window
        if (win && win.openBrewSettings) win.openBrewSettings()
    }

    // Scale warning: saved BLE scale not connected or connection failed, or app fell back to simulated scale
    // Don't warn if a USB scale is connected — it satisfies the "have a real scale" requirement (not available on iOS)
    // "expectedYield" is a purely computed target — it never needs a scale, so never warn for it.
    property bool showScaleWarning: root.dataMode !== "expectedYield"
        && (!root.scaleConnected || root.isFlowScale)
        && (BLEManager.scaleConnectionFailed || Settings.primaryScaleAddress !== "")
        && (Qt.platform.os === "ios" || !UsbScaleManager.scaleConnected)

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // Accessibility: expose weight/status to screen readers
    // Reactive computed property so TalkBack re-announces when scale state or weight changes.
    readonly property string _accessibleName: {
        if (root.showScaleWarning)
            return BLEManager.scaleConnectionFailed
                ? TranslationManager.translate("statusbar.scale_not_found_tap", "Scale not found. Tap to scan")
                : TranslationManager.translate("statusbar.scale_connecting", "Scale connecting")
        if (root.scaleConnected)
            return TranslationManager.translate("idle.accessible.scale.weight", "Scale weight:") + " " + root.weightText() + ". " + TranslationManager.translate("idle.accessible.scale.tare", "Tap to tare")
        return TranslationManager.translate("idle.accessible.scale.none", "No scale connected")
    }

    Accessible.role: Accessible.Button
    Accessible.name: root._accessibleName
    // Long-press for brew settings is only available in compact mode (scaleMouseArea).
    // Long-press is also gated on accessibilityEnabled — safe to advertise to screen reader users.
    Accessible.description: root.isCompact && root.scaleConnected
        ? TranslationManager.translate("idle.accessible.scale.hint", "Long-press for brew settings.")
        : ""
    Accessible.focusable: true
    Accessible.onPressAction: {
        if (root.showScaleWarning)
            BLEManager.scanForDevices()
        else if (root.scaleConnected)
            MachineState.tareScale()
    }

    // Per-instance color override; "default"/unset keeps the dynamic state color
    // below (tap feedback, ratio/flow-scale state). A named choice forces a
    // static tint over all of it.
    readonly property string colorChoice: (modelData && modelData.color) ? modelData.color : "default"

    // Shared color logic
    function scaleColor(pressed) {
        var natural
        if (pressed) natural = Theme.accentColor
        else if (ProfileManager.brewByRatioActive) natural = Theme.weightColor
        else if (root.isFlowScale) natural = Theme.textSecondaryColor
        else natural = Theme.weightColor
        return WidgetColor.resolve(root.colorChoice, natural)
    }

    function weightText() {
        var weight = root.displayedWeight().toFixed(1)
        var suffix = (root.isFlowScale && root.dataMode !== "expectedYield") ? "g~" : "g"
        if (root.showRatio && ProfileManager.brewByRatioActive) {
            return weight + suffix + " 1:" + ProfileManager.brewByRatio.toFixed(1)
        }
        return weight + suffix
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactWarning.visible ? compactWarning.width : compactScaleRow.implicitWidth
        implicitHeight: compactWarning.visible ? compactWarning.implicitHeight : compactScaleRow.implicitHeight

        // Scale warning (connecting / not found / simulated fallback) - tap to scan
        Rectangle {
            id: compactWarning
            visible: root.showScaleWarning
            Accessible.ignored: true
            anchors.centerIn: parent
            width: compactWarningRow.implicitWidth + Theme.spacingMedium
            height: Theme.touchTargetMin
            color: BLEManager.scaleConnectionFailed ? Theme.errorColor : "transparent"
            radius: Theme.scaled(4)

            Row {
                id: compactWarningRow
                anchors.centerIn: parent
                spacing: Theme.spacingSmall

                Tr {
                    key: "statusbar.scale_not_found"
                    fallback: "Scale not found"
                    visible: BLEManager.scaleConnectionFailed
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }

                Tr {
                    key: "statusbar.scale_ellipsis"
                    fallback: "Scale..."
                    visible: !BLEManager.scaleConnectionFailed
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: BLEManager.scanForDevices()
            }
        }

        // Scale connected: weight display with tare/ratio interaction
        Row {
            id: compactScaleRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall
            visible: (root.scaleConnected || root.dataMode === "expectedYield") && !root.showScaleWarning

            ThemedIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.displayMode === "icon"
                source: "qrc:/icons/scale.svg"
                iconSize: Theme.scaled(18)
                color: root.scaleColor(scaleMouseArea.pressed)
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.displayMode !== "icon"
                width: Theme.scaled(8)
                height: Theme.scaled(8)
                radius: Theme.scaled(4)
                color: root.scaleColor(scaleMouseArea.pressed)
            }

            Text {
                text: root.weightText()
                color: root.scaleColor(scaleMouseArea.pressed)
                font: Theme.bodyFont
                Accessible.ignored: true
            }
        }

        // Disconnected (no saved scale)
        Text {
            anchors.centerIn: parent
            // expectedYield is a computed target that renders without a scale, so don't also show "--".
            visible: !root.scaleConnected && !root.showScaleWarning && !root.isFlowScale
                     && root.dataMode !== "expectedYield"
            text: "--"
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
            Accessible.ignored: true
        }

        // Tare / ratio interaction overlay
        MouseArea {
            id: scaleMouseArea
            anchors.fill: parent
            anchors.margins: -Theme.spacingSmall
            visible: (root.scaleConnected || root.dataMode === "expectedYield") && !root.showScaleWarning
            cursorShape: Qt.PointingHandCursor

            property int tapCount: 0
            property var lastTapTime: 0
            property bool longPressTriggered: false

            onPressed: {
                longPressTriggered = false
                if (root.accessibilityEnabled && !MachineState.isFlowing) {
                    longPressTimer.start()
                }
            }

            onReleased: {
                longPressTimer.stop()
                if (longPressTriggered) {
                    longPressTriggered = false
                    return
                }
            }

            onCanceled: {
                longPressTimer.stop()
                longPressTriggered = false
            }

            onClicked: {
                if (longPressTriggered) return

                if (MachineState.isFlowing) {
                    MachineState.tareScale()
                    return
                }

                var now = Date.now()
                var timeSinceLast = now - lastTapTime

                if (timeSinceLast < 300) {
                    tapCount++
                } else {
                    tapCount = 1
                }
                lastTapTime = now

                if (tapCount >= 2) {
                    tapCount = 0
                    singleTapTimer.stop()
                    root.openBrewSettings()
                } else {
                    singleTapTimer.restart()
                }
            }
        }

        Timer {
            id: longPressTimer
            interval: 600
            onTriggered: {
                scaleMouseArea.longPressTriggered = true
                root.openBrewSettings()
            }
        }

        Timer {
            id: singleTapTimer
            interval: 300
            onTriggered: {
                if (scaleMouseArea.tapCount === 1) {
                    MachineState.tareScale()
                }
                scaleMouseArea.tapCount = 0
            }
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: fullColumn.implicitWidth
        implicitHeight: fullColumn.implicitHeight

        ColumnLayout {
            id: fullColumn
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            ThemedIcon {
                Layout.alignment: Qt.AlignHCenter
                visible: root.displayMode === "icon"
                source: "qrc:/icons/scale.svg"
                iconSize: Theme.scaled(26)
                color: root.scaleColor(fullTapArea.pressed)
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: (root.scaleConnected || root.dataMode === "expectedYield") && !root.showScaleWarning
                text: root.weightText()
                color: root.scaleColor(fullTapArea.pressed)
                font: Theme.valueFont
                Accessible.ignored: true
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: !root.scaleConnected && !root.showScaleWarning
                         && root.dataMode !== "expectedYield"
                text: "--"
                color: Theme.textSecondaryColor
                font: Theme.valueFont
                Accessible.ignored: true
            }

            Tr {
                Layout.alignment: Qt.AlignHCenter
                visible: root.showScaleWarning && BLEManager.scaleConnectionFailed
                key: "statusbar.scale_not_found_tap"
                fallback: "Scale not found. Tap to scan"
                color: Theme.errorColor
                font: Theme.labelFont
                Accessible.ignored: true
            }

            Tr {
                Layout.alignment: Qt.AlignHCenter
                visible: root.showScaleWarning && !BLEManager.scaleConnectionFailed
                key: "statusbar.scale_ellipsis"
                fallback: "Scale..."
                color: Theme.textSecondaryColor
                font: Theme.labelFont
                Accessible.ignored: true
            }

            Tr {
                Layout.alignment: Qt.AlignHCenter
                key: "idle.label.scaleweight"
                fallback: "Scale Weight"
                color: Theme.textSecondaryColor
                font: Theme.labelFont
                Accessible.ignored: true
            }
        }

        MouseArea {
            id: fullTapArea
            anchors.fill: parent
            cursorShape: root.showScaleWarning ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                if (root.showScaleWarning)
                    BLEManager.scanForDevices()
                else if (root.scaleConnected)
                    MachineState.tareScale()
            }
        }
    }
}
