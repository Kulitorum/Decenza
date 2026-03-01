import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: descalingPage
    objectName: "descalingPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = TranslationManager.translate("descaling.title", "Descaling")
    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("descaling.title", "Descaling")

    // Restore normal operation when leaving descale page
    Component.onDestruction: {
        Settings.steamDisabled = false
        MainController.uploadCurrentProfile()
    }

    property bool isDescaling: MachineState.phase === MachineStateType.Phase.Descaling
    property bool showRinseInstructions: false

    // Track when descaling completes to show rinse instructions
    Connections {
        target: MachineState
        function onPhaseChanged() {
            if (!isDescaling && descalingPage.visible) {
                // Descaling just finished, show rinse instructions
                if (MachineState.phase === MachineStateType.Phase.Idle ||
                    MachineState.phase === MachineStateType.Phase.Ready) {
                    showRinseInstructions = true
                }
            }
        }
    }

    // Get user-friendly substate description
    function getDescaleStepDescription(subState) {
        switch (subState) {
            case 8:  return TranslationManager.translate("descaling.step.init", "Initializing descale cycle...")
            case 9:  return TranslationManager.translate("descaling.step.fillGroup", "Filling group head with solution...")
            case 10: return TranslationManager.translate("descaling.step.return", "Circulating through boiler...")
            case 11: return TranslationManager.translate("descaling.step.group", "Flushing group head...")
            case 12: return TranslationManager.translate("descaling.step.steam", "Descaling steam system...")
            default: return TranslationManager.translate("descaling.step.running", "Descaling in progress...")
        }
    }

    // Get progress percentage based on substate (8-12 = 5 steps)
    function getDescaleProgress(subState) {
        if (subState < 8) return 0
        if (subState > 12) return 1
        return (subState - 8 + 1) / 5  // Steps 8-12 = 20%, 40%, 60%, 80%, 100%
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.scaled(80)
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: Theme.scaled(16)

            // === DESCALING IN PROGRESS VIEW ===
            Item {
                visible: isDescaling
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(Theme.scaled(500), parent.width - Theme.scaled(40))
                    spacing: Theme.scaled(20)

                    Item { Layout.preferredHeight: Theme.scaled(40) }

                    // Progress indicator
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(200)
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Theme.scaled(16)

                            Tr {
                                Layout.alignment: Qt.AlignHCenter
                                key: "descaling.inprogress.title"
                                fallback: "Descaling in Progress"
                                font: Theme.titleFont
                                color: Theme.textColor
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: getDescaleStepDescription(DE1Device.subState)
                                font: Theme.bodyFont
                                color: Theme.textSecondaryColor
                            }

                            // Progress bar
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: Theme.scaled(300)
                                Layout.preferredHeight: Theme.scaled(12)
                                radius: Theme.scaled(6)
                                color: Theme.backgroundColor

                                Rectangle {
                                    width: parent.width * getDescaleProgress(DE1Device.subState)
                                    height: parent.height
                                    radius: Theme.scaled(6)
                                    color: Theme.primaryColor

                                    Behavior on width { NumberAnimation { duration: 300 } }
                                }
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: Math.round(getDescaleProgress(DE1Device.subState) * 100) + "%"
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                            }
                        }
                    }

                    // Timer display
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: MachineState.shotTime.toFixed(0) + "s"
                        font: Theme.timerFont
                        color: Theme.textColor
                    }

                    Tr {
                        Layout.alignment: Qt.AlignHCenter
                        key: "descaling.inprogress.dontstop"
                        fallback: "Do not stop the machine during descaling"
                        font: Theme.captionFont
                        color: Theme.warningColor
                    }

                    Item { Layout.fillHeight: true }

                    // Stop button (emergency only, for headless machines)
                    Rectangle {
                        id: descaleStopButton
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: Theme.scaled(200)
                        Layout.preferredHeight: Theme.scaled(50)
                        visible: DE1Device.isHeadless
                        radius: Theme.cardRadius
                        color: stopTapHandler.isPressed ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor
                        border.color: "white"
                        border.width: Theme.scaled(2)

                        Text {
                            anchors.centerIn: parent
                            text: "STOP"
                            color: "white"
                            font.pixelSize: Theme.scaled(18)
                            font.weight: Font.Bold
                        }

                        AccessibleTapHandler {
                            id: stopTapHandler
                            anchors.fill: parent
                            accessibleName: "Emergency stop descaling"
                            accessibleItem: descaleStopButton
                            onAccessibleClicked: DE1Device.stopOperation()
                        }
                    }

                    Item { Layout.preferredHeight: Theme.scaled(20) }
                }
            }

            // === RINSE INSTRUCTIONS VIEW ===
            ColumnLayout {
                visible: showRinseInstructions && !isDescaling
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                Rectangle {
                    id: rinseCard
                    Layout.fillWidth: true
                    implicitHeight: rinseContent.implicitHeight + Theme.scaled(32)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: rinseContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(16)
                        spacing: Theme.scaled(12)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            Image {
                                source: "qrc:/emoji/2705.svg"  // Checkmark
                                sourceSize.width: Theme.scaled(24)
                                sourceSize.height: Theme.scaled(24)
                            }

                            Tr {
                                key: "descaling.rinse.title"
                                fallback: "Descale Complete - Now Rinse!"
                                font: Theme.titleFont
                                color: Theme.primaryColor
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: Theme.scaled(1)
                            color: Theme.textSecondaryColor
                            opacity: 0.3
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.important"
                            fallback: "IMPORTANT: Rinsing is critical! Citric acid remains in the water lines and must be flushed out completely."
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.bold: true
                            color: Theme.warningColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.step1.title"
                            fallback: "1. Clean the water tank"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.step1.desc"
                            fallback: "Empty and rinse the water tank thoroughly. Fill with fresh filtered water."
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.step2.title"
                            fallback: "2. Rinse the group head"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.step2.desc"
                            fallback: "Run flush cycles until the app shows 'refill'. Refill and repeat. Taste the water - if it tastes acidic or smells like vitamin C, flush more. Expect to use 4+ liters."
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.step3.title"
                            fallback: "3. Rinse the steam line"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.step3.desc"
                            fallback: "Keep the steam tip removed. Run steam for 100 seconds, repeat 5 times. Taste the water after - if acidic, run more steam cycles."
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.total"
                            fallback: "Total water needed: approximately 7-8 liters for thorough rinsing."
                            font.pixelSize: Theme.captionFont.pixelSize
                            font.italic: true
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                // Done button
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Theme.scaled(200)
                    Layout.preferredHeight: Theme.scaled(50)
                    radius: Theme.cardRadius
                    color: doneArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor

                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("descaling.button.done", "Done")
                    Accessible.focusable: true
                    Accessible.onPressAction: doneArea.clicked(null)

                    Tr {
                        anchors.centerIn: parent
                        key: "descaling.button.done"
                        fallback: "Done"
                        color: "white"
                        font.pixelSize: Theme.scaled(18)
                        font.weight: Font.Bold
                        Accessible.ignored: true
                    }

                    MouseArea {
                        id: doneArea
                        anchors.fill: parent
                        onClicked: {
                            showRinseInstructions = false
                            root.goToIdle()
                        }
                    }
                }
            }

            // === PREPARATION VIEW ===
            ColumnLayout {
                visible: !isDescaling && !showRinseInstructions
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                // Warning banner
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: warningContent.implicitHeight + Theme.scaled(24)
                    color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.15)
                    radius: Theme.cardRadius
                    border.color: Theme.warningColor
                    border.width: 1

                    ColumnLayout {
                        id: warningContent
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(8)

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.warning.title"
                            fallback: "Important Warnings"
                            font.pixelSize: Theme.subtitleFont.pixelSize
                            font.bold: true
                            color: Theme.warningColor
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.warning.citriconly"
                            fallback: "\u2022 Use CITRIC ACID ONLY - other descaling products can damage seals and void warranty"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.warning.steam"
                            fallback: "• Disable steam heater and wait until steam temp is below 60°C (can take 1 hour)"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.warning.softwater"
                            fallback: "\u2022 Use soft or distilled water (TDS < 120ppm) for the solution - hard water buffers the acid"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.warning.notneeded"
                            fallback: "\u2022 If your water TDS is below 120ppm, you may not need to descale at all"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Solution preparation + Steam heater (two columns)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    // Left column: Solution recipe
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: solutionContent.implicitHeight + Theme.scaled(24)
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius

                        ColumnLayout {
                            id: solutionContent
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(12)
                            spacing: Theme.scaled(8)

                            Tr {
                                key: "descaling.solution.title"
                                fallback: "Prepare 5% Citric Acid Solution"
                                font: Theme.subtitleFont
                                color: Theme.textColor
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(60)
                                color: Theme.backgroundColor
                                radius: Theme.scaled(8)

                                RowLayout {
                                    anchors.centerIn: parent
                                    spacing: Theme.scaled(30)

                                    Column {
                                        spacing: Theme.scaled(4)
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "1540 ml"
                                            font: Theme.titleFont
                                            color: Theme.flowColor
                                        }
                                        Tr {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            key: "descaling.solution.water"
                                            fallback: "water (room temp)"
                                            font: Theme.captionFont
                                            color: Theme.textSecondaryColor
                                        }
                                    }

                                    Text {
                                        text: "+"
                                        font: Theme.titleFont
                                        color: Theme.textSecondaryColor
                                    }

                                    Column {
                                        spacing: Theme.scaled(4)
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "80 g"
                                            font: Theme.titleFont
                                            color: Theme.pressureColor
                                        }
                                        Tr {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            key: "descaling.solution.citric"
                                            fallback: "citric acid"
                                            font: Theme.captionFont
                                            color: Theme.textSecondaryColor
                                        }
                                    }
                                }
                            }

                            Tr {
                                Layout.fillWidth: true
                                key: "descaling.solution.note"
                                fallback: "Mix until fully dissolved. Do NOT use hot water (80-100°C) - room temperature to warm (20-40°C) is best."
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                            }

                            Tr {
                                Layout.fillWidth: true
                                key: "descaling.solution.oldmachine"
                                fallback: "For v1.0/v1.1 machines: Never exceed 5% concentration (can damage old pressure sensor)."
                                font.pixelSize: Theme.captionFont.pixelSize
                                font.italic: true
                                color: Theme.warningColor
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // Right column: Steam heater control
                    Rectangle {
                        Layout.preferredWidth: Theme.scaled(160)
                        Layout.fillHeight: true
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius

                        ColumnLayout {
                            id: steamContent
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(12)
                            spacing: Theme.scaled(8)

                            Tr {
                                Layout.alignment: Qt.AlignHCenter
                                key: "descaling.steam.title"
                                fallback: "Steam Heater"
                                font: Theme.subtitleFont
                                color: Theme.textColor
                            }

                            Item { Layout.fillHeight: true }

                            // Temperature readout
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                property real temp: typeof DE1Device.steamTemperature === 'number' ? DE1Device.steamTemperature : 0
                                text: temp.toFixed(0) + "°C"
                                font.pixelSize: Theme.scaled(36)
                                font.weight: Font.Bold
                                color: temp >= 60 ? Theme.errorColor : Theme.primaryColor
                            }

                            Item { Layout.fillHeight: true }

                            // Toggle button
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(36)
                                radius: Theme.cardRadius
                                color: steamToggleArea.pressed
                                    ? Qt.darker(Settings.steamDisabled ? Theme.primaryColor : Theme.errorColor, 1.2)
                                    : (Settings.steamDisabled ? Theme.primaryColor : Theme.errorColor)

                                Accessible.role: Accessible.Button
                                Accessible.name: Settings.steamDisabled
                                    ? TranslationManager.translate("descaling.steam.enable", "Enable") + " " + TranslationManager.translate("descaling.steam.accessible", "steam heater")
                                    : TranslationManager.translate("descaling.steam.disable", "Disable") + " " + TranslationManager.translate("descaling.steam.accessible", "steam heater")
                                Accessible.focusable: true
                                Accessible.onPressAction: steamToggleArea.clicked(null)

                                Text {
                                    anchors.centerIn: parent
                                    text: Settings.steamDisabled
                                        ? TranslationManager.translate("descaling.steam.enable", "Enable")
                                        : TranslationManager.translate("descaling.steam.disable", "Disable")
                                    color: "white"
                                    font.pixelSize: Theme.scaled(14)
                                    font.weight: Font.Bold
                                    Accessible.ignored: true
                                }

                                MouseArea {
                                    id: steamToggleArea
                                    anchors.fill: parent
                                    onClicked: {
                                        if (Settings.steamDisabled) {
                                            // Enable: restore saved temperature (sendSteamTemperature clears flag)
                                            MainController.sendSteamTemperature(Settings.steamTemperature)
                                        } else {
                                            // Disable: send 0 temp (sendSteamTemperature sets flag)
                                            MainController.sendSteamTemperature(0)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Setup steps
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: stepsContent.implicitHeight + Theme.scaled(24)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: stepsContent
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "descaling.steps.title"
                            fallback: "Setup Steps"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.steps.1"
                            fallback: "1. Disable steam heater and wait for steam temp to drop below 60°C"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.steps.2"
                            fallback: "2. Pour the citric acid solution into the water tank"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.steps.3"
                            fallback: "3. Remove the steam tip from the wand"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.steps.4"
                            fallback: "4. Remove the drip tray cover (keep tray in place to catch water)"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.steps.5"
                            fallback: "5. Optional: Remove portafilter screen and brass diffusers for separate cleaning"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: Theme.scaled(1)
                            color: Theme.textSecondaryColor
                            opacity: 0.3
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.steps.duration"
                            fallback: "The descale cycle takes about 6 minutes. You can repeat up to 3 times until the solution is used up (empty drip tray between cycles)."
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Start button
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Theme.scaled(8)
                    Layout.preferredWidth: Theme.scaled(250)
                    Layout.preferredHeight: Theme.scaled(56)
                    radius: Theme.cardRadius
                    color: startArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor

                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("descaling.button.start", "Start Descaling")
                    Accessible.focusable: true
                    Accessible.onPressAction: startArea.clicked(null)

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "descaling.button.start"
                            fallback: "Start Descaling"
                            color: "white"
                            font.pixelSize: Theme.scaled(20)
                            font.weight: Font.Bold
                            Accessible.ignored: true
                        }
                    }

                    MouseArea {
                        id: startArea
                        anchors.fill: parent
                        onClicked: {
                            DE1Device.startDescale()
                        }
                    }
                }

                Item { Layout.preferredHeight: Theme.scaled(20) }
            }
        }
    }

    // Bottom bar
    BottomBar {
        visible: !isDescaling
        title: TranslationManager.translate("descaling.title", "Descaling")
        onBackClicked: {
            showRinseInstructions = false
            root.goBack()
        }
    }
}
