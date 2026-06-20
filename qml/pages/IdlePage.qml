import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"
import "../components/layout"

Page {
    id: idlePage
    objectName: "idlePage"
    property alias idleBrewDialog: idleBrewDialog
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
            idleBrewDialog.open()
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

    // Center zone Y-offsets (user-configurable positioning)
    property int centerStatusYOffset: layoutConfig.offsets ? (layoutConfig.offsets.centerStatus || 0) : 0
    property int centerTopYOffset: layoutConfig.offsets ? (layoutConfig.offsets.centerTop || 0) : 0
    property int centerMiddleYOffset: layoutConfig.offsets ? (layoutConfig.offsets.centerMiddle || 0) : 0

    // Center zone scales (user-configurable sizing)
    property real centerStatusScale: layoutConfig.scales ? (layoutConfig.scales.centerStatus || 1.0) : 1.0
    property real centerTopScale: layoutConfig.scales ? (layoutConfig.scales.centerTop || 1.0) : 1.0
    property real centerMiddleScale: layoutConfig.scales ? (layoutConfig.scales.centerMiddle || 1.0) : 1.0

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("idle.pageTitle", "Idle")
        MainController.bagStorage.requestInventory()
        MainController.equipmentStorage.requestInventory()
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

    // Idle bean auto-capture: when the dose cup (with beans) rests stable on the
    // scale, set the dose + stop-at-weight (same as the "Weigh beans" button),
    // ding, and confirm on the button text. Active on the plain home screen and
    // in espresso/beans mode (NOT while showing steam/hot-water/flush presets,
    // where the scale is for milk/water), and bounded to a dose-plausible weight
    // (<= 45 g net) so a milk pitcher or water vessel never trips it.
    property bool beanCaptureShown: false
    property string beanCaptureText: ""
    Timer { id: idleBeanCaptureTimer; interval: 3500; onTriggered: idlePage.beanCaptureShown = false }
    StableWeightCapture {
        id: beanCapture
        weight: ScaleDevice.connected ? Math.max(0, MachineState.scaleWeight - Settings.brew.doseCupTareWeight) : 0
        active: ScaleDevice.connected && !ScaleDevice.isFlowScale
                && idlePage.activePresetFunction !== "steam"
                && idlePage.activePresetFunction !== "hotwater"
                && idlePage.activePresetFunction !== "flush"
        minWeight: 5
        maxWeight: 45
        tolerance: 0.5
        stableMs: 2500
        onStableCaptured: function(net) {
            if (net < 3) return
            Settings.dye.dyeBeanWeight = net
            Settings.brew.brewYieldOverride = net * Settings.brew.lastUsedRatio
            idlePage.beanCaptureText = TranslationManager.translate("idle.doseCaptured", "Dose set: %1g").arg(net.toFixed(1))
            idlePage.beanCaptureShown = true
            idleBeanCaptureTimer.restart()
            if (typeof AccessibilityManager !== "undefined") {
                AccessibilityManager.playCaptureDing()
                if (AccessibilityManager.enabled)
                    AccessibilityManager.announce(idlePage.beanCaptureText)
            }
        }
    }

    // Idle milk auto-capture: while the steam presets are showing on the home
    // screen and the selected pitcher is calibrated (has a reference milk weight),
    // rest the milk pitcher on the scale -> lock the steam time proportionally,
    // ding, and show a confirmation. This is the steam equivalent of the bean
    // auto-capture above; the dedicated Steam page has its own copy.
    property bool milkCaptureShown: false
    property string milkCaptureText: ""
    // Last milk weight measured this session (for the bottom status row). 0 = none yet.
    property real measuredMilkG: 0
    Timer { id: idleMilkCaptureTimer; interval: 3500; onTriggered: idlePage.milkCaptureShown = false }
    StableWeightCapture {
        id: idleMilkCapture
        weight: {
            if (!ScaleDevice.connected || ScaleDevice.isFlowScale) return 0
            var p = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
            if (!p || p.disabled) return 0
            var pw = p.pitcherWeightG ?? 0
            var milk = pw > 0 ? (MachineState.scaleWeight - pw) : MachineState.scaleWeight
            return (milk > 20 && milk < 1500) ? milk : 0
        }
        active: idlePage.activePresetFunction === "steam"
                && ScaleDevice.connected && !ScaleDevice.isFlowScale
        minWeight: 20
        tolerance: 1.5
        stableMs: 2500
        onStableCaptured: function(milk) {
            idlePage.measuredMilkG = milk  // record measured milk for the status row
            var p = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
            if (!p || p.disabled) return
            var calib = p.calibMilkG ?? 0
            if (calib <= 0) return  // preset not calibrated (no reference milk) — nothing to lock
            var t = Math.max(5, Math.min(120, Math.round(p.duration * (milk / calib))))
            Settings.brew.steamTimeout = t
            idlePage.milkCaptureText = TranslationManager.translate("idle.steamCaptured", "Steam time: %1s for %2g milk").arg(t).arg(milk.toFixed(0))
            idlePage.milkCaptureShown = true
            idleMilkCaptureTimer.restart()
            if (typeof AccessibilityManager !== "undefined") {
                AccessibilityManager.playCaptureDing()
                if (AccessibilityManager.enabled)
                    AccessibilityManager.announce(idlePage.milkCaptureText)
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

    // Small flashing reminder shown while a cup of beans or a pitcher of milk is
    // settling on the scale (something is on the scale but the capture hasn't
    // fired yet). Disappears the instant it captures (the bell rings).
    Text {
        id: waitForBellHint
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.scaled(70)
        z: 1500
        horizontalAlignment: Text.AlignHCenter
        readonly property bool beansSettling: beanCapture.active && !beanCapture.isCaptured
                                              && beanCapture.weight >= beanCapture.minWeight
        readonly property bool milkSettling: idleMilkCapture.active && !idleMilkCapture.isCaptured
                                            && idleMilkCapture.weight >= idleMilkCapture.minWeight
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

    // Auto-tare scale and announce presets when activePresetFunction changes
    onActivePresetFunctionChanged: {
        // Auto-tare when steam pills appear so the scale starts at 0
        // before the user places the pitcher
        if (activePresetFunction === "steam" && typeof MachineState !== "undefined") {
            MachineState.tareScale()
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

    // Brew dialog opened from shot plan line
    BrewDialog {
        id: idleBrewDialog
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
            }

            Item { Layout.fillWidth: true }

            LayoutBarZone {
                zoneName: "topRight"
                items: idlePage.topRightItems
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

                // Track scale weight changes and bump version to refresh pill suffix text
                property int steamPillSuffixVersion: 0
                Connections {
                    target: MachineState
                    function onScaleWeightChanged() {
                        if (steamPresetLoader.active) steamPresetLoader.steamPillSuffixVersion++
                    }
                }

                sourceComponent: PresetPillRow {
                    maxWidth: steamPresetLoader.width
                    presets: Settings.brew.steamPitcherPresets
                    selectedIndex: Settings.brew.selectedSteamPitcher
                    pillSuffixMaxWidth: Theme.scaled(60)  // Reserve ~"(1234g)" worth of width
                    pillSuffixVersion: steamPresetLoader.steamPillSuffixVersion
                    supportLongPress: true

                    pillSuffixFn: function(index) {
                        if (!ScaleDevice.connected || ScaleDevice.isFlowScale) return ""
                        var preset = Settings.brew.steamPitcherPresets[index]
                        if (!preset) return ""
                        var pitcherWeight = preset.pitcherWeightG ?? 0
                        if (pitcherWeight <= 0) return ""
                        var milkWeight = Math.max(0, MachineState.scaleWeight - pitcherWeight)
                        return " (" + Math.round(milkWeight) + "g)"
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
                            // Weight-scaled steaming (DSx2-style): if this pitcher is
                            // calibrated (a reference milk weight is paired with its
                            // duration), scale the steam time by the actual milk weight
                            // so it auto-stops proportionally.
                            var calibMilk = preset.calibMilkG ?? 0
                            if (calibMilk > 0 && ScaleDevice.connected && !ScaleDevice.isFlowScale) {
                                var pitcherWt = preset.pitcherWeightG ?? 0
                                // Net milk = scale - saved pitcher weight, or the raw
                                // reading if the user tared the scale instead.
                                var milk = pitcherWt > 0 ? (MachineState.scaleWeight - pitcherWt)
                                                         : MachineState.scaleWeight
                                // If milk isn't on the scale right now (e.g. lifted to the
                                // wand), fall back to the last measured weight so the time
                                // still scales.
                                if (!(milk > 20 && milk < 1500))
                                    milk = idlePage.measuredMilkG
                                if (milk > 20 && milk < 1500)
                                    Settings.brew.steamTimeout = Math.max(5, Math.min(120,
                                        Math.round(preset.duration * milk / calibMilk)))
                                else
                                    Settings.brew.steamTimeout = preset.duration  // no milk measured yet
                            } else {
                                Settings.brew.steamTimeout = preset.duration  // not calibrated → fixed
                            }
                            Settings.brew.steamFlow = preset.flow !== undefined ? preset.flow : 150
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

                    // Long-press a pitcher to open the steam page settings, where you
                    // set the duration and tap Calibrate (with milk on the scale) to
                    // teach this pitcher its milk-weight -> steam-time reference.
                    onPresetLongPressed: function(index) {
                        Settings.brew.selectedSteamPitcher = index
                        pageStack.push(Qt.resolvedUrl("SteamPage.qml"))
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

                    // Bean weight: weigh the dose from the scale (minus the stored
                    // dose-cup tare) and apply it — sets the dose and the stop-at-weight
                    // (dose x ratio) only; temperature and grind are left untouched. The
                    // button shows a live preview of the net beans on the scale.
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: ScaleDevice.connected && !ScaleDevice.isFlowScale
                        spacing: Theme.scaled(8)

                        // Small, unobtrusive live net-weight readout. Beans now
                        // auto-capture when stable, so this is a readout (not a
                        // button) — it shows the live net beans and briefly flashes
                        // the captured dose in the accent color.
                        Text {
                            id: weighBeansText
                            horizontalAlignment: Text.AlignHCenter
                            // True while prompting the user to place beans (nothing on
                            // the scale yet) — this state gently blinks.
                            readonly property bool showingPlacePrompt: !idlePage.beanCaptureShown
                                && Math.max(0, MachineState.scaleWeight - Settings.brew.doseCupTareWeight) < 1
                            text: {
                                if (idlePage.beanCaptureShown)
                                    return idlePage.beanCaptureText
                                var net = Math.max(0, MachineState.scaleWeight - Settings.brew.doseCupTareWeight)
                                if (net >= 1)
                                    return net.toFixed(1) + " g " + TranslationManager.translate("idle.label.onScale", "on scale")
                                return TranslationManager.translate("idle.label.placeBeansOnScale", "Place Beans on Scale") + "\n"
                                     + TranslationManager.translate("idle.label.placeBeansHint", "(and wait for the beep before removing)")
                            }
                            color: idlePage.beanCaptureShown ? Theme.primaryColor : Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(14)
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
    // Bottom bar (from layout bottomLeft/bottomRight zones)
    // ============================================================
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.bottomBarHeight
        color: Theme.bottomBarColor

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingMedium

            LayoutBarZone {
                zoneName: "bottomLeft"
                items: idlePage.bottomLeftItems
                Layout.fillHeight: true
            }

            Item { Layout.fillWidth: true }

            LayoutBarZone {
                zoneName: "bottomRight"
                items: idlePage.bottomRightItems
                Layout.fillHeight: true
            }
        }
    }

    // Profile preview popup for long-press on espresso pills
    ProfilePreviewPopup {
        id: profilePreviewPopup
    }
}
