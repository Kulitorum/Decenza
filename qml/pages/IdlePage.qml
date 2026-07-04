import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../components"
import "../components/layout"

Page {
    id: idlePage
    objectName: "idlePage"
    // Exposed so the global Brew Settings dialog (main.qml) can source the live
    // empty-scale virtual zero while this is the current page.
    readonly property real scaleVirtualZero: beanCapture.virtualZero
    background: Rectangle { color: Theme.backgroundColor }

    // True when the app is allowed to start machine operations on-screen.
    // The hardware Group Head Controller (GHC), when present and active, takes
    // exclusive control of starting shots/steam/etc., so on-screen start calls
    // are only valid in headless (no/inactive GHC) or simulation mode.
    readonly property bool canStartOperations: DE1Device.isHeadless || DE1Device.simulationMode

    StackView.onActivated: {
        root.currentPageTitle = TranslationManager.translate("idle.pageTitle", "Idle")
        if (root.pendingBrewDialog) {
            root.pendingBrewDialog = false
            root.openBrewSettings()
        }
    }

    // Secret developer mode: hold top-right corner for 5 seconds to simulate a completed shot
    Item {
        anchors.top: parent.top
        anchors.right: parent.right
        width: Theme.scaled(80)
        height: Theme.scaled(80)
        z: 100

        Timer {
            id: fakeShortHoldTimer
            interval: 5000
            onTriggered: {
                console.log("DEV: Simulating completed shot")
                MainController.generateFakeShotData()
                pageStack.push(Qt.resolvedUrl("EspressoPage.qml"))
                fakeShowMetadataTimer.start()
            }
        }

        Timer {
            id: fakeShowMetadataTimer
            interval: 300
            onTriggered: {
                var shotId = MainController.lastSavedShotId
                console.log("DEV: Opening PostShotReviewPage with shotId:", shotId)
                pageStack.push(Qt.resolvedUrl("PostShotReviewPage.qml"), { editShotId: shotId })
            }
        }

        MouseArea {
            anchors.fill: parent
            onPressed: fakeShortHoldTimer.start()
            onReleased: fakeShortHoldTimer.stop()
            onCanceled: fakeShortHoldTimer.stop()
        }
    }

    // ============================================================
    // Layout configuration
    // ============================================================

    // Parse layout and extract zone items
    property var layoutConfig: {
        var raw = Settings.network.layoutConfiguration
        try {
            return JSON.parse(raw)
        } catch(e) {
            return { zones: {} }
        }
    }

    property var topLeftItems: layoutConfig.zones ? (layoutConfig.zones.topLeft || []) : []
    property var topRightItems: layoutConfig.zones ? (layoutConfig.zones.topRight || []) : []
    property var centerStatusItems: layoutConfig.zones ? (layoutConfig.zones.centerStatus || []) : []
    property var centerTopItems: layoutConfig.zones ? (layoutConfig.zones.centerTop || []) : []
    property var centerMiddleItems: layoutConfig.zones ? (layoutConfig.zones.centerMiddle || []) : []
    property var bottomLeftItems: layoutConfig.zones ? (layoutConfig.zones.bottomLeft || []) : []
    property var bottomRightItems: layoutConfig.zones ? (layoutConfig.zones.bottomRight || []) : []
    // Lower-mid bar: optional full-width band above the bottom action bar.
    property var lowerMidBarItems: layoutConfig.zones ? (layoutConfig.zones.lowerMidBar || []) : []

    // Center zone Y-offsets (user-configurable positioning)
    property int centerStatusYOffset: layoutConfig.offsets ? (layoutConfig.offsets.centerStatus || 0) : 0
    property int centerTopYOffset: layoutConfig.offsets ? (layoutConfig.offsets.centerTop || 0) : 0
    property int centerMiddleYOffset: layoutConfig.offsets ? (layoutConfig.offsets.centerMiddle || 0) : 0

    // Center zone scales (user-configurable sizing)
    property real centerStatusScale: layoutConfig.scales ? (layoutConfig.scales.centerStatus || 1.0) : 1.0
    property real centerTopScale: layoutConfig.scales ? (layoutConfig.scales.centerTop || 1.0) : 1.0
    property real centerMiddleScale: layoutConfig.scales ? (layoutConfig.scales.centerMiddle || 1.0) : 1.0

    // Per-zone item size ("compact" | "large"); bars grow to fit large items.
    function zoneItemSize(zone) {
        return (layoutConfig.zoneOptions && layoutConfig.zoneOptions[zone]
                && layoutConfig.zoneOptions[zone].itemSize) || "compact"
    }

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("idle.pageTitle", "Idle")
        MainController.bagStorage.requestInventory()
        MainController.equipmentStorage.requestInventory()
        _publishOperationMode()
    }

    // Inventory bags for the beans pill row (bean-bag-inventory: pills are
    // bags, selection is activeBagId, no dirty state — edits write through).
    // Capped to the 5 most recently used (inventoryReady is MRU-ordered);
    // the full inventory lives on the Beans page.
    property var inventoryBags: []

    function bagLabel(bag) {
        if (!bag) return ""
        var coffee = bag.coffeeName || ""
        return coffee.length > 0 ? coffee : (bag.roasterName || "")
    }

    Connections {
        target: MainController.bagStorage
        function onInventoryReady(bags) {
            idlePage.inventoryBags = bags.slice(0, 5)
        }
        function onBagsChanged() {
            MainController.bagStorage.requestInventory()
        }
    }

    // Equipment packages for the equipment pill row (add-basket-equipment): pills
    // are packages, selection is activeEquipmentId. Capped to the 5 most recently
    // used (inventoryReady is MRU-ordered); the full inventory lives on the
    // Equipment page.
    property var inventoryEquipment: []

    function equipmentLabel(pkg) {
        if (!pkg) return ""
        if (pkg.name && String(pkg.name).length > 0) return String(pkg.name)
        return [pkg.grinderBrand || "", pkg.grinderModel || ""]
                .filter(function(s) { return s.length > 0 }).join(" ")
    }

    Connections {
        target: MainController.equipmentStorage
        function onInventoryReady(packages) {
            idlePage.inventoryEquipment = packages.slice(0, 5)
        }
        function onPackagesChanged() {
            MainController.equipmentStorage.requestInventory()
        }
    }

    // Track which function's presets are showing (used by center-zone action items)
    property string activePresetFunction: ""  // "", "steam", "espresso", "hotwater", "flush", "beans", "equipment"

    // Idle bean auto-capture: tracks a virtual zero off the empty scale, then when
    // the dose cup (with beans) rests stable it sets the dose (dyeBeanWeight) and
    // stop-at-weight (brewYieldOverride = dose x lastUsedRatio), optionally dings
    // (if doseCaptureSoundEnabled), and confirms on the readout. Net dose =
    // (load - virtualZero) - cupWeight, so it is robust to
    // an un-zeroed/drifting scale. The baseline tracks even with no cup saved (so the
    // "Weigh" button can reuse it); all user-visible behaviour and the actual capture
    // stay gated on a saved cup (doseCupTareWeight > 0). Stays armed whether or not the
    // brew dialog is open — one persistent latch means an already-weighed cup is not
    // re-captured on open/close. Only on home/espresso mode (NOT steam/hot-water/flush,
    // where the scale is for milk/water).
    property bool beanCaptureShown: false
    property string beanCaptureText: ""
    Timer { id: idleBeanCaptureTimer; interval: 3500; onTriggered: idlePage.beanCaptureShown = false }
    StableWeightCapture {
        id: beanCapture
        rawWeight: (ScaleDevice && ScaleDevice.connected) ? MachineState.scaleWeight : 0
        cupWeight: Settings.brew.doseCupTareWeight
        active: ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale
                && idlePage.activePresetFunction !== "steam"
                && idlePage.activePresetFunction !== "hotwater"
                && idlePage.activePresetFunction !== "flush"
        minNet: 5
        maxNet: 45
        tolerance: 0.5
        stableMs: 2500
        onStableCaptured: function(net) {
            // net is always >= minNet (5 g) here — no extra floor needed.
            // Always write the canonical dose + yield. The shared Brew Settings
            // dialog reflects it via its dyeBeanWeight watcher while it is open.
            Settings.dye.dyeBeanWeight = net
            Settings.brew.brewYieldOverride = net * Settings.brew.lastUsedRatio
            idlePage.beanCaptureText = TranslationManager.translate("idle.doseCaptured", "Dose set: %1g").arg(net.toFixed(1))
            idlePage.beanCaptureShown = true
            idleBeanCaptureTimer.restart()
            if (typeof AccessibilityManager !== "undefined") {
                if (Settings.brew.doseCaptureSoundEnabled)
                    AccessibilityManager.playCaptureDing()
                if (AccessibilityManager.enabled)
                    AccessibilityManager.announce(idlePage.beanCaptureText)
            }
        }
    }

    // When the scale is zeroed/tared, the old virtual zero is stale (it would
    // double-count the offset that was just removed) — re-establish the baseline.
    Connections {
        target: MachineState
        function onTareCompleted() { beanCapture.reset() }
    }

    // Idle milk auto-capture: while the steam presets are showing on the home
    // screen and the selected pitcher has a saved empty-pitcher weight (cupWeight),
    // rest the milk pitcher on the scale to record the milk weight. If the pitcher
    // is ALSO calibrated (has a reference milk weight), the steam time is locked
    // proportionally with a ding + confirmation. This is the steam equivalent of
    // the bean auto-capture above; the dedicated Steam page has its own copy.
    property bool milkCaptureShown: false
    property string milkCaptureText: ""
    // Last milk weight measured this session (for the bottom status row). 0 = none yet.
    property real measuredMilkG: 0
    Timer { id: idleMilkCaptureTimer; interval: 3500; onTriggered: idlePage.milkCaptureShown = false }
    StableWeightCapture {
        id: idleMilkCapture
        // Virtual-zero model (same as the bean capture): raw scale reading minus the
        // empty-pitcher weight (cupWeight) gives net milk, robust to an un-zeroed
        // scale. Auto-capture requires a saved pitcher weight (cupWeight > 0),
        // symmetric with beans requiring a saved dose-cup tare.
        rawWeight: (ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale) ? MachineState.scaleWeight : 0
        cupWeight: {
            var p = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
            return (p && !p.disabled) ? (p.pitcherWeightG ?? 0) : 0
        }
        // Opt-in (Settings.brew.milkAutoCaptureEnabled, default OFF — calibrating a
        // pitcher turns it on) and only while
        // the steam flow is showing AND this page is the active StackView page — so a
        // stray weight never silently changes the steam stop time, and the capture
        // can't double-fire alongside SteamPage's own copy when SteamPage is pushed
        // on top (long-press) while activePresetFunction is still "steam".
        active: Settings.brew.milkAutoCaptureEnabled
                && idlePage.activePresetFunction === "steam"
                && idlePage.StackView.status === StackView.Active
                && ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale
        minNet: 50   // nobody steams < 50 g milk; floor also keeps a bean cup from tripping milk capture
        maxNet: 1500
        tolerance: 1.5
        stableMs: 2500
        onStableCaptured: function(milk) {
            idlePage.measuredMilkG = milk  // record measured (net) milk for the status row
            // Latch the measured milk for the baseline pair (committed atomically with
            // the duration at session end, in main.qml). Recorded even for an
            // uncalibrated preset so the very first calibration-bootstrap steam can be
            // adopted — and never as a half-pair, since the time half is written there.
            if (Window.window) Window.window.sessionMeasuredMilkG = milk
            // Single source of truth (SettingsBrew): 0 when off/uncalibrated → nothing to lock.
            var t = Settings.brew.scaledSteamTime(Settings.brew.selectedSteamPitcher, milk)
            if (t <= 0) return
            Settings.brew.steamTimeout = t
            // Push to the DE1 now (same as the steam-preset selection) so a GHC/auto
            // steam actually uses the scaled time, not the machine's last-sent value.
            MainController.applySteamSettings()
            idlePage.milkCaptureText = TranslationManager.translate("idle.steamCaptured", "Steam time: %1s for %2g milk").arg(t).arg(milk.toFixed(0))
            idlePage.milkCaptureShown = true
            idleMilkCaptureTimer.restart()
            if (typeof AccessibilityManager !== "undefined") {
                if (Settings.brew.doseCaptureSoundEnabled)
                    AccessibilityManager.playCaptureDing()
                if (AccessibilityManager.enabled)
                    AccessibilityManager.announce(idlePage.milkCaptureText)
            }
        }
    }
    // Re-zero the milk capture when the scale is tared (same reason as the bean one).
    Connections {
        target: MachineState
        function onTareCompleted() { idleMilkCapture.reset() }
    }

    // Clear the last measured milk when a steam session ends, so the next steam isn't
    // scaled from a stale measurement (e.g. the home-screen preset path's fallback).
    // Phase leaves Steaming only at the true session end (Puffing/Ending stay in the
    // Steaming phase by machinestate.cpp's load-bearing invariant).
    Connections {
        target: MachineState
        property bool wasSteaming: false
        function onPhaseChanged() {
            if (MachineState.phase === MachineStateType.Phase.Steaming)
                wasSteaming = true
            else if (wasSteaming) {
                idlePage.measuredMilkG = 0
                wasSteaming = false
            }
        }
    }

    // Transient confirmation banner for the milk-weight capture (auto-dismiss).
    Rectangle {
        visible: idlePage.milkCaptureShown
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.scaled(12)
        z: 2000
        width: milkBannerLabel.implicitWidth + Theme.scaled(32)
        height: milkBannerLabel.implicitHeight + Theme.scaled(20)
        radius: Theme.cardRadius
        color: Theme.primaryColor
        Text {
            id: milkBannerLabel
            anchors.centerIn: parent
            text: idlePage.milkCaptureText
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
    }

    // Small flashing reminder shown while a dose cup of beans is settling on the
    // scale (a load is present but stable-capture hasn't fired yet). Disappears the
    // instant capture completes; a ding plays then only if doseCaptureSoundEnabled.
    Text {
        id: waitForBellHint
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.scaled(70)
        z: 1500
        horizontalAlignment: Text.AlignHCenter
        // Only while a load sits in the capture window with a cup saved. Outside
        // [minNet, maxNet] no capture will ever fire, so don't tell the user to wait
        // for a bell that can't ring (a too-heavy cup or the wrong vessel).
        readonly property bool beansSettling: beanCapture.active && !beanCapture.isCaptured
                                              && Settings.brew.doseCupTareWeight > 0
                                              && beanCapture.loadPresent
                                              && beanCapture.netWeight >= beanCapture.minNet
                                              && beanCapture.netWeight <= beanCapture.maxNet
        readonly property bool milkSettling: idleMilkCapture.active && !idleMilkCapture.isCaptured
                                            && idleMilkCapture.cupWeight > 0
                                            && idleMilkCapture.loadPresent
                                            && idleMilkCapture.netWeight >= idleMilkCapture.minNet
                                            && idleMilkCapture.netWeight <= idleMilkCapture.maxNet
        visible: beansSettling || milkSettling
        text: TranslationManager.translate("scale.waitForBell", "Wait for the bell before you take it off the scale")
        color: Theme.warningColor
        font: Theme.labelFont
        SequentialAnimation on opacity {
            running: waitForBellHint.visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.25; duration: 450 }
            NumberAnimation { to: 1.0; duration: 450 }
        }
    }

    // Scale-load detector for the "place the pitcher" prompt below. Gated on
    // milkAutoCaptureEnabled (same as idleMilkCapture): with weight-timed steaming OFF, placing the
    // pitcher captures nothing and no beep follows, so the prompt must not tell the user to do it.
    // It never captures; we only read loadPresent.
    StableWeightCapture {
        id: idlePitcherDetect
        rawWeight: (ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale) ? MachineState.scaleWeight : 0
        active: Settings.brew.milkAutoCaptureEnabled
                && idlePage.activePresetFunction === "steam"
                && idlePage.StackView.status === StackView.Active
                && ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale
        minNet: 999999   // never auto-captures; only provides loadPresent
        loadThreshold: 50  // an empty pitcher already weighs well above this
    }

    // Publish the selected operation (espresso/steam/…) to the Theme singleton so the
    // persistent status-bar widgets (e.g. the page-aware Plan widget) can tell what the
    // user has selected on the idle screen — they load as separate components and can't
    // read this property by scope. Cleared when this page isn't the active one.
    function _publishOperationMode() {
        Theme.currentOperationMode =
            (StackView.status === StackView.Active) ? activePresetFunction : ""
    }
    StackView.onStatusChanged: _publishOperationMode()

    // Auto-tare scale and announce presets when activePresetFunction changes
    onActivePresetFunctionChanged: {
        _publishOperationMode()
        // Auto-tare when steam pills appear so the scale starts at 0
        // before the user places the pitcher
        if (activePresetFunction === "steam" && typeof MachineState !== "undefined") {
            MachineState.tareScale()
            // Fresh steam attempt: drop any milk captured but not consumed by a prior
            // (abandoned) attempt, so it can't scale this one.
            if (Window.window) Window.window.sessionMeasuredMilkG = 0
        }

        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled && activePresetFunction !== "") {
            var presets = []
            var selectedName = ""
            switch (activePresetFunction) {
                case "espresso":
                    presets = Settings.app.favoriteProfiles
                    if (Settings.app.selectedFavoriteProfile >= 0 && Settings.app.selectedFavoriteProfile < presets.length) {
                        selectedName = presets[Settings.app.selectedFavoriteProfile].name
                    }
                    break
                case "steam":
                    presets = Settings.brew.steamPitcherPresets
                    if (Settings.brew.selectedSteamPitcher >= 0 && Settings.brew.selectedSteamPitcher < presets.length) {
                        selectedName = presets[Settings.brew.selectedSteamPitcher].name
                    }
                    break
                case "hotwater":
                    presets = Settings.brew.waterVesselPresets
                    if (Settings.brew.selectedWaterVessel >= 0 && Settings.brew.selectedWaterVessel < presets.length) {
                        selectedName = presets[Settings.brew.selectedWaterVessel].name
                    }
                    break
                case "flush":
                    presets = Settings.brew.flushPresets
                    if (Settings.brew.selectedFlushPreset >= 0 && Settings.brew.selectedFlushPreset < presets.length) {
                        selectedName = presets[Settings.brew.selectedFlushPreset].name
                    }
                    break
                case "beans":
                    presets = idlePage.inventoryBags.map(function(b) { return { name: idlePage.bagLabel(b) } })
                    for (var bi = 0; bi < idlePage.inventoryBags.length; ++bi) {
                        if (idlePage.inventoryBags[bi].id === Settings.dye.activeBagId) {
                            selectedName = idlePage.bagLabel(idlePage.inventoryBags[bi])
                            break
                        }
                    }
                    break
                case "equipment":
                    presets = idlePage.inventoryEquipment.map(function(p) { return { name: idlePage.equipmentLabel(p) } })
                    for (var ei = 0; ei < idlePage.inventoryEquipment.length; ++ei) {
                        if (idlePage.inventoryEquipment[ei].id === Settings.dye.activeEquipmentId) {
                            selectedName = idlePage.equipmentLabel(idlePage.inventoryEquipment[ei])
                            break
                        }
                    }
                    break
            }

            if (presets.length > 0) {
                var names = []
                for (var i = 0; i < presets.length; i++) {
                    names.push(presets[i].name)
                }
                var announcement = presets.length + " " + TranslationManager.translate("idle.accessible.presets", "presets") + ": " + names.join(", ")
                if (selectedName !== "") {
                    announcement += ". " + selectedName + " " + TranslationManager.translate("idle.accessible.isSelected", "is selected")
                }
                AccessibilityManager.announce(announcement)
            }
        }
    }

    // Click away to hide presets (disabled in accessibility mode to prevent mis-clicks)
    MouseArea {
        anchors.fill: parent
        z: -1
        enabled: activePresetFunction !== "" &&
                 !(typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
        onClicked: activePresetFunction = ""
    }

    // ============================================================
    // Top info section (from layout topLeft/topRight zones)
    // ============================================================
    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        spacing: Theme.scaled(20)

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.scaled(50)

            LayoutBarZone {
                zoneName: "topLeft"
                items: idlePage.topLeftItems
                itemSize: idlePage.zoneItemSize("topLeft")
            }

            Item { Layout.fillWidth: true }

            LayoutBarZone {
                zoneName: "topRight"
                items: idlePage.topRightItems
                itemSize: idlePage.zoneItemSize("topRight")
            }
        }
    }

    // ============================================================
    // Center content (from layout centerTop/centerMiddle zones)
    // ============================================================
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: Theme.scaled(50)
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        spacing: Theme.scaled(20)

        // Status readouts (temp, water level, connection)
        LayoutCenterZone {
            Layout.fillWidth: true
            Layout.topMargin: idlePage.centerStatusYOffset
            zoneName: "centerStatus"
            items: idlePage.centerStatusItems
            visible: idlePage.centerStatusItems.length > 0
            zoneScale: idlePage.centerStatusScale
        }

        // Main action buttons from centerTop zone
        LayoutCenterZone {
            id: centerTopZone
            Layout.fillWidth: true
            Layout.topMargin: idlePage.centerTopYOffset
            zoneName: "centerTop"
            items: idlePage.centerTopItems
            zoneScale: idlePage.centerTopScale
        }

        // Inline preset rows (for center-zone action buttons)
        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: activePresetFunction !== "" ? activePresetRow.implicitHeight : 0
            Layout.fillWidth: true
            Layout.maximumWidth: Theme.scaled(900)
            Layout.leftMargin: Theme.standardMargin
            Layout.rightMargin: Theme.standardMargin
            clip: true

            property var activePresetRow: {
                switch (activePresetFunction) {
                    case "steam": return steamPresetLoader
                    case "espresso": return espressoColumnLoader
                    case "hotwater": return hotWaterPresetLoader
                    case "flush": return flushPresetLoader
                    case "beans": return beanPresetLoader
                    case "equipment": return equipmentPresetLoader
                    default: return steamPresetLoader
                }
            }

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
            }

            Loader {
                id: steamPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "steam"
                visible: active

                sourceComponent: Column {
                    width: parent ? parent.width : 0
                    spacing: Theme.scaled(8)

                    PresetPillRow {
                        anchors.horizontalCenter: parent.horizontalCenter
                        maxWidth: steamPresetLoader.width
                        presets: Settings.brew.steamPitcherPresets
                        selectedIndex: Settings.brew.selectedSteamPitcher
                        supportLongPress: true

                        // Show pitcher presets as "<name> Pitcher" (e.g. "Small Pitcher"),
                        // skipping the disabled "Off" preset and any name that already
                        // mentions a pitcher.
                        pillLabelFn: function(index, name) {
                            var preset = Settings.brew.steamPitcherPresets[index]
                            if (preset && preset.disabled) return name
                            if (!name || name.toLowerCase().indexOf("pitcher") >= 0) return name
                            return name + " " + TranslationManager.translate("idle.label.pitcherSuffix", "Pitcher")
                        }

                        onPresetSelected: function(index) {
                            var wasAlreadySelected = (index === Settings.brew.selectedSteamPitcher)
                            Settings.brew.selectedSteamPitcher = index
                            var preset = Settings.brew.getSteamPitcherPreset(index)
                            if (preset && preset.disabled) {
                                // "Off" preset — disable the steam heater; don't touch
                                // steamTimeout/steamFlow (preset.duration/flow are undefined
                                // for disabled presets and writing undefined to these int
                                // properties errors), and don't start steam on re-tap.
                                MainController.turnOffSteamHeater()
                                return
                            }
                            if (preset) {
                                // Weight-scaled steaming (DSx2-style), via the single source of
                                // truth in SettingsBrew. Net milk on the scale now, or the last
                                // measured weight if the pitcher was lifted to the wand.
                                var idx = Settings.brew.selectedSteamPitcher
                                var milk = (ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale)
                                           ? Settings.brew.netMilkForPitcher(idx, MachineState.scaleWeight) : 0
                                if (milk <= 0)
                                    milk = idlePage.measuredMilkG
                                Settings.brew.steamTimeout = Settings.brew.effectiveSteamDurationSec(idx, milk)
                                Settings.brew.steamFlow = preset.flow !== undefined ? preset.flow : 150
                                Settings.brew.steamTemperature = (preset.temperature !== undefined) ? preset.temperature : Settings.brew.steamTemperature
                            }
                            MainController.applySteamSettings()

                            if (wasAlreadySelected) {
                                if (MachineState.isReady && idlePage.canStartOperations) {
                                    DE1Device.startSteam()
                                } else {
                                    console.log("Cannot start steam - machine not ready, phase:", MachineState.phase)
                                    if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                        AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                                }
                            }
                        }

                        // Long-press a pitcher to open the steam page settings, where you set
                        // the duration and either tap "Weigh" next to Reference milk (with milk
                        // on the scale) or "Use as baseline" to teach this pitcher its
                        // milk-weight -> steam-time reference.
                        onPresetLongPressed: function(index) {
                            Settings.brew.selectedSteamPitcher = index
                            pageStack.push(Qt.resolvedUrl("SteamPage.qml"))
                        }
                    }

                    // "Place the milk pitcher on the scale" — same position as the bean prompt (below
                    // the pills). Shown only while idlePitcherDetect is active (weight-timed steaming on,
                    // steam selected, scale connected) and nothing is on the scale yet. Gently blinks.
                    // The hint promises a beep ONLY when the capture sound will actually play — the
                    // ding is separately gated on doseCaptureSoundEnabled (default off).
                    Text {
                        id: steamPlacePrompt
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        visible: idlePitcherDetect.active && !idlePitcherDetect.loadPresent
                        text: TranslationManager.translate("idle.label.placePitcherOnScale", "Place the milk pitcher on the scale") + "\n"
                            + (Settings.brew.doseCaptureSoundEnabled
                                ? TranslationManager.translate("idle.label.placePitcherHint", "(and wait for the beep before removing)")
                                : TranslationManager.translate("idle.label.placeHintNoSound", "(hold still until the weight registers)"))
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                        SequentialAnimation on opacity {
                            running: steamPlacePrompt.visible
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.45; duration: 800 }
                            NumberAnimation { to: 1.0; duration: 800 }
                        }
                    }
                }
            }

            Loader {
                id: espressoColumnLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "espresso"
                visible: active
                sourceComponent: Column {
                    width: parent ? parent.width : 0
                    spacing: Theme.scaled(8)

                    PresetPillRow {
                        anchors.horizontalCenter: parent.horizontalCenter
                        maxWidth: espressoColumnLoader.width

                        presets: Settings.app.favoriteProfiles
                        selectedIndex: Settings.app.selectedFavoriteProfile
                        supportLongPress: true
                        modified: ProfileManager.profileModified
                        modifiedIsReadOnly: ProfileManager.isCurrentProfileReadOnly

                        onPresetSelected: function(index) {
                            var wasAlreadySelected = (index === Settings.app.selectedFavoriteProfile)
                            Settings.app.selectedFavoriteProfile = index
                            var preset = Settings.app.getFavoriteProfile(index)

                            if (wasAlreadySelected) {
                                if (MachineState.isReady && idlePage.canStartOperations) {
                                    DE1Device.startEspresso()
                                } else {
                                    console.log("Cannot start espresso - machine not ready, phase:", MachineState.phase)
                                    if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                        AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                                }
                            } else {
                                if (preset && preset.filename) {
                                    ProfileManager.loadProfile(preset.filename)
                                }
                            }
                        }

                        onPresetLongPressed: function(index) {
                            var preset = Settings.app.getFavoriteProfile(index)
                            if (preset && preset.filename) {
                                if (index !== Settings.app.selectedFavoriteProfile) {
                                    Settings.app.selectedFavoriteProfile = index
                                    ProfileManager.loadProfile(preset.filename)
                                }
                                profilePreviewPopup.profileFilename = preset.filename
                                profilePreviewPopup.profileName = preset.name || ""
                                profilePreviewPopup.open()
                            }
                        }
                    }

                    // Green pill showing non-favorite profile name
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: Settings.app.selectedFavoriteProfile === -1 && idlePage.canStartOperations
                        spacing: Theme.scaled(8)

                        Rectangle {
                            id: nonFavoriteProfilePill
                            width: nonFavoriteProfileText.implicitWidth + Theme.scaled(40)
                            height: Theme.scaled(50)
                            radius: Theme.scaled(10)
                            color: Theme.successColor

                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: (ProfileManager.currentProfileName || "") + " " + TranslationManager.translate("idle.accessible.startespresso", "Start espresso")
                            Accessible.focusable: true
                            Accessible.onPressAction: idleNonFavMouseArea.clicked(null)
                            Keys.onReturnPressed: { idleNonFavMouseArea.clicked(null); event.accepted = true }
                            Keys.onSpacePressed:  { idleNonFavMouseArea.clicked(null); event.accepted = true }

                            Text {
                                id: nonFavoriteProfileText
                                anchors.centerIn: parent
                                text: ProfileManager.currentProfileName || ""
                                color: Theme.primaryContrastColor
                                font.pixelSize: Theme.scaled(16)
                                font.bold: true
                                Accessible.ignored: true
                            }

                            MouseArea {
                                id: idleNonFavMouseArea
                                anchors.fill: parent
                                onClicked: {
                                    if (MachineState.isReady && idlePage.canStartOperations) {
                                        DE1Device.startEspresso()
                                    } else {
                                        console.log("Cannot start espresso - machine not ready, phase:", MachineState.phase)
                                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                            AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                                    }
                                }
                            }
                        }

                        ProfileInfoButton {
                            anchors.verticalCenter: parent.verticalCenter
                            profileFilename: Settings.app.currentProfile
                            profileName: ProfileManager.currentProfileName

                            onClicked: {
                                pageStack.push(Qt.resolvedUrl("ProfileInfoPage.qml"), {
                                    profileFilename: Settings.app.currentProfile,
                                    profileName: ProfileManager.currentProfileName
                                })
                            }
                        }
                    }

                    // Bean weight: live net-weight readout for the auto-capture above
                    // (net = load minus the virtual zero and the saved cup). Only shown
                    // once a dose-cup tare is saved (doseCupTareWeight > 0); with no cup
                    // saved the auto-capture is off, so the readout/prompt stays hidden.
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale
                                 && Settings.brew.doseCupTareWeight > 0
                        spacing: Theme.scaled(8)

                        // Small, unobtrusive live net-weight readout. Beans now
                        // auto-capture when stable, so this is a readout (not a
                        // button) — it shows the live net beans and briefly flashes
                        // the captured dose in the accent color.
                        Text {
                            id: weighBeansText
                            horizontalAlignment: Text.AlignHCenter
                            // True while prompting the user to place beans (no load on
                            // the scale yet) — this state gently blinks.
                            readonly property bool showingPlacePrompt: !idlePage.beanCaptureShown
                                && !beanCapture.loadPresent
                            text: {
                                if (idlePage.beanCaptureShown)
                                    return idlePage.beanCaptureText
                                if (beanCapture.loadPresent)
                                    return beanCapture.netWeight.toFixed(1) + " g " + TranslationManager.translate("idle.label.onScale", "on scale")
                                // The beep hint only when the capture sound will actually play
                                // (doseCaptureSoundEnabled, default off) — same rule as the pitcher prompt.
                                return TranslationManager.translate("idle.label.placeBeansOnScale", "Place Beans on Scale") + "\n"
                                     + (Settings.brew.doseCaptureSoundEnabled
                                         ? TranslationManager.translate("idle.label.placeBeansHint", "(and wait for the beep before removing)")
                                         : TranslationManager.translate("idle.label.placeHintNoSound", "(hold still until the weight registers)"))
                            }
                            color: idlePage.beanCaptureShown ? Theme.primaryColor : Theme.textSecondaryColor
                            font: Theme.labelFont
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                            onShowingPlacePromptChanged: if (!showingPlacePrompt) opacity = 1.0
                            SequentialAnimation on opacity {
                                running: weighBeansText.showingPlacePrompt
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.45; duration: 800 }
                                NumberAnimation { to: 1.0; duration: 800 }
                            }
                        }
                    }
                }
            }

            Loader {
                id: hotWaterPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "hotwater"
                visible: active
                sourceComponent: PresetPillRow {
                    maxWidth: hotWaterPresetLoader.width
                    presets: Settings.brew.waterVesselPresets
                    selectedIndex: Settings.brew.selectedWaterVessel

                    onPresetSelected: function(index) {
                        var wasAlreadySelected = (index === Settings.brew.selectedWaterVessel)
                        Settings.brew.selectedWaterVessel = index
                        var preset = Settings.brew.getWaterVesselPreset(index)
                        if (preset) {
                            Settings.brew.waterVolume = preset.volume
                        }
                        MainController.applyHotWaterSettings()

                        if (wasAlreadySelected) {
                            if (MachineState.isReady && idlePage.canStartOperations) {
                                DE1Device.startHotWater()
                            } else {
                                console.log("Cannot start hot water - machine not ready, phase:", MachineState.phase)
                                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                    AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                            }
                        }
                    }
                }
            }

            Loader {
                id: flushPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "flush"
                visible: active
                sourceComponent: PresetPillRow {
                    maxWidth: flushPresetLoader.width
                    presets: Settings.brew.flushPresets
                    selectedIndex: Settings.brew.selectedFlushPreset

                    onPresetSelected: function(index) {
                        var wasAlreadySelected = (index === Settings.brew.selectedFlushPreset)
                        Settings.brew.selectedFlushPreset = index
                        var preset = Settings.brew.getFlushPreset(index)
                        if (preset) {
                            Settings.brew.flushFlow = preset.flow
                            Settings.brew.flushSeconds = preset.seconds
                        }
                        MainController.applyFlushSettings()

                        if (wasAlreadySelected) {
                            if (MachineState.isReady && idlePage.canStartOperations) {
                                DE1Device.startFlush()
                            } else {
                                console.log("Cannot start flush - machine not ready, phase:", MachineState.phase)
                                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                    AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                            }
                        }
                    }
                }
            }

            Loader {
                id: beanPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "beans"
                visible: active
                sourceComponent: PresetPillRow {
                    id: inlineBeanPresetRow
                    maxWidth: beanPresetLoader.width
                    presets: idlePage.inventoryBags.map(function(b) { return { name: idlePage.bagLabel(b) } })
                    selectedIndex: {
                        var list = idlePage.inventoryBags
                        for (var i = 0; i < list.length; ++i) {
                            if (list[i].id === Settings.dye.activeBagId) return i
                        }
                        return -1
                    }

                    onPresetSelected: function(index) {
                        var bag = idlePage.inventoryBags[index]
                        if (!bag) return
                        Settings.dye.activeBagId = bag.id
                    }
                }
            }

            Loader {
                id: equipmentPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "equipment"
                visible: active
                sourceComponent: PresetPillRow {
                    maxWidth: equipmentPresetLoader.width
                    presets: idlePage.inventoryEquipment.map(function(p) { return { name: idlePage.equipmentLabel(p) } })
                    selectedIndex: {
                        var list = idlePage.inventoryEquipment
                        for (var i = 0; i < list.length; ++i) {
                            if (list[i].id === Settings.dye.activeEquipmentId) return i
                        }
                        return -1
                    }

                    onPresetSelected: function(index) {
                        var pkg = idlePage.inventoryEquipment[index]
                        if (!pkg) return
                        Settings.dye.switchToEquipment(pkg)
                    }
                }
            }
        }

        // Center middle zone (shot plan, etc.)
        LayoutCenterZone {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: idlePage.centerMiddleYOffset
            zoneName: "centerMiddle"
            items: idlePage.centerMiddleItems
            zoneScale: idlePage.centerMiddleScale
        }
    }

    // ============================================================
    // Lower-mid bar (optional, from layout lowerMidBar zone)
    // Full-width band above the bottom action bar. Empty -> zero height.
    // Height-gated: hidden unless the viewport has room for the bar plus a
    // minimum center height (runtime-adaptive, no device-class check). This is a
    // heuristic on total free height, not a hard guarantee against the
    // vertically-centered center content reaching under the bar on mid-height
    // viewports; raise the threshold if overlap is seen.
    // ============================================================
    property var lowerMidBarOptions: layoutConfig.zoneOptions ? (layoutConfig.zoneOptions.lowerMidBar || ({})) : ({})
    // Lower-mid bar position (offset) + scale, matching the center-zone controls.
    readonly property int lowerMidBarYOffset: layoutConfig.offsets ? (layoutConfig.offsets.lowerMidBar || 0) : 0
    readonly property real lowerMidBarScale: layoutConfig.scales ? (layoutConfig.scales.lowerMidBar || 1.0) : 1.0
    readonly property bool lowerMidBarHasItems: idlePage.lowerMidBarItems.length > 0
    // Auto-grow: the band fits its content (large item-size makes it taller),
    // never smaller than the standard bar height.
    readonly property real lowerMidBarFullHeight: Math.max(Theme.scaled(82), lmbZone.implicitHeight)
    readonly property bool lowerMidBarFits:
        (idlePage.height - Theme.statusBarHeight - Theme.bottomBarHeight - lowerMidBarFullHeight) >= Theme.scaled(220)
    readonly property bool lowerMidBarVisible: lowerMidBarHasItems && lowerMidBarFits

    Rectangle {
        id: lowerMidBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        // Negative offset (the editor's "up") lifts the band off the bottom bar.
        anchors.bottomMargin: -idlePage.lowerMidBarYOffset
        visible: idlePage.lowerMidBarVisible
        height: visible ? idlePage.lowerMidBarFullHeight : 0
        color: Theme.zoneBackgroundColor(idlePage.lowerMidBarOptions.style)

        LayoutBarZone {
            id: lmbZone
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium
            zoneName: "lowerMidBar"
            items: idlePage.lowerMidBarItems
            distribution: idlePage.lowerMidBarOptions.distribution || "packed"
            alignment: idlePage.lowerMidBarOptions.alignment || "center"
            zoneStyle: idlePage.lowerMidBarOptions.style || "standard"
            itemSize: idlePage.lowerMidBarOptions.itemSize || "compact"
            zoneScale: idlePage.lowerMidBarScale
        }
    }

    // ============================================================
    // Bottom bar (from layout bottomLeft/bottomRight zones)
    // ============================================================
    Rectangle {
        id: bottomBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        // Auto-grow to fit large item-size; standard bar height otherwise.
        height: Math.max(Theme.bottomBarHeight, blZone.implicitHeight, brZone.implicitHeight)
        color: Theme.bottomBarColor

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingMedium

            LayoutBarZone {
                id: blZone
                zoneName: "bottomLeft"
                items: idlePage.bottomLeftItems
                itemSize: idlePage.zoneItemSize("bottomLeft")
                Layout.fillHeight: true
            }

            Item { Layout.fillWidth: true }

            LayoutBarZone {
                id: brZone
                zoneName: "bottomRight"
                items: idlePage.bottomRightItems
                itemSize: idlePage.zoneItemSize("bottomRight")
                Layout.fillHeight: true
            }
        }
    }

    // Profile preview popup for long-press on espresso pills
    ProfilePreviewPopup {
        id: profilePreviewPopup
    }
}
