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

    Component.onCompleted: root.currentPageTitle = "Settings"
    StackView.onActivated: root.currentPageTitle = "Settings"

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

    // GPU-draining overlay when battery drain is active
    Rectangle {
        id: drainOverlay
        anchors.fill: parent
        z: 1000
        visible: BatteryDrainer.running
        color: "#000000"

        // Tap anywhere to stop
        MouseArea {
            anchors.fill: parent
            onClicked: BatteryDrainer.stop()
        }

        // Heavy GPU animation - multiple rotating gradients with blur
        Repeater {
            model: 8
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 1.5
                height: parent.height * 1.5
                color: "transparent"
                rotation: index * 45

                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.hsla(Math.random(), 1, 0.5, 0.3) }
                    GradientStop { position: 0.5; color: Qt.hsla(Math.random(), 1, 0.5, 0.3) }
                    GradientStop { position: 1.0; color: Qt.hsla(Math.random(), 1, 0.5, 0.3) }
                }

                RotationAnimation on rotation {
                    from: index * 45
                    to: index * 45 + 360
                    duration: 2000 + index * 500
                    loops: Animation.Infinite
                    running: BatteryDrainer.running
                }
            }
        }

        // Many animated circles for GPU load
        Repeater {
            model: 50
            Rectangle {
                property real startX: Math.random() * drainOverlay.width
                property real startY: Math.random() * drainOverlay.height
                x: startX
                y: startY
                width: 50 + Math.random() * 100
                height: width
                radius: width / 2
                color: "transparent"
                border.width: 3
                border.color: Qt.hsla(index / 50.0, 1, 0.5, 0.7)

                SequentialAnimation on scale {
                    loops: Animation.Infinite
                    running: BatteryDrainer.running
                    NumberAnimation { from: 0.5; to: 2.0; duration: 1000 + Math.random() * 2000; easing.type: Easing.InOutQuad }
                    NumberAnimation { from: 2.0; to: 0.5; duration: 1000 + Math.random() * 2000; easing.type: Easing.InOutQuad }
                }

                RotationAnimation on rotation {
                    from: 0; to: 360
                    duration: 3000 + Math.random() * 2000
                    loops: Animation.Infinite
                    running: BatteryDrainer.running
                }

                NumberAnimation on opacity {
                    from: 0.3; to: 1.0
                    duration: 500 + Math.random() * 1000
                    loops: Animation.Infinite
                    running: BatteryDrainer.running
                }
            }
        }

        // Status text
        Column {
            anchors.centerIn: parent
            spacing: 20

            Tr {
                anchors.horizontalCenter: parent.horizontalCenter
                key: "settings.drain.drainingBattery"
                fallback: "DRAINING BATTERY"
                color: "white"
                font.pixelSize: 48
                font.bold: true

                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    running: BatteryDrainer.running
                    NumberAnimation { from: 1.0; to: 0.3; duration: 500 }
                    NumberAnimation { from: 0.3; to: 1.0; duration: 500 }
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 40

                Column {
                    spacing: 4
                    Tr {
                        key: "settings.drain.cpu"
                        fallback: "CPU"
                        color: "#ff6666"
                        font.pixelSize: 18
                        font.bold: true
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    Text {
                        text: BatteryDrainer.cpuUsage.toFixed(0) + "%"
                        color: "#ff6666"
                        font.pixelSize: 36
                        font.bold: true
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    Text {
                        text: BatteryDrainer.cpuCores + " cores active"
                        color: "#ff9999"
                        font.pixelSize: 14
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }

                Column {
                    spacing: 4
                    Tr {
                        key: "settings.drain.gpu"
                        fallback: "GPU"
                        color: "#66ff66"
                        font.pixelSize: 18
                        font.bold: true
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    Text {
                        text: BatteryDrainer.gpuUsage.toFixed(0) + "%"
                        color: "#66ff66"
                        font.pixelSize: 36
                        font.bold: true
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    Text {
                        text: "Animations active"
                        color: "#99ff99"
                        font.pixelSize: 14
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }

            Tr {
                anchors.horizontalCenter: parent.horizontalCenter
                key: "settings.drain.screenMaxBrightness"
                fallback: "Screen: MAX brightness"
                color: "#ffaa66"
                font.pixelSize: 24
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Battery: " + BatteryManager.batteryPercent + "%"
                color: "yellow"
                font.pixelSize: 32
                font.bold: true
            }

            Tr {
                anchors.horizontalCenter: parent.horizontalCenter
                key: "settings.drain.tapToStop"
                fallback: "Tap anywhere to stop"
                color: "#aaaaaa"
                font.pixelSize: 18
            }
        }
    }

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
                    ["Bluetooth", "Preferences", "Screensaver", "Visualizer", "AI", "Accessibility", "Themes", "Language", "History", "Update", "Debug"] :
                    ["Bluetooth", "Preferences", "Screensaver", "Visualizer", "AI", "Accessibility", "Themes", "Language", "History", "Update"]
                if (currentIndex >= 0 && currentIndex < tabNames.length) {
                    AccessibilityManager.announce(tabNames[currentIndex] + " tab")
                }
            }
        }

        background: Rectangle {
            color: "transparent"
        }

        TabButton {
            id: bluetoothTab
            text: "Bluetooth"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 0
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 0 ? Theme.textColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 0 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: "Bluetooth tab" + (tabBar.currentIndex === 0 ? ", selected" : "")
                accessibleItem: bluetoothTab
                onAccessibleClicked: tabBar.currentIndex = 0
            }
        }

        TabButton {
            id: preferencesTabButton
            text: "Preferences"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 1
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 1 ? Theme.textColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 1 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: "Preferences tab" + (tabBar.currentIndex === 1 ? ", selected" : "")
                accessibleItem: preferencesTabButton
                onAccessibleClicked: tabBar.currentIndex = 1
            }
        }

        TabButton {
            id: screensaverTab
            text: "Screensaver"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 2
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 2 ? Theme.textColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 2 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: "Screensaver tab" + (tabBar.currentIndex === 2 ? ", selected" : "")
                accessibleItem: screensaverTab
                onAccessibleClicked: tabBar.currentIndex = 2
            }
        }

        TabButton {
            id: visualizerTabButton
            text: "Visualizer"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 3
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 3 ? Theme.textColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 3 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: "Visualizer tab" + (tabBar.currentIndex === 3 ? ", selected" : "")
                accessibleItem: visualizerTabButton
                onAccessibleClicked: tabBar.currentIndex = 3
            }
        }

        TabButton {
            id: aiTabButton
            text: "AI"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 4
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 4 ? Theme.textColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 4 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: "AI tab" + (tabBar.currentIndex === 4 ? ", selected" : "")
                accessibleItem: aiTabButton
                onAccessibleClicked: tabBar.currentIndex = 4
            }
        }

        TabButton {
            id: accessibilityTabButton
            text: "Access"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 5
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 5 ? Theme.textColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 5 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: "Accessibility tab" + (tabBar.currentIndex === 5 ? ", selected" : "")
                accessibleItem: accessibilityTabButton
                onAccessibleClicked: tabBar.currentIndex = 5
            }
        }

        TabButton {
            id: themesTabButton
            text: "Themes"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 6
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 6 ? Theme.textColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 6 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: "Themes tab" + (tabBar.currentIndex === 6 ? ", selected" : "")
                accessibleItem: themesTabButton
                onAccessibleClicked: tabBar.currentIndex = 6
            }
        }

        TabButton {
            id: languageTabButton
            text: "Language"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 7
            contentItem: Row {
                spacing: 4
                Text {
                    text: parent.parent.text
                    font: parent.parent.font
                    color: tabBar.currentIndex === 7 ? Theme.textColor : Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    visible: TranslationManager.currentLanguage !== "en" && TranslationManager.untranslatedCount > 0
                    width: badgeText.width + 8
                    height: 16
                    radius: 8
                    color: Theme.warningColor
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text: TranslationManager.untranslatedCount > 99 ? "99+" : TranslationManager.untranslatedCount
                        font.pixelSize: 10
                        font.bold: true
                        color: "white"
                    }
                }
            }
            background: Rectangle {
                color: tabBar.currentIndex === 7 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: "Language tab" + (tabBar.currentIndex === 7 ? ", selected" : "")
                accessibleItem: languageTabButton
                onAccessibleClicked: tabBar.currentIndex = 7
            }
        }

        TabButton {
            id: historyTabButton
            text: "History"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 8
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 8 ? Theme.textColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 8 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: "History tab" + (tabBar.currentIndex === 8 ? ", selected" : "")
                accessibleItem: historyTabButton
                onAccessibleClicked: tabBar.currentIndex = 8
            }
        }

        TabButton {
            id: updateTabButton
            text: "Update"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 9
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 9 ? Theme.textColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 9 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: "Update tab" + (tabBar.currentIndex === 9 ? ", selected" : "")
                accessibleItem: updateTabButton
                onAccessibleClicked: tabBar.currentIndex = 9
            }
        }

        TabButton {
            id: debugTabButton
            visible: Settings.isDebugBuild
            text: "Debug"
            width: visible ? implicitWidth : 0
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 10
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 10 ? Theme.textColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 10 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: "Debug tab" + (tabBar.currentIndex === 10 ? ", selected" : "")
                accessibleItem: debugTabButton
                onAccessibleClicked: tabBar.currentIndex = 10
            }
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

        // Tab 2: Screensaver/Network - preloads async in background
        Loader {
            id: screensaverLoader
            active: true
            asynchronous: true
            source: "settings/SettingsScreensaverTab.qml"
        }

        // Tab 3: Visualizer - preloads async in background
        Loader {
            id: visualizerLoader
            active: true
            asynchronous: true
            source: "settings/SettingsVisualizerTab.qml"
        }

        // Tab 4: AI - preloads async in background
        Loader {
            id: aiLoader
            active: true
            asynchronous: true
            source: "settings/SettingsAITab.qml"
        }

        // Tab 5: Accessibility - preloads async in background
        Loader {
            id: accessibilityLoader
            active: true
            asynchronous: true
            source: "settings/SettingsAccessibilityTab.qml"
        }

        // Tab 6: Themes - preloads async in background
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

        // Tab 7: Language - preloads async in background
        Loader {
            id: languageLoader
            active: true
            asynchronous: true
            source: "settings/SettingsLanguageTab.qml"
        }

        // Tab 8: History - preloads async in background
        Loader {
            id: historyLoader
            active: true
            asynchronous: true
            source: "settings/SettingsShotHistoryTab.qml"
        }

        // Tab 9: Update - preloads async in background
        Loader {
            id: updateLoader
            active: true
            asynchronous: true
            source: "settings/SettingsUpdateTab.qml"
        }

        // Tab 10: Debug - only in debug builds
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
        width: 500
        height: 540
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
            bottomPadding: 0
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
            spacing: 15

            // Step indicator
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                spacing: 10

                Repeater {
                    model: 6
                    Rectangle {
                        width: 30
                        height: 30
                        radius: 15
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
                    spacing: 15

                    Text {
                        text: "Calibrate Your Flow Sensor"
                        color: Theme.textColor
                        font.pixelSize: 18
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: "This wizard will dispense water at 3 different flow rates.\n\nYou'll need:\n- A separate scale (kitchen scale, etc.)\n- An empty cup\n- About 300g of water total (100g per test)"
                        color: Theme.textSecondaryColor
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: "After each test, enter the weight shown on your scale."
                        color: Theme.textColor
                        font.pixelSize: 14
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
                        spacing: 15

                        Text {
                            text: "Test " + (testStep.testIndex + 1) + ": " + flowCalibrationDialog.testNames[testStep.testIndex] + " Flow"
                            color: Theme.textColor
                            font.pixelSize: 18
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
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: 150
                            height: 150
                            radius: 75
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
                                font.pixelSize: 24
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }

                        ColumnLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 10
                            visible: flowCalibrationDialog.flowIntegrals[testStep.testIndex] > 0

                            Text {
                                text: "Actual weight from your scale:"
                                color: Theme.textColor
                                Layout.alignment: Qt.AlignHCenter
                            }

                            ValueInput {
                                id: weightInput
                                Layout.preferredWidth: 150
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
                    spacing: 15

                    Text {
                        text: "Calibration Results"
                        color: Theme.textColor
                        font.pixelSize: 18
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
                        spacing: 15

                        Text {
                            text: "Average calibration factor:"
                            color: Theme.textColor
                            font.pixelSize: 16
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
                            font.pixelSize: 24
                            font.bold: true
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                // Step 5: Verification (simplified)
                ColumnLayout {
                    spacing: 12

                    Text {
                        text: "Verify Calibration"
                        color: Theme.textColor
                        font.pixelSize: 18
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
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 150
                        height: 80
                        radius: 8
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
                            font.pixelSize: 20
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    ColumnLayout {
                        visible: flowCalibrationDialog.verificationComplete
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 10

                        Text {
                            text: "Enter actual weight from your scale:"
                            color: Theme.textColor
                            font.pixelSize: 14
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
                            width: 150
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
                spacing: 10

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
        width: 300
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
                leftPadding: 12
                rightPadding: 12
                topPadding: 12
                bottomPadding: 12
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

                Button {
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

                Button {
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
        title: "Settings"
        onBackClicked: root.goToIdle()
    }
}
