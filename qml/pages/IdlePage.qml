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
    property string activePresetFunction: ""  // "", "steam", "espresso", "hotwater", "flush"

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
            implicitHeight: mainButtonsRow.implicitHeight + Theme.scaled(20)
            color: "transparent"

            // Calculate button size to fit available width while maintaining aspect ratio
            readonly property int buttonCount: 4 + (beanInfoButton.visible ? 1 : 0) + (historyButton.visible ? 1 : 0)  // 4 main + shotInfo + history
            readonly property real availableWidth: width - Theme.scaled(20) - (buttonCount - 1) * Theme.scaled(10)
            readonly property real buttonWidth: Math.min(Theme.scaled(150), availableWidth / buttonCount)
            readonly property real buttonHeight: buttonWidth * 0.8  // Maintain 150:120 aspect ratio

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
                    KeyNavigation.down: activePresetFunction === "espresso" ? espressoPresetRow : sleepButton

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
                    translationFallback: "Bean Info"
                    iconSource: "qrc:/icons/edit.svg"
                    iconSize: Theme.scaled(43)
                    backgroundColor: Theme.primaryColor
                    visible: Settings.visualizerExtendedMetadata
                    enabled: DE1Device.guiEnabled
                    onClicked: pageStack.push(Qt.resolvedUrl("BeanInfoPage.qml"))

                    KeyNavigation.left: flushButton
                    KeyNavigation.right: historyButton.visible ? historyButton : null
                    KeyNavigation.down: settingsButton

                    Accessible.description: TranslationManager.translate("idle.accessible.beaninfo.description", "Set up bean and grinder info for your shots.")
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
                    KeyNavigation.down: settingsButton

                    Accessible.description: TranslationManager.translate("idle.accessible.history.description", "View and compare past shots")
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

            // Get the currently active preset row
            property var activePresetRow: {
                switch (activePresetFunction) {
                    case "steam": return steamPresetRow
                    case "espresso": return espressoColumn
                    case "hotwater": return hotWaterPresetRow
                    case "flush": return flushPresetRow
                    default: return steamPresetRow  // fallback
                }
            }

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
            }

            // All preset rows stacked in same position
            PresetPillRow {
                id: steamPresetRow
                anchors.horizontalCenter: parent.horizontalCenter
                visible: activePresetFunction === "steam"
                opacity: visible ? 1.0 : 0.0

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
                        // Tap on already-selected pill = start operation (settings already applied)
                        if (MachineState.isReady) {
                            console.log("[IdlePage] Starting steam...")
                            DE1Device.startSteam()
                        } else {
                            console.log("[IdlePage] NOT starting steam - MachineState.isReady is false, phase:", MachineState.phase)
                        }
                    }
                    // Otherwise just select (settings applied above, ready for next tap)
                }

                Behavior on opacity { NumberAnimation { duration: 150 } }
            }

            // Espresso presets - column containing favorites + optional non-favorite pill
            Column {
                id: espressoColumn
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                visible: activePresetFunction === "espresso"
                opacity: visible ? 1.0 : 0.0
                spacing: Theme.scaled(8)

                PresetPillRow {
                    id: espressoPresetRow
                    anchors.horizontalCenter: parent.horizontalCenter

                    presets: Settings.favoriteProfiles
                    selectedIndex: Settings.selectedFavoriteProfile

                    KeyNavigation.up: espressoButton
                    KeyNavigation.down: sleepButton

                    // Pre-load the selected profile when pills become visible
                    onVisibleChanged: {
                        if (visible && Settings.selectedFavoriteProfile >= 0) {
                            var preset = Settings.getFavoriteProfile(Settings.selectedFavoriteProfile)
                            if (preset && preset.filename) {
                                MainController.loadProfile(preset.filename)
                            }
                        }
                    }

                    onPresetSelected: function(index) {
                        var wasAlreadySelected = (index === Settings.selectedFavoriteProfile)
                        console.log("[IdlePage] Espresso pill selected, index:", index, "wasAlreadySelected:", wasAlreadySelected, "isReady:", MachineState.isReady)
                        Settings.selectedFavoriteProfile = index
                        var preset = Settings.getFavoriteProfile(index)

                        if (wasAlreadySelected) {
                            // Tap on already-selected pill = start operation (profile already uploaded)
                            if (MachineState.isReady) {
                                console.log("[IdlePage] Starting espresso...")
                                DE1Device.startEspresso()
                            } else {
                                console.log("[IdlePage] NOT starting espresso - MachineState.isReady is false, phase:", MachineState.phase)
                            }
                        } else {
                            // Tap on different pill = select it and upload profile
                            if (preset && preset.filename) {
                                console.log("[IdlePage] Loading profile:", preset.filename)
                                MainController.loadProfile(preset.filename)
                            }
                        }
                    }
                }

                // Green pill showing non-favorite profile name (when loaded from history)
                Rectangle {
                    id: nonFavoriteProfilePill
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: Settings.selectedFavoriteProfile === -1
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
                            // Tap on loaded profile pill = start espresso
                            if (MachineState.isReady) {
                                console.log("[IdlePage] Starting espresso with non-favorite profile...")
                                DE1Device.startEspresso()
                            } else {
                                console.log("[IdlePage] NOT starting espresso - MachineState.isReady is false, phase:", MachineState.phase)
                            }
                        }
                    }
                }

                Behavior on opacity { NumberAnimation { duration: 150 } }
            }

            PresetPillRow {
                id: hotWaterPresetRow
                anchors.horizontalCenter: parent.horizontalCenter
                visible: activePresetFunction === "hotwater"
                opacity: visible ? 1.0 : 0.0

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
                        // Tap on already-selected pill = start operation (settings already applied)
                        if (MachineState.isReady) {
                            console.log("[IdlePage] Starting hot water...")
                            DE1Device.startHotWater()
                        } else {
                            console.log("[IdlePage] NOT starting hot water - MachineState.isReady is false, phase:", MachineState.phase)
                        }
                    }
                    // Otherwise just select (settings applied above, ready for next tap)
                }

                Behavior on opacity { NumberAnimation { duration: 150 } }
            }

            PresetPillRow {
                id: flushPresetRow
                anchors.horizontalCenter: parent.horizontalCenter
                visible: activePresetFunction === "flush"
                opacity: visible ? 1.0 : 0.0

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
                        // Tap on already-selected pill = start operation (settings already applied)
                        if (MachineState.isReady) {
                            console.log("[IdlePage] Starting flush...")
                            DE1Device.startFlush()
                        } else {
                            console.log("[IdlePage] NOT starting flush - MachineState.isReady is false, phase:", MachineState.phase)
                        }
                    }
                    // Otherwise just select (settings applied above, ready for next tap)
                }

                Behavior on opacity { NumberAnimation { duration: 150 } }
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

            // Temperature (tap to announce)
            Item {
                id: temperatureStatus
                Layout.alignment: Qt.AlignTop
                implicitWidth: temperatureColumn.width
                implicitHeight: temperatureColumn.height

                ColumnLayout {
                    id: temperatureColumn
                    spacing: Theme.spacingSmall
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: DE1Device.temperature.toFixed(1) + "°C"
                        color: Theme.temperatureColor
                        font: Theme.valueFont
                    }
                    Tr {
                        Layout.alignment: Qt.AlignHCenter
                        key: "idle.label.grouptemp"
                        fallback: "Group Temp"
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                            AccessibilityManager.announceLabel("Group temperature: " + DE1Device.temperature.toFixed(1) + " degrees Celsius")
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
                    // Put scale to sleep and disconnect (if connected)
                    if (ScaleDevice && ScaleDevice.connected) {
                        ScaleDevice.sleep()
                        scaleDisconnectTimer.start()
                    }
                    DE1Device.goToSleep()
                    console.log("[IdlePage] Calling goToScreensaver")
                    root.goToScreensaver()
                }

                Keys.onReturnPressed: doSleep()
                Keys.onEnterPressed: doSleep()
                Keys.onSpacePressed: doSleep()

                Timer {
                    id: scaleDisconnectTimer
                    interval: 300
                    repeat: false
                    onTriggered: {
                        if (ScaleDevice) {
                            ScaleDevice.disconnectFromScale()
                        }
                    }
                }

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

}
