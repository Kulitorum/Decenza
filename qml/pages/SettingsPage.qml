import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import DecenzaDE1
import "../components"

Page {
    id: settingsPage
    objectName: "settingsPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = TranslationManager.translate("settings.title", "Settings")
    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("settings.title", "Settings")

    // Requested tab to switch to (set before pushing page)
    property int requestedTabIndex: -1

    // Timer to switch tab after page is fully loaded
    Timer {
        id: tabSwitchTimer
        interval: 500  // Wait for all tabs to be created
        repeat: false
        onTriggered: {
            if (settingsPage.requestedTabIndex >= 0) {
                tabBar.currentIndex = settingsPage.requestedTabIndex
                settingsPage.requestedTabIndex = -1
            }
        }
    }

    StackView.onActivating: {
        if (requestedTabIndex >= 0) {
            tabSwitchTimer.start()
        }
    }

    // Flag to prevent navigation during calibration
    property bool calibrationInProgress: false

    // Tab bar at top
    TabBar {
        id: tabBar
        anchors.top: parent.top
        anchors.topMargin: Theme.pageTopMargin
        anchors.left: parent.left
        anchors.leftMargin: Theme.standardMargin
        z: 2

        property bool accessibilityCustomHandler: true

        onCurrentIndexChanged: {
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                var tabNames = Settings.isDebugBuild ?
                    ["Bluetooth", "Preferences", "Options", "Screensaver", "Visualizer", "AI", "Accessibility", "Themes", "Language", "History", "Update", "About", "Debug"] :
                    ["Bluetooth", "Preferences", "Options", "Screensaver", "Visualizer", "AI", "Accessibility", "Themes", "Language", "History", "Update", "About"]
                if (currentIndex >= 0 && currentIndex < tabNames.length) {
                    AccessibilityManager.announce(tabNames[currentIndex] + " tab")
                }
            }
        }

        background: Rectangle {
            color: "transparent"
        }

        StyledTabButton {
            id: bluetoothTab
            text: TranslationManager.translate("settings.tab.bluetooth", "Bluetooth")
            tabLabel: TranslationManager.translate("settings.tab.bluetooth", "Bluetooth")
        }

        StyledTabButton {
            id: preferencesTabButton
            text: TranslationManager.translate("settings.tab.preferences", "Preferences")
            tabLabel: TranslationManager.translate("settings.tab.preferences", "Preferences")
        }

        StyledTabButton {
            id: optionsTabButton
            text: TranslationManager.translate("settings.tab.options", "Options")
            tabLabel: TranslationManager.translate("settings.tab.options", "Options")
        }

        StyledTabButton {
            id: screensaverTab
            text: TranslationManager.translate("settings.tab.screensaver", "Screensaver")
            tabLabel: TranslationManager.translate("settings.tab.screensaver", "Screensaver")
        }

        StyledTabButton {
            id: visualizerTabButton
            text: TranslationManager.translate("settings.tab.visualizer", "Visualizer")
            tabLabel: TranslationManager.translate("settings.tab.visualizer", "Visualizer")
        }

        StyledTabButton {
            id: aiTabButton
            text: TranslationManager.translate("settings.tab.ai", "AI")
            tabLabel: TranslationManager.translate("settings.tab.ai", "AI")
        }

        StyledTabButton {
            id: accessibilityTabButton
            text: TranslationManager.translate("settings.tab.accessibility", "Access")
            tabLabel: TranslationManager.translate("settings.tab.accessibility.full", "Accessibility")
        }

        StyledTabButton {
            id: themesTabButton
            text: TranslationManager.translate("settings.tab.themes", "Themes")
            tabLabel: TranslationManager.translate("settings.tab.themes", "Themes")
        }

        // Language tab with badge for untranslated strings
        StyledTabButton {
            id: languageTabButton
            text: TranslationManager.translate("settings.tab.language", "Language")
            tabLabel: TranslationManager.translate("settings.tab.language", "Language")

            // Override contentItem to add badge
            contentItem: Row {
                spacing: Theme.scaled(4)
                Text {
                    text: languageTabButton.text
                    font: languageTabButton.font
                    color: languageTabButton.checked ? Theme.textColor : Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    visible: TranslationManager.currentLanguage !== "en" && TranslationManager.untranslatedCount > 0
                    width: badgeText.width + 8
                    height: Theme.scaled(16)
                    radius: Theme.scaled(8)
                    color: Theme.warningColor
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text: TranslationManager.untranslatedCount > 99 ? "99+" : TranslationManager.untranslatedCount
                        font.pixelSize: Theme.scaled(10)
                        font.bold: true
                        color: "white"
                    }
                }
            }
        }

        StyledTabButton {
            id: historyTabButton
            text: TranslationManager.translate("settings.tab.history", "History")
            tabLabel: TranslationManager.translate("settings.tab.history", "History")
        }

        StyledTabButton {
            id: updateTabButton
            text: TranslationManager.translate("settings.tab.update", "Update")
            tabLabel: TranslationManager.translate("settings.tab.update", "Update")
        }

        StyledTabButton {
            id: aboutTabButton
            text: TranslationManager.translate("settings.tab.about", "About")
            tabLabel: TranslationManager.translate("settings.tab.about", "About")
        }

        StyledTabButton {
            id: debugTabButton
            visible: Settings.isDebugBuild
            text: TranslationManager.translate("settings.tab.debug", "Debug")
            tabLabel: TranslationManager.translate("settings.tab.debug", "Debug")
            width: visible ? implicitWidth : 0
        }
    }

    // Tab content area - all tabs preload in background
    StackLayout {
        id: tabContent
        anchors.top: tabBar.bottom
        anchors.topMargin: Theme.spacingMedium
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        anchors.bottomMargin: Theme.spacingMedium
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin

        currentIndex: tabBar.currentIndex

        // Tab 0: Bluetooth - loads synchronously (first tab appears instantly)
        Loader {
            id: bluetoothLoader
            active: true
            asynchronous: false
            source: "settings/SettingsBluetoothTab.qml"
        }

        // Tab 1: Preferences - preloads async in background
        Loader {
            id: preferencesLoader
            active: true
            asynchronous: true
            source: "settings/SettingsPreferencesTab.qml"
            onLoaded: {
                item.openFlowCalibrationDialog.connect(function() {
                    flowCalibrationDialog.open()
                })
            }
        }

        // Tab 2: Options - preloads async in background
        Loader {
            id: optionsLoader
            active: true
            asynchronous: true
            source: "settings/SettingsOptionsTab.qml"
        }

        // Tab 3: Screensaver/Network - preloads async in background
        Loader {
            id: screensaverLoader
            active: true
            asynchronous: true
            source: "settings/SettingsScreensaverTab.qml"
        }

        // Tab 4: Visualizer - preloads async in background
        Loader {
            id: visualizerLoader
            active: true
            asynchronous: true
            source: "settings/SettingsVisualizerTab.qml"
        }

        // Tab 5: AI - preloads async in background
        Loader {
            id: aiLoader
            active: true
            asynchronous: true
            source: "settings/SettingsAITab.qml"
        }

        // Tab 6: Accessibility - preloads async in background
        Loader {
            id: accessibilityLoader
            active: true
            asynchronous: true
            source: "settings/SettingsAccessibilityTab.qml"
        }

        // Tab 7: Themes - preloads async in background
        Loader {
            id: themesLoader
            active: true
            asynchronous: true
            source: "settings/SettingsThemesTab.qml"
            onLoaded: {
                item.openSaveThemeDialog.connect(function() {
                    saveThemeDialog.open()
                })
            }
        }

        // Tab 8: Language - preloads async in background
        Loader {
            id: languageLoader
            active: true
            asynchronous: true
            source: "settings/SettingsLanguageTab.qml"
        }

        // Tab 9: History - preloads async in background
        Loader {
            id: historyLoader
            active: true
            asynchronous: true
            source: "settings/SettingsShotHistoryTab.qml"
        }

        // Tab 10: Update - preloads async in background
        Loader {
            id: updateLoader
            active: true
            asynchronous: true
            source: "settings/SettingsUpdateTab.qml"
        }

        // Tab 11: About
        Loader {
            id: aboutLoader
            active: true
            asynchronous: true
            source: "settings/SettingsAboutTab.qml"
        }

        // Tab 12: Debug - only in debug builds
        Loader {
            id: debugLoader
            active: Settings.isDebugBuild
            asynchronous: true
            source: "settings/SettingsDebugTab.qml"
        }
    }

    // Flow Calibration Dialog (kept in main page due to root.calibrationInProgress reference)
    Dialog {
        id: flowCalibrationDialog
        modal: true
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2 - 30
        width: Theme.scaled(500)
        height: Theme.scaled(540)
        closePolicy: Popup.NoAutoClose
        padding: 20

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        header: Text {
            text: "Flow Sensor Calibration"
            color: Theme.textColor
            font: Theme.subtitleFont
            padding: 20
            bottomPadding: Theme.scaled(0)
        }

        property int currentStep: 0
        property var testFlows: [3.0, 6.0, 9.0]
        property var testNames: ["Low", "Medium", "High"]
        property var measuredWeights: [0, 0, 0]
        property var flowIntegrals: [0, 0, 0]
        property bool isDispensing: false
        property double currentFlowIntegral: 0
        property double verificationTarget: 100
        property double verificationFlowScaleWeight: 0
        property double verificationActualWeight: 0
        property bool verificationComplete: false

        onOpened: {
            currentStep = 0
            measuredWeights = [0, 0, 0]
            flowIntegrals = [0, 0, 0]
            isDispensing = false
            currentFlowIntegral = 0
            verificationFlowScaleWeight = 0
            verificationActualWeight = 0
            verificationComplete = false
        }

        onClosed: {
            settingsPage.calibrationInProgress = false
            MainController.restoreCurrentProfile()
        }

        Connections {
            target: MachineState
            enabled: flowCalibrationDialog.isDispensing

            function onShotEnded() {
                if (!flowCalibrationDialog.isDispensing) return

                if (flowCalibrationDialog.currentStep === 5) {
                    flowCalibrationDialog.verificationFlowScaleWeight = FlowScale.weight
                    flowCalibrationDialog.verificationComplete = true
                    flowCalibrationDialog.isDispensing = false
                } else {
                    var testIdx = flowCalibrationDialog.currentStep - 1
                    var newIntegrals = flowCalibrationDialog.flowIntegrals.slice()
                    newIntegrals[testIdx] = FlowScale.rawFlowIntegral
                    flowCalibrationDialog.flowIntegrals = newIntegrals
                    flowCalibrationDialog.isDispensing = false
                }
            }
        }

        Connections {
            target: FlowScale
            enabled: flowCalibrationDialog.isDispensing

            function onRawFlowIntegralChanged() {
                flowCalibrationDialog.currentFlowIntegral = FlowScale.rawFlowIntegral
            }
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            // Step indicator
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(10)

                Repeater {
                    model: 6
                    Rectangle {
                        width: Theme.scaled(30)
                        height: Theme.scaled(30)
                        radius: Theme.scaled(15)
                        color: index <= flowCalibrationDialog.currentStep ? Theme.primaryColor : Theme.surfaceColor
                        border.color: Theme.primaryColor
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: index === 0 ? "!" : (index === 5 ? "v" : index)
                            color: index <= flowCalibrationDialog.currentStep ? "white" : Theme.textSecondaryColor
                            font.bold: true
                        }
                    }
                }
            }

            // Content based on step
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: flowCalibrationDialog.currentStep

                // Step 0: Introduction
                ColumnLayout {
                    spacing: Theme.scaled(15)

                    Text {
                        text: "Calibrate Your Flow Sensor"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(18)
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: "This wizard will dispense water at 3 different flow rates.\n\nYou'll need:\n- A separate scale (kitchen scale, etc.)\n- An empty cup\n- About 300g of water total (100g per test)"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(14)
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: "After each test, enter the weight shown on your scale."
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    Item { Layout.fillHeight: true }
                }

                // Steps 1-3: Test runs
                Repeater {
                    model: 3

                    ColumnLayout {
                        id: testStep
                        property int testIndex: index
                        spacing: Theme.scaled(15)

                        Text {
                            text: "Test " + (testStep.testIndex + 1) + ": " + flowCalibrationDialog.testNames[testStep.testIndex] + " Flow"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(18)
                            font.bold: true
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: flowCalibrationDialog.isDispensing ?
                                  "Dispensing water... (will stop at ~100g)" :
                                  flowCalibrationDialog.flowIntegrals[testStep.testIndex] > 0 ?
                                      "Enter the weight from your scale below, then press Next" :
                                      "1. Place empty cup on your scale (will auto-tare)\n2. Press 'Ready' then press espresso button on DE1"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(14)
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: Theme.scaled(150)
                            height: Theme.scaled(150)
                            radius: Theme.scaled(75)
                            color: "transparent"
                            border.color: flowCalibrationDialog.isDispensing ? Theme.primaryColor : Theme.surfaceColor
                            border.width: 4
                            visible: flowCalibrationDialog.currentStep === testStep.testIndex + 1

                            Text {
                                anchors.centerIn: parent
                                text: flowCalibrationDialog.isDispensing ?
                                      flowCalibrationDialog.currentFlowIntegral.toFixed(0) + "g\n(raw)" :
                                      flowCalibrationDialog.flowIntegrals[testStep.testIndex] > 0 ?
                                          flowCalibrationDialog.flowIntegrals[testStep.testIndex].toFixed(0) + "g\n(raw)" :
                                          "Ready"
                                color: flowCalibrationDialog.isDispensing || flowCalibrationDialog.flowIntegrals[testStep.testIndex] > 0 ?
                                       Theme.primaryColor : Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(24)
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }

                        ColumnLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: Theme.scaled(10)
                            visible: flowCalibrationDialog.flowIntegrals[testStep.testIndex] > 0

                            Text {
                                text: "Actual weight from your scale:"
                                color: Theme.textColor
                                Layout.alignment: Qt.AlignHCenter
                            }

                            ValueInput {
                                id: weightInput
                                Layout.preferredWidth: Theme.scaled(150)
                                Layout.alignment: Qt.AlignHCenter
                                value: 100
                                from: 50
                                to: 200
                                stepSize: 1
                                suffix: " g"
                                valueColor: Theme.primaryColor

                                onValueModified: function(newValue) {
                                    weightInput.value = newValue
                                    var newWeights = flowCalibrationDialog.measuredWeights.slice()
                                    newWeights[testStep.testIndex] = newValue
                                    flowCalibrationDialog.measuredWeights = newWeights
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // Step 4: Results
                ColumnLayout {
                    spacing: Theme.scaled(15)

                    Text {
                        text: "Calibration Results"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(18)
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    GridLayout {
                        columns: 4
                        rowSpacing: 8
                        columnSpacing: 20
                        Layout.alignment: Qt.AlignHCenter

                        Text { text: "Test"; color: Theme.textSecondaryColor; font.bold: true }
                        Text { text: "Raw Flow"; color: Theme.textSecondaryColor; font.bold: true }
                        Text { text: "Actual"; color: Theme.textSecondaryColor; font.bold: true }
                        Text { text: "Factor"; color: Theme.textSecondaryColor; font.bold: true }

                        Text { text: "Low"; color: Theme.textColor }
                        Text { text: flowCalibrationDialog.flowIntegrals[0].toFixed(1) + "g"; color: Theme.textColor }
                        Text { text: flowCalibrationDialog.measuredWeights[0].toFixed(1) + "g"; color: Theme.textColor }
                        Text {
                            text: flowCalibrationDialog.flowIntegrals[0] > 0 ?
                                  (flowCalibrationDialog.measuredWeights[0] / flowCalibrationDialog.flowIntegrals[0]).toFixed(3) : "-"
                            color: Theme.primaryColor
                            font.bold: true
                        }

                        Text { text: "Medium"; color: Theme.textColor }
                        Text { text: flowCalibrationDialog.flowIntegrals[1].toFixed(1) + "g"; color: Theme.textColor }
                        Text { text: flowCalibrationDialog.measuredWeights[1].toFixed(1) + "g"; color: Theme.textColor }
                        Text {
                            text: flowCalibrationDialog.flowIntegrals[1] > 0 ?
                                  (flowCalibrationDialog.measuredWeights[1] / flowCalibrationDialog.flowIntegrals[1]).toFixed(3) : "-"
                            color: Theme.primaryColor
                            font.bold: true
                        }

                        Text { text: "High"; color: Theme.textColor }
                        Text { text: flowCalibrationDialog.flowIntegrals[2].toFixed(1) + "g"; color: Theme.textColor }
                        Text { text: flowCalibrationDialog.measuredWeights[2].toFixed(1) + "g"; color: Theme.textColor }
                        Text {
                            text: flowCalibrationDialog.flowIntegrals[2] > 0 ?
                                  (flowCalibrationDialog.measuredWeights[2] / flowCalibrationDialog.flowIntegrals[2]).toFixed(3) : "-"
                            color: Theme.primaryColor
                            font.bold: true
                        }
                    }

                    Item { height: 10 }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Theme.scaled(15)

                        Text {
                            text: "Average calibration factor:"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                        }

                        Text {
                            text: {
                                var sum = 0
                                var count = 0
                                for (var i = 0; i < 3; i++) {
                                    if (flowCalibrationDialog.flowIntegrals[i] > 0 && flowCalibrationDialog.measuredWeights[i] > 0) {
                                        sum += flowCalibrationDialog.measuredWeights[i] / flowCalibrationDialog.flowIntegrals[i]
                                        count++
                                    }
                                }
                                return count > 0 ? (sum / count).toFixed(3) : "-"
                            }
                            color: Theme.primaryColor
                            font.pixelSize: Theme.scaled(24)
                            font.bold: true
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                // Step 5: Verification (simplified)
                ColumnLayout {
                    spacing: Theme.scaled(12)

                    Text {
                        text: "Verify Calibration"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(18)
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: flowCalibrationDialog.verificationComplete ?
                              "Verification complete! Compare the weights below." :
                              flowCalibrationDialog.isDispensing ?
                              "Dispensing..." :
                              "Dispense " + flowCalibrationDialog.verificationTarget + "g using the new calibration."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(13)
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: Theme.scaled(150)
                        height: Theme.scaled(80)
                        radius: Theme.scaled(8)
                        color: Theme.backgroundColor
                        border.color: flowCalibrationDialog.isDispensing ? Theme.primaryColor : Theme.surfaceColor
                        border.width: 2
                        visible: flowCalibrationDialog.isDispensing || flowCalibrationDialog.verificationComplete

                        Text {
                            anchors.centerIn: parent
                            text: flowCalibrationDialog.verificationComplete ?
                                  flowCalibrationDialog.verificationFlowScaleWeight.toFixed(1) + "g\n(FlowScale)" :
                                  FlowScale.weight.toFixed(1) + "g"
                            color: Theme.primaryColor
                            font.pixelSize: Theme.scaled(20)
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    ColumnLayout {
                        visible: flowCalibrationDialog.verificationComplete
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Theme.scaled(10)

                        Text {
                            text: "Enter actual weight from your scale:"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            Layout.alignment: Qt.AlignHCenter
                        }

                        ValueInput {
                            id: verificationWeightInput
                            value: flowCalibrationDialog.verificationActualWeight > 0 ? flowCalibrationDialog.verificationActualWeight : 100
                            from: 0
                            to: 500
                            decimals: 1
                            stepSize: 0.5
                            suffix: "g"
                            width: Theme.scaled(150)
                            Layout.alignment: Qt.AlignHCenter
                            onValueModified: function(newValue) {
                                flowCalibrationDialog.verificationActualWeight = newValue
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // Navigation buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(10)

                AccessibleButton {
                    text: "Cancel"
                    accessibleName: "Cancel calibration"
                    onClicked: flowCalibrationDialog.close()
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    id: calibrationNextButton
                    text: flowCalibrationDialog.currentStep === 0 ? "Start" :
                          flowCalibrationDialog.currentStep < 4 ?
                              (flowCalibrationDialog.isDispensing ? "Waiting..." :
                               flowCalibrationDialog.flowIntegrals[flowCalibrationDialog.currentStep - 1] > 0 ? "Next" : "Ready") :
                          flowCalibrationDialog.currentStep === 4 ? "Save and Verify" :
                          flowCalibrationDialog.isDispensing ? "Waiting..." :
                          flowCalibrationDialog.verificationComplete ? "Done" : "Start"
                    accessibleName: text
                    enabled: !flowCalibrationDialog.isDispensing

                    contentItem: Text {
                        text: calibrationNextButton.text
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        if (flowCalibrationDialog.currentStep === 0) {
                            flowCalibrationDialog.currentStep = 1
                        } else if (flowCalibrationDialog.currentStep < 4) {
                            var testIdx = flowCalibrationDialog.currentStep - 1
                            if (flowCalibrationDialog.flowIntegrals[testIdx] === 0) {
                                FlowScale.resetRawFlowIntegral()
                                flowCalibrationDialog.currentFlowIntegral = 0
                                flowCalibrationDialog.isDispensing = true
                                settingsPage.calibrationInProgress = true
                                var flowRate = flowCalibrationDialog.testFlows[testIdx]
                                MainController.startCalibrationDispense(flowRate, 100)
                            } else if (flowCalibrationDialog.measuredWeights[testIdx] > 0) {
                                flowCalibrationDialog.currentStep++
                            }
                        } else if (flowCalibrationDialog.currentStep === 4) {
                            var sum = 0
                            var count = 0
                            for (var i = 0; i < 3; i++) {
                                if (flowCalibrationDialog.flowIntegrals[i] > 0 && flowCalibrationDialog.measuredWeights[i] > 0) {
                                    sum += flowCalibrationDialog.measuredWeights[i] / flowCalibrationDialog.flowIntegrals[i]
                                    count++
                                }
                            }
                            if (count > 0) {
                                Settings.flowCalibrationFactor = sum / count
                            }
                            flowCalibrationDialog.currentStep = 5
                        } else if (flowCalibrationDialog.currentStep === 5) {
                            if (flowCalibrationDialog.verificationComplete) {
                                flowCalibrationDialog.close()
                            } else {
                                FlowScale.resetWeight()
                                flowCalibrationDialog.isDispensing = true
                                settingsPage.calibrationInProgress = true
                                MainController.startVerificationDispense(flowCalibrationDialog.verificationTarget)
                            }
                        }
                    }
                }
            }
        }
    }

    // Save Theme Dialog
    Dialog {
        id: saveThemeDialog
        modal: true
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2 - keyboardOffset
        width: Theme.scaled(300)
        padding: 20

        property string themeName: ""
        property real keyboardOffset: 0

        Behavior on y {
            NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
        }

        Connections {
            target: Qt.inputMethod
            function onVisibleChanged() {
                if (Qt.inputMethod.visible && saveThemeDialog.visible) {
                    saveThemeDialog.keyboardOffset = parent.height * 0.25
                } else {
                    saveThemeDialog.keyboardOffset = 0
                }
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        onOpened: {
            themeName = ""
            themeNameInput.text = ""
            themeNameInput.forceActiveFocus()
        }

        onClosed: {
            keyboardOffset = 0
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingMedium

            Tr {
                key: "settings.themes.saveTheme"
                fallback: "Save Theme"
                color: Theme.textColor
                font: Theme.subtitleFont
                Layout.alignment: Qt.AlignHCenter
            }

            TextField {
                id: themeNameInput
                Layout.fillWidth: true
                color: Theme.textColor
                placeholderTextColor: Theme.textSecondaryColor
                leftPadding: Theme.scaled(12)
                rightPadding: Theme.scaled(12)
                topPadding: Theme.scaled(12)
                bottomPadding: Theme.scaled(12)
                background: Rectangle {
                    color: Theme.backgroundColor
                    radius: Theme.buttonRadius
                    border.color: themeNameInput.activeFocus ? Theme.primaryColor : Theme.borderColor
                    border.width: 1
                }
                onTextChanged: saveThemeDialog.themeName = text
                onAccepted: {
                    if (saveThemeDialog.themeName.trim().length > 0) {
                        Settings.saveCurrentTheme(saveThemeDialog.themeName.trim())
                        if (themesLoader.item) themesLoader.item.refreshPresets()
                        saveThemeDialog.close()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                StyledButton {
                    Layout.fillWidth: true
                    property string buttonText: TranslationManager.translate("common.cancel", "Cancel")
                    text: buttonText
                    onClicked: saveThemeDialog.close()
                    background: Rectangle {
                        color: Theme.surfaceColor
                        radius: Theme.buttonRadius
                        border.color: Theme.borderColor
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.buttonText
                        color: Theme.textColor
                        font: Theme.labelFont
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                StyledButton {
                    Layout.fillWidth: true
                    property string buttonText: TranslationManager.translate("common.save", "Save")
                    text: buttonText
                    enabled: saveThemeDialog.themeName.trim().length > 0
                    onClicked: {
                        var name = saveThemeDialog.themeName.trim()
                        if (name.length > 0 && name !== "Default") {
                            Settings.saveCurrentTheme(name)
                            if (themesLoader.item) themesLoader.item.refreshPresets()
                            saveThemeDialog.close()
                        }
                    }
                    background: Rectangle {
                        color: parent.enabled ? Theme.primaryColor : Theme.surfaceColor
                        radius: Theme.buttonRadius
                        opacity: parent.pressed ? 0.8 : 1.0
                    }
                    contentItem: Text {
                        text: parent.buttonText
                        color: parent.enabled ? "white" : Theme.textSecondaryColor
                        font: Theme.labelFont
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }

    // Bottom bar with back button
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("settings.title", "Settings")
        onBackClicked: root.goBack()
    }
}
