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

        // Left column: Offline Mode, Shot Map
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
                                // Save to persistent Settings
                                Settings.simulationMode = checked
                                // Also set on devices for current session
                                DE1Device.simulationMode = checked
                                if (ScaleDevice) {
                                    ScaleDevice.simulationMode = checked
                                }
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
                                accessibleName: TranslationManager.translate("settings.options.testLocation", "Test location on shot map")
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

            Item { Layout.fillHeight: true }
        }

        // Middle column: Water Level, Water Refill, Headless Machine (conditional)
        ColumnLayout {
            Layout.preferredWidth: Theme.scaled(350)
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

            // Water Level Status (always shown, but content varies based on refill kit)
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: waterLevelContent.implicitHeight + Theme.scaled(30)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                // Refill kit is active if: forced ON (1), or auto-detect (2) AND detected
                // NOT active if: forced OFF (0), or auto-detect (2) AND not detected
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
                            visible: parent.parent.parent.refillKitActive

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
                            visible: parent.parent.parent.refillKitActive
                        }
                    }

                    // Current water level display
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: levelDisplay.implicitHeight + Theme.scaled(20)
                        color: {
                            var level = DE1Device.waterLevelMm
                            // Warn if critically low (< 10mm) when refill kit is active
                            if (parent.parent.refillKitActive && level < 10) {
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
                                visible: parent.parent.parent.parent.refillKitActive && DE1Device.waterLevelMm < 10
                            }
                        }
                    }

                    // Display unit toggle (always shown - user preference regardless of refill kit)
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

            // Water Refill Threshold (only when refill kit is not active - manual refill only)
            Rectangle {
                id: waterRefillThresholdCard
                Layout.fillWidth: true
                implicitHeight: refillContent.implicitHeight + Theme.scaled(30)
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                // Show threshold controls when refill kit is NOT active (respects force-off override)
                visible: {
                    var override = Settings.refillKitOverride
                    var detected = DE1Device.refillKitDetected === 1
                    // Hide if: forced ON (1), or auto-detect (2) AND detected
                    // Show if: forced OFF (0), or auto-detect (2) AND not detected
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

            // Spacer
            Item { Layout.fillHeight: true }
        }

        // Spacer
        Item { Layout.fillWidth: true }
    }
}
