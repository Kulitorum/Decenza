import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

KeyboardAwareContainer {
    id: preferencesTab
    textFields: [manualCityField]

    // Local properties
    property int autoSleepMinutes: Settings.value("autoSleepMinutes", 60)
    property bool configurePageScaleEnabled: Theme.configurePageScaleEnabled

    Flickable {
        id: contentFlickable
        anchors.fill: parent
        contentHeight: mainLayout.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        RowLayout {
            id: mainLayout
            width: parent.width
            spacing: Theme.scaled(15)

            // ========== LEFT COLUMN ==========
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(350)
                Layout.alignment: Qt.AlignTop
                spacing: Theme.scaled(15)

                // Auto-sleep settings
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(160)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.preferences.autoSleep"
                            fallback: "Auto-Sleep"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Tr {
                            key: "settings.preferences.autoSleepDesc"
                            fallback: "Put the machine to sleep after inactivity"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Item { Layout.fillHeight: true }

                        ValueInput {
                            id: sleepInput
                            Layout.fillWidth: true
                            from: 0
                            to: 240
                            stepSize: 5
                            decimals: 0
                            value: preferencesTab.autoSleepMinutes
                            displayText: value === 0 ? TranslationManager.translate("settings.preferences.never", "Never") : (value + " " + TranslationManager.translate("settings.preferences.min", "min"))
                            accessibleName: TranslationManager.translate("settings.preferences.autoSleep", "Auto-Sleep")

                            onValueModified: function(newValue) {
                                preferencesTab.autoSleepMinutes = newValue
                                Settings.setValue("autoSleepMinutes", newValue)
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // Reset BLE on wake
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: bleResetContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: bleResetContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(10)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            ColumnLayout {
                                spacing: Theme.scaled(1)

                                Tr {
                                    key: "settings.preferences.resetBleOnWake"
                                    fallback: "Reset BLE connection on wake"
                                    color: Theme.textColor
                                    font.pixelSize: Theme.scaled(14)
                                }

                                Tr {
                                    Layout.fillWidth: true
                                    key: "settings.preferences.resetBleOnWakeDesc"
                                    fallback: "Cycle all Bluetooth connections when the machine wakes from sleep"
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.scaled(11)
                                    wrapMode: Text.WordWrap
                                }
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: Settings.resetBleOnWake
                                accessibleName: TranslationManager.translate("settings.preferences.resetBleOnWake", "Reset BLE connection on wake")
                                onToggled: Settings.resetBleOnWake = checked
                            }
                        }
                    }
                }

                // Refill Kit
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: refillKitContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    property bool kitAvailable: DE1Device.refillKitDetected > 0

                    ColumnLayout {
                        id: refillKitContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(8)
                        opacity: parent.kitAvailable ? 1.0 : 0.5

                        Tr {
                            key: "settings.preferences.refillKit"
                            fallback: "Refill Kit"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.preferences.refillKitDesc"
                            fallback: "Control whether the machine uses an automatic water refill kit"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: {
                                var status = DE1Device.refillKitDetected
                                if (status === 1) return TranslationManager.translate("settings.preferences.refillKitDetected", "Status: Detected")
                                if (status === 0) return TranslationManager.translate("settings.preferences.refillKitNotDetected", "Status: Not detected")
                                return TranslationManager.translate("settings.preferences.refillKitUnknown", "Status: Unknown")
                            }
                            color: DE1Device.refillKitDetected === 1 ? Theme.successColor : Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Row {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(42)
                            spacing: Theme.scaled(8)

                            Repeater {
                                model: [
                                    { value: 2, label: TranslationManager.translate("settings.preferences.refillKitAuto", "Auto"), desc: TranslationManager.translate("settings.preferences.refillKitAutoDesc", "Auto-detect") },
                                    { value: 0, label: TranslationManager.translate("settings.preferences.refillKitOff", "Off"), desc: TranslationManager.translate("settings.preferences.refillKitOffDesc", "Force off") },
                                    { value: 1, label: TranslationManager.translate("settings.preferences.refillKitOn", "On"), desc: TranslationManager.translate("settings.preferences.refillKitOnDesc", "Force on") }
                                ]

                                delegate: Rectangle {
                                    id: refillKitButton
                                    width: (parent.width - 2 * parent.spacing) / 3
                                    height: parent.height
                                    radius: Theme.scaled(6)
                                    color: Settings.refillKitOverride === modelData.value ?
                                           Theme.primaryColor : Theme.backgroundColor
                                    border.color: Settings.refillKitOverride === modelData.value ?
                                                  Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1

                                    ColumnLayout {
                                        anchors.centerIn: parent
                                        spacing: Theme.scaled(2)

                                        Text {
                                            text: modelData.label
                                            color: Settings.refillKitOverride === modelData.value ?
                                                   "white" : Theme.textColor
                                            font.pixelSize: Theme.scaled(14)
                                            font.bold: true
                                            Layout.alignment: Qt.AlignHCenter
                                        }

                                        Text {
                                            text: modelData.desc
                                            color: Settings.refillKitOverride === modelData.value ?
                                                   Qt.rgba(1, 1, 1, 0.7) : Theme.textSecondaryColor
                                            font.pixelSize: Theme.scaled(10)
                                            Layout.alignment: Qt.AlignHCenter
                                        }
                                    }

                                    AccessibleMouseArea {
                                        anchors.fill: parent
                                        accessibleName: modelData.label + " refill kit mode. " + modelData.desc +
                                                       (Settings.refillKitOverride === modelData.value ? ", selected" : "")
                                        accessibleItem: refillKitButton
                                        onAccessibleClicked: Settings.refillKitOverride = modelData.value
                                    }
                                }
                            }
                        }
                    }
                }

                // Offline Mode
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: offlineContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: offlineContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "settings.preferences.offlineMode"
                            fallback: "Offline Mode"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.preferences.offlineModeDesc"
                            fallback: "Use the app without a connected DE1 machine"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Tr {
                                key: "settings.preferences.unlockGui"
                                fallback: "Unlock GUI"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }

                            // Status indicator when simulation mode is active
                            Rectangle {
                                visible: Settings.simulationMode
                                Layout.leftMargin: Theme.scaled(8)
                                implicitWidth: statusLabel.implicitWidth + Theme.scaled(12)
                                implicitHeight: Theme.scaled(20)
                                radius: Theme.scaled(10)
                                color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2)
                                border.width: 1
                                border.color: Theme.primaryColor

                                Text {
                                    id: statusLabel
                                    anchors.centerIn: parent
                                    text: TranslationManager.translate("settings.preferences.simulationActive", "Active")
                                    color: Theme.primaryColor
                                    font.pixelSize: Theme.scaled(11)
                                    font.bold: true
                                }

                                Accessible.role: Accessible.StaticText
                                Accessible.name: TranslationManager.translate("settings.preferences.simulationModeActive", "Simulation mode is active")
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: Settings.simulationMode
                                accessibleName: TranslationManager.translate("settings.preferences.unlockGui", "Unlock GUI")
                                onToggled: {
                                    // Save to persistent Settings — takes effect on next launch
                                    Settings.simulationMode = checked
                                    restartRequiredText.visible = true
                                }
                            }
                        }

                        Text {
                            id: restartRequiredText
                            visible: false
                            text: "Restart required for this change to take effect"
                            color: Theme.warningColor
                            font.pixelSize: Theme.scaled(12)
                            Layout.leftMargin: Theme.scaled(15)
                            Layout.bottomMargin: Theme.scaled(5)
                        }
                    }
                }

                // Launcher Mode (Android only)
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: launcherContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    visible: Qt.platform.os === "android"

                    ColumnLayout {
                        id: launcherContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "settings.options.launcherMode"
                            fallback: "Launcher Mode"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.options.launcherModeDesc"
                            fallback: "Set Decenza as the Android home screen. Press Home to return here instead of the default launcher."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Tr {
                                key: "settings.options.useAsLauncher"
                                fallback: "Use as Home Screen"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: Settings.launcherMode
                                accessibleName: TranslationManager.translate(
                                    "settings.options.useAsLauncher", "Use as Home Screen")
                                onToggled: Settings.launcherMode = checked
                            }
                        }

                        Tr {
                            Layout.fillWidth: true
                            visible: Settings.launcherMode
                            key: "settings.options.launcherModeHint"
                            fallback: "Android will ask you to choose a default launcher. Select Decenza and tap \"Always\"."
                            color: Theme.warningColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Shot Map Settings
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: shotMapContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: shotMapContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.shotmap.title"
                            fallback: "Shot Map"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.shotmap.description"
                            fallback: "Share your shots on the global map at decenza.coffee"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Tr {
                                key: "settings.shotmap.enable"
                                fallback: "Enable Shot Map"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: MainController.shotReporter ? MainController.shotReporter.enabled : false
                                accessibleName: TranslationManager.translate("settings.shotmap.enable", "Enable Shot Map")
                                onCheckedChanged: {
                                    if (MainController.shotReporter) {
                                        MainController.shotReporter.enabled = checked
                                        // Auto-open Location Settings if GPS is disabled at system level
                                        if (checked && !MainController.shotReporter.isGpsEnabled()) {
                                            MainController.shotReporter.openLocationSettings()
                                        }
                                    }
                                }
                            }
                        }

                        // Location status (only when enabled)
                        Text {
                            visible: MainController.shotReporter && MainController.shotReporter.enabled
                            text: {
                                if (!MainController.shotReporter) return ""
                                if (MainController.shotReporter.hasLocation) {
                                    var city = MainController.shotReporter.currentCity()
                                    var country = MainController.shotReporter.currentCountryCode()
                                    var prefix = MainController.shotReporter.usingManualCity ? "Manual: " : "GPS: "
                                    var lat = MainController.shotReporter.latitude.toFixed(1)
                                    var lon = MainController.shotReporter.longitude.toFixed(1)
                                    return prefix + city + (country ? ", " + country : "") + " (" + lat + ", " + lon + ")"
                                }
                                return "GPS disabled - enable in Android Settings"
                            }
                            color: MainController.shotReporter && MainController.shotReporter.hasLocation ? Theme.textColor : Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        // Manual city input (shown when enabled)
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: MainController.shotReporter && MainController.shotReporter.enabled
                            spacing: Theme.scaled(5)

                            Text {
                                text: "Manual city (fallback if GPS unavailable):"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(11)
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(8)

                                StyledTextField {
                                    id: manualCityField
                                    Layout.fillWidth: true
                                    placeholder: "e.g. Copenhagen, Denmark"
                                    text: MainController.shotReporter ? MainController.shotReporter.manualCity : ""
                                    onEditingFinished: {
                                        if (MainController.shotReporter) {
                                            MainController.shotReporter.manualCity = text
                                        }
                                    }
                                }

                                AccessibleButton {
                                    text: "Test"
                                    accessibleName: TranslationManager.translate("settings.options.testLocation", "Test location on shot map")
                                    enabled: MainController.shotReporter && MainController.shotReporter.hasLocation
                                    onClicked: mapTestPopup.open()
                                }
                            }
                        }
                    }
                }
            }

            // ========== MIDDLE COLUMN ==========
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(350)
                Layout.alignment: Qt.AlignTop
                spacing: Theme.scaled(15)

                // Per-Screen Scale Configuration
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: scaleContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: scaleContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.preferences.perScreenScale"
                            fallback: "Per-Screen Scale"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.preferences.perScreenScaleDesc"
                            fallback: "Adjust UI scale individually for each screen to optimize readability."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(10)

                            Tr {
                                key: "settings.preferences.configureScalePerScreen"
                                fallback: "Configure scale per screen"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: preferencesTab.configurePageScaleEnabled
                                accessibleName: TranslationManager.translate("settings.preferences.configureScale", "Configure scale per screen")
                                onClicked: {
                                    console.log("Switch clicked, checked =", checked)
                                    Settings.setValue("ui/configurePageScale", checked)
                                    // Also set Theme directly as fallback
                                    Theme.configurePageScaleEnabled = checked
                                    console.log("Theme.configurePageScaleEnabled =", Theme.configurePageScaleEnabled)
                                }
                            }
                        }

                        Tr {
                            visible: preferencesTab.configurePageScaleEnabled
                            Layout.fillWidth: true
                            key: "settings.preferences.scaleHint"
                            fallback: "Navigate to each screen and use the floating control to adjust its scale."
                            color: Theme.primaryColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Battery / Charging settings
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: batteryContent.implicitHeight + Theme.scaled(20)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: batteryContent
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.preferences.batteryCharging"
                            fallback: "Battery Charging"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        // Battery status
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(10)

                            Tr {
                                key: "settings.preferences.battery"
                                fallback: "Battery:"
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
                                width: Theme.scaled(30)
                                height: Theme.scaled(14)
                                radius: Theme.scaled(2)
                                color: "transparent"
                                border.color: Theme.textSecondaryColor
                                border.width: 1

                                Rectangle {
                                    x: 2
                                    y: 2
                                    width: (parent.width - 4) * BatteryManager.batteryPercent / 100
                                    height: parent.height - 4
                                    radius: Theme.scaled(1)
                                    color: BatteryManager.batteryPercent < 20 ? Theme.errorColor :
                                           BatteryManager.batteryPercent < 50 ? Theme.warningColor :
                                           Theme.successColor
                                }

                                // Battery terminal
                                Rectangle {
                                    x: parent.width
                                    y: 4
                                    width: Theme.scaled(3)
                                    height: Theme.scaled(6)
                                    radius: Theme.scaled(1)
                                    color: Theme.textSecondaryColor
                                }
                            }

                            Item { Layout.fillWidth: true }

                            Tr {
                                key: BatteryManager.isCharging ? "settings.preferences.charging" : "settings.preferences.notCharging"
                                fallback: BatteryManager.isCharging ? "Charging" : "Not charging"
                                color: BatteryManager.isCharging ? Theme.successColor : Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }
                        }

                        // Smart charging mode selector
                        Tr {
                            key: "settings.preferences.smartChargingMode"
                            fallback: "Smart Charging Mode"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Row {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(42)
                            spacing: Theme.scaled(8)

                            Repeater {
                                model: [
                                    { value: 0, label: TranslationManager.translate("settings.preferences.chargingOff", "Off"), desc: TranslationManager.translate("settings.preferences.alwaysCharging", "Always charging") },
                                    { value: 1, label: TranslationManager.translate("settings.preferences.chargingOn", "On"), desc: "55-65%" },
                                    { value: 2, label: TranslationManager.translate("settings.preferences.chargingNight", "Night"), desc: "90-95%" }
                                ]

                                delegate: Rectangle {
                                    id: chargingModeButton
                                    width: (parent.width - 2 * parent.spacing) / 3
                                    height: parent.height
                                    radius: Theme.scaled(6)
                                    color: BatteryManager.chargingMode === modelData.value ?
                                           Theme.primaryColor : Theme.backgroundColor
                                    border.color: BatteryManager.chargingMode === modelData.value ?
                                                  Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1

                                    ColumnLayout {
                                        anchors.centerIn: parent
                                        spacing: Theme.scaled(2)

                                        Text {
                                            text: modelData.label
                                            color: BatteryManager.chargingMode === modelData.value ?
                                                   "white" : Theme.textColor
                                            font.pixelSize: Theme.scaled(14)
                                            font.bold: true
                                            Layout.alignment: Qt.AlignHCenter
                                        }

                                        Text {
                                            text: modelData.desc
                                            color: BatteryManager.chargingMode === modelData.value ?
                                                   Qt.rgba(1, 1, 1, 0.7) : Theme.textSecondaryColor
                                            font.pixelSize: Theme.scaled(10)
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

                        // Explanation text
                        Text {
                            text: BatteryManager.chargingMode === 0 ?
                                  TranslationManager.translate("settings.preferences.chargingOffDesc", "Charger is always on. Battery stays at 100%.") :
                                  BatteryManager.chargingMode === 1 ?
                                  TranslationManager.translate("settings.preferences.chargingOnDesc", "Cycles between 55-65% to extend battery lifespan.") :
                                  TranslationManager.translate("settings.preferences.chargingNightDesc", "Keeps battery at 90-95% when active. Allows deeper discharge when sleeping.")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        // Manual charger toggle
                        RowLayout {
                            Layout.fillWidth: true
                            visible: BatteryManager.chargingMode === 0

                            Tr {
                                key: "settings.preferences.usbCharger"
                                fallback: "USB Charger"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: DE1Device.usbChargerOn
                                accessibleName: TranslationManager.translate("settings.preferences.usbCharger", "USB Charger")
                                onClicked: DE1Device.setUsbChargerOn(checked)
                            }
                        }

                    }
                }

                // Flow Calibration
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: flowCalContent.implicitHeight + Theme.scaled(20)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: flowCalContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(6)

                        Text {
                            text: TranslationManager.translate("settings.preferences.flowCalibration", "Flow Calibration")
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.preferences.currentMultiplier", "Current:") + " " + Settings.flowCalibrationMultiplier.toFixed(2)
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }

                            Item { Layout.fillWidth: true }

                            AccessibleButton {
                                accessibleName: TranslationManager.translate("settings.preferences.openFlowCalibration", "Open Flow Calibration")
                                text: TranslationManager.translate("settings.preferences.calibrate", "Calibrate")
                                primary: true
                                onClicked: pageStack.push(Qt.resolvedUrl("../FlowCalibrationPage.qml"))
                            }
                        }
                    }
                }

                // Water Level Status
                Rectangle {
                    id: waterLevelCard
                    Layout.fillWidth: true
                    implicitHeight: waterLevelContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    property bool refillKitActive: Settings.refillKitOverride === 1 ||
                                                    (Settings.refillKitOverride === 2 && DE1Device.refillKitDetected === 1)

                    ColumnLayout {
                        id: waterLevelContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(12)

                        // Header
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            Tr {
                                key: "settings.options.waterLevel"
                                fallback: "Water Level"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(16)
                                font.bold: true
                            }

                            // Refill kit active indicator
                            Rectangle {
                                width: Theme.scaled(20)
                                height: Theme.scaled(20)
                                radius: Theme.scaled(10)
                                color: Theme.successColor + "30"
                                visible: waterLevelCard.refillKitActive

                                Text {
                                    anchors.centerIn: parent
                                    text: "\u2713"  // Checkmark
                                    color: Theme.successColor
                                    font.pixelSize: Theme.scaled(12)
                                    font.bold: true
                                }
                            }

                            Tr {
                                key: "settings.options.refillKitActive"
                                fallback: "Auto-refill active"
                                color: Theme.successColor
                                font.pixelSize: Theme.scaled(12)
                                visible: waterLevelCard.refillKitActive
                            }
                        }

                        // Current water level display
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: levelDisplay.implicitHeight + Theme.scaled(20)
                            color: {
                                var level = DE1Device.waterLevelMm
                                if (waterLevelCard.refillKitActive && level < 10) {
                                    return Theme.errorColor + "20"
                                }
                                return Theme.backgroundColor
                            }
                            radius: Theme.scaled(6)

                            ColumnLayout {
                                id: levelDisplay
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(10)
                                spacing: Theme.scaled(4)

                                Text {
                                    text: {
                                        var level = DE1Device.waterLevelMm
                                        var ml = DE1Device.waterLevelMl
                                        var percent = DE1Device.waterLevel
                                        return ml + " ml (" + percent.toFixed(0) + "%) · " + level.toFixed(1) + " mm"
                                    }
                                    color: Theme.textColor
                                    font.pixelSize: Theme.scaled(18)
                                    font.bold: true
                                }

                                // Warning for low water with active refill kit
                                Text {
                                    Layout.fillWidth: true
                                    text: TranslationManager.translate("settings.options.refillKitMalfunction",
                                        "⚠ Water critically low despite refill kit - check kit connection")
                                    color: Theme.errorColor
                                    font.pixelSize: Theme.scaled(12)
                                    wrapMode: Text.WordWrap
                                    visible: waterLevelCard.refillKitActive && DE1Device.waterLevelMm < 10
                                }
                            }
                        }

                        // Display unit toggle
                        RowLayout {
                            Layout.fillWidth: true

                            Tr {
                                key: "settings.options.showInMl"
                                fallback: "Show in milliliters (ml)"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: Settings.waterLevelDisplayUnit === "ml"
                                accessibleName: TranslationManager.translate("settings.options.showInMl", "Show in milliliters")
                                onToggled: {
                                    Settings.waterLevelDisplayUnit = checked ? "ml" : "percent"
                                }
                            }
                        }
                    }
                }

                // Water Refill Threshold (only when refill kit is not active)
                Rectangle {
                    id: waterRefillThresholdCard
                    Layout.fillWidth: true
                    implicitHeight: refillContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    visible: {
                        var override = Settings.refillKitOverride
                        var detected = DE1Device.refillKitDetected === 1
                        return override === 0 || (override === 2 && !detected)
                    }

                    ColumnLayout {
                        id: refillContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(8)

                        Text {
                            text: TranslationManager.translate("settings.options.waterRefillLevel", "Water Refill Threshold")
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.options.waterRefillLevelDesc", "Water level at which the machine warns you to refill")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        ValueInput {
                            Layout.fillWidth: true
                            from: 3
                            to: 70
                            stepSize: 1
                            decimals: 0
                            value: Settings.waterRefillPoint
                            suffix: " mm"
                            accessibleName: TranslationManager.translate("settings.options.waterRefillLevelAccessible", "Water refill level")
                            onValueModified: function(newValue) {
                                Settings.waterRefillPoint = newValue
                            }
                        }
                    }
                }

                // Headless Machine Settings (only visible on headless machines)
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: headlessContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    visible: DE1Device.isHeadless

                    ColumnLayout {
                        id: headlessContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.options.headlessMachine"
                            fallback: "Headless Machine"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Tr {
                                key: "settings.options.singlePressStopPurge"
                                fallback: "Single press to stop & purge"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                id: headlessStopSwitch
                                checked: Settings.headlessSkipPurgeConfirm
                                accessibleName: TranslationManager.translate("settings.options.singlePressStopPurge", "Single press to stop and purge")
                                onClicked: {
                                    Settings.headlessSkipPurgeConfirm = checked
                                }
                            }
                        }
                    }
                }
            }

            // ========== RIGHT COLUMN ==========
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(350)
                Layout.alignment: Qt.AlignTop
                spacing: Theme.scaled(15)

                // Steam Heater Settings
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: steamContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: steamContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.preferences.steamHeater"
                            fallback: "Steam Heater"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Tr {
                            key: "settings.preferences.steamHeaterDesc"
                            fallback: "Pre-heat for faster steaming"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Text {
                            property real temp: typeof DE1Device.steamTemperature === 'number' ? DE1Device.steamTemperature : 0
                            text: TranslationManager.translate("settings.preferences.current", "Current:") + " " + temp.toFixed(0) + "°C"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Tr {
                                key: "settings.preferences.keepSteamHeaterOn"
                                fallback: "Keep heater on when idle"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)

                                Accessible.role: Accessible.StaticText
                                Accessible.name: text
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                id: steamHeaterSwitch
                                checked: Settings.keepSteamHeaterOn
                                accessibleName: TranslationManager.translate("settings.preferences.keepSteamHeaterOn", "Keep heater on when idle")
                                onClicked: {
                                    Settings.keepSteamHeaterOn = checked
                                    MainController.applySteamSettings()
                                }
                            }
                        }

                        // Auto flush steam wand setting
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(4)

                            Text {
                                text: "Auto flush wand after"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }

                            ValueInput {
                                Layout.fillWidth: true
                                from: 0
                                to: 60
                                stepSize: 1
                                decimals: 0
                                value: Settings.steamAutoFlushSeconds
                                valueColor: value > 0 ? Theme.primaryColor : Theme.textSecondaryColor
                                displayText: value === 0 ? "Off" : value + "s"
                                accessibleName: TranslationManager.translate("settings.preferences.autoFlushDuration", "Auto flush duration")
                                onValueModified: function(newValue) {
                                    Settings.steamAutoFlushSeconds = newValue
                                }
                            }
                        }
                    }
                }

                // Virtual Scale (FlowScale)
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: flowScaleContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: flowScaleContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(10)

                        Text {
                            text: TranslationManager.translate("settings.preferences.virtualScale", "Virtual Scale")
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.preferences.virtualScaleDesc",
                                  "Estimate cup weight from the machine's flow sensor when no Bluetooth scale is connected. Accuracy depends on dose weight being set correctly.")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.preferences.useVirtualScale", "Enable virtual scale")
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: Settings.useFlowScale
                                accessibleName: TranslationManager.translate("settings.preferences.useVirtualScale", "Enable virtual scale")
                                onClicked: Settings.useFlowScale = checked
                            }
                        }
                    }
                }

                // Auto-Wake Timer Card
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: autoWakeContent.implicitHeight + Theme.scaled(24)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: autoWakeContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(8)

                        // Track selected day (0-6, Mon-Sun)
                        property int selectedDay: 0
                        property var schedule: Settings.autoWakeSchedule
                        property var selectedDayData: schedule[selectedDay] || {enabled: false, hour: 7, minute: 0}

                        // Title
                        Tr {
                            key: "settings.options.autoWake"
                            fallback: "Auto-Wake Timer"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        // Row 1: Day buttons
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(4)

                            Repeater {
                                model: ["M", "T", "W", "T", "F", "S", "S"]

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Theme.scaled(32)
                                    radius: Theme.scaled(6)

                                    property bool isSelected: autoWakeContent.selectedDay === index
                                    property bool isEnabled: {
                                        var sched = Settings.autoWakeSchedule
                                        return sched[index] ? sched[index].enabled : false
                                    }

                                    color: isSelected ? Qt.lighter(Theme.primaryColor, 1.3) :
                                           isEnabled ? Theme.primaryColor :
                                           Theme.backgroundColor
                                    border.color: isSelected ? "white" :
                                                  isEnabled ? Theme.primaryColor : Theme.borderColor
                                    border.width: isSelected ? 2 : 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: parent.isSelected || parent.isEnabled ? "white" : Theme.textSecondaryColor
                                        font.pixelSize: Theme.scaled(14)
                                        font.bold: parent.isSelected || parent.isEnabled
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: autoWakeContent.selectedDay = index
                                    }
                                }
                            }
                        }

                        // Row 2: Wake enabled toggle for selected day
                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "Wake"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: autoWakeContent.selectedDayData.enabled || false
                                accessibleName: "Wake enabled for selected day"
                                onToggled: Settings.setAutoWakeDayEnabled(autoWakeContent.selectedDay, checked)
                            }
                        }

                        // Row 3: Time inputs (50/50 width)
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            ValueInput {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(38)
                                from: 0
                                to: 23
                                stepSize: 1
                                decimals: 0
                                value: autoWakeContent.selectedDayData.hour ?? 7
                                enabled: autoWakeContent.selectedDayData.enabled ?? false
                                valueColor: enabled ? Theme.primaryColor : Theme.textSecondaryColor
                                displayText: value < 10 ? "0" + value.toFixed(0) : value.toFixed(0)
                                accessibleName: TranslationManager.translate("settings.options.wakeHour", "Wake hour")
                                onValueModified: function(newValue) {
                                    Settings.setAutoWakeDayTime(autoWakeContent.selectedDay, newValue, autoWakeContent.selectedDayData.minute ?? 0)
                                }
                            }

                            Text {
                                text: ":"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(18)
                                font.bold: true
                            }

                            ValueInput {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(38)
                                from: 0
                                to: 59
                                stepSize: 1
                                decimals: 0
                                value: autoWakeContent.selectedDayData.minute ?? 0
                                enabled: autoWakeContent.selectedDayData.enabled ?? false
                                valueColor: enabled ? Theme.primaryColor : Theme.textSecondaryColor
                                displayText: value < 10 ? "0" + value.toFixed(0) : value.toFixed(0)
                                accessibleName: TranslationManager.translate("settings.options.wakeMinute", "Wake minute")
                                onValueModified: function(newValue) {
                                    Settings.setAutoWakeDayTime(autoWakeContent.selectedDay, autoWakeContent.selectedDayData.hour ?? 7, newValue)
                                }
                            }
                        }

                        // Row 4: Stay awake toggle and duration
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            Text {
                                text: "And stay awake for"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                id: stayAwakeSwitch
                                checked: Settings.autoWakeStayAwakeEnabled
                                accessibleName: "Stay awake after auto-wake"
                                onToggled: Settings.autoWakeStayAwakeEnabled = checked
                            }
                        }

                        // Row 5: Stay awake duration (ValueInput)
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)
                            visible: Settings.autoWakeStayAwakeEnabled

                            ValueInput {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(38)
                                from: 15
                                to: 480
                                stepSize: 15
                                decimals: 0
                                value: Settings.autoWakeStayAwakeMinutes
                                valueColor: Theme.primaryColor
                                displayText: {
                                    var mins = value
                                    if (mins >= 60) {
                                        var hours = Math.floor(mins / 60)
                                        var remainMins = mins % 60
                                        if (remainMins === 0) {
                                            return hours + "h"
                                        }
                                        return hours + "h " + remainMins + "m"
                                    }
                                    return mins + " min"
                                }
                                accessibleName: TranslationManager.translate("settings.options.stayAwakeDuration", "Stay awake duration")
                                onValueModified: function(newValue) {
                                    Settings.autoWakeStayAwakeMinutes = newValue
                                }
                            }
                        }
                    }
                }

                // Stop-at-Weight Calibration Card
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: sawContent.implicitHeight + Theme.scaled(24)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: sawContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(4)

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.options.stopAtWeightCalibration", "Stop-at-Weight Calibration")
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                                font.bold: true
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: Settings.sawLearnedLag.toFixed(2) + "s"
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(14)
                                font.bold: true
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: (Settings.scaleType || TranslationManager.translate("settings.options.none", "none")) + " · " + TranslationManager.translate("settings.options.autoLearns", "auto-learns timing")
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: TranslationManager.translate("settings.options.reset", "Reset")
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(12)
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -Theme.scaled(4)
                                    onClicked: Settings.resetSawLearning()
                                }
                            }
                        }
                    }
                }

                // Heater Calibration Card
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: calibrateContent.implicitHeight + Theme.scaled(24)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: calibrateContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(4)

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.calibration.title", "Heater Calibration")
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                                font.bold: true
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: TranslationManager.translate("settings.calibration.calibrate", "Calibrate...")
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(12)
                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -Theme.scaled(4)
                                    accessibleName: TranslationManager.translate("settings.calibration.openCalibration", "Open heater calibration")
                                    onClicked: calibrationWarningDialog.open()
                                }
                            }
                        }

                        Text {
                            text: TranslationManager.translate("settings.calibration.description", "Idle temp, warmup flow rates, timeout")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }
                    }
                }
            }

            // Spacer
            Item { Layout.fillWidth: true }
        }
    }

    // Scroll indicator — shows when more content is below
    Rectangle {
        id: scrollIndicator
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.scaled(8)
        width: Theme.scaled(28)
        height: Theme.scaled(28)
        radius: Theme.scaled(14)
        color: Theme.primaryColor
        border.color: "white"
        border.width: 2
        opacity: 0.9
        visible: contentFlickable.contentHeight > contentFlickable.height &&
                 contentFlickable.contentY + contentFlickable.height < contentFlickable.contentHeight - 10

        Text {
            anchors.centerIn: parent
            text: "\u2193"
            color: "white"
            font.pixelSize: Theme.scaled(16)
            font.bold: true
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                contentFlickable.contentY = Math.min(
                    contentFlickable.contentHeight - contentFlickable.height,
                    contentFlickable.contentY + contentFlickable.height * 0.3
                )
            }
        }
    }

    // Map Test Popup
    Popup {
        id: mapTestPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, Theme.scaled(800))
        height: Math.min(parent.height * 0.8, Theme.scaled(500))
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.backgroundColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(10)

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "Your location on the Shot Map"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: "Close"
                    accessibleName: TranslationManager.translate("settings.options.closeMapTest", "Close map test popup")
                    onClicked: mapTestPopup.close()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#1a1a2e"
                radius: Theme.cardRadius
                clip: true

                Loader {
                    id: mapLoader
                    anchors.fill: parent
                    active: mapTestPopup.visible && Settings.hasQuick3D
                    source: "qrc:/qt/qml/DecenzaDE1/qml/components/ShotMapScreensaver.qml"
                    onLoaded: {
                        item.testMode = true
                        item.testLatitude = Qt.binding(function() { return MainController.shotReporter ? MainController.shotReporter.latitude : 0 })
                        item.testLongitude = Qt.binding(function() { return MainController.shotReporter ? MainController.shotReporter.longitude : 0 })
                    }
                }

                // Fallback when Quick3D is not available
                Text {
                    anchors.centerIn: parent
                    visible: !Settings.hasQuick3D
                    text: TranslationManager.translate("settings.options.quick3dRequired", "3D Map requires Qt Quick3D\n(not available on this platform)")
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Text {
                Layout.fillWidth: true
                text: {
                    if (!MainController.shotReporter) return ""
                    var lat = MainController.shotReporter.latitude.toFixed(1)
                    var lon = MainController.shotReporter.longitude.toFixed(1)
                    var city = MainController.shotReporter.currentCity()
                    return "Location: " + city + " at coordinates " + lat + ", " + lon
                }
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(12)
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // Heater Calibration Warning Dialog
    Popup {
        id: calibrationWarningDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(400))
        modal: true
        dim: true
        padding: Theme.scaled(20)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.warningColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Text {
                text: TranslationManager.translate("settings.calibration.warningTitle", "Heater Calibration")
                color: Theme.warningColor
                font.pixelSize: Theme.scaled(16)
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.calibration.warningMessage", "Bad calibration settings might make your espresso machine unusable. Only proceed if you know what you are doing.")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(13)
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                AccessibleButton {
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("settings.calibration.cancelWarning", "Cancel calibration")
                    onClicked: calibrationWarningDialog.close()
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("settings.calibration.proceed", "Proceed")
                    accessibleName: TranslationManager.translate("settings.calibration.proceedWarning", "Proceed to calibration")
                    onClicked: {
                        calibrationWarningDialog.close()
                        calibrationPopup.open()
                    }
                }
            }
        }
    }

    // Heater Calibration Popup
    Popup {
        id: calibrationPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, Theme.scaled(500))
        height: Math.min(calibrationColumn.implicitHeight + 2 * padding, parent.height * 0.85)
        modal: true
        dim: true
        padding: Theme.scaled(20)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: heaterIdleTempSlider.forceActiveFocus()

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: Flickable {
            contentHeight: calibrationColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick

            ColumnLayout {
                id: calibrationColumn
                width: parent.width
                spacing: Theme.scaled(16)

                // Title
                Text {
                    text: TranslationManager.translate("settings.calibration.title", "Heater Calibration")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(18)
                    font.bold: true
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // Heater idle temperature
                Text { text: TranslationManager.translate("settings.calibration.heaterIdleTemp", "Heater idle temperature"); font: Theme.captionFont; color: Theme.temperatureColor }
                ValueInput { id: heaterIdleTempSlider; Layout.fillWidth: true; valueColor: Theme.temperatureColor; accessibleName: TranslationManager.translate("settings.calibration.heaterIdleTemp", "Heater idle temperature"); from: 0; to: 990; stepSize: 5; displayText: (value / 10).toFixed(1) + "\u00B0C"; rangeText: "0.0\u00B0C \u2014 99.0\u00B0C"; value: Settings.heaterIdleTemp; onValueModified: function(newValue) { Settings.heaterIdleTemp = Math.round(newValue) }; KeyNavigation.tab: heaterWarmupFlowSlider; KeyNavigation.backtab: doneButton }

                // Heater warmup flow rate
                Text { text: TranslationManager.translate("settings.calibration.heaterWarmupFlow", "Heater warmup flow rate"); font: Theme.captionFont; color: Theme.flowColor }
                ValueInput { id: heaterWarmupFlowSlider; Layout.fillWidth: true; valueColor: Theme.flowColor; accessibleName: TranslationManager.translate("settings.calibration.heaterWarmupFlow", "Heater warmup flow rate"); from: 5; to: 60; stepSize: 1; displayText: (value / 10).toFixed(1) + " mL/s"; rangeText: "0.5 \u2014 6.0 mL/s"; value: Settings.heaterWarmupFlow; onValueModified: function(newValue) { Settings.heaterWarmupFlow = Math.round(newValue) }; KeyNavigation.tab: heaterTestFlowSlider; KeyNavigation.backtab: heaterIdleTempSlider }

                // Heater test flow rate
                Text { text: TranslationManager.translate("settings.calibration.heaterTestFlow", "Heater test flow rate"); font: Theme.captionFont; color: Theme.flowColor }
                ValueInput { id: heaterTestFlowSlider; Layout.fillWidth: true; valueColor: Theme.flowColor; accessibleName: TranslationManager.translate("settings.calibration.heaterTestFlow", "Heater test flow rate"); from: 5; to: 80; stepSize: 1; displayText: (value / 10).toFixed(1) + " mL/s"; rangeText: "0.5 \u2014 8.0 mL/s"; value: Settings.heaterTestFlow; onValueModified: function(newValue) { Settings.heaterTestFlow = Math.round(newValue) }; KeyNavigation.tab: heaterTestTimeoutSlider; KeyNavigation.backtab: heaterWarmupFlowSlider }

                // Heater test time-out
                Text { text: TranslationManager.translate("settings.calibration.heaterTestTimeout", "Heater test time-out"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                ValueInput { id: heaterTestTimeoutSlider; Layout.fillWidth: true; accessibleName: TranslationManager.translate("settings.calibration.heaterTestTimeout", "Heater test time-out"); from: 10; to: 300; stepSize: 1; suffix: "s"; value: Settings.heaterWarmupTimeout; onValueModified: function(newValue) { Settings.heaterWarmupTimeout = Math.round(newValue) }; KeyNavigation.tab: steamTwoTapSwitch; KeyNavigation.backtab: heaterTestFlowSlider }

                // Steam two-tap stop
                RowLayout { Layout.fillWidth: true
                    Text { text: TranslationManager.translate("settings.calibration.steamTwoTapStop", "Steam two-tap stop"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    Item { Layout.fillWidth: true }
                    StyledSwitch {
                        id: steamTwoTapSwitch
                        accessibleName: TranslationManager.translate("settings.calibration.steamTwoTapStop", "Steam two-tap stop")
                        checked: Settings.steamTwoTapStop
                        onToggled: Settings.steamTwoTapStop = checked
                        KeyNavigation.tab: defaultsButton
                        KeyNavigation.backtab: heaterTestTimeoutSlider
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.calibration.steamTwoTapStopDesc", "First tap goes to puffs, second tap stops steam")
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                    wrapMode: Text.WordWrap
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // Defaults for cafe button
                AccessibleButton {
                    id: defaultsButton
                    Layout.fillWidth: true
                    primary: true
                    text: TranslationManager.translate("settings.calibration.defaultsForCafe", "Defaults for cafe")
                    accessibleName: TranslationManager.translate("settings.calibration.defaultsForCafeAccessible", "Reset heater calibration to cafe defaults")
                    onClicked: {
                        Settings.heaterIdleTemp = 990
                        Settings.heaterWarmupFlow = 20
                        Settings.heaterTestFlow = 40
                        Settings.heaterWarmupTimeout = 10
                        Settings.steamTwoTapStop = true
                    }
                    KeyNavigation.tab: doneButton
                    KeyNavigation.backtab: steamTwoTapSwitch
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    AccessibleButton {
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("settings.calibration.cancelCalibration", "Cancel calibration")
                        onClicked: calibrationPopup.close()
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        id: doneButton
                        text: TranslationManager.translate("settings.calibration.done", "Done")
                        accessibleName: TranslationManager.translate("settings.calibration.closeCalibration", "Close heater calibration")
                        onClicked: calibrationPopup.close()
                        KeyNavigation.tab: heaterIdleTempSlider
                        KeyNavigation.backtab: defaultsButton
                    }
                }
            }
        }
    }
}
