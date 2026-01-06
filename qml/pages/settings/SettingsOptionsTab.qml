import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: optionsTab

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: Offline Mode and Steam Heater
        ColumnLayout {
            Layout.preferredWidth: Theme.scaled(350)
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

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

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            checked: DE1Device.simulationMode
                            onToggled: {
                                DE1Device.simulationMode = checked
                                if (ScaleDevice) {
                                    ScaleDevice.simulationMode = checked
                                }
                            }
                        }
                    }
                }
            }

            // Water Level Display
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: waterLevelContent.implicitHeight + Theme.scaled(30)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: waterLevelContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.options.waterLevelDisplay"
                        fallback: "Water Level Display"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.options.waterLevelDisplayDesc"
                        fallback: "Choose how to display the water tank level"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

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
                            onToggled: {
                                Settings.waterLevelDisplayUnit = checked ? "ml" : "percent"
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
                        Layout.fillWidth: true
                        key: "settings.preferences.steamHeaterDesc"
                        fallback: "Keep the steam heater warm when machine is idle for faster steaming"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
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

            Item { Layout.fillHeight: true }
        }

        // Right column: Headless Machine Settings
        ColumnLayout {
            Layout.preferredWidth: Theme.scaled(350)
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

            // Headless Machine Settings
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: headlessContent.implicitHeight + Theme.scaled(30)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

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

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.options.headlessMachineDesc"
                        fallback: "Settings for DE1 machines without a touchscreen (headless mode)"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.textSecondaryColor
                        opacity: 0.3
                    }

                    Tr {
                        key: "settings.options.steamStopBehavior"
                        fallback: "Steam Stop Behavior"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.options.steamStopBehaviorDesc"
                        fallback: "Controls what happens when you press STOP while steaming"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
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
                            onClicked: {
                                Settings.headlessSkipPurgeConfirm = checked
                            }
                        }
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: Settings.headlessSkipPurgeConfirm ?
                            "settings.options.singlePressDesc" :
                            "settings.options.twoPressDesc"
                        fallback: Settings.headlessSkipPurgeConfirm ?
                            "STOP immediately stops steam and triggers auto-purge (2 second delay)" :
                            "First press stops steam, second press (PURGE) triggers the purge sequence"
                        color: Theme.primaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        // Spacer
        Item { Layout.fillWidth: true }
    }
}
