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

    // Local property bound to Theme (single source of truth)
    property bool configurePageScaleEnabled: Theme.configurePageScaleEnabled

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: Auto-sleep and Per-Screen Scale
        ColumnLayout {
            Layout.preferredWidth: Theme.scaled(350)
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

            // Per-Screen Scale Configuration
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

                    Item { Layout.fillHeight: true }

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
        }

        // Middle column: Battery / Charging
        ColumnLayout {
            Layout.preferredWidth: Theme.scaled(350)
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

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

        }

        // Right column: Flow Sensor Calibration
        ColumnLayout {
            Layout.preferredWidth: Theme.scaled(350)
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

            // Flow Sensor Calibration
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: flowCalContent.implicitHeight + Theme.scaled(30)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: flowCalContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
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

                    // Calibration buttons
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(10)

                        AccessibleButton {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.preferences.startCalibration", "Start Calibration")
                            accessibleName: "Start flow sensor calibration"
                            enabled: DE1Device.guiEnabled
                            onClicked: preferencesTab.openFlowCalibrationDialog()
                            background: Rectangle {
                                radius: Theme.scaled(6)
                                color: parent.enabled ? Theme.primaryColor : Theme.backgroundColor
                                border.color: parent.enabled ? Theme.primaryColor : Theme.textSecondaryColor
                                border.width: 1
                            }
                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled ? "white" : Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(14)
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        AccessibleButton {
                            Layout.preferredWidth: Theme.scaled(80)
                            text: qsTr("Reset")
                            accessibleName: "Reset flow calibration to 1.0"
                            enabled: Settings.flowCalibrationFactor !== 1.0
                            onClicked: Settings.flowCalibrationFactor = 1.0
                            background: Rectangle {
                                radius: Theme.scaled(6)
                                color: "transparent"
                                border.color: parent.enabled ? Theme.textSecondaryColor : Theme.backgroundColor
                                border.width: 1
                            }
                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled ? Theme.textColor : Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(14)
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }

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
                            Layout.preferredHeight: Theme.scaled(36)
                            from: 0
                            to: 60
                            stepSize: 1
                            decimals: 0
                            value: Settings.steamAutoFlushSeconds
                            valueColor: value > 0 ? Theme.primaryColor : Theme.textSecondaryColor
                            displayText: value === 0 ? "Off" : value + "s"
                            accessibleName: qsTr("Auto flush duration")
                            onValueModified: function(newValue) {
                                Settings.steamAutoFlushSeconds = newValue
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        // Spacer
        Item { Layout.fillWidth: true }
    }
}
