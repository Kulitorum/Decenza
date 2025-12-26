import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App
import "../components"

Page {
    objectName: "settingsPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = "Settings"

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
        anchors.bottomMargin: 150
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

    // Tab bar at top
    TabBar {
        id: tabBar
        anchors.top: parent.top
        anchors.topMargin: 80
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
    }

    // Tab content area
    StackLayout {
        id: tabContent
        anchors.top: tabBar.bottom
        anchors.topMargin: 15
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        anchors.bottomMargin: 15
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin

        currentIndex: tabBar.currentIndex

        // ============ BLUETOOTH TAB ============
        Item {
            RowLayout {
                anchors.fill: parent
                spacing: 15

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
            RowLayout {
                anchors.fill: parent
                spacing: 15

                // Auto-sleep settings
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
                            Layout.fillWidth: true
                            from: 0
                            to: 240
                            stepSize: 1
                            decimals: 0
                            value: Settings.value("autoSleepMinutes", 0)
                            displayText: value === 0 ? "Never" : (value + " min")

                            onValueModified: function(newValue) {
                                Settings.setValue("autoSleepMinutes", newValue)
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // About box
                Rectangle {
                    Layout.preferredWidth: 200
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
                            text: "DE1 Controller"
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
    }

    // Bottom bar with back button
    Rectangle {
        id: bottomBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 70
        color: Theme.primaryColor

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 20
            spacing: 15

            // Back button
            RoundButton {
                Layout.preferredWidth: 100
                Layout.preferredHeight: 70
                icon.source: "qrc:/icons/back.svg"
                icon.width: 28
                icon.height: 28
                flat: true
                icon.color: "white"
                onClicked: root.goToIdle()
            }

            Text {
                text: "Settings"
                color: "white"
                font.pixelSize: 20
                font.bold: true
            }

            Item { Layout.fillWidth: true }
        }
    }
}
