import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"
import "../components/layout"

Page {
    id: idlePage
    objectName: "idlePage"
    background: Rectangle { color: Theme.backgroundColor }

    StackView.onActivated: {
        root.currentPageTitle = "Idle"
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
        var raw = Settings.layoutConfiguration
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
        root.currentPageTitle = "Idle"
    }

    // Track which function's presets are showing (used by center-zone action items)
    property string activePresetFunction: ""  // "", "steam", "espresso", "hotwater", "flush", "beans"

    // Announce presets when they appear (accessibility)
    onActivePresetFunctionChanged: {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled && activePresetFunction !== "") {
            var presets = []
            var selectedName = ""
            switch (activePresetFunction) {
                case "espresso":
                    presets = Settings.favoriteProfiles
                    if (Settings.selectedFavoriteProfile >= 0 && Settings.selectedFavoriteProfile < presets.length) {
                        selectedName = presets[Settings.selectedFavoriteProfile].name
                    }
                    break
                case "steam":
                    presets = Settings.steamPitcherPresets
                    if (Settings.selectedSteamPitcher >= 0 && Settings.selectedSteamPitcher < presets.length) {
                        selectedName = presets[Settings.selectedSteamPitcher].name
                    }
                    break
                case "hotwater":
                    presets = Settings.waterVesselPresets
                    if (Settings.selectedWaterVessel >= 0 && Settings.selectedWaterVessel < presets.length) {
                        selectedName = presets[Settings.selectedWaterVessel].name
                    }
                    break
                case "flush":
                    presets = Settings.flushPresets
                    if (Settings.selectedFlushPreset >= 0 && Settings.selectedFlushPreset < presets.length) {
                        selectedName = presets[Settings.selectedFlushPreset].name
                    }
                    break
                case "beans":
                    presets = Settings.beanPresets
                    if (Settings.selectedBeanPreset >= 0 && Settings.selectedBeanPreset < presets.length) {
                        selectedName = presets[Settings.selectedBeanPreset].name
                    }
                    break
            }

            if (presets.length > 0) {
                var names = []
                for (var i = 0; i < presets.length; i++) {
                    names.push(presets[i].name)
                }
                var announcement = presets.length + " presets: " + names.join(", ")
                if (selectedName !== "") {
                    announcement += ". " + selectedName + " is selected"
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
                sourceComponent: PresetPillRow {
                    maxWidth: steamPresetLoader.width
                    presets: Settings.steamPitcherPresets
                    selectedIndex: Settings.selectedSteamPitcher

                    onPresetSelected: function(index) {
                        var wasAlreadySelected = (index === Settings.selectedSteamPitcher)
                        Settings.selectedSteamPitcher = index
                        var preset = Settings.getSteamPitcherPreset(index)
                        if (preset) {
                            Settings.steamTimeout = preset.duration
                            Settings.steamFlow = preset.flow !== undefined ? preset.flow : 150
                        }
                        MainController.applySteamSettings()

                        if (wasAlreadySelected) {
                            if (MachineState.isReady) {
                                DE1Device.startSteam()
                            } else {
                                console.log("Cannot start steam - machine not ready, phase:", MachineState.phase)
                                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                    AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                            }
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

                        presets: Settings.favoriteProfiles
                        selectedIndex: Settings.selectedFavoriteProfile
                        supportLongPress: true

                        onPresetSelected: function(index) {
                            var wasAlreadySelected = (index === Settings.selectedFavoriteProfile)
                            Settings.selectedFavoriteProfile = index
                            var preset = Settings.getFavoriteProfile(index)

                            if (wasAlreadySelected) {
                                if (MachineState.isReady) {
                                    DE1Device.startEspresso()
                                } else {
                                    console.log("Cannot start espresso - machine not ready, phase:", MachineState.phase)
                                    if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                        AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                                }
                            } else {
                                if (preset && preset.filename) {
                                    MainController.loadProfile(preset.filename)
                                }
                            }
                        }

                        onPresetLongPressed: function(index) {
                            var preset = Settings.getFavoriteProfile(index)
                            if (preset && preset.filename) {
                                if (index !== Settings.selectedFavoriteProfile) {
                                    Settings.selectedFavoriteProfile = index
                                    MainController.loadProfile(preset.filename)
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
                        visible: Settings.selectedFavoriteProfile === -1
                        spacing: Theme.scaled(8)

                        Rectangle {
                            width: nonFavoriteProfileText.implicitWidth + Theme.scaled(40)
                            height: Theme.scaled(50)
                            radius: Theme.scaled(10)
                            color: Theme.successColor

                            Text {
                                id: nonFavoriteProfileText
                                anchors.centerIn: parent
                                text: MainController.currentProfileName || ""
                                color: "white"
                                font.pixelSize: Theme.scaled(16)
                                font.bold: true
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (MachineState.isReady) {
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
                            profileFilename: Settings.currentProfile
                            profileName: MainController.currentProfileName

                            onClicked: {
                                pageStack.push(Qt.resolvedUrl("ProfileInfoPage.qml"), {
                                    profileFilename: Settings.currentProfile,
                                    profileName: MainController.currentProfileName
                                })
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
                    presets: Settings.waterVesselPresets
                    selectedIndex: Settings.selectedWaterVessel

                    onPresetSelected: function(index) {
                        var wasAlreadySelected = (index === Settings.selectedWaterVessel)
                        Settings.selectedWaterVessel = index
                        var preset = Settings.getWaterVesselPreset(index)
                        if (preset) {
                            Settings.waterVolume = preset.volume
                        }
                        MainController.applyHotWaterSettings()

                        if (wasAlreadySelected) {
                            if (MachineState.isReady) {
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
                    presets: Settings.flushPresets
                    selectedIndex: Settings.selectedFlushPreset

                    onPresetSelected: function(index) {
                        var wasAlreadySelected = (index === Settings.selectedFlushPreset)
                        Settings.selectedFlushPreset = index
                        var preset = Settings.getFlushPreset(index)
                        if (preset) {
                            Settings.flushFlow = preset.flow
                            Settings.flushSeconds = preset.seconds
                        }
                        MainController.applyFlushSettings()

                        if (wasAlreadySelected) {
                            if (MachineState.isReady) {
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
                    maxWidth: beanPresetLoader.width
                    presets: Settings.beanPresets
                    selectedIndex: Settings.selectedBeanPreset

                    onPresetSelected: function(index) {
                        Settings.selectedBeanPreset = index
                        Settings.applyBeanPreset(index)
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
        color: Theme.surfaceColor

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
