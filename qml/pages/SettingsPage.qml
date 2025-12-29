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
        anchors.leftMargin: Theme.standardMargin
        z: 2  // Above content frame

        // Skip global tap handler - we announce via onCurrentIndexChanged
        property bool accessibilityCustomHandler: true

        // Announce tab when changed (accessibility)
        onCurrentIndexChanged: {
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                var tabNames = IsDebugBuild ? ["Bluetooth", "Preferences", "Screensaver", "Visualizer", "Accessibility", "Themes", "Debug"] : ["Bluetooth", "Preferences", "Screensaver", "Visualizer", "Accessibility", "Themes"]
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
            Accessible.name: "Bluetooth tab" + (tabBar.currentIndex === 0 ? ", selected" : "")
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
            Accessible.name: "Preferences tab" + (tabBar.currentIndex === 1 ? ", selected" : "")
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
            Accessible.name: "Screensaver tab" + (tabBar.currentIndex === 2 ? ", selected" : "")
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
            Accessible.name: "Visualizer tab" + (tabBar.currentIndex === 3 ? ", selected" : "")
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
            id: accessibilityTabButton
            text: "Access"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 4
            Accessible.name: "Accessibility tab" + (tabBar.currentIndex === 4 ? ", selected" : "")
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
                accessibleName: "Accessibility tab" + (tabBar.currentIndex === 4 ? ", selected" : "")
                accessibleItem: accessibilityTabButton
                onAccessibleClicked: tabBar.currentIndex = 4
            }
        }

        TabButton {
            id: themesTabButton
            text: "Themes"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 5
            Accessible.name: "Themes tab" + (tabBar.currentIndex === 5 ? ", selected" : "")
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
                accessibleName: "Themes tab" + (tabBar.currentIndex === 5 ? ", selected" : "")
                accessibleItem: themesTabButton
                onAccessibleClicked: tabBar.currentIndex = 5
            }
        }

        TabButton {
            id: debugTabButton
            visible: typeof IsDebugBuild !== "undefined" && IsDebugBuild
            text: "Debug"
            width: implicitWidth
            font.pixelSize: 14
            font.bold: tabBar.currentIndex === 6
            Accessible.name: "Debug tab" + (tabBar.currentIndex === 6 ? ", selected" : "")
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
                accessibleName: "Debug tab" + (tabBar.currentIndex === 6 ? ", selected" : "")
                accessibleItem: debugTabButton
                onAccessibleClicked: tabBar.currentIndex = 6
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

                            AccessibleButton {
                                text: BLEManager.scanning ? "Stop Scan" : "Scan for DE1"
                                accessibleName: BLEManager.scanning ? "Stop scanning for DE1" : "Scan for DE1 machine"
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
                                text: {
                                    if (ScaleDevice && ScaleDevice.connected) {
                                        return ScaleDevice.name === "Flow Scale" ? "Simulated" : "Connected"
                                    }
                                    return BLEManager.scaleConnectionFailed ? "Not found" : "Disconnected"
                                }
                                color: {
                                    if (ScaleDevice && ScaleDevice.connected) {
                                        return ScaleDevice.name === "Flow Scale" ? Theme.warningColor : Theme.successColor
                                    }
                                    return BLEManager.scaleConnectionFailed ? Theme.errorColor : Theme.textSecondaryColor
                                }
                            }

                            Item { Layout.fillWidth: true }

                            AccessibleButton {
                                text: BLEManager.scanning ? "Scanning..." : "Scan for Scales"
                                accessibleName: BLEManager.scanning ? "Scanning for scales" : "Scan for Bluetooth scales"
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

                            AccessibleButton {
                                text: "Forget"
                                accessibleName: "Forget saved scale"
                                onClicked: {
                                    Settings.scaleAddress = ""
                                    Settings.scaleType = ""
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

                            AccessibleButton {
                                text: "Tare"
                                accessibleName: "Tare scale to zero"
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
            property int autoSleepMinutes: Settings.value("autoSleepMinutes", 60)

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
                                    id: chargingModeButton
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

                                    AccessibleMouseArea {
                                        anchors.fill: parent
                                        accessibleName: modelData.label + " charging mode. " + modelData.desc +
                                                       (BatteryManager.chargingMode === modelData.value ? ", selected" : "")
                                        accessibleItem: chargingModeButton
                                        onAccessibleClicked: BatteryManager.chargingMode = modelData.value
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
                        AccessibleButton {
                            Layout.fillWidth: true
                            text: BatteryDrainer.running ? "DRAINING... (tap to stop)" : "Drain Battery (Test)"
                            accessibleName: BatteryDrainer.running ? "Stop battery drain test" : "Start battery drain test"
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

                // Flow Sensor Calibration
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
                            text: "Flow Sensor Calibration"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            text: "Calibrate the virtual scale for users without a BLE scale"
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Item { height: 5 }

                        // Current factor display
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Text {
                                text: "Current factor:"
                                color: Theme.textSecondaryColor
                            }

                            Text {
                                text: Settings.flowCalibrationFactor.toFixed(3)
                                color: Theme.primaryColor
                                font.bold: true
                            }
                        }

                        Item { Layout.fillHeight: true }

                        // Calibration button
                        AccessibleButton {
                            Layout.fillWidth: true
                            text: "Start Calibration"
                            accessibleName: "Start flow sensor calibration"
                            enabled: DE1Device.connected
                            onClicked: flowCalibrationDialog.open()
                            background: Rectangle {
                                radius: 6
                                color: parent.enabled ? Theme.primaryColor : Theme.backgroundColor
                                border.color: parent.enabled ? Theme.primaryColor : Theme.textSecondaryColor
                                border.width: 1
                            }
                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled ? "white" : Theme.textSecondaryColor
                                font.pixelSize: 14
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Text {
                            text: "Requires a separate scale to measure water weight"
                            color: Theme.textSecondaryColor
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
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

                        AccessibleButton {
                            text: "Refresh Categories"
                            accessibleName: "Refresh screensaver categories"
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

                            AccessibleButton {
                                text: "Refresh Videos"
                                accessibleName: "Refresh screensaver videos"
                                onClicked: ScreensaverManager.refreshCatalog()
                                enabled: !ScreensaverManager.isRefreshing
                            }

                            AccessibleButton {
                                text: "Clear Cache"
                                accessibleName: "Clear video cache"
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

            // Keyboard offset for shifting content up
            property real keyboardOffset: 0

            // Delay timer to prevent jumpy behavior when switching fields
            Timer {
                id: keyboardResetTimer
                interval: 100
                onTriggered: {
                    if (!Qt.inputMethod.visible) {
                        visualizerTab.keyboardOffset = 0
                    }
                }
            }

            // Function to update keyboard offset for a given field
            function updateKeyboardOffset(focusedField) {
                if (!focusedField) return

                // Qt doesn't report keyboard height on Android, so calculate based on field position
                // Keyboard typically covers bottom ~50% of screen, shift fields to top 30%
                var fieldPos = focusedField.mapToItem(visualizerTab, 0, 0)
                var fieldBottom = fieldPos.y + focusedField.height
                var safeZone = visualizerTab.height * 0.30

                keyboardOffset = Math.max(0, fieldBottom - safeZone)
            }

            // Track keyboard visibility and update offset
            Connections {
                target: Qt.inputMethod
                function onVisibleChanged() {
                    if (Qt.inputMethod.visible) {
                        keyboardResetTimer.stop()
                        var focusedField = usernameField.activeFocus ? usernameField :
                                          (passwordField.activeFocus ? passwordField : null)
                        visualizerTab.updateKeyboardOffset(focusedField)
                    } else {
                        keyboardResetTimer.restart()
                    }
                }
            }

            RowLayout {
                width: parent.width
                height: parent.height
                y: -visualizerTab.keyboardOffset
                spacing: 15

                Behavior on y {
                    NumberAnimation { duration: 250; easing.type: Easing.OutQuad }
                }

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
                                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                                background: Rectangle {
                                    color: Theme.backgroundColor
                                    radius: 4
                                    border.color: usernameField.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                }
                                onTextChanged: Settings.visualizerUsername = text
                                onAccepted: passwordField.forceActiveFocus()
                                Keys.onReturnPressed: passwordField.forceActiveFocus()
                                Keys.onEnterPressed: passwordField.forceActiveFocus()
                                onActiveFocusChanged: if (activeFocus && Qt.inputMethod.visible) visualizerTab.updateKeyboardOffset(usernameField)
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
                                inputMethodHints: Qt.ImhNoAutoUppercase
                                background: Rectangle {
                                    color: Theme.backgroundColor
                                    radius: 4
                                    border.color: passwordField.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                }
                                onTextChanged: Settings.visualizerPassword = text
                                onAccepted: {
                                    passwordField.focus = false
                                    Qt.inputMethod.hide()
                                }
                                Keys.onReturnPressed: { passwordField.focus = false; Qt.inputMethod.hide() }
                                Keys.onEnterPressed: { passwordField.focus = false; Qt.inputMethod.hide() }
                                onActiveFocusChanged: if (activeFocus && Qt.inputMethod.visible) visualizerTab.updateKeyboardOffset(passwordField)
                            }
                        }

                        Item { height: 5 }

                        // Test connection button
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            AccessibleButton {
                                text: "Test Connection"
                                accessibleName: "Test Visualizer connection"
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
                            id: signUpLink
                            text: "Don't have an account? Sign up at visualizer.coffee"
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap

                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: "Sign up at visualizer.coffee. Opens web browser"
                                accessibleItem: signUpLink
                                onAccessibleClicked: Qt.openUrlExternally("https://visualizer.coffee/users/sign_up")
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
                                    id: lastShotLink
                                    text: MainController.visualizer.lastShotUrl
                                    color: Theme.primaryColor
                                    font.pixelSize: 12
                                    visible: MainController.visualizer.lastShotUrl.length > 0

                                    AccessibleMouseArea {
                                        anchors.fill: parent
                                        accessibleName: "View last shot on visualizer. Opens web browser"
                                        accessibleItem: lastShotLink
                                        onAccessibleClicked: Qt.openUrlExternally(MainController.visualizer.lastShotUrl)
                                    }
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }

        // ============ ACCESSIBILITY TAB ============
        Item {
            id: accessibilityTab

            ColumnLayout {
                anchors.fill: parent
                spacing: 15

                // Main accessibility settings card
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 380
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 12

                        Text {
                            text: "Accessibility"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            text: "Screen reader support and audio feedback for blind and visually impaired users"
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        // Tip about backdoor activation
                        Rectangle {
                            Layout.fillWidth: true
                            height: tipText.implicitHeight + 16
                            radius: 6
                            color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                            border.color: Theme.primaryColor
                            border.width: 1

                            Text {
                                id: tipText
                                anchors.fill: parent
                                anchors.margins: 8
                                text: "Tip: 4-finger tap anywhere to toggle accessibility on/off"
                                color: Theme.primaryColor
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Item { height: 5 }

                        // Enable toggle
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15

                            Text {
                                text: "Enable Accessibility"
                                color: Theme.textColor
                                font.pixelSize: 14
                            }

                            Item { Layout.fillWidth: true }

                            Switch {
                                checked: AccessibilityManager.enabled
                                onCheckedChanged: AccessibilityManager.enabled = checked
                            }
                        }

                        // TTS toggle
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15
                            opacity: AccessibilityManager.enabled ? 1.0 : 0.5

                            Text {
                                text: "Voice Announcements"
                                color: Theme.textColor
                                font.pixelSize: 14
                            }

                            Item { Layout.fillWidth: true }

                            Switch {
                                checked: AccessibilityManager.ttsEnabled
                                enabled: AccessibilityManager.enabled
                                onCheckedChanged: {
                                    if (AccessibilityManager.enabled) {
                                        if (checked) {
                                            // Enable first, then announce
                                            AccessibilityManager.ttsEnabled = true
                                            AccessibilityManager.announce("Voice announcements enabled", true)
                                        } else {
                                            // Announce first, then disable
                                            AccessibilityManager.announce("Voice announcements disabled", true)
                                            AccessibilityManager.ttsEnabled = false
                                        }
                                    } else {
                                        AccessibilityManager.ttsEnabled = checked
                                    }
                                }
                            }
                        }

                        // Tick sound toggle
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15
                            opacity: AccessibilityManager.enabled ? 1.0 : 0.5

                            ColumnLayout {
                                spacing: 2
                                Text {
                                    text: "Frame Tick Sound"
                                    color: Theme.textColor
                                    font.pixelSize: 14
                                }
                                Text {
                                    text: "Play a tick when extraction frames change"
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: 11
                                }
                            }

                            Item { Layout.fillWidth: true }

                            Switch {
                                checked: AccessibilityManager.tickEnabled
                                enabled: AccessibilityManager.enabled
                                onCheckedChanged: {
                                    AccessibilityManager.tickEnabled = checked
                                    if (AccessibilityManager.enabled) {
                                        AccessibilityManager.announce(checked ? "Frame tick sound enabled" : "Frame tick sound disabled", true)
                                    }
                                }
                            }
                        }

                        // Tick sound picker and volume
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15
                            opacity: (AccessibilityManager.enabled && AccessibilityManager.tickEnabled) ? 1.0 : 0.5

                            Text {
                                text: "Tick Sound"
                                color: Theme.textColor
                                font.pixelSize: 14
                            }

                            Item { Layout.fillWidth: true }

                            ValueInput {
                                value: AccessibilityManager.tickSoundIndex
                                from: 1
                                to: 4
                                stepSize: 1
                                suffix: ""
                                displayText: "Sound " + value
                                accessibleName: "Select tick sound, 1 to 4. Current: " + value
                                enabled: AccessibilityManager.enabled && AccessibilityManager.tickEnabled
                                onValueModified: function(newValue) {
                                    AccessibilityManager.tickSoundIndex = newValue
                                }
                            }

                            ValueInput {
                                value: AccessibilityManager.tickVolume
                                from: 10
                                to: 100
                                stepSize: 10
                                suffix: "%"
                                accessibleName: "Tick volume. Current: " + value + " percent"
                                enabled: AccessibilityManager.enabled && AccessibilityManager.tickEnabled
                                onValueModified: function(newValue) {
                                    AccessibilityManager.tickVolume = newValue
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // Spacer
                Item { Layout.fillHeight: true }
            }
        }

        // ============ THEMES TAB ============
        Item {
            id: themesTab

            // Currently selected color for editing
            property string selectedColorName: "primaryColor"
            property color selectedColorValue: Theme.primaryColor

            // Color definitions with display names and categories
            property var colorDefinitions: [
                { category: "Core UI", colors: [
                    { name: "backgroundColor", display: "Background" },
                    { name: "surfaceColor", display: "Surface" },
                    { name: "primaryColor", display: "Primary" },
                    { name: "secondaryColor", display: "Secondary" },
                    { name: "textColor", display: "Text" },
                    { name: "textSecondaryColor", display: "Text Secondary" },
                    { name: "accentColor", display: "Accent" },
                    { name: "borderColor", display: "Border" }
                ]},
                { category: "Status", colors: [
                    { name: "successColor", display: "Success" },
                    { name: "warningColor", display: "Warning" },
                    { name: "errorColor", display: "Error" }
                ]},
                { category: "Chart", colors: [
                    { name: "pressureColor", display: "Pressure" },
                    { name: "pressureGoalColor", display: "Pressure Goal" },
                    { name: "flowColor", display: "Flow" },
                    { name: "flowGoalColor", display: "Flow Goal" },
                    { name: "temperatureColor", display: "Temperature" },
                    { name: "temperatureGoalColor", display: "Temp Goal" },
                    { name: "weightColor", display: "Weight" }
                ]}
            ]

            function getColorValue(colorName) {
                return Theme[colorName] || "#ffffff"
            }

            function selectColor(colorName) {
                selectedColorName = colorName
                selectedColorValue = getColorValue(colorName)
                colorEditor.setColor(selectedColorValue)
            }

            function applyColorChange(newColor) {
                Settings.setThemeColor(selectedColorName, newColor.toString())
                selectedColorValue = newColor
            }

            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingMedium

                // Left panel - Color list
                Rectangle {
                    Layout.preferredWidth: parent.width * 0.4
                    Layout.fillHeight: true
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingMedium
                        spacing: Theme.spacingSmall

                        Text {
                            text: "Theme: " + Settings.activeThemeName
                            color: Theme.textColor
                            font: Theme.subtitleFont
                        }

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            ScrollBar.vertical.policy: ScrollBar.AsNeeded
                            contentWidth: availableWidth
                            clip: true

                            ColumnLayout {
                                width: parent.width
                                spacing: Theme.spacingSmall

                                Repeater {
                                    model: themesTab.colorDefinitions

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4

                                        // Category header
                                        Text {
                                            text: modelData.category
                                            color: Theme.textSecondaryColor
                                            font: Theme.labelFont
                                            topPadding: index > 0 ? Theme.spacingSmall : 0
                                        }

                                        // Color swatches in this category
                                        Repeater {
                                            id: colorRepeater
                                            property var colorList: modelData.colors
                                            model: colorList.length

                                            ColorSwatch {
                                                property var colorData: colorRepeater.colorList[index]
                                                Layout.fillWidth: true
                                                colorName: colorData.name
                                                displayName: colorData.display
                                                colorValue: themesTab.getColorValue(colorData.name)
                                                selected: themesTab.selectedColorName === colorData.name
                                                onClicked: themesTab.selectColor(colorData.name)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Bottom buttons
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            Button {
                                text: "Reset"
                                onClicked: Settings.resetThemeToDefault()
                                background: Rectangle {
                                    color: Theme.errorColor
                                    radius: Theme.buttonRadius
                                    opacity: parent.pressed ? 0.8 : 1.0
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: "white"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }
                }

                // Right panel - Color editor
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingMedium
                        spacing: Theme.spacingSmall

                        Text {
                            text: "Edit: " + themesTab.selectedColorName
                            color: Theme.textColor
                            font: Theme.subtitleFont
                        }

                        ColorEditor {
                            id: colorEditor
                            Layout.fillWidth: true
                            Layout.preferredHeight: 140

                            Component.onCompleted: setColor(themesTab.selectedColorValue)

                            onColorChanged: {
                                themesTab.applyColorChange(colorEditor.color)
                            }
                        }

                            // Preset themes in horizontal scroll
                            ScrollView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 44
                                ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                                ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                                contentHeight: availableHeight
                                clip: true

                                Row {
                                    height: parent.height
                                    spacing: Theme.spacingSmall

                                    Repeater {
                                        id: presetRepeater
                                        model: Settings.getPresetThemes()

                                        Rectangle {
                                            height: 36
                                            width: presetRow.width + (modelData.isBuiltIn ? 0 : deleteBtn.width + 4)
                                            color: modelData.primaryColor
                                            radius: Theme.buttonRadius
                                            border.color: Settings.activeThemeName === modelData.name ? "white" : "transparent"
                                            border.width: 2

                                            Row {
                                                id: presetRow
                                                anchors.left: parent.left
                                                anchors.verticalCenter: parent.verticalCenter
                                                leftPadding: 12
                                                rightPadding: modelData.isBuiltIn ? 12 : 4

                                                Text {
                                                    text: modelData.name
                                                    color: "white"
                                                    font: Theme.labelFont
                                                    anchors.verticalCenter: parent.verticalCenter

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        anchors.margins: -8
                                                        onClicked: Settings.applyPresetTheme(modelData.name)
                                                    }
                                                }
                                            }

                                            // Delete button for user themes
                                            Rectangle {
                                                id: deleteBtn
                                                visible: !modelData.isBuiltIn
                                                width: 24
                                                height: 24
                                                radius: 12
                                                color: deleteArea.pressed ? Qt.darker(parent.color, 1.3) : Qt.darker(parent.color, 1.15)
                                                anchors.right: parent.right
                                                anchors.rightMargin: 6
                                                anchors.verticalCenter: parent.verticalCenter

                                                Text {
                                                    text: "✕"
                                                    color: "white"
                                                    font.pixelSize: 12
                                                    font.bold: true
                                                    anchors.centerIn: parent
                                                }

                                                MouseArea {
                                                    id: deleteArea
                                                    anchors.fill: parent
                                                    onClicked: {
                                                        Settings.deleteUserTheme(modelData.name)
                                                        presetRepeater.model = Settings.getPresetThemes()
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    // Save current theme button
                                    Rectangle {
                                        height: 36
                                        width: saveText.width + 24
                                        color: Theme.surfaceColor
                                        radius: Theme.buttonRadius
                                        border.color: Theme.borderColor
                                        border.width: 1

                                        Text {
                                            id: saveText
                                            text: "+ Save"
                                            color: Theme.textColor
                                            font: Theme.labelFont
                                            anchors.centerIn: parent
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: saveThemeDialog.open()
                                        }
                                    }
                                }
                            }

                            // Random theme button
                            Button {
                                Layout.fillWidth: true
                                text: "Random Theme"
                                onClicked: {
                                    var randomHue = Math.random() * 360
                                    var randomSat = 65 + Math.random() * 20  // 65-85%
                                    var randomLight = 50 + Math.random() * 10  // 50-60%
                                    var palette = Settings.generatePalette(randomHue, randomSat, randomLight)
                                    Settings.customThemeColors = palette
                                    Settings.setActiveThemeName("Custom")
                                }
                                background: Rectangle {
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0.0; color: "#ff6b6b" }
                                        GradientStop { position: 0.25; color: "#ffd93d" }
                                        GradientStop { position: 0.5; color: "#6bcb77" }
                                        GradientStop { position: 0.75; color: "#4d96ff" }
                                        GradientStop { position: 1.0; color: "#9b59b6" }
                                    }
                                    radius: Theme.buttonRadius
                                    opacity: parent.pressed ? 0.8 : 1.0
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: "white"
                                    font.pixelSize: Theme.bodyFont.pixelSize
                                    font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }
            }

        // ============ DEBUG TAB ============
        Item {
            id: debugTab

            ColumnLayout {
                anchors.fill: parent
                spacing: 15

                // Simulation section
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 180
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 12

                        Text {
                            text: "Simulation"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            text: "Enable these options to test the app without hardware connected."
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 20

                            Text {
                                text: "Simulate machine connection"
                                color: Theme.textColor
                                font.pixelSize: 14
                            }

                            Item { Layout.fillWidth: true }

                            Switch {
                                checked: DE1Device.simulationMode
                                onToggled: {
                                    DE1Device.simulationMode = checked
                                    if (ScaleDevice) {
                                        ScaleDevice.simulationMode = checked
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 20

                            Text {
                                text: "Headless mode (no GHC)"
                                color: Theme.textColor
                                font.pixelSize: 14
                            }

                            Item { Layout.fillWidth: true }

                            Switch {
                                checked: DE1Device.isHeadless
                                onToggled: {
                                    DE1Device.isHeadless = checked
                                }
                            }
                        }
                    }
                }

                // Battery Drainer section (Android only)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    visible: Qt.platform.os === "android"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 12

                        Text {
                            text: "Battery Drain Test"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            text: "Drains battery by maxing CPU, GPU, screen brightness and flashlight. Useful for testing smart charging."
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 20

                            Text {
                                text: BatteryDrainer.running ? "Draining... Tap overlay to stop" : "Battery: " + BatteryManager.batteryPercent + "%"
                                color: BatteryDrainer.running ? Theme.warningColor : Theme.textColor
                                font.pixelSize: 14
                            }

                            Item { Layout.fillWidth: true }

                            AccessibleButton {
                                text: BatteryDrainer.running ? "Stop" : "Start Drain"
                                accessibleName: BatteryDrainer.running ? "Stop battery drain" : "Start battery drain test"
                                onClicked: {
                                    if (BatteryDrainer.running) {
                                        BatteryDrainer.stop()
                                    } else {
                                        BatteryDrainer.start()
                                    }
                                }
                            }
                        }
                    }
                }

                // Window Resolution section (Windows/desktop only)
                Rectangle {
                    id: resolutionSection
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    visible: Qt.platform.os === "windows"

                    // Resolution presets (Decent tablet first as default, landscape only)
                    property var resolutions: [
                        { name: "Decent Tablet", width: 1200, height: 800 },
                        { name: "Tablet 7\"", width: 1024, height: 600 },
                        { name: "Tablet 10\"", width: 1280, height: 800 },
                        { name: "iPad 10.2\"", width: 1080, height: 810 },
                        { name: "iPad Pro 11\"", width: 1194, height: 834 },
                        { name: "iPad Pro 12.9\"", width: 1366, height: 1024 },
                        { name: "Desktop HD", width: 1280, height: 720 },
                        { name: "Desktop Full HD", width: 1920, height: 1080 }
                    ]

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 12

                        Text {
                            text: "Window Resolution"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 20

                            Text {
                                text: "Resize window to test UI scaling"
                                color: Theme.textSecondaryColor
                                font.pixelSize: 12
                            }

                            Item { Layout.fillWidth: true }

                            ComboBox {
                                id: resolutionCombo
                                Layout.preferredWidth: 200
                                model: resolutionSection.resolutions
                                textRole: "name"
                                displayText: Window.window ? (Window.window.width + " × " + Window.window.height) : "Select..."

                                delegate: ItemDelegate {
                                    width: resolutionCombo.width
                                    contentItem: Text {
                                        text: modelData.name + " (" + modelData.width + "×" + modelData.height + ")"
                                        color: Theme.textColor
                                        font.pixelSize: 13
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    highlighted: resolutionCombo.highlightedIndex === index
                                    background: Rectangle {
                                        color: highlighted ? Theme.accentColor : Theme.surfaceColor
                                    }
                                }

                                background: Rectangle {
                                    color: Theme.backgroundColor
                                    border.color: Theme.textSecondaryColor
                                    border.width: 1
                                    radius: 4
                                }

                                contentItem: Text {
                                    text: resolutionCombo.displayText
                                    color: Theme.textColor
                                    font.pixelSize: 13
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: 8
                                }

                                onActivated: function(index) {
                                    var res = resolutionSection.resolutions[index]
                                    if (Window.window && res) {
                                        Window.window.width = res.width
                                        Window.window.height = res.height
                                    }
                                }
                            }
                        }
                    }
                }

                // Spacer
                Item { Layout.fillHeight: true }
            }
        }
    }

    // Flow Calibration Dialog
    Dialog {
        id: flowCalibrationDialog
        modal: true
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2 - 30  // Shift up slightly for bottom headroom
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

        property int currentStep: 0  // 0=intro, 1-3=tests, 4=results, 5=verification
        property var testFlows: [3.0, 6.0, 9.0]  // ml/s
        property var testNames: ["Low", "Medium", "High"]
        property var measuredWeights: [0, 0, 0]
        property var flowIntegrals: [0, 0, 0]
        property bool isDispensing: false
        property double currentFlowIntegral: 0
        // Verification step
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
            // Clear calibration flag and restore the user's original profile
            root.calibrationInProgress = false
            MainController.restoreCurrentProfile()
        }

        // Track when calibration shot ends
        Connections {
            target: MachineState
            enabled: flowCalibrationDialog.isDispensing

            function onShotEnded() {
                if (!flowCalibrationDialog.isDispensing) return

                if (flowCalibrationDialog.currentStep === 5) {
                    // Verification complete - save FlowScale's calibrated weight
                    flowCalibrationDialog.verificationFlowScaleWeight = FlowScale.weight
                    flowCalibrationDialog.verificationComplete = true
                    flowCalibrationDialog.isDispensing = false
                    console.log("Verification complete: FlowScale=" + FlowScale.weight.toFixed(1) + "g")
                } else {
                    // Calibration test - save the raw flow integral
                    var testIdx = flowCalibrationDialog.currentStep - 1
                    // Must replace array to trigger QML binding updates
                    var newIntegrals = flowCalibrationDialog.flowIntegrals.slice()
                    newIntegrals[testIdx] = FlowScale.rawFlowIntegral
                    flowCalibrationDialog.flowIntegrals = newIntegrals
                    flowCalibrationDialog.isDispensing = false
                    console.log("Calibration test " + (testIdx + 1) + " complete: raw=" + FlowScale.rawFlowIntegral.toFixed(1) + "g")
                }
            }
        }

        // Update current flow integral display from FlowScale
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
                            text: index === 0 ? "!" : (index === 5 ? "✓" : index)
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
                        text: "This wizard will dispense water at 3 different flow rates.\n\nYou'll need:\n• A separate scale (kitchen scale, etc.)\n• An empty cup\n• About 300g of water total (100g per test)"
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
                        property int testIndex: index  // Capture index for this item
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

                        // Dispensing indicator
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

                        // Weight input (after dispensing)
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
                                    // Must replace array to trigger QML binding updates
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

                    // Results table
                    GridLayout {
                        columns: 4
                        rowSpacing: 8
                        columnSpacing: 20
                        Layout.alignment: Qt.AlignHCenter

                        Text { text: "Test"; color: Theme.textSecondaryColor; font.bold: true }
                        Text { text: "Raw Flow"; color: Theme.textSecondaryColor; font.bold: true }
                        Text { text: "Actual"; color: Theme.textSecondaryColor; font.bold: true }
                        Text { text: "Factor"; color: Theme.textSecondaryColor; font.bold: true }

                        // Low
                        Text { text: "Low"; color: Theme.textColor }
                        Text { text: flowCalibrationDialog.flowIntegrals[0].toFixed(1) + "g"; color: Theme.textColor }
                        Text { text: flowCalibrationDialog.measuredWeights[0].toFixed(1) + "g"; color: Theme.textColor }
                        Text {
                            text: flowCalibrationDialog.flowIntegrals[0] > 0 ?
                                  (flowCalibrationDialog.measuredWeights[0] / flowCalibrationDialog.flowIntegrals[0]).toFixed(3) : "-"
                            color: Theme.primaryColor
                            font.bold: true
                        }

                        // Medium
                        Text { text: "Medium"; color: Theme.textColor }
                        Text { text: flowCalibrationDialog.flowIntegrals[1].toFixed(1) + "g"; color: Theme.textColor }
                        Text { text: flowCalibrationDialog.measuredWeights[1].toFixed(1) + "g"; color: Theme.textColor }
                        Text {
                            text: flowCalibrationDialog.flowIntegrals[1] > 0 ?
                                  (flowCalibrationDialog.measuredWeights[1] / flowCalibrationDialog.flowIntegrals[1]).toFixed(3) : "-"
                            color: Theme.primaryColor
                            font.bold: true
                        }

                        // High
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

                    // Average factor
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 15

                        Text {
                            text: "Average calibration factor:"
                            color: Theme.textColor
                            font.pixelSize: 16
                        }

                        Text {
                            id: averageFactorText
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

                // Step 5: Verification
                ColumnLayout {
                    spacing: 12

                    Text {
                        text: "Verify Calibration"
                        color: Theme.textColor
                        font.pixelSize: 18
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    // Show saved calibration factor
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 10

                        Text {
                            text: "Saved factor:"
                            color: Theme.textSecondaryColor
                            font.pixelSize: 14
                        }
                        Text {
                            text: Settings.flowCalibrationFactor.toFixed(3)
                            color: Theme.primaryColor
                            font.pixelSize: 16
                            font.bold: true
                        }
                    }

                    // Compact results table
                    GridLayout {
                        columns: 4
                        rowSpacing: 4
                        columnSpacing: 12
                        Layout.alignment: Qt.AlignHCenter

                        Text { text: "Test"; color: Theme.textSecondaryColor; font.pixelSize: 12 }
                        Text { text: "Raw"; color: Theme.textSecondaryColor; font.pixelSize: 12 }
                        Text { text: "Actual"; color: Theme.textSecondaryColor; font.pixelSize: 12 }
                        Text { text: "Factor"; color: Theme.textSecondaryColor; font.pixelSize: 12 }

                        Text { text: "Low"; color: Theme.textColor; font.pixelSize: 12 }
                        Text { text: flowCalibrationDialog.flowIntegrals[0].toFixed(0) + "g"; color: Theme.textColor; font.pixelSize: 12 }
                        Text { text: flowCalibrationDialog.measuredWeights[0].toFixed(0) + "g"; color: Theme.textColor; font.pixelSize: 12 }
                        Text { text: flowCalibrationDialog.flowIntegrals[0] > 0 ? (flowCalibrationDialog.measuredWeights[0] / flowCalibrationDialog.flowIntegrals[0]).toFixed(2) : "-"; color: Theme.primaryColor; font.pixelSize: 12 }

                        Text { text: "Med"; color: Theme.textColor; font.pixelSize: 12 }
                        Text { text: flowCalibrationDialog.flowIntegrals[1].toFixed(0) + "g"; color: Theme.textColor; font.pixelSize: 12 }
                        Text { text: flowCalibrationDialog.measuredWeights[1].toFixed(0) + "g"; color: Theme.textColor; font.pixelSize: 12 }
                        Text { text: flowCalibrationDialog.flowIntegrals[1] > 0 ? (flowCalibrationDialog.measuredWeights[1] / flowCalibrationDialog.flowIntegrals[1]).toFixed(2) : "-"; color: Theme.primaryColor; font.pixelSize: 12 }

                        Text { text: "High"; color: Theme.textColor; font.pixelSize: 12 }
                        Text { text: flowCalibrationDialog.flowIntegrals[2].toFixed(0) + "g"; color: Theme.textColor; font.pixelSize: 12 }
                        Text { text: flowCalibrationDialog.measuredWeights[2].toFixed(0) + "g"; color: Theme.textColor; font.pixelSize: 12 }
                        Text { text: flowCalibrationDialog.flowIntegrals[2] > 0 ? (flowCalibrationDialog.measuredWeights[2] / flowCalibrationDialog.flowIntegrals[2]).toFixed(2) : "-"; color: Theme.primaryColor; font.pixelSize: 12 }
                    }

                    Rectangle { height: 1; Layout.fillWidth: true; color: Theme.surfaceColor }

                    Text {
                        text: flowCalibrationDialog.verificationComplete ?
                              "Verification complete! Compare the weights below." :
                              flowCalibrationDialog.isDispensing ?
                              "Dispensing... FlowScale will stop at " + flowCalibrationDialog.verificationTarget + "g" :
                              "Dispense " + flowCalibrationDialog.verificationTarget + "g using the new calibration.\nTare your external scale, then press Start."
                        color: Theme.textSecondaryColor
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    // Live weight display during verification
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

                    // Input for actual weight after verification
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
                            onValueModified: flowCalibrationDialog.verificationActualWeight = newValue
                        }

                        // Comparison result
                        Text {
                            visible: flowCalibrationDialog.verificationActualWeight > 0
                            text: {
                                var diff = flowCalibrationDialog.verificationActualWeight - flowCalibrationDialog.verificationFlowScaleWeight
                                var pct = (diff / flowCalibrationDialog.verificationFlowScaleWeight * 100).toFixed(1)
                                if (Math.abs(diff) < 3) {
                                    return "✓ Excellent! Difference: " + diff.toFixed(1) + "g (" + pct + "%)"
                                } else if (Math.abs(diff) < 5) {
                                    return "Good. Difference: " + diff.toFixed(1) + "g (" + pct + "%)"
                                } else {
                                    return "Consider re-calibrating. Difference: " + diff.toFixed(1) + "g (" + pct + "%)"
                                }
                            }
                            color: {
                                var diff = Math.abs(flowCalibrationDialog.verificationActualWeight - flowCalibrationDialog.verificationFlowScaleWeight)
                                return diff < 3 ? Theme.primaryColor : (diff < 5 ? Theme.textColor : Theme.warningColor)
                            }
                            font.pixelSize: 14
                            font.bold: true
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
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
                    accessibleName: flowCalibrationDialog.currentStep === 0 ? "Start calibration" :
                          flowCalibrationDialog.currentStep < 4 ?
                              (flowCalibrationDialog.isDispensing ? "Dispensing water" :
                               flowCalibrationDialog.flowIntegrals[flowCalibrationDialog.currentStep - 1] > 0 ? "Next step" : "Ready to dispense") :
                          flowCalibrationDialog.currentStep === 4 ? "Save and verify" :
                          flowCalibrationDialog.isDispensing ? "Dispensing water" :
                          flowCalibrationDialog.verificationComplete ? "Done, close dialog" : "Start verification"
                    enabled: !flowCalibrationDialog.isDispensing

                    contentItem: Text {
                        text: calibrationNextButton.text
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        if (flowCalibrationDialog.currentStep === 0) {
                            // Start first test
                            flowCalibrationDialog.currentStep = 1
                        } else if (flowCalibrationDialog.currentStep < 4) {
                            var testIdx = flowCalibrationDialog.currentStep - 1
                            if (flowCalibrationDialog.flowIntegrals[testIdx] === 0) {
                                // Upload calibration profile - user must press espresso button
                                FlowScale.resetRawFlowIntegral()  // Reset raw integral tracking
                                flowCalibrationDialog.currentFlowIntegral = 0
                                flowCalibrationDialog.isDispensing = true
                                root.calibrationInProgress = true  // Prevent navigation to espresso page
                                var flowRate = flowCalibrationDialog.testFlows[testIdx]
                                MainController.startCalibrationDispense(flowRate, 100)  // 100g target for better precision
                            } else if (flowCalibrationDialog.measuredWeights[testIdx] > 0) {
                                // Move to next step
                                flowCalibrationDialog.currentStep++
                            }
                        } else if (flowCalibrationDialog.currentStep === 4) {
                            // Save factor and go to verification
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
                                // Done - close dialog
                                flowCalibrationDialog.close()
                            } else {
                                // Start verification dispense
                                FlowScale.resetWeight()  // Tare FlowScale before verification
                                flowCalibrationDialog.isDispensing = true
                                root.calibrationInProgress = true
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

        // Shift dialog up when keyboard appears
        Connections {
            target: Qt.inputMethod
            function onVisibleChanged() {
                if (Qt.inputMethod.visible && saveThemeDialog.visible) {
                    // Move dialog to upper third of screen
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

            Text {
                text: "Save Theme"
                color: Theme.textColor
                font: Theme.subtitleFont
                Layout.alignment: Qt.AlignHCenter
            }

            TextField {
                id: themeNameInput
                Layout.fillWidth: true
                placeholderText: "Enter theme name"
                color: Theme.textColor
                placeholderTextColor: Theme.textSecondaryColor
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
                        presetRepeater.model = Settings.getPresetThemes()
                        saveThemeDialog.close()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Button {
                    Layout.fillWidth: true
                    text: "Cancel"
                    onClicked: saveThemeDialog.close()
                    background: Rectangle {
                        color: Theme.surfaceColor
                        radius: Theme.buttonRadius
                        border.color: Theme.borderColor
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.text
                        color: Theme.textColor
                        font: Theme.labelFont
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: "Save"
                    enabled: saveThemeDialog.themeName.trim().length > 0
                    onClicked: {
                        var name = saveThemeDialog.themeName.trim()
                        if (name.length > 0 && name !== "Default") {
                            Settings.saveCurrentTheme(name)
                            presetRepeater.model = Settings.getPresetThemes()
                            saveThemeDialog.close()
                        }
                    }
                    background: Rectangle {
                        color: parent.enabled ? Theme.primaryColor : Theme.surfaceColor
                        radius: Theme.buttonRadius
                        opacity: parent.pressed ? 0.8 : 1.0
                    }
                    contentItem: Text {
                        text: parent.text
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
