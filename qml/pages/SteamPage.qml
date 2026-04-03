import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "../components"

Page {
    objectName: "steamPage"
    background: Rectangle { color: Theme.backgroundColor }

    property string pageTitle: steamPageTitle.text
    Tr { id: steamPageTitle; key: "steam.title"; fallback: "Steam"; visible: false }

    Component.onCompleted: {
        root.currentPageTitle = pageTitle
        // Sync Settings with selected preset
        Settings.steamTimeout = getCurrentPitcherDuration()
        Settings.steamFlow = getCurrentPitcherFlow()
        // Start heating steam heater (ignores keepSteamHeaterOn - user wants to steam)
        // startSteamHeating clears steamDisabled flag automatically
        MainController.startSteamHeating()
    }
    StackView.onActivated: root.currentPageTitle = pageTitle

    property bool isSteaming: MachineState.phase === MachineStateType.Phase.Steaming || root.debugLiveView
    property int editingPitcherIndex: -1  // For the edit popup
    property bool steamSoftStopped: false  // For two-stage stop on headless machines
    property bool wasSteaming: false  // Track if we were steaming (to turn off heater after)

    // Check if steam heater needs heating
    readonly property real currentSteamTemp: DE1Device.steamTemperature
    readonly property real targetSteamTemp: Settings.steamTemperature
    readonly property bool isHeatingUp: !isSteaming && currentSteamTemp < (targetSteamTemp - 5)  // 5°C tolerance

    // Check if DE1 is in Steam state but still heating (FinalHeating/Heating substate)
    // DE1::State::Steam = 5
    readonly property bool isSteamHeating: DE1Device.state === 5 && !isSteaming

    // Check if DE1 is in Puffing state (waiting for user to press STOP or auto-flush)
    // DE1::SubState::Puffing = 20
    readonly property bool isPuffing: DE1Device.state === 5 && DE1Device.subState === 20

    // Debug logging for steam phase issues
    Connections {
        target: MachineState
        function onPhaseChanged() {
            console.log("SteamPage: MachineState.phase changed to", MachineState.phase, "isSteaming=", isSteaming)
        }
    }
    Connections {
        target: DE1Device
        function onStateChanged() {
            console.log("SteamPage: DE1Device.state changed to", DE1Device.stateString, "(", DE1Device.state, ")")
        }
        function onSubStateChanged() {
            console.log("SteamPage: DE1Device.subState changed to", DE1Device.subStateString)
        }
    }

    // Reset state when steaming starts/ends
    onIsSteamingChanged: {
        console.log("SteamPage: isSteaming changed to", isSteaming, "phase=", MachineState.phase, "steamSoftStopped=", steamSoftStopped)
        if (isSteaming) {
            wasSteaming = true
            steamSoftStopped = false
            // Reset to preset value (discard any +5s/-5s adjustments from previous session)
            Settings.steamTimeout = getCurrentPitcherDuration()
            Settings.steamFlow = getCurrentPitcherFlow()
            MainController.startSteamHeating()
        } else {
            console.log("SteamPage: Settings view now visible (isSteaming=false)")
            // Turn off steam heater after steaming if keepSteamHeaterOn is false
            if (wasSteaming && !Settings.keepSteamHeaterOn) {
                console.log("SteamPage: Turning off steam heater (keepSteamHeaterOn=false)")
                MainController.sendSteamTemperature(0)  // This sets steamDisabled=true
            }
            wasSteaming = false
        }
    }

    // Helper to format flow as readable value (handles undefined/NaN)
    // Steam flow is stored as 0.01 ml/s units (e.g., 150 = 1.5 ml/s)
    function flowToDisplay(flow) {
        if (flow === undefined || flow === null || isNaN(flow)) {
            return "1.50"  // Default
        }
        return (flow / 100).toFixed(2)
    }

    // Get current pitcher's values with defaults
    function getCurrentPitcherDuration() {
        var preset = Settings.getSteamPitcherPreset(Settings.selectedSteamPitcher)
        return preset ? preset.duration : 30
    }

    function getCurrentPitcherFlow() {
        var preset = Settings.getSteamPitcherPreset(Settings.selectedSteamPitcher)
        return (preset && preset.flow !== undefined) ? preset.flow : 150
    }

    function getCurrentPitcherName() {
        var preset = Settings.getSteamPitcherPreset(Settings.selectedSteamPitcher)
        return preset ? preset.name : ""
    }

    // Save current pitcher with new values
    function saveCurrentPitcher(duration, flow) {
        var name = getCurrentPitcherName()
        if (name) {
            Settings.updateSteamPitcherPreset(Settings.selectedSteamPitcher, name, duration, flow)
        }
    }

    // Steam view mode: "timer" (default) or "chart"
    property string steamViewMode: Settings.value("steam/steamView", "timer")
    Connections {
        target: Settings
        function onValueChanged(key) {
            if (key === "steam/steamView")
                steamViewMode = Settings.value("steam/steamView", "timer")
        }
    }

    // Warning banner (auto-dismiss after 5 seconds — allowed per CLAUDE.md for UI auto-dismiss)
    property string warningText: ""
    property bool warningVisible: false
    Timer {
        id: warningDismissTimer
        interval: 5000
        onTriggered: warningVisible = false
    }

    // Warning connections
    Connections {
        target: SteamHealthTracker

        function onPressureTooHigh() {
            warningText = TranslationManager.translate("steam.warning.pressureHigh",
                "Warning: steam pressure is too high")
            warningVisible = true
            warningDismissTimer.restart()
        }
        function onTemperatureTooHigh() {
            warningText = TranslationManager.translate("steam.warning.temperatureHigh",
                "Warning: steam temperature is too high")
            warningVisible = true
            warningDismissTimer.restart()
        }
        function onDescaleWarning() {
            steamWarningDialog.warningMessage = TranslationManager.translate("steam.warning.descale",
                "Your machine may need descaling. Steam pressure was consistently too high.")
            steamWarningDialog.open()
        }
        function onTemperatureWarning(message) {
            steamWarningDialog.warningMessage = message
            steamWarningDialog.open()
        }
        function onScaleBuildupWarning(message) {
            steamWarningDialog.warningMessage = message
            steamWarningDialog.open()
        }
    }

    // Post-session warning dialog
    Dialog {
        id: steamWarningDialog
        property string warningMessage: ""
        title: TranslationManager.translate("steam.warning.title", "Steam Warning")
        modal: true
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(360))
        padding: Theme.spacingMedium

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Text {
                text: steamWarningDialog.warningMessage
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Accessible.ignored: true
            }

            AccessibleButton {
                text: TranslationManager.translate("common.button.ok", "OK")
                accessibleName: TranslationManager.translate("common.button.ok", "OK")
                Layout.alignment: Qt.AlignRight
                onClicked: steamWarningDialog.close()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.pageTopMargin  // Space for bottom bar
        spacing: Theme.scaled(15)

        // === STEAMING VIEW ===
        // Also shown during steam heating (DE1 in Steam state but FinalHeating substate)
        // Stay visible during soft-stop (waiting for purge) and Puffing (auto-flush countdown)
        ColumnLayout {
            visible: isSteaming || steamSoftStopped || isSteamHeating || isPuffing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(20)

            // Top row: preset pills + view toggle button
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Item { Layout.fillWidth: true }

                // Preset pills for quick switching during steaming
                Row {
                    spacing: Theme.scaled(8)

                    Repeater {
                        model: Settings.steamPitcherPresets

                        Rectangle {
                            width: livePitcherText.implicitWidth + 24
                            height: Theme.scaled(36)
                            radius: Theme.scaled(18)
                            color: index === Settings.selectedSteamPitcher ? Theme.primaryColor : Theme.surfaceColor
                            border.color: index === Settings.selectedSteamPitcher ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: 1

                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.name + (index === Settings.selectedSteamPitcher ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                            Accessible.focusable: true
                            Accessible.onPressAction: livePitcherMa.clicked(null)

                            Text {
                                id: livePitcherText
                                anchors.centerIn: parent
                                text: modelData.name
                                color: index === Settings.selectedSteamPitcher ? Theme.primaryContrastColor : Theme.textColor
                                font: Theme.bodyFont
                                Accessible.ignored: true
                            }

                            MouseArea {
                                id: livePitcherMa
                                anchors.fill: parent
                                onClicked: {
                                    Settings.selectedSteamPitcher = index
                                    var flow = modelData.flow !== undefined ? modelData.flow : 150
                                    Settings.steamTimeout = modelData.duration
                                    Settings.steamFlow = flow
                                    MainController.startSteamHeating()
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // View toggle button (graph/timer)
                Rectangle {
                    width: Theme.scaled(44)
                    height: Theme.scaled(44)
                    radius: Theme.cardRadius
                    color: viewToggleMa.containsMouse ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor

                    Accessible.ignored: true

                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/icons/Graph.svg"
                        sourceSize.width: Theme.scaled(24)
                        sourceSize.height: Theme.scaled(24)

                        layer.enabled: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: Theme.textColor
                        }
                    }

                    AccessibleMouseArea {
                        id: viewToggleMa
                        anchors.fill: parent
                        hoverEnabled: true
                        accessibleName: TranslationManager.translate("steam.viewToggle.accessibility",
                            "Switch between timer and chart view")
                        accessibleItem: parent
                        onAccessibleClicked: {
                            var newMode = steamViewMode === "timer" ? "chart" : "timer"
                            steamViewMode = newMode
                            Settings.setValue("steam/steamView", newMode)
                        }
                    }
                }
            }

            // Warning banner (live warnings during steaming)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: warningBannerText.implicitHeight + Theme.spacingSmall * 2
                visible: warningVisible
                radius: Theme.cardRadius
                color: Theme.errorColor

                Text {
                    id: warningBannerText
                    anchors.centerIn: parent
                    width: parent.width - Theme.spacingMedium * 2
                    text: warningText
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Accessible.ignored: true
                }

                Accessible.role: Accessible.StaticText
                Accessible.name: warningText
                Accessible.focusable: true
            }

            // === TIMER VIEW (default) ===
            ColumnLayout {
                visible: steamViewMode === "timer"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.scaled(20)

            Item { Layout.fillHeight: true }

            // Timer with target and adjustment buttons
            Column {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(8)

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.spacingMedium

                    // Decrease time button (hidden during heating and puffing)
                    Rectangle {
                        id: decreaseTimeBtn
                        visible: !isSteamHeating && !isPuffing
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.scaled(48)
                        height: width
                        radius: Theme.cardRadius
                        color: decreaseMouseArea.pressed ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
                        border.color: Theme.borderColor
                        border.width: 1

                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("steam.decreaseTime", "Decrease steam time by 5 seconds")
                        Accessible.focusable: true
                        Accessible.onPressAction: decreaseMouseArea.clicked(null)

                        Text {
                            anchors.centerIn: parent
                            text: "-5s"
                            color: Theme.textColor
                            font: Theme.bodyFont
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: decreaseMouseArea
                            anchors.fill: parent
                            onClicked: {
                                var newTime = Math.max(5, Settings.steamTimeout - 5)
                                Settings.steamTimeout = newTime
                                MainController.startSteamHeating()
                            }
                        }
                    }

                    Text {
                        id: steamProgressText
                        // Show temperature during heating, countdown during puffing, time during steaming
                        text: {
                            if (isSteamHeating) {
                                return Math.round(currentSteamTemp) + "°C / " + Math.round(targetSteamTemp) + "°C"
                            } else if (isPuffing && root.steamAutoFlushCountdown > 0) {
                                return root.steamAutoFlushCountdown.toFixed(1) + "s / " + Settings.steamAutoFlushSeconds + "s"
                            } else {
                                return MachineState.shotTime.toFixed(1) + "s / " + Settings.steamTimeout + "s"
                            }
                        }
                        color: Theme.textColor
                        font: Theme.timerFont
                    }

                    // Increase time button (hidden during heating and puffing)
                    Rectangle {
                        id: increaseTimeBtn
                        visible: !isSteamHeating && !isPuffing
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.scaled(48)
                        height: width
                        radius: Theme.cardRadius
                        color: increaseMouseArea.pressed ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
                        border.color: Theme.borderColor
                        border.width: 1

                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("steam.increaseTime", "Increase steam time by 5 seconds")
                        Accessible.focusable: true
                        Accessible.onPressAction: increaseMouseArea.clicked(null)

                        Text {
                            anchors.centerIn: parent
                            text: "+5s"
                            color: Theme.textColor
                            font: Theme.bodyFont
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: increaseMouseArea
                            anchors.fill: parent
                            onClicked: {
                                var newTime = Math.min(120, Settings.steamTimeout + 5)
                                Settings.steamTimeout = newTime
                                MainController.startSteamHeating()
                            }
                        }
                    }
                }

                // Progress bar
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: steamProgressText.width + decreaseTimeBtn.width + increaseTimeBtn.width + Theme.spacingMedium * 2
                    height: Theme.scaled(8)
                    radius: Theme.scaled(4)
                    color: Theme.surfaceColor

                    Rectangle {
                        // Show temperature progress during heating, countdown during puffing, time during steaming
                        width: {
                            if (isSteamHeating) {
                                return parent.width * Math.min(1, currentSteamTemp / targetSteamTemp)
                            } else if (isPuffing && Settings.steamAutoFlushSeconds > 0) {
                                // Countdown: progress goes from full to empty
                                return parent.width * Math.min(1, root.steamAutoFlushCountdown / Settings.steamAutoFlushSeconds)
                            } else {
                                return parent.width * Math.min(1, MachineState.shotTime / Settings.steamTimeout)
                            }
                        }
                        height: parent.height
                        radius: Theme.scaled(4)
                        color: isSteamHeating ? Theme.warningColor : (isPuffing ? Theme.secondaryColor : Theme.primaryColor)
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // Full-width Steam Flow control
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(40)
                Layout.rightMargin: Theme.scaled(40)
                spacing: Theme.scaled(12)

                Tr {
                    Layout.alignment: Qt.AlignHCenter
                    key: "steam.label.steamFlow"
                    fallback: "Steam Flow"
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                }

                ValueInput {
                    id: steamingFlowSlider
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(80)
                    from: 40
                    to: 250
                    stepSize: 5
                    decimals: 0
                    value: Settings.steamFlow
                    displayText: flowToDisplay(value)
                    accessibleName: TranslationManager.translate("steam.label.steamFlow", "Steam Flow")
                    onValueModified: function(newValue) {
                        steamingFlowSlider.value = newValue
                        MainController.setSteamFlowImmediate(newValue)
                        saveCurrentPitcher(getCurrentPitcherDuration(), newValue)
                    }
                }

                Tr {
                    Layout.alignment: Qt.AlignHCenter
                    key: "steam.hint.flowHint"
                    fallback: "Low = flat, High = foamy"
                    color: Theme.textSecondaryColor
                    font: Theme.labelFont
                }
            }

            } // end timer view ColumnLayout

            // === CHART VIEW ===
            ColumnLayout {
                visible: steamViewMode === "chart"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.scaled(8)

                SteamGraph {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                // Condensed info row
                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.spacingLarge

                    Text {
                        text: {
                            if (isSteamHeating) {
                                return Math.round(currentSteamTemp) + "°C / " + Math.round(targetSteamTemp) + "°C"
                            } else if (isPuffing && root.steamAutoFlushCountdown > 0) {
                                return root.steamAutoFlushCountdown.toFixed(1) + "s / " + Settings.steamAutoFlushSeconds + "s"
                            } else {
                                return MachineState.shotTime.toFixed(1) + "s / " + Settings.steamTimeout + "s"
                            }
                        }
                        color: Theme.textColor
                        font: Theme.subtitleFont
                        Accessible.ignored: true
                    }

                    Text {
                        text: flowToDisplay(Settings.steamFlow) + " mL/s"
                        color: Theme.flowColor
                        font: Theme.subtitleFont
                        Accessible.ignored: true
                    }

                    Text {
                        text: Math.round(currentSteamTemp) + "°C"
                        color: Theme.temperatureColor
                        font: Theme.subtitleFont
                        Accessible.ignored: true
                    }
                }
            }

            // Stop button for headless machines (two-stage for steam)
            // First press: stops steam flow (soft stop)
            // Second press: requests Idle which triggers hose purge
            Rectangle {
                id: steamStopButton
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Theme.scaled(200)
                Layout.preferredHeight: Theme.scaled(60)
                visible: DE1Device.isHeadless
                radius: Theme.cardRadius
                color: stopTapHandler.isPressed
                    ? Qt.darker((steamSoftStopped && !Settings.headlessSkipPurgeConfirm) ? Theme.primaryColor : Theme.errorColor, 1.2)
                    : ((steamSoftStopped && !Settings.headlessSkipPurgeConfirm) ? Theme.primaryColor : Theme.errorColor)
                border.color: Theme.primaryContrastColor
                border.width: Theme.scaled(2)

                Text {
                    id: stopButtonText
                    anchors.centerIn: parent
                    text: (steamSoftStopped && !Settings.headlessSkipPurgeConfirm) ? "PURGE" : "STOP"
                    color: Theme.primaryContrastColor
                    font.pixelSize: Theme.scaled(24)
                    font.weight: Font.Bold
                }

                // Using TapHandler for better touch responsiveness
                AccessibleTapHandler {
                    id: stopTapHandler
                    anchors.fill: parent
                    accessibleName: steamSoftStopped ? "Purge steam wand" : "Stop steaming"
                    accessibleItem: steamStopButton
                    onAccessibleClicked: {
                        if (Settings.headlessSkipPurgeConfirm) {
                            // Single press mode: stop immediately and trigger auto-purge
                            DE1Device.requestIdle()
                            root.goToIdle()
                        } else if (steamSoftStopped) {
                            // Two-press mode, second press: request Idle to trigger purge
                            steamSoftStopped = false  // Reset before navigating
                            DE1Device.requestIdle()
                            root.goToIdle()
                        } else {
                            // Two-press mode, first press: soft stop steam without purge
                            // Sends 1-second timeout which triggers elapsed > target stop
                            MainController.softStopSteam()
                            steamSoftStopped = true
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.scaled(20) }
        }

        // === SETTINGS VIEW ===
        // Hide during soft-stop (waiting for purge), steam heating, and puffing
        ColumnLayout {
            visible: !isSteaming && !steamSoftStopped && !isSteamHeating && !isPuffing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(12)

            // Steam heater heating indicator
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(60)
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: isHeatingUp

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(15)

                    // Heating icon (animated)
                    Text {
                        text: "\ue88a"  // heating icon (whatshot)
                        font.family: "Material Icons"
                        font.pixelSize: Theme.scaled(28)
                        color: Theme.warningColor

                        SequentialAnimation on opacity {
                            running: isHeatingUp
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.4; to: 1.0; duration: 600 }
                            NumberAnimation { from: 1.0; to: 0.4; duration: 600 }
                        }
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "steam.label.heatingUp"
                            fallback: "Heating steam..."
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                        }

                        // Progress bar
                        Rectangle {
                            width: parent.width
                            height: Theme.scaled(6)
                            radius: Theme.scaled(3)
                            color: Theme.backgroundColor

                            Rectangle {
                                width: parent.width * Math.min(1, Math.max(0, currentSteamTemp / targetSteamTemp))
                                height: parent.height
                                radius: parent.radius
                                color: Theme.warningColor
                            }
                        }
                    }

                    // Temperature display
                    Text {
                        text: currentSteamTemp.toFixed(0) + " / " + targetSteamTemp.toFixed(0) + "°C"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(14)
                    }
                }
            }

            // Pitcher Presets Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(90)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(20)

                    Tr {
                        key: "steam.label.pitcherPreset"
                        fallback: "Pitcher Preset"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(24)
                    }

                    // Pitcher preset buttons with drag-and-drop
                    Row {
                        id: pitcherPresetsRow
                        spacing: Theme.scaled(8)

                        property int draggedIndex: -1

                        Repeater {
                            id: pitcherRepeater
                            model: Settings.steamPitcherPresets

                            Item {
                                id: pitcherDelegate
                                width: pitcherPill.width
                                height: Theme.scaled(36)

                                property int pitcherIndex: index

                                Rectangle {
                                    id: pitcherPill
                                    width: pitcherText.implicitWidth + 24
                                    height: Theme.scaled(36)
                                    radius: Theme.scaled(18)
                                    color: pitcherDelegate.pitcherIndex === Settings.selectedSteamPitcher ? Theme.primaryColor : Theme.backgroundColor
                                    border.color: pitcherDelegate.pitcherIndex === Settings.selectedSteamPitcher ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: dragArea.drag.active ? 0.8 : 1.0

                                    Accessible.role: Accessible.Button
                                    Accessible.name: modelData.name + " " + TranslationManager.translate("steam.accessibility.preset", "preset") +
                                                     (pitcherDelegate.pitcherIndex === Settings.selectedSteamPitcher ?
                                                      ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                                    Accessible.focusable: true
                                    Accessible.onPressAction: {
                                        Settings.selectedSteamPitcher = pitcherDelegate.pitcherIndex
                                        var flow = modelData.flow !== undefined ? modelData.flow : 150
                                        durationSlider.value = modelData.duration
                                        flowSlider.value = flow
                                        Settings.steamTimeout = modelData.duration
                                        Settings.steamFlow = flow
                                        MainController.startSteamHeating()
                                    }

                                    Drag.active: dragArea.drag.active
                                    Drag.source: pitcherDelegate
                                    Drag.hotSpot.x: width / 2
                                    Drag.hotSpot.y: height / 2

                                    states: State {
                                        when: dragArea.drag.active
                                        ParentChange { target: pitcherPill; parent: pitcherPresetsRow }
                                        AnchorChanges { target: pitcherPill; anchors.verticalCenter: undefined }
                                    }

                                    Text {
                                        id: pitcherText
                                        anchors.centerIn: parent
                                        text: modelData.name
                                        color: pitcherDelegate.pitcherIndex === Settings.selectedSteamPitcher ? Theme.primaryContrastColor : Theme.textColor
                                        font: Theme.bodyFont
                                        Accessible.ignored: true
                                    }

                                    MouseArea {
                                        id: dragArea
                                        anchors.fill: parent
                                        drag.target: pitcherPill
                                        drag.axis: Drag.XAxis

                                        property bool held: false
                                        property bool moved: false

                                        onPressed: {
                                            held = false
                                            moved = false
                                            holdTimer.start()
                                        }

                                        onReleased: {
                                            holdTimer.stop()
                                            if (!moved && !held) {
                                                // Simple click - select the pitcher
                                                Settings.selectedSteamPitcher = pitcherDelegate.pitcherIndex
                                                var flow = modelData.flow !== undefined ? modelData.flow : 150
                                                durationSlider.value = modelData.duration
                                                flowSlider.value = flow
                                                Settings.steamTimeout = modelData.duration
                                                Settings.steamFlow = flow
                                                MainController.startSteamHeating()
                                            }
                                            pitcherPill.Drag.drop()
                                            pitcherPresetsRow.draggedIndex = -1
                                        }

                                        onPositionChanged: {
                                            if (drag.active) {
                                                moved = true
                                                pitcherPresetsRow.draggedIndex = pitcherDelegate.pitcherIndex
                                            }
                                        }

                                        onDoubleClicked: {
                                            holdTimer.stop()
                                            held = true  // Prevent single-click selection on release
                                            editingPitcherIndex = pitcherDelegate.pitcherIndex
                                            editPitcherNameInput.text = modelData.name
                                            editPitcherPopup.open()
                                        }

                                        Timer {
                                            id: holdTimer
                                            interval: 500
                                            onTriggered: {
                                                if (!dragArea.moved) {
                                                    dragArea.held = true
                                                    editingPitcherIndex = pitcherDelegate.pitcherIndex
                                                    editPitcherNameInput.text = modelData.name
                                                    editPitcherPopup.open()
                                                }
                                            }
                                        }
                                    }
                                }

                                DropArea {
                                    anchors.fill: parent
                                    onEntered: function(drag) {
                                        var fromIndex = drag.source.pitcherIndex
                                        var toIndex = pitcherDelegate.pitcherIndex
                                        if (fromIndex !== toIndex) {
                                            Settings.moveSteamPitcherPreset(fromIndex, toIndex)
                                        }
                                    }
                                }
                            }
                        }

                        // Add button
                        Rectangle {
                            id: addPitcherButton
                            width: Theme.scaled(36)
                            height: Theme.scaled(36)
                            radius: Theme.scaled(18)
                            color: Theme.backgroundColor
                            border.color: Theme.textSecondaryColor
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "+"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(20)
                            }

                            // Using TapHandler for better touch responsiveness
                            AccessibleTapHandler {
                                anchors.fill: parent
                                accessibleName: TranslationManager.translate("steam.accessible.addPreset", "Add new steam preset")
                                accessibleItem: addPitcherButton
                                onAccessibleClicked: addPitcherDialog.open()
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Tr {
                        key: "steam.hint.presetReorder"
                        fallback: "Drag to reorder, hold or double-click to edit"
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                    }
                }
            }

            // Settings frame
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(16)
                    spacing: Theme.scaled(8)

                    // Duration (per-pitcher, auto-saves)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)

                        Tr {
                            key: "steam.label.duration"
                            fallback: "Duration"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(24)
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: durationSlider
                            Layout.preferredWidth: Theme.scaled(180)
                            from: 1
                            to: 120
                            stepSize: 1
                            decimals: 0
                            suffix: " s"
                            value: getCurrentPitcherDuration()
                            valueColor: Theme.primaryColor
                            accessibleName: TranslationManager.translate("steam.label.duration", "Duration")
                            onValueModified: function(newValue) {
                                durationSlider.value = newValue
                                Settings.steamTimeout = newValue
                                saveCurrentPitcher(newValue, flowSlider.value)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3 }

                    // Steam Flow (per-pitcher, auto-saves)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)

                        Column {
                            Tr {
                                key: "steam.label.steamFlow"
                                fallback: "Steam Flow"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(24)
                            }
                            Tr {
                                key: "steam.hint.flowHint"
                                fallback: "Low = flat, High = foamy"
                                color: Theme.textSecondaryColor
                                font: Theme.labelFont
                            }
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: flowSlider
                            Layout.preferredWidth: Theme.scaled(180)
                            from: 40
                            to: 250
                            stepSize: 5
                            decimals: 0
                            value: getCurrentPitcherFlow()
                            displayText: flowToDisplay(value)
                            valueColor: Theme.primaryColor
                            accessibleName: TranslationManager.translate("steam.label.steamFlow", "Steam Flow")
                            onValueModified: function(newValue) {
                                flowSlider.value = newValue
                                MainController.setSteamFlowImmediate(newValue)
                                saveCurrentPitcher(durationSlider.value, newValue)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3 }

                    // Temperature (global setting)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)

                        Column {
                            Tr {
                                key: "steam.label.temperature"
                                fallback: "Temperature"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(24)
                            }
                            Tr {
                                key: "steam.hint.temperatureHint"
                                fallback: "Higher = drier steam"
                                color: Theme.textSecondaryColor
                                font: Theme.labelFont
                            }
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: steamTempSlider
                            Layout.preferredWidth: Theme.scaled(180)
                            from: 120
                            to: 170
                            stepSize: 1
                            decimals: 0
                            suffix: "°C"
                            value: Settings.steamTemperature
                            valueColor: Theme.temperatureColor
                            accessibleName: TranslationManager.translate("steam.label.temperature", "Steam Temperature")
                            onValueModified: function(newValue) {
                                steamTempSlider.value = newValue
                                MainController.setSteamTemperatureImmediate(newValue)
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        Item { Layout.fillHeight: true; visible: isSteaming || steamSoftStopped }
    }

    // Hidden translation helper for "No pitcher"
    Tr { id: noPitcherText; key: "steam.label.noPitcher"; fallback: "No pitcher"; visible: false }

    // Bottom bar (hide during soft-stop waiting for purge, steam heating, and puffing)
    BottomBar {
        visible: !isSteaming && !steamSoftStopped && !isSteamHeating && !isPuffing
        title: getCurrentPitcherName() || noPitcherText.text
        onBackClicked: {
            // Turn off heater if keepSteamHeaterOn is false, otherwise keep it warm
            if (!Settings.keepSteamHeaterOn) {
                MainController.sendSteamTemperature(0)  // This sets steamDisabled=true
            } else {
                MainController.applySteamSettings()
            }
            root.goToIdle()
        }

        Text {
            text: durationSlider.value.toFixed(0) + "s"
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: Theme.primaryContrastColor; opacity: 0.3 }
        Tr {
            id: flowLabelText
            key: "steam.label.flow"
            fallback: "Flow"
            visible: false
        }
        Text {
            text: flowLabelText.text + " " + flowToDisplay(flowSlider.value)
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: Theme.primaryContrastColor; opacity: 0.3 }
        Text {
            text: steamTempSlider.value.toFixed(0) + "°C"
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
    }


    // Edit Pitcher Popup (rename/delete)
    Dialog {
        id: editPitcherPopup
        x: (parent.width - width) / 2
        y: editPitcherPopupAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        property bool editPitcherPopupAtTop: false
        onOpened: {
            editPitcherPopupAtTop = false
            editPitcherNameInput.forceActiveFocus()
        }
        onClosed: editPitcherPopupAtTop = false

        Connections {
            target: Qt.inputMethod
            function onVisibleChanged() {
                if (Qt.inputMethod.visible && editPitcherPopup.opened) {
                    editPitcherPopup.editPitcherPopupAtTop = true
                }
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(10)
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Tr {
                key: "steam.popup.editPitcher"
                fallback: "Edit Pitcher"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            Tr { id: pitcherNamePlaceholder; key: "steam.placeholder.pitcherName"; fallback: "Pitcher name"; visible: false }

            Rectangle {
                Layout.preferredWidth: Theme.scaled(280)
                Layout.preferredHeight: Theme.scaled(44)
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: Theme.scaled(4)

                TextInput {
                    id: editPitcherNameInput
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("steam.accessible.renamePitcher", "Rename pitcher preset")
                    Accessible.description: text
                    Accessible.focusable: true

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: pitcherNamePlaceholder.text
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                        Accessible.ignored: true
                    }
                }
            }

            Tr { id: deleteButtonText; key: "steam.button.delete"; fallback: "Delete"; visible: false }
            Tr { id: cancelButtonText; key: "steam.button.cancel"; fallback: "Cancel"; visible: false }
            Tr { id: saveButtonText; key: "steam.button.save"; fallback: "Save"; visible: false }

            RowLayout {
                spacing: Theme.scaled(10)

                AccessibleButton {
                    text: deleteButtonText.text
                    accessibleName: TranslationManager.translate("steam.deletePitcherPreset", "Delete this pitcher preset")
                    destructive: true
                    onClicked: {
                        Settings.removeSteamPitcherPreset(editingPitcherIndex)
                        editPitcherPopup.close()
                    }
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: cancelButtonText.text
                    accessibleName: TranslationManager.translate("steam.cancelEditingPitcher", "Cancel editing pitcher preset")
                    onClicked: editPitcherPopup.close()
                }

                AccessibleButton {
                    primary: true
                    text: saveButtonText.text
                    accessibleName: TranslationManager.translate("steam.savePitcherChanges", "Save changes to pitcher preset")
                    onClicked: {
                        var preset = Settings.getSteamPitcherPreset(editingPitcherIndex)
                        Settings.updateSteamPitcherPreset(editingPitcherIndex, editPitcherNameInput.text, preset.duration, preset.flow)
                        editPitcherPopup.close()
                    }
                }
            }
        }
    }

    // Add Pitcher Dialog
    Dialog {
        id: addPitcherDialog
        x: (parent.width - width) / 2
        y: addPitcherDialogAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        property bool addPitcherDialogAtTop: false
        onOpened: {
            addPitcherDialogAtTop = false
            newPitcherName.text = ""
            newPitcherName.forceActiveFocus()
        }
        onClosed: addPitcherDialogAtTop = false

        Connections {
            target: Qt.inputMethod
            function onVisibleChanged() {
                if (Qt.inputMethod.visible && addPitcherDialog.opened) {
                    addPitcherDialog.addPitcherDialogAtTop = true
                }
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(10)
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Tr {
                key: "steam.popup.addPitcherPreset"
                fallback: "Add Pitcher Preset"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            Tr { id: addPitcherNamePlaceholder; key: "steam.placeholder.pitcherName"; fallback: "Pitcher name"; visible: false }
            Tr { id: addCancelButtonText; key: "steam.button.cancel"; fallback: "Cancel"; visible: false }
            Tr { id: addButtonText; key: "steam.button.add"; fallback: "Add"; visible: false }

            Rectangle {
                Layout.preferredWidth: Theme.scaled(280)
                Layout.preferredHeight: Theme.scaled(44)
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: Theme.scaled(4)

                TextInput {
                    id: newPitcherName
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("steam.accessible.newPitcherName", "New pitcher preset name")
                    Accessible.description: text
                    Accessible.focusable: true

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: addPitcherNamePlaceholder.text
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                        Accessible.ignored: true
                    }
                }
            }

            RowLayout {
                spacing: Theme.scaled(10)

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: addCancelButtonText.text
                    accessibleName: TranslationManager.translate("steam.cancelAddingPitcher", "Cancel adding new pitcher preset")
                    onClicked: addPitcherDialog.close()
                }

                AccessibleButton {
                    primary: true
                    text: addButtonText.text
                    accessibleName: TranslationManager.translate("steam.addNewPitcher", "Add new pitcher preset with entered name")
                    onClicked: {
                        if (newPitcherName.text.trim() !== "") {
                            var presetCount = Settings.steamPitcherPresets.length
                            Settings.addSteamPitcherPreset(newPitcherName.text.trim(), 30, 150)
                            Settings.selectedSteamPitcher = presetCount
                            newPitcherName.text = ""
                            addPitcherDialog.close()
                        }
                    }
                }
            }
        }
    }

    // Update sliders when selected pitcher changes
    Connections {
        target: Settings
        function onSelectedSteamPitcherChanged() {
            durationSlider.value = getCurrentPitcherDuration()
            flowSlider.value = getCurrentPitcherFlow()
        }
        function onSteamPitcherPresetsChanged() {
            durationSlider.value = getCurrentPitcherDuration()
            flowSlider.value = getCurrentPitcherFlow()
        }
    }
}
