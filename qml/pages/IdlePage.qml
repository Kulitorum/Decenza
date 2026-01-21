import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: idlePage
    objectName: "idlePage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: {
        root.currentPageTitle = "Idle"
        espressoButton.forceActiveFocus()
    }
    StackView.onActivated: {
        root.currentPageTitle = "Idle"
        espressoButton.forceActiveFocus()
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
                // Generate fake shot data
                MainController.generateFakeShotData()
                // Navigate: push EspressoPage, then BeanInfoPage on top
                pageStack.push(Qt.resolvedUrl("EspressoPage.qml"))
                // Small delay to let espresso page load, then push metadata
                fakeShowMetadataTimer.start()
            }
        }

        Timer {
            id: fakeShowMetadataTimer
            interval: 300
            onTriggered: {
                // DEV: Show post-shot review with most recent shot (if any)
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

    // Track which function's presets are showing
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

    // Main content area - centered, offset down to account for top status section
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: Theme.scaled(50)  // Push down to avoid status overlap
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        spacing: Theme.scaled(20)

        // Card containing main action buttons
        Rectangle {
            id: mainButtonsCard
            Layout.fillWidth: true
            Layout.preferredHeight: buttonHeight + Theme.scaled(20)
            color: "transparent"

            // Calculate button size to fit available width
            readonly property int buttonCount: 4 + (beanInfoButton.visible ? 1 : 0) + (historyButton.visible ? 1 : 0) + (autoFavoritesButton.visible ? 1 : 0)  // 4 main + shotInfo + history + autoFavorites
            readonly property real availableWidth: width - Theme.scaled(20) - (buttonCount - 1) * Theme.scaled(10)
            readonly property real buttonWidth: Math.min(Theme.scaled(150), availableWidth / buttonCount)
            // Fixed height to ensure content always fits
            readonly property real buttonHeight: Theme.scaled(120)

            RowLayout {
                id: mainButtonsRow
                anchors.centerIn: parent
                spacing: Theme.scaled(10)

                ActionButton {
                    id: espressoButton
                    implicitWidth: mainButtonsCard.buttonWidth
                    implicitHeight: mainButtonsCard.buttonHeight
                    translationKey: "idle.button.espresso"
                    translationFallback: "Espresso"
                    iconSource: "qrc:/icons/espresso.svg"
                    enabled: DE1Device.guiEnabled
                    // Green when a non-favorite profile is loaded (selectedFavoriteProfile === -1)
                    backgroundColor: Settings.selectedFavoriteProfile === -1 ? Theme.successColor : Theme.primaryColor
                    onClicked: {
                        activePresetFunction = (activePresetFunction === "espresso") ? "" : "espresso"
                    }
                    onPressAndHold: root.goToProfileSelector()
                    onDoubleClicked: root.goToProfileSelector()

                    KeyNavigation.right: steamButton
                    KeyNavigation.down: sleepButton

                    Accessible.description: TranslationManager.translate("idle.accessible.espresso.description", "Start espresso. Double-tap to select profile. Long-press for settings.")
                }

                ActionButton {
                    id: steamButton
                    implicitWidth: mainButtonsCard.buttonWidth
                    implicitHeight: mainButtonsCard.buttonHeight
                    translationKey: "idle.button.steam"
                    translationFallback: "Steam"
                    iconSource: "qrc:/icons/steam.svg"
                    enabled: DE1Device.guiEnabled
                    onClicked: {
                        activePresetFunction = (activePresetFunction === "steam") ? "" : "steam"
                    }
                    onPressAndHold: root.goToSteam()
                    onDoubleClicked: root.goToSteam()

                    KeyNavigation.left: espressoButton
                    KeyNavigation.right: hotWaterButton
                    KeyNavigation.down: activePresetFunction === "steam" ? steamPresetRow : sleepButton

                    Accessible.description: TranslationManager.translate("idle.accessible.steam.description", "Start steaming milk. Long-press to configure.")
                }

                ActionButton {
                    id: hotWaterButton
                    implicitWidth: mainButtonsCard.buttonWidth
                    implicitHeight: mainButtonsCard.buttonHeight
                    translationKey: "idle.button.hotwater"
                    translationFallback: "Hot Water"
                    iconSource: "qrc:/icons/water.svg"
                    enabled: DE1Device.guiEnabled
                    onClicked: {
                        activePresetFunction = (activePresetFunction === "hotwater") ? "" : "hotwater"
                    }
                    onPressAndHold: root.goToHotWater()
                    onDoubleClicked: root.goToHotWater()

                    KeyNavigation.left: steamButton
                    KeyNavigation.right: flushButton
                    KeyNavigation.down: activePresetFunction === "hotwater" ? hotWaterPresetRow : settingsButton

                    Accessible.description: TranslationManager.translate("idle.accessible.hotwater.description", "Dispense hot water. Long-press to configure.")
                }

                ActionButton {
                    id: flushButton
                    implicitWidth: mainButtonsCard.buttonWidth
                    implicitHeight: mainButtonsCard.buttonHeight
                    translationKey: "idle.button.flush"
                    translationFallback: "Flush"
                    iconSource: "qrc:/icons/flush.svg"
                    enabled: DE1Device.guiEnabled
                    onClicked: {
                        activePresetFunction = (activePresetFunction === "flush") ? "" : "flush"
                    }
                    onPressAndHold: root.goToFlush()
                    onDoubleClicked: root.goToFlush()

                    KeyNavigation.left: hotWaterButton
                    KeyNavigation.right: beanInfoButton.visible ? beanInfoButton : (historyButton.visible ? historyButton : null)
                    KeyNavigation.down: activePresetFunction === "flush" ? flushPresetRow : settingsButton

                    Accessible.description: TranslationManager.translate("idle.accessible.flush.description", "Flush the group head. Long-press to configure.")
                }

                ActionButton {
                    id: beanInfoButton
                    implicitWidth: mainButtonsCard.buttonWidth
                    implicitHeight: mainButtonsCard.buttonHeight
                    translationKey: "idle.button.beaninfo"
                    translationFallback: "Beans"
                    iconSource: "qrc:/icons/edit.svg"
                    iconSize: Theme.scaled(43)
                    backgroundColor: Theme.primaryColor
                    visible: Settings.visualizerExtendedMetadata
                    enabled: DE1Device.guiEnabled
                    onClicked: {
                        activePresetFunction = (activePresetFunction === "beans") ? "" : "beans"
                    }
                    onPressAndHold: pageStack.push(Qt.resolvedUrl("BeanInfoPage.qml"))
                    onDoubleClicked: pageStack.push(Qt.resolvedUrl("BeanInfoPage.qml"))

                    KeyNavigation.left: flushButton
                    KeyNavigation.right: historyButton.visible ? historyButton : null
                    KeyNavigation.down: activePresetFunction === "beans" ? beanPresetRow : settingsButton

                    Accessible.description: TranslationManager.translate("idle.accessible.beaninfo.description", "Set up bean and grinder info for your shots. Long-press for settings.")
                }

                ActionButton {
                    id: historyButton
                    visible: Settings.showHistoryButton
                    implicitWidth: mainButtonsCard.buttonWidth
                    implicitHeight: mainButtonsCard.buttonHeight
                    translationKey: "idle.button.history"
                    translationFallback: "History"
                    iconSource: "qrc:/icons/espresso.svg"
                    iconSize: Theme.scaled(43)
                    backgroundColor: Theme.primaryColor
                    onClicked: pageStack.push(Qt.resolvedUrl("ShotHistoryPage.qml"))

                    KeyNavigation.left: beanInfoButton.visible ? beanInfoButton : flushButton
                    KeyNavigation.right: autoFavoritesButton.visible ? autoFavoritesButton : null
                    KeyNavigation.down: settingsButton

                    Accessible.description: TranslationManager.translate("idle.accessible.history.description", "View and compare past shots")
                }

                ActionButton {
                    id: autoFavoritesButton
                    visible: Settings.autoFavoritesEnabled
                    implicitWidth: mainButtonsCard.buttonWidth
                    implicitHeight: mainButtonsCard.buttonHeight
                    translationKey: "idle.button.autofavorites"
                    translationFallback: "Favorites"
                    iconSource: "qrc:/icons/star.svg"
                    iconSize: Theme.scaled(43)
                    backgroundColor: Theme.warningColor
                    onClicked: pageStack.push(Qt.resolvedUrl("AutoFavoritesPage.qml"))

                    KeyNavigation.left: historyButton.visible ? historyButton :
                                        (beanInfoButton.visible ? beanInfoButton : flushButton)
                    KeyNavigation.down: settingsButton

                    Accessible.description: TranslationManager.translate("idle.accessible.autofavorites.description", "Open auto-favorites list of recent bean and profile combinations")
                }
            }
        }

        // Single container for all preset rows - ensures consistent Y position
        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: activePresetFunction !== "" ? activePresetRow.implicitHeight : 0
            Layout.fillWidth: true
            Layout.maximumWidth: Theme.scaled(900)
            Layout.leftMargin: Theme.standardMargin
            Layout.rightMargin: Theme.standardMargin
            clip: true

            // Get the currently active preset loader
            property var activePresetRow: {
                switch (activePresetFunction) {
                    case "steam": return steamPresetLoader
                    case "espresso": return espressoColumnLoader
                    case "hotwater": return hotWaterPresetLoader
                    case "flush": return flushPresetLoader
                    case "beans": return beanPresetLoader
                    default: return steamPresetLoader  // fallback
                }
            }

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
            }

            // All preset rows stacked in same position - use Loaders for lazy creation
            Loader {
                id: steamPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "steam"
                visible: active
                sourceComponent: PresetPillRow {
                    id: steamPresetRow
                    maxWidth: steamPresetLoader.width
                    presets: Settings.steamPitcherPresets
                    selectedIndex: Settings.selectedSteamPitcher

                    KeyNavigation.up: steamButton
                    KeyNavigation.down: sleepButton

                    onPresetSelected: function(index) {
                        var wasAlreadySelected = (index === Settings.selectedSteamPitcher)
                        console.log("[IdlePage] Steam pill selected, index:", index, "wasAlreadySelected:", wasAlreadySelected, "isReady:", MachineState.isReady)
                        Settings.selectedSteamPitcher = index
                        var preset = Settings.getSteamPitcherPreset(index)
                        if (preset) {
                            Settings.steamTimeout = preset.duration
                            Settings.steamFlow = preset.flow !== undefined ? preset.flow : 150
                        }
                        MainController.applySteamSettings()

                        if (wasAlreadySelected) {
                            if (MachineState.isReady) {
                                console.log("[IdlePage] Starting steam...")
                                DE1Device.startSteam()
                            } else {
                                console.log("[IdlePage] NOT starting steam - MachineState.isReady is false, phase:", MachineState.phase)
                            }
                        }
                    }
                }
            }

            // Espresso presets - column containing favorites + optional non-favorite pill
            Loader {
                id: espressoColumnLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "espresso"
                visible: active
                sourceComponent: Column {
                    id: espressoColumn
                    width: parent ? parent.width : 0
                    spacing: Theme.scaled(8)

                    PresetPillRow {
                        id: espressoPresetRow
                        anchors.horizontalCenter: parent.horizontalCenter
                        maxWidth: espressoColumnLoader.width

                        presets: Settings.favoriteProfiles
                        selectedIndex: Settings.selectedFavoriteProfile
                        supportLongPress: true

                        KeyNavigation.up: espressoButton
                        KeyNavigation.down: sleepButton

                        // Note: Profile is already loaded by MainController on startup.
                        // Don't re-load here as it would override the correct profile with
                        // whatever is at selectedFavoriteProfile index (bug fix).

                        onPresetSelected: function(index) {
                            var wasAlreadySelected = (index === Settings.selectedFavoriteProfile)
                            console.log("[IdlePage] Espresso pill selected, index:", index, "wasAlreadySelected:", wasAlreadySelected, "isReady:", MachineState.isReady)
                            Settings.selectedFavoriteProfile = index
                            var preset = Settings.getFavoriteProfile(index)

                            if (wasAlreadySelected) {
                                if (MachineState.isReady) {
                                    console.log("[IdlePage] Starting espresso...")
                                    DE1Device.startEspresso()
                                } else {
                                    console.log("[IdlePage] NOT starting espresso - MachineState.isReady is false, phase:", MachineState.phase)
                                }
                            } else {
                                if (preset && preset.filename) {
                                    console.log("[IdlePage] Loading profile:", preset.filename)
                                    MainController.loadProfile(preset.filename)
                                }
                            }
                        }

                        onPresetLongPressed: function(index) {
                            var preset = Settings.getFavoriteProfile(index)
                            if (preset && preset.filename) {
                                if (index !== Settings.selectedFavoriteProfile) {
                                    console.log("[IdlePage] Long-press selecting profile:", preset.filename)
                                    Settings.selectedFavoriteProfile = index
                                    MainController.loadProfile(preset.filename)
                                }
                                console.log("[IdlePage] Long-press showing preview for:", preset.filename)
                                profilePreviewPopup.profileFilename = preset.filename
                                profilePreviewPopup.profileName = preset.name || ""
                                profilePreviewPopup.open()
                            }
                        }
                    }

                    // Green pill showing non-favorite profile name (when loaded from history)
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: Settings.selectedFavoriteProfile === -1
                        spacing: Theme.scaled(8)

                        Rectangle {
                            id: nonFavoriteProfilePill
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
                                        console.log("[IdlePage] Starting espresso with non-favorite profile...")
                                        DE1Device.startEspresso()
                                    } else {
                                        console.log("[IdlePage] NOT starting espresso - MachineState.isReady is false, phase:", MachineState.phase)
                                    }
                                }
                            }
                        }

                        // Info button for non-favorite profile
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
                    id: hotWaterPresetRow
                    maxWidth: hotWaterPresetLoader.width
                    presets: Settings.waterVesselPresets
                    selectedIndex: Settings.selectedWaterVessel

                    KeyNavigation.up: hotWaterButton
                    KeyNavigation.down: settingsButton

                    onPresetSelected: function(index) {
                        var wasAlreadySelected = (index === Settings.selectedWaterVessel)
                        console.log("[IdlePage] HotWater pill selected, index:", index, "wasAlreadySelected:", wasAlreadySelected, "isReady:", MachineState.isReady)
                        Settings.selectedWaterVessel = index
                        var preset = Settings.getWaterVesselPreset(index)
                        if (preset) {
                            Settings.waterVolume = preset.volume
                        }
                        MainController.applyHotWaterSettings()

                        if (wasAlreadySelected) {
                            if (MachineState.isReady) {
                                console.log("[IdlePage] Starting hot water...")
                                DE1Device.startHotWater()
                            } else {
                                console.log("[IdlePage] NOT starting hot water - MachineState.isReady is false, phase:", MachineState.phase)
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
                    id: flushPresetRow
                    maxWidth: flushPresetLoader.width
                    presets: Settings.flushPresets
                    selectedIndex: Settings.selectedFlushPreset

                    KeyNavigation.up: flushButton
                    KeyNavigation.down: settingsButton

                    onPresetSelected: function(index) {
                        var wasAlreadySelected = (index === Settings.selectedFlushPreset)
                        console.log("[IdlePage] Flush pill selected, index:", index, "wasAlreadySelected:", wasAlreadySelected, "isReady:", MachineState.isReady)
                        Settings.selectedFlushPreset = index
                        var preset = Settings.getFlushPreset(index)
                        if (preset) {
                            Settings.flushFlow = preset.flow
                            Settings.flushSeconds = preset.seconds
                        }
                        MainController.applyFlushSettings()

                        if (wasAlreadySelected) {
                            if (MachineState.isReady) {
                                console.log("[IdlePage] Starting flush...")
                                DE1Device.startFlush()
                            } else {
                                console.log("[IdlePage] NOT starting flush - MachineState.isReady is false, phase:", MachineState.phase)
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
                    id: beanPresetRow
                    maxWidth: beanPresetLoader.width
                    presets: Settings.beanPresets
                    selectedIndex: Settings.selectedBeanPreset

                    KeyNavigation.up: beanInfoButton
                    KeyNavigation.down: settingsButton

                    onPresetSelected: function(index) {
                        console.log("[IdlePage] Bean pill selected, index:", index)
                        Settings.selectedBeanPreset = index
                        Settings.applyBeanPreset(index)
                    }
                }
            }
        }
    }

    // Top info section
    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin  // Leave room for status bar
        spacing: Theme.scaled(20)

        // Status section
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.scaled(50)

            // Temperature (tap to announce, double-tap to set override)
            Item {
                id: temperatureStatus
                Layout.alignment: Qt.AlignTop
                implicitWidth: temperatureColumn.width
                implicitHeight: temperatureColumn.height

                // Effective target temperature (override or profile)
                readonly property double effectiveTargetTemp: Settings.hasTemperatureOverride
                    ? Settings.temperatureOverride
                    : MainController.profileTargetTemperature

                ColumnLayout {
                    id: temperatureColumn
                    spacing: Theme.spacingSmall
                    // Temperature display: current / target (target smaller, colored blue when override)
                    Row {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Theme.scaled(4)
                        Text {
                            text: DE1Device.temperature.toFixed(1) + "°C"
                            color: Theme.temperatureColor
                            font: Theme.valueFont
                        }
                        Text {
                            anchors.baseline: parent.children[0].baseline
                            text: "/ " + temperatureStatus.effectiveTargetTemp.toFixed(1) + "°C"
                            color: Settings.hasTemperatureOverride ? Theme.primaryColor : Theme.textSecondaryColor
                            font.family: Theme.valueFont.family
                            font.pixelSize: Theme.valueFont.pixelSize / 2
                        }
                    }
                    // Label with override indicator
                    Row {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Theme.scaled(4)
                        Tr {
                            key: "idle.label.grouptemp"
                            fallback: "Group Temp"
                            color: Theme.textSecondaryColor
                            font: Theme.labelFont
                        }
                        Text {
                            visible: Settings.hasTemperatureOverride
                            text: "(override)"
                            color: Theme.primaryColor
                            font: Theme.labelFont
                        }
                    }
                }

                // Double-tap handler for temperature override
                Timer {
                    id: doubleTapTimer
                    interval: 300
                    property bool waitingForSecondTap: false
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (doubleTapTimer.waitingForSecondTap) {
                            // Double tap - open override popup
                            doubleTapTimer.waitingForSecondTap = false
                            doubleTapTimer.stop()
                            temperatureOverridePopup.open()
                        } else {
                            // First tap - start waiting for second
                            doubleTapTimer.waitingForSecondTap = true
                            doubleTapTimer.start()
                        }
                    }
                }

                Connections {
                    target: doubleTapTimer
                    function onTriggered() {
                        // Timer expired without second tap - announce for accessibility
                        doubleTapTimer.waitingForSecondTap = false
                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                            var announcement = "Group temperature: " + DE1Device.temperature.toFixed(1) + " degrees, target: " + temperatureStatus.effectiveTargetTemp.toFixed(0) + " degrees"
                            if (Settings.hasTemperatureOverride) {
                                announcement += " (override active)"
                            }
                            AccessibilityManager.announceLabel(announcement)
                        }
                    }
                }
            }

            // Water level (tap to announce)
            Item {
                id: waterLevelStatus
                Layout.alignment: Qt.AlignTop
                implicitWidth: waterLevelColumn.width
                implicitHeight: waterLevelColumn.height

                property bool showMl: Settings.waterLevelDisplayUnit === "ml"

                ColumnLayout {
                    id: waterLevelColumn
                    spacing: Theme.spacingSmall
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: waterLevelStatus.showMl
                            ? DE1Device.waterLevelMl + " ml"
                            : DE1Device.waterLevel.toFixed(0) + "%"
                        color: DE1Device.waterLevel > 20 ? Theme.primaryColor : Theme.warningColor
                        font: Theme.valueFont
                    }
                    Tr {
                        Layout.alignment: Qt.AlignHCenter
                        key: "idle.label.waterlevel"
                        fallback: "Water Level"
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                            var warning = DE1Device.waterLevel <= 20 ? ". Warning: water level is low" : ""
                            if (waterLevelStatus.showMl) {
                                AccessibilityManager.announceLabel("Water level: " + DE1Device.waterLevelMl + " milliliters" + warning)
                            } else {
                                AccessibilityManager.announceLabel("Water level: " + DE1Device.waterLevel.toFixed(0) + " percent" + warning)
                            }
                        }
                    }
                }
            }

            // Connection status (tap to announce)
            Item {
                id: connectionStatus
                Layout.alignment: Qt.AlignTop
                implicitWidth: connectionIndicator.implicitWidth
                implicitHeight: connectionIndicator.implicitHeight

                ConnectionIndicator {
                    id: connectionIndicator
                    machineConnected: DE1Device.connected
                    scaleConnected: ScaleDevice && ScaleDevice.connected
                    isFlowScale: ScaleDevice && ScaleDevice.name === "Flow Scale"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                            var status = DE1Device.connected ? "Machine connected" : "Machine disconnected"
                            if (ScaleDevice && ScaleDevice.connected) {
                                if (ScaleDevice.name === "Flow Scale") {
                                    status += ". Using simulated scale from flow sensor"
                                } else {
                                    status += ". Scale connected: " + ScaleDevice.name
                                }
                            } else {
                                status += ". No scale connected"
                            }
                            AccessibilityManager.announceLabel(status)
                        }
                    }
                }
            }
        }
    }

    // Bottom bar with Sleep and Settings
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

            // Sleep button - fills bar height
            Item {
                id: sleepButton
                Layout.fillHeight: true
                Layout.preferredWidth: sleepButtonBg.implicitWidth
                Layout.topMargin: Theme.spacingSmall
                Layout.bottomMargin: Theme.spacingSmall
                activeFocusOnTab: true

                property bool enabled: DE1Device.guiEnabled

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("idle.accessible.sleep", "Sleep")
                Accessible.description: TranslationManager.translate("idle.accessible.sleep.description", "Put the machine to sleep")
                Accessible.focusable: true

                KeyNavigation.up: espressoButton
                KeyNavigation.right: settingsButton

                function doSleep() {
                    console.log("[IdlePage] doSleep called, enabled:", enabled)
                    if (!enabled) return
                    // Put scale to LCD-off mode (keep connected for wake)
                    if (ScaleDevice && ScaleDevice.connected) {
                        ScaleDevice.disableLcd()  // LCD off only, stay connected
                        // Do NOT disconnect - scale stays connected for wake
                    }
                    DE1Device.goToSleep()
                    console.log("[IdlePage] Calling goToScreensaver")
                    root.goToScreensaver()
                }

                Keys.onReturnPressed: doSleep()
                Keys.onEnterPressed: doSleep()
                Keys.onSpacePressed: doSleep()

                Rectangle {
                    id: sleepButtonBg
                    anchors.fill: parent
                    implicitWidth: Theme.scaled(140)
                    color: sleepTapHandler.isPressed ? Qt.darker("#555555", 1.2) : "#555555"
                    radius: Theme.cardRadius
                    opacity: sleepButton.enabled ? 1.0 : 0.5

                    // Focus indicator
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -Theme.focusMargin
                        visible: sleepButton.activeFocus
                        color: "transparent"
                        border.width: Theme.focusBorderWidth
                        border.color: Theme.focusColor
                        radius: parent.radius + Theme.focusMargin
                    }
                }

                RowLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall
                    Image {
                        source: "qrc:/icons/sleep.svg"
                        sourceSize.width: Theme.scaled(28)
                        sourceSize.height: Theme.scaled(28)
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Tr {
                        key: "idle.button.sleep"
                        fallback: "Sleep"
                        font: Theme.bodyFont
                        color: "white"
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Using TapHandler for better touch responsiveness
                AccessibleTapHandler {
                    id: sleepTapHandler
                    anchors.fill: parent
                    enabled: sleepButton.enabled
                    supportLongPress: true
                    longPressInterval: 1000
                    accessibleName: TranslationManager.translate("idle.accessible.sleep", "Sleep") + ". " + TranslationManager.translate("idle.accessible.sleep.description", "Put the machine to sleep")
                    accessibleItem: sleepButton
                    onAccessibleClicked: {
                        console.log("[IdlePage] Sleep button tapped")
                        sleepButton.doSleep()
                    }
                    onAccessibleLongPressed: Qt.quit()
                }
            }

            Item { Layout.fillWidth: true }

            // Settings button - square, fills bar height
            Item {
                id: settingsButton
                Layout.preferredWidth: Theme.bottomBarHeight
                Layout.preferredHeight: Theme.bottomBarHeight
                activeFocusOnTab: true

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("idle.accessible.settings", "Settings")
                Accessible.description: TranslationManager.translate("idle.accessible.settings.description", "Open application settings")
                Accessible.focusable: true

                KeyNavigation.up: flushButton
                KeyNavigation.left: sleepButton

                Keys.onReturnPressed: root.goToSettings()
                Keys.onEnterPressed: root.goToSettings()
                Keys.onSpacePressed: root.goToSettings()

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/settings.svg"
                    sourceSize.width: Theme.scaled(32)
                    sourceSize.height: Theme.scaled(32)
                }

                // Focus indicator
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -Theme.focusMargin
                    visible: settingsButton.activeFocus
                    color: "transparent"
                    border.width: Theme.focusBorderWidth
                    border.color: Theme.focusColor
                    radius: Theme.scaled(4)
                }

                // Using TapHandler for better touch responsiveness
                AccessibleTapHandler {
                    anchors.fill: parent
                    accessibleName: "Settings. Open application settings"
                    accessibleItem: settingsButton
                    onAccessibleClicked: root.goToSettings()
                }
            }
        }
    }

    // Profile preview popup for long-press on espresso pills
    ProfilePreviewPopup {
        id: profilePreviewPopup
    }

    // Temperature override popup
    Dialog {
        id: temperatureOverridePopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Theme.scaled(350)
        modal: true
        padding: 0

        property double tempValue: Settings.hasTemperatureOverride
            ? Settings.temperatureOverride
            : MainController.profileTargetTemperature

        onAboutToShow: {
            tempValue = Settings.hasTemperatureOverride
                ? Settings.temperatureOverride
                : MainController.profileTargetTemperature
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: "white"
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.topMargin: Theme.scaled(10)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Temperature Override")
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Content
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(16)

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Set a one-shot temperature override for the next espresso. This will reset after the shot completes.")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    wrapMode: Text.Wrap
                }

                // Temperature input
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: qsTr("Temperature:")
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                    }

                    ValueInput {
                        id: tempOverrideInput
                        Layout.fillWidth: true
                        value: temperatureOverridePopup.tempValue
                        from: 70
                        to: 100
                        stepSize: 0.5
                        decimals: 1
                        suffix: "°C"
                        valueColor: Theme.temperatureColor
                        accentColor: Theme.temperatureColor
                        accessibleName: qsTr("Temperature override")
                        onValueModified: function(newValue) {
                            temperatureOverridePopup.tempValue = newValue
                        }
                    }
                }

                // Profile default reference
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Profile default: %1°C").arg(MainController.profileTargetTemperature.toFixed(1))
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                }
            }

            // Buttons
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                spacing: Theme.scaled(10)

                // Clear override button (only visible if override is active)
                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(50)
                    visible: Settings.hasTemperatureOverride
                    text: qsTr("Clear")
                    accessibleName: qsTr("Clear temperature override")
                    onClicked: {
                        Settings.clearTemperatureOverride()
                        MainController.uploadCurrentProfile()
                        temperatureOverridePopup.close()
                    }
                    background: Rectangle {
                        implicitHeight: Theme.scaled(50)
                        radius: Theme.buttonRadius
                        color: parent.down ? Qt.darker(Theme.warningColor, 1.2) : Theme.warningColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(50)
                    text: qsTr("Cancel")
                    accessibleName: qsTr("Cancel temperature override")
                    onClicked: temperatureOverridePopup.close()
                    background: Rectangle {
                        implicitHeight: Theme.scaled(50)
                        radius: Theme.buttonRadius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.textSecondaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(50)
                    text: qsTr("Set")
                    accessibleName: qsTr("Set temperature override")
                    onClicked: {
                        Settings.temperatureOverride = temperatureOverridePopup.tempValue
                        MainController.uploadCurrentProfile()
                        temperatureOverridePopup.close()
                    }
                    background: Rectangle {
                        implicitHeight: Theme.scaled(50)
                        radius: Theme.buttonRadius
                        color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }

}
