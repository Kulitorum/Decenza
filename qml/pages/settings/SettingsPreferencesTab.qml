import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: preferencesTab

    // Signal to request opening flow calibration dialog (handled by parent)
    signal openFlowCalibrationDialog()

    // Local property to track auto-sleep value
    property int autoSleepMinutes: Settings.value("autoSleepMinutes", 60)

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: Auto-sleep and About stacked
        ColumnLayout {
            Layout.preferredWidth: Theme.scaled(300)
            Layout.fillHeight: true
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
                        from: 0
                        to: 240
                        stepSize: 5
                        decimals: 0
                        value: preferencesTab.autoSleepMinutes
                        displayText: value === 0 ? TranslationManager.translate("settings.preferences.never", "Never") : (value + " " + TranslationManager.translate("settings.preferences.min", "min"))

                        onValueModified: function(newValue) {
                            preferencesTab.autoSleepMinutes = newValue
                            Settings.setValue("autoSleepMinutes", newValue)
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // UI Scale (debug)
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(8)

                    Text {
                        text: "UI Scale (debug)"
                        color: Theme.warningColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.preferences.uiScaleDesc"
                        fallback: "Adjust UI density. Lower = smaller elements, more detail."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                    }

                    Item { Layout.fillHeight: true }

                    ValueInput {
                        id: scaleInput
                        from: 0.5
                        to: 2.0
                        stepSize: 0.01
                        decimals: 2
                        value: Theme.scaleMultiplier

                        onValueModified: function(newValue) {
                            Theme.scaleMultiplier = newValue
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        // Middle column: Battery / Charging and Water Level stacked
        ColumnLayout {
            Layout.preferredWidth: Theme.scaled(300)
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

            // Battery / Charging settings
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.preferences.batteryCharging"
                        fallback: "Battery Charging"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.preferences.batteryChargingDesc"
                        fallback: "Control the USB charger output from the DE1"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

                    Item { height: 5 }

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
                            border.width: Theme.scaled(1)

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

                    Item { height: 10 }

                    // Smart charging mode selector
                    Tr {
                        key: "settings.preferences.smartChargingMode"
                        fallback: "Smart Charging Mode"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Repeater {
                            model: [
                                { value: 0, label: TranslationManager.translate("settings.preferences.chargingOff", "Off"), desc: TranslationManager.translate("settings.preferences.alwaysCharging", "Always charging") },
                                { value: 1, label: TranslationManager.translate("settings.preferences.chargingOn", "On"), desc: "55-65%" },
                                { value: 2, label: TranslationManager.translate("settings.preferences.chargingNight", "Night"), desc: "90-95%" }
                            ]

                            delegate: Rectangle {
                                id: chargingModeButton
                                Layout.fillWidth: true
                                height: Theme.scaled(50)
                                radius: Theme.scaled(6)
                                color: BatteryManager.chargingMode === modelData.value ?
                                       Theme.primaryColor : Theme.backgroundColor
                                border.color: BatteryManager.chargingMode === modelData.value ?
                                              Theme.primaryColor : Theme.textSecondaryColor
                                border.width: Theme.scaled(1)

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

                    Item { height: 5 }

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

                    Item { Layout.fillHeight: true }

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
                            onClicked: DE1Device.setUsbChargerOn(checked)
                        }
                    }

                    // Battery drain button for testing
                    AccessibleButton {
                        Layout.fillWidth: true
                        text: BatteryDrainer.running ? TranslationManager.translate("settings.preferences.draining", "DRAINING... (tap to stop)") : TranslationManager.translate("settings.preferences.drainBattery", "Drain Battery (Test)")
                        accessibleName: BatteryDrainer.running ? "Stop battery drain test" : "Start battery drain test"
                        background: Rectangle {
                            radius: Theme.scaled(6)
                            color: BatteryDrainer.running ? Theme.errorColor : Theme.backgroundColor
                            border.color: Theme.errorColor
                            border.width: Theme.scaled(1)
                        }
                        contentItem: Text {
                            text: parent.text
                            color: BatteryDrainer.running ? "white" : Theme.errorColor
                            font.pixelSize: Theme.scaled(12)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: BatteryDrainer.toggle()
                    }
                }
            }

            // Water Level Calibration
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(140)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.preferences.waterLevelCalibration"
                        fallback: "Water Level Sensor"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.preferences.waterLevelCalibrationDesc"
                        fallback: "Auto-calibrates after two refill cycles"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                    }

                    // Current calibration values
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(15)

                        Text {
                            text: TranslationManager.translate("settings.preferences.min", "Min:") + " " + Settings.waterLevelMinMm.toFixed(0) + "mm"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                        }

                        Text {
                            text: TranslationManager.translate("settings.preferences.max", "Max:") + " " + Settings.waterLevelMaxMm.toFixed(0) + "mm"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                        }

                        Text {
                            text: TranslationManager.translate("settings.preferences.now", "Now:") + " " + DE1Device.waterLevelMm.toFixed(0) + "mm"
                            color: Theme.primaryColor
                            font.pixelSize: Theme.scaled(11)
                        }
                    }

                    // Reset button
                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.preferences.resetCalibration", "Reset Calibration")
                        accessibleName: "Reset water level calibration to default values"
                        background: Rectangle {
                            radius: Theme.scaled(6)
                            color: Theme.backgroundColor
                            border.color: Theme.textSecondaryColor
                            border.width: Theme.scaled(1)
                        }
                        contentItem: Text {
                            text: parent.text
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(11)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: Settings.resetWaterLevelCalibration()
                    }
                }
            }
        }

        // Right column: Flow Sensor Calibration and Steam Heater stacked
        ColumnLayout {
            Layout.preferredWidth: Theme.scaled(300)
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

            // Flow Sensor Calibration
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(180)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.preferences.flowSensorCalibration"
                        fallback: "Flow Sensor Calibration"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.preferences.flowSensorCalibrationDesc"
                        fallback: "Calibrate the virtual scale for users without a BLE scale"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

                    // Current factor display
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.preferences.currentFactor"
                            fallback: "Current factor:"
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
                        text: TranslationManager.translate("settings.preferences.startCalibration", "Start Calibration")
                        accessibleName: "Start flow sensor calibration"
                        enabled: DE1Device.connected
                        onClicked: preferencesTab.openFlowCalibrationDialog()
                        background: Rectangle {
                            radius: Theme.scaled(6)
                            color: parent.enabled ? Theme.primaryColor : Theme.backgroundColor
                            border.color: parent.enabled ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: Theme.scaled(1)
                        }
                        contentItem: Text {
                            text: parent.text
                            color: parent.enabled ? "white" : Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(14)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            // Steam Heater Settings
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
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
                        Layout.fillWidth: true
                        key: "settings.preferences.steamHeaterDesc"
                        fallback: "Keep the steam heater warm when machine is idle for faster steaming"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

                    Item { Layout.fillHeight: true }

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
                            onClicked: {
                                Settings.keepSteamHeaterOn = checked
                                MainController.applySteamSettings()
                            }

                            Accessible.role: Accessible.CheckBox
                            Accessible.name: "Keep steam heater on when idle"
                            Accessible.checked: checked
                        }
                    }
                }
            }
        }

        // Spacer
        Item { Layout.fillWidth: true }
    }
}
