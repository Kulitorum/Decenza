import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import DecenzaDE1
import "../components"

Page {
    objectName: "settingsPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = "Settings"
    StackView.onActivated: root.currentPageTitle = "Settings"

    // Tap 5x anywhere for simulation mode
    property int simTapCount: 0

    Timer {
        id: simTapResetTimer
        interval: 2000
        onTriggered: simTapCount = 0
    }

    // Simulation mode hint toast
    Rectangle {
        id: simToast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.scaled(150)
        width: simToastText.implicitWidth + 30
        height: simToastText.implicitHeight + 16
        radius: height / 2
        color: "#333"
        opacity: 0
        z: 100

        Text {
            id: simToastText
            anchors.centerIn: parent
            text: simTapCount >= 5 ?
                  (DE1Device.simulationMode ? "Simulation ON" : "Simulation OFF") :
                  (5 - simTapCount) + " taps to toggle simulation"
            color: "white"
            font.pixelSize: 14
        }

        Behavior on opacity { NumberAnimation { duration: 150 } }

        Timer {
            id: simToastHideTimer
            interval: 1500
            onTriggered: simToast.opacity = 0
        }
    }

    MouseArea {
        anchors.fill: parent
        z: -1  // Behind all other controls
        onClicked: {
            simTapCount++
            simTapResetTimer.restart()

            if (simTapCount >= 5) {
                var newState = !DE1Device.simulationMode
                console.log("Simulation mode toggled:", newState ? "ON" : "OFF")
                DE1Device.simulationMode = newState
                if (ScaleDevice) {
                    ScaleDevice.simulationMode = newState
                }
                simToast.opacity = 1
                simToastHideTimer.restart()
                simTapCount = 0
            } else if (simTapCount >= 3) {
                simToast.opacity = 1
                simToastHideTimer.restart()
            }
        }
    }

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

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "DRAINING BATTERY"
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
                    Text {
                        text: "CPU"
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
                    Text {
                        text: "GPU"
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

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Screen: MAX brightness"
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

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Tap anywhere to stop"
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
        anchors.right: parent.right
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin

        background: Rectangle {
            color: "transparent"
        }

        TabButton {
            text: "Bluetooth"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 0
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 0 ? Theme.primaryColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 0 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
        }

        TabButton {
            text: "Preferences"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 1
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 1 ? Theme.primaryColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 1 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
        }

        TabButton {
            text: "Screensaver"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 2
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 2 ? Theme.primaryColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 2 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
        }

        TabButton {
            text: "Visualizer"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 3
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 3 ? Theme.primaryColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 3 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
        }

        TabButton {
            text: "Resolution"
            width: implicitWidth
            visible: Qt.platform.os === "windows"
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 4
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: tabBar.currentIndex === 4 ? Theme.primaryColor : Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tabBar.currentIndex === 4 ? Theme.surfaceColor : "transparent"
                radius: 6
            }
        }
    }

    // Tab content area
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

        // ============ BLUETOOTH TAB ============
        Item {
            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingMedium

                // Machine Connection
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 10

                        Text {
                            text: "Machine"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "Status:"
                                color: Theme.textSecondaryColor
                            }

                            Text {
                                text: DE1Device.connected ? "Connected" : "Disconnected"
                                color: DE1Device.connected ? Theme.successColor : Theme.errorColor
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                text: BLEManager.scanning ? "Stop Scan" : "Scan for DE1"
                                onClicked: {
                                    console.log("DE1 scan button clicked! scanning=" + BLEManager.scanning)
                                    if (BLEManager.scanning) {
                                        BLEManager.stopScan()
                                    } else {
                                        BLEManager.startScan()
                                    }
                                }
                            }
                        }

                        Text {
                            text: "Firmware: " + (DE1Device.firmwareVersion || "Unknown")
                            color: Theme.textSecondaryColor
                            visible: DE1Device.connected
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            clip: true
                            model: BLEManager.discoveredDevices

                            delegate: ItemDelegate {
                                width: ListView.view.width
                                contentItem: Text {
                                    text: modelData.name + " (" + modelData.address + ")"
                                    color: Theme.textColor
                                }
                                background: Rectangle {
                                    color: parent.hovered ? Theme.accentColor : "transparent"
                                    radius: 4
                                }
                                onClicked: DE1Device.connectToDevice(modelData.address)
                            }

                            Label {
                                anchors.centerIn: parent
                                text: "No devices found"
                                visible: parent.count === 0
                                color: Theme.textSecondaryColor
                            }
                        }

                        // DE1 scan log
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Qt.darker(Theme.surfaceColor, 1.2)
                            radius: 4

                            ScrollView {
                                id: de1LogScroll
                                anchors.fill: parent
                                anchors.margins: 8
                                clip: true

                                TextArea {
                                    id: de1LogText
                                    readOnly: true
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: 11
                                    font.family: "monospace"
                                    wrapMode: Text.Wrap
                                    background: null
                                    text: ""
                                }
                            }

                            Connections {
                                target: BLEManager
                                function onDe1LogMessage(message) {
                                    de1LogText.text += message + "\n"
                                    de1LogScroll.ScrollBar.vertical.position = 1.0 - de1LogScroll.ScrollBar.vertical.size
                                }
                            }
                        }
                    }
                }

                // Scale Connection
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 10

                        Text {
                            text: "Scale"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "Status:"
                                color: Theme.textSecondaryColor
                            }

                            Text {
                                text: (ScaleDevice && ScaleDevice.connected) ? "Connected" :
                                      BLEManager.scaleConnectionFailed ? "Not found" : "Disconnected"
                                color: (ScaleDevice && ScaleDevice.connected) ? Theme.successColor :
                                       BLEManager.scaleConnectionFailed ? Theme.errorColor : Theme.textSecondaryColor
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                text: BLEManager.scanning ? "Scanning..." : "Scan for Scales"
                                enabled: !BLEManager.scanning
                                onClicked: BLEManager.scanForScales()
                            }
                        }

                        // FlowScale fallback notice
                        Rectangle {
                            Layout.fillWidth: true
                            height: flowScaleNotice.implicitHeight + 16
                            radius: 6
                            color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.15)
                            border.color: Theme.warningColor
                            border.width: 1
                            visible: ScaleDevice && ScaleDevice.name === "Flow Scale"

                            Text {
                                id: flowScaleNotice
                                anchors.fill: parent
                                anchors.margins: 8
                                text: "Using Flow Scale (estimated weight from DE1 flow data)"
                                color: Theme.warningColor
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        // Saved scale info
                        RowLayout {
                            Layout.fillWidth: true
                            visible: BLEManager.hasSavedScale

                            Text {
                                text: "Saved scale:"
                                color: Theme.textSecondaryColor
                            }

                            Text {
                                text: Settings.scaleType || "Unknown"
                                color: Theme.textColor
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                text: "Forget"
                                onClicked: {
                                    Settings.setScaleAddress("")
                                    Settings.setScaleType("")
                                    BLEManager.clearSavedScale()
                                }
                            }
                        }

                        // Show weight when connected
                        RowLayout {
                            Layout.fillWidth: true
                            visible: ScaleDevice && ScaleDevice.connected

                            Text {
                                text: "Weight:"
                                color: Theme.textSecondaryColor
                            }

                            Text {
                                text: MachineState.scaleWeight.toFixed(1) + " g"
                                color: Theme.textColor
                                font: Theme.bodyFont
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                text: "Tare"
                                onClicked: {
                                    if (ScaleDevice) ScaleDevice.tare()
                                }
                            }
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            clip: true
                            visible: !ScaleDevice || !ScaleDevice.connected
                            model: BLEManager.discoveredScales

                            delegate: ItemDelegate {
                                width: ListView.view.width
                                contentItem: RowLayout {
                                    Text {
                                        text: modelData.name
                                        color: Theme.textColor
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: modelData.type
                                        color: Theme.textSecondaryColor
                                        font.pixelSize: 12
                                    }
                                }
                                background: Rectangle {
                                    color: parent.hovered ? Theme.accentColor : "transparent"
                                    radius: 4
                                }
                                onClicked: {
                                    console.log("Connect to scale:", modelData.name, modelData.type)
                                }
                            }

                            Label {
                                anchors.centerIn: parent
                                text: "No scales found"
                                visible: parent.count === 0
                                color: Theme.textSecondaryColor
                            }
                        }

                        // Scale scan log
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Qt.darker(Theme.surfaceColor, 1.2)
                            radius: 4

                            ScrollView {
                                id: scaleLogScroll
                                anchors.fill: parent
                                anchors.margins: 8
                                clip: true

                                TextArea {
                                    id: scaleLogText
                                    readOnly: true
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: 11
                                    font.family: "monospace"
                                    wrapMode: Text.Wrap
                                    background: null
                                    text: ""
                                }
                            }

                            Connections {
                                target: BLEManager
                                function onScaleLogMessage(message) {
                                    scaleLogText.text += message + "\n"
                                    scaleLogScroll.ScrollBar.vertical.position = 1.0 - scaleLogScroll.ScrollBar.vertical.size
                                }
                            }
                        }
                    }
                }
            }
        }

        // ============ PREFERENCES TAB ============
        Item {
            id: preferencesTab
            // Local property to track auto-sleep value
            property int autoSleepMinutes: Settings.value("autoSleepMinutes", 0)

            RowLayout {
                anchors.fill: parent
                spacing: 15

                // Left column: Auto-sleep and About stacked
                ColumnLayout {
                    Layout.preferredWidth: 300
                    Layout.fillHeight: true
                    spacing: 15

                    // Auto-sleep settings
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 160
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 15
                            spacing: 10

                            Text {
                                text: "Auto-Sleep"
                                color: Theme.textColor
                                font.pixelSize: 16
                                font.bold: true
                            }

                            Text {
                                text: "Put the machine to sleep after inactivity"
                                color: Theme.textSecondaryColor
                                font.pixelSize: 12
                            }

                            Item { Layout.fillHeight: true }

                            ValueInput {
                                id: sleepInput
                                Layout.preferredWidth: Theme.scaled(150)
                                from: 0
                                to: 240
                                stepSize: 5
                                decimals: 0
                                value: preferencesTab.autoSleepMinutes
                                displayText: value === 0 ? "Never" : (value + " min")

                                onValueModified: function(newValue) {
                                    preferencesTab.autoSleepMinutes = newValue
                                    Settings.setValue("autoSleepMinutes", newValue)
                                }
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }

                    // About box
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 15
                            spacing: 8

                            Text {
                                text: "About"
                                color: Theme.textColor
                                font.pixelSize: 16
                                font.bold: true
                            }

                            Item { Layout.fillHeight: true }

                            Text {
                                text: "Decenza DE1"
                                color: Theme.textColor
                                font.pixelSize: 14
                            }

                            Text {
                                text: "Version 1.0.0"
                                color: DE1Device.simulationMode ? Theme.primaryColor : Theme.textSecondaryColor
                                font.pixelSize: 12
                            }

                            Text {
                                text: "Build #" + BuildNumber
                                color: Theme.accentColor
                                font.pixelSize: 18
                                font.bold: true
                            }

                            Text {
                                text: DE1Device.simulationMode ? "SIMULATION MODE" : "Built with Qt 6"
                                color: DE1Device.simulationMode ? Theme.primaryColor : Theme.textSecondaryColor
                                font.pixelSize: 12
                                font.bold: DE1Device.simulationMode
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }
                }

                // Battery / Charging settings
                Rectangle {
                    Layout.preferredWidth: 300
                    Layout.fillHeight: true
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 10

                        Text {
                            text: "Battery Charging"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            text: "Control the USB charger output from the DE1"
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Item { height: 5 }

                        // Battery status
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Text {
                                text: "Battery:"
                                color: Theme.textSecondaryColor
                            }

                            Text {
                                text: BatteryManager.batteryPercent + "%"
                                color: BatteryManager.batteryPercent < 20 ? Theme.errorColor :
                                       BatteryManager.batteryPercent < 50 ? Theme.warningColor :
                                       Theme.successColor
                                font.bold: true
                            }

                            Rectangle {
                                width: 30
                                height: 14
                                radius: 2
                                color: "transparent"
                                border.color: Theme.textSecondaryColor
                                border.width: 1

                                Rectangle {
                                    x: 2
                                    y: 2
                                    width: (parent.width - 4) * BatteryManager.batteryPercent / 100
                                    height: parent.height - 4
                                    radius: 1
                                    color: BatteryManager.batteryPercent < 20 ? Theme.errorColor :
                                           BatteryManager.batteryPercent < 50 ? Theme.warningColor :
                                           Theme.successColor
                                }

                                // Battery terminal
                                Rectangle {
                                    x: parent.width
                                    y: 4
                                    width: 3
                                    height: 6
                                    radius: 1
                                    color: Theme.textSecondaryColor
                                }
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: BatteryManager.isCharging ? "Charging" : "Not charging"
                                color: BatteryManager.isCharging ? Theme.successColor : Theme.textSecondaryColor
                                font.pixelSize: 12
                            }
                        }

                        Item { height: 10 }

                        // Smart charging mode selector
                        Text {
                            text: "Smart Charging Mode"
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: [
                                    { value: 0, label: "Off", desc: "Always charging" },
                                    { value: 1, label: "On", desc: "55-65%" },
                                    { value: 2, label: "Night", desc: "90-95%" }
                                ]

                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    height: 50
                                    radius: 6
                                    color: BatteryManager.chargingMode === modelData.value ?
                                           Theme.primaryColor : Theme.backgroundColor
                                    border.color: BatteryManager.chargingMode === modelData.value ?
                                                  Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1

                                    ColumnLayout {
                                        anchors.centerIn: parent
                                        spacing: 2

                                        Text {
                                            text: modelData.label
                                            color: BatteryManager.chargingMode === modelData.value ?
                                                   "white" : Theme.textColor
                                            font.pixelSize: 14
                                            font.bold: true
                                            Layout.alignment: Qt.AlignHCenter
                                        }

                                        Text {
                                            text: modelData.desc
                                            color: BatteryManager.chargingMode === modelData.value ?
                                                   Qt.rgba(1, 1, 1, 0.7) : Theme.textSecondaryColor
                                            font.pixelSize: 10
                                            Layout.alignment: Qt.AlignHCenter
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: BatteryManager.chargingMode = modelData.value
                                    }
                                }
                            }
                        }

                        Item { height: 5 }

                        // Explanation text
                        Text {
                            text: BatteryManager.chargingMode === 0 ?
                                  "Charger is always on. Battery stays at 100%." :
                                  BatteryManager.chargingMode === 1 ?
                                  "Cycles between 55-65% to extend battery lifespan." :
                                  "Keeps battery at 90-95% when active. Allows deeper discharge when sleeping."
                            color: Theme.textSecondaryColor
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Item { Layout.fillHeight: true }

                        // Manual charger toggle
                        RowLayout {
                            Layout.fillWidth: true
                            visible: BatteryManager.chargingMode === 0

                            Text {
                                text: "USB Charger"
                                color: Theme.textColor
                                font.pixelSize: 14
                            }

                            Item { Layout.fillWidth: true }

                            Switch {
                                checked: DE1Device.usbChargerOn
                                onClicked: DE1Device.setUsbChargerOn(checked)
                            }
                        }

                        // Battery drain button for testing
                        Button {
                            Layout.fillWidth: true
                            text: BatteryDrainer.running ? "DRAINING... (tap to stop)" : "Drain Battery (Test)"
                            background: Rectangle {
                                radius: 6
                                color: BatteryDrainer.running ? Theme.errorColor : Theme.backgroundColor
                                border.color: Theme.errorColor
                                border.width: 1
                            }
                            contentItem: Text {
                                text: parent.text
                                color: BatteryDrainer.running ? "white" : Theme.errorColor
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: BatteryDrainer.toggle()
                        }
                    }
                }

                // Spacer
                Item { Layout.fillWidth: true }
            }
        }

        // ============ SCREENSAVER TAB ============
        Item {
            RowLayout {
                anchors.fill: parent
                spacing: 15

                // Category selector
                Rectangle {
                    Layout.preferredWidth: 250
                    Layout.fillHeight: true
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 10

                        Text {
                            text: "Video Category"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            text: "Choose a theme for screensaver videos"
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Item { height: 10 }

                        // Category list
                        ListView {
                            id: categoryList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: ScreensaverManager.categories
                            spacing: 2
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: ItemDelegate {
                                width: ListView.view.width
                                height: 36
                                highlighted: modelData.id === ScreensaverManager.selectedCategoryId

                                background: Rectangle {
                                    color: parent.highlighted ? Theme.primaryColor :
                                           parent.hovered ? Qt.darker(Theme.backgroundColor, 1.2) : Theme.backgroundColor
                                    radius: 6
                                }

                                contentItem: Text {
                                    text: modelData.name
                                    color: parent.highlighted ? "white" : Theme.textColor
                                    font.pixelSize: 14
                                    font.bold: parent.highlighted
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: 10
                                }

                                onClicked: {
                                    ScreensaverManager.selectedCategoryId = modelData.id
                                }
                            }

                            Label {
                                anchors.centerIn: parent
                                text: ScreensaverManager.isFetchingCategories ? "Loading..." : "No categories"
                                visible: parent.count === 0
                                color: Theme.textSecondaryColor
                            }
                        }

                        Button {
                            text: "Refresh Categories"
                            Layout.fillWidth: true
                            enabled: !ScreensaverManager.isFetchingCategories
                            onClicked: ScreensaverManager.refreshCategories()
                        }
                    }
                }

                // Screensaver settings
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 15

                        Text {
                            text: "Screensaver Settings"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        // Status row
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 20

                            ColumnLayout {
                                spacing: 4

                                Text {
                                    text: "Current Category"
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: 12
                                }

                                Text {
                                    text: ScreensaverManager.selectedCategoryName
                                    color: Theme.primaryColor
                                    font.pixelSize: 16
                                    font.bold: true
                                }
                            }

                            ColumnLayout {
                                spacing: 4

                                Text {
                                    text: "Videos"
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: 12
                                }

                                Text {
                                    text: ScreensaverManager.itemCount + (ScreensaverManager.isDownloading ? " (downloading...)" : "")
                                    color: Theme.textColor
                                    font.pixelSize: 16
                                }
                            }

                            ColumnLayout {
                                spacing: 4

                                Text {
                                    text: "Cache Usage"
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: 12
                                }

                                Text {
                                    text: (ScreensaverManager.cacheUsedBytes / 1024 / 1024).toFixed(0) + " MB / " +
                                          (ScreensaverManager.maxCacheBytes / 1024 / 1024 / 1024).toFixed(1) + " GB"
                                    color: Theme.textColor
                                    font.pixelSize: 16
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        // Download progress
                        Rectangle {
                            Layout.fillWidth: true
                            height: 6
                            radius: 3
                            color: Qt.darker(Theme.surfaceColor, 1.3)
                            visible: ScreensaverManager.isDownloading

                            Rectangle {
                                width: parent.width * ScreensaverManager.downloadProgress
                                height: parent.height
                                radius: 3
                                color: Theme.primaryColor

                                Behavior on width { NumberAnimation { duration: 200 } }
                            }
                        }

                        // Toggles row
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 30

                            RowLayout {
                                spacing: 10

                                Text {
                                    text: "Enabled"
                                    color: Theme.textColor
                                    font.pixelSize: 14
                                }

                                Switch {
                                    checked: ScreensaverManager.enabled
                                    onCheckedChanged: ScreensaverManager.enabled = checked
                                }
                            }

                            RowLayout {
                                spacing: 10

                                Text {
                                    text: "Cache Videos"
                                    color: Theme.textColor
                                    font.pixelSize: 14
                                }

                                Switch {
                                    checked: ScreensaverManager.cacheEnabled
                                    onCheckedChanged: ScreensaverManager.cacheEnabled = checked
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Item { Layout.fillHeight: true }

                        // Action buttons
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Button {
                                text: "Refresh Videos"
                                onClicked: ScreensaverManager.refreshCatalog()
                                enabled: !ScreensaverManager.isRefreshing
                            }

                            Button {
                                text: "Clear Cache"
                                onClicked: ScreensaverManager.clearCache()
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }
        }

        // ============ VISUALIZER TAB ============
        Item {
            id: visualizerTab

            // Connection test result message
            property string testResultMessage: ""
            property bool testResultSuccess: false

            RowLayout {
                anchors.fill: parent
                spacing: 15

                // Account settings
                Rectangle {
                    Layout.preferredWidth: 350
                    Layout.fillHeight: true
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 12

                        Text {
                            text: "Visualizer.coffee Account"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            text: "Upload your shots to visualizer.coffee for tracking and analysis"
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Item { height: 5 }

                        // Username
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: "Username / Email"
                                color: Theme.textSecondaryColor
                                font.pixelSize: 12
                            }

                            TextField {
                                id: usernameField
                                Layout.fillWidth: true
                                text: Settings.visualizerUsername
                                placeholderText: "your@email.com"
                                font: Theme.bodyFont
                                color: Theme.textColor
                                placeholderTextColor: Theme.textSecondaryColor
                                background: Rectangle {
                                    color: Theme.backgroundColor
                                    radius: 4
                                    border.color: usernameField.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                }
                                onTextChanged: Settings.visualizerUsername = text
                            }
                        }

                        // Password
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: "Password"
                                color: Theme.textSecondaryColor
                                font.pixelSize: 12
                            }

                            TextField {
                                id: passwordField
                                Layout.fillWidth: true
                                text: Settings.visualizerPassword
                                placeholderText: "••••••••"
                                echoMode: TextInput.Password
                                font: Theme.bodyFont
                                color: Theme.textColor
                                placeholderTextColor: Theme.textSecondaryColor
                                background: Rectangle {
                                    color: Theme.backgroundColor
                                    radius: 4
                                    border.color: passwordField.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                }
                                onTextChanged: Settings.visualizerPassword = text
                            }
                        }

                        Item { height: 5 }

                        // Test connection button
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Button {
                                text: "Test Connection"
                                enabled: usernameField.text.length > 0 && passwordField.text.length > 0
                                onClicked: {
                                    visualizerTab.testResultMessage = "Testing..."
                                    MainController.visualizer.testConnection()
                                }
                                background: Rectangle {
                                    implicitWidth: 140
                                    implicitHeight: 40
                                    radius: 6
                                    color: parent.enabled ? Theme.primaryColor : Theme.backgroundColor
                                    border.color: parent.enabled ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: parent.enabled ? "white" : Theme.textSecondaryColor
                                    font: Theme.bodyFont
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            Text {
                                text: visualizerTab.testResultMessage
                                color: visualizerTab.testResultSuccess ? Theme.successColor : Theme.errorColor
                                font.pixelSize: 12
                                visible: visualizerTab.testResultMessage.length > 0
                            }
                        }

                        Connections {
                            target: MainController.visualizer
                            function onConnectionTestResult(success, message) {
                                visualizerTab.testResultSuccess = success
                                visualizerTab.testResultMessage = message
                            }
                        }

                        Item { Layout.fillHeight: true }

                        // Sign up link
                        Text {
                            text: "Don't have an account? Sign up at visualizer.coffee"
                            color: Theme.primaryColor
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Qt.openUrlExternally("https://visualizer.coffee/users/sign_up")
                            }
                        }
                    }
                }

                // Upload settings
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 12

                        Text {
                            text: "Upload Settings"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        // Auto-upload toggle
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15

                            ColumnLayout {
                                spacing: 2

                                Text {
                                    text: "Auto-Upload Shots"
                                    color: Theme.textColor
                                    font.pixelSize: 14
                                }

                                Text {
                                    text: "Automatically upload espresso shots after completion"
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: 12
                                }
                            }

                            Item { Layout.fillWidth: true }

                            Switch {
                                checked: Settings.visualizerAutoUpload
                                onCheckedChanged: Settings.visualizerAutoUpload = checked
                            }
                        }

                        // Minimum duration
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15

                            ColumnLayout {
                                spacing: 2

                                Text {
                                    text: "Minimum Duration"
                                    color: Theme.textColor
                                    font.pixelSize: 14
                                }

                                Text {
                                    text: "Only upload shots longer than this (skip aborted shots)"
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: 12
                                }
                            }

                            Item { Layout.fillWidth: true }

                            ValueInput {
                                id: minDurationInput
                                value: Settings.visualizerMinDuration
                                from: 0
                                to: 30
                                stepSize: 1
                                suffix: " sec"
                                Layout.preferredWidth: 120

                                onValueModified: function(newValue) {
                                    Settings.visualizerMinDuration = newValue
                                }
                            }
                        }

                        Item { height: 10 }

                        // Last upload status
                        Rectangle {
                            Layout.fillWidth: true
                            height: statusColumn.implicitHeight + 20
                            color: Qt.darker(Theme.surfaceColor, 1.2)
                            radius: 8

                            ColumnLayout {
                                id: statusColumn
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6

                                Text {
                                    text: "Last Upload"
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: 12
                                }

                                Text {
                                    text: MainController.visualizer.lastUploadStatus || "No uploads yet"
                                    color: MainController.visualizer.lastUploadStatus.indexOf("Failed") >= 0 ?
                                           Theme.errorColor :
                                           MainController.visualizer.lastUploadStatus.indexOf("successful") >= 0 ?
                                           Theme.successColor : Theme.textColor
                                    font.pixelSize: 14
                                }

                                Text {
                                    text: MainController.visualizer.lastShotUrl
                                    color: Theme.primaryColor
                                    font.pixelSize: 12
                                    visible: MainController.visualizer.lastShotUrl.length > 0

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: Qt.openUrlExternally(MainController.visualizer.lastShotUrl)
                                    }
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }

        // ============ RESOLUTION TAB (Windows only) ============
        Item {
            id: resolutionTab
            visible: Qt.platform.os === "windows"

            // Resolution presets
            property var resolutions: [
                { name: "Phone - iPhone 15 Pro Max", width: 430, height: 932, category: "phone" },
                { name: "Phone - iPhone 15", width: 393, height: 852, category: "phone" },
                { name: "Phone - Pixel 8", width: 411, height: 915, category: "phone" },
                { name: "Phone - Galaxy S24", width: 360, height: 780, category: "phone" },
                { name: "Tablet 7\" - Portrait", width: 600, height: 1024, category: "tablet" },
                { name: "Tablet 7\" - Landscape", width: 1024, height: 600, category: "tablet" },
                { name: "Tablet 10\" - Portrait", width: 800, height: 1280, category: "tablet" },
                { name: "Tablet 10\" - Landscape", width: 1280, height: 800, category: "tablet" },
                { name: "iPad 10.2\" - Portrait", width: 810, height: 1080, category: "tablet" },
                { name: "iPad 10.2\" - Landscape", width: 1080, height: 810, category: "tablet" },
                { name: "iPad Pro 11\" - Portrait", width: 834, height: 1194, category: "tablet" },
                { name: "iPad Pro 11\" - Landscape", width: 1194, height: 834, category: "tablet" },
                { name: "iPad Pro 12.9\" - Portrait", width: 1024, height: 1366, category: "tablet" },
                { name: "iPad Pro 12.9\" - Landscape", width: 1366, height: 1024, category: "tablet" },
                { name: "Desktop HD", width: 1280, height: 720, category: "desktop" },
                { name: "Desktop Full HD", width: 1920, height: 1080, category: "desktop" }
            ]

            ColumnLayout {
                anchors.fill: parent
                spacing: 15

                // Current resolution display
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 80
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 20

                        Column {
                            spacing: 4

                            Text {
                                text: "Current Window Size"
                                color: Theme.textSecondaryColor
                                font.pixelSize: 12
                            }

                            Text {
                                text: Window.window ? (Window.window.width + " × " + Window.window.height) : "Unknown"
                                color: Theme.primaryColor
                                font.pixelSize: 24
                                font.bold: true
                            }
                        }

                        Column {
                            spacing: 4

                            Text {
                                text: "Scale Factor"
                                color: Theme.textSecondaryColor
                                font.pixelSize: 12
                            }

                            Text {
                                text: Theme.scale.toFixed(2) + "x"
                                color: Theme.textColor
                                font.pixelSize: 24
                                font.bold: true
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                // Resolution grid
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 10

                        Text {
                            text: "Select Resolution"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            text: "Click to resize window. Use for testing UI scaling."
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                        }

                        GridView {
                            id: resolutionGrid
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            cellWidth: 200
                            cellHeight: 70
                            clip: true
                            model: resolutionTab.resolutions

                            delegate: Rectangle {
                                width: resolutionGrid.cellWidth - 8
                                height: resolutionGrid.cellHeight - 8
                                radius: 8
                                color: mouseArea.containsMouse ? Qt.lighter(Theme.backgroundColor, 1.2) : Theme.backgroundColor
                                border.color: Theme.textSecondaryColor
                                border.width: 1

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 4

                                    Text {
                                        text: modelData.name
                                        color: Theme.textColor
                                        font.pixelSize: 12
                                        font.bold: true
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: modelData.width + " × " + modelData.height
                                        color: modelData.category === "phone" ? Theme.primaryColor :
                                               modelData.category === "tablet" ? Theme.successColor :
                                               Theme.warningColor
                                        font.pixelSize: 16
                                        font.bold: true
                                    }
                                }

                                MouseArea {
                                    id: mouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        if (Window.window) {
                                            Window.window.width = modelData.width
                                            Window.window.height = modelData.height
                                        }
                                    }
                                }
                            }
                        }
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
