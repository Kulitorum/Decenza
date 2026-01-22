import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

KeyboardAwareContainer {
    id: optionsTab
    textFields: [manualCityField]

    RowLayout {
        width: parent.width
        height: parent.height
        spacing: Theme.scaled(15)

        // Left column: Offline Mode, Water Level Display, Steam Heater
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
                            accessibleName: TranslationManager.translate("settings.preferences.unlockGui", "Unlock GUI")
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
                            accessibleName: TranslationManager.translate("settings.preferences.keepSteamHeaterOn", "Keep heater on when idle")
                            onClicked: {
                                Settings.keepSteamHeaterOn = checked
                                // applySteamSettings() respects keepSteamHeaterOn setting:
                                // - In Ready state: always sends steam temp (machine heating)
                                // - In Idle with keepSteamHeaterOn=false: sends 0 to turn off heater
                                // - In Idle with keepSteamHeaterOn=true: sends steam temp
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

        // Middle column: Water Level, Headless Machine (conditional) + placeholder
        ColumnLayout {
            Layout.preferredWidth: Theme.scaled(350)
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

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
                            accessibleName: TranslationManager.translate("settings.options.showInMl", "Show in milliliters")
                            onToggled: {
                                Settings.waterLevelDisplayUnit = checked ? "ml" : "percent"
                            }
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
                                accessibleName: qsTr("Test location on shot map")
                                enabled: MainController.shotReporter && MainController.shotReporter.hasLocation
                                onClicked: mapTestPopup.open()
                            }
                        }
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
                            accessibleName: qsTr("Close map test popup")
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
                            text: qsTr("3D Map requires Qt Quick3D\n(not available on this platform)")
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

            // Spacer
            Item { Layout.fillHeight: true }
        }

        // Right column: Auto-Wake Timer
        ColumnLayout {
            Layout.preferredWidth: Theme.scaled(350)
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

            // Auto-Wake Timer Card
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: autoWakeContent.implicitHeight + Theme.scaled(30)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: autoWakeContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(12)

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
                                Layout.preferredHeight: Theme.scaled(36)
                                radius: Theme.scaled(6)

                                property bool isSelected: autoWakeContent.selectedDay === index
                                property bool isEnabled: {
                                    var sched = Settings.autoWakeSchedule
                                    return sched[index] ? sched[index].enabled : false
                                }

                                // Selected: lighter primary, Enabled: primary, Neither: background
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
                            Layout.preferredHeight: Theme.scaled(44)
                            from: 0
                            to: 23
                            stepSize: 1
                            decimals: 0
                            value: autoWakeContent.selectedDayData.hour ?? 7
                            enabled: autoWakeContent.selectedDayData.enabled ?? false
                            valueColor: enabled ? Theme.primaryColor : Theme.textSecondaryColor
                            displayText: value < 10 ? "0" + value.toFixed(0) : value.toFixed(0)
                            accessibleName: qsTr("Wake hour")
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
                            Layout.preferredHeight: Theme.scaled(44)
                            from: 0
                            to: 59
                            stepSize: 1
                            decimals: 0
                            value: autoWakeContent.selectedDayData.minute ?? 0
                            enabled: autoWakeContent.selectedDayData.enabled ?? false
                            valueColor: enabled ? Theme.primaryColor : Theme.textSecondaryColor
                            displayText: value < 10 ? "0" + value.toFixed(0) : value.toFixed(0)
                            accessibleName: qsTr("Wake minute")
                            onValueModified: function(newValue) {
                                Settings.setAutoWakeDayTime(autoWakeContent.selectedDay, autoWakeContent.selectedDayData.hour ?? 7, newValue)
                            }
                        }
                    }

                    // Separator
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.scaled(4)
                        height: 1
                        color: Theme.borderColor
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
                            Layout.preferredHeight: Theme.scaled(44)
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
                            accessibleName: qsTr("Stay awake duration")
                            onValueModified: function(newValue) {
                                Settings.autoWakeStayAwakeMinutes = newValue
                            }
                        }
                    }
                }
            }

            // Spacer
            Item { Layout.fillHeight: true }
        }

        // Spacer
        Item { Layout.fillWidth: true }
    }
}
