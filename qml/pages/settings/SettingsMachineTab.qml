import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

KeyboardAwareContainer {
    id: machineTab
    textFields: [manualCityField]
    targetFlickable: contentFlickable

    // Local properties
    property int postShotReviewTimeout: Settings.value("postShotReviewTimeout", 31)
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

            // ========== LEFT COLUMN: Machine Settings ==========
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(350)
                Layout.alignment: Qt.AlignTop
                spacing: Theme.scaled(15)

                // Battery / Charging settings
                Rectangle {
                    objectName: "batteryCharging"
                    Layout.fillWidth: true
                    implicitHeight: batteryContent.implicitHeight + Theme.scaled(20)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: batteryContent
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(4)

                        Text {
                            text: TranslationManager.translate("settings.preferences.batteryCharging", "Battery Charging")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
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

                            Text {
                                text: TranslationManager.translate(
                                    BatteryManager.isCharging ? "settings.preferences.charging" : "settings.preferences.notCharging",
                                    BatteryManager.isCharging ? "Charging" : "Not charging")
                                color: BatteryManager.isCharging ? Theme.successColor : Theme.textSecondaryColor
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(12)
                            }
                        }

                        // Smart charging mode selector
                        Text {
                            text: TranslationManager.translate("settings.preferences.smartChargingMode", "Smart Charging Mode")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
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
                            font.family: Theme.captionFont.family
                            font.pixelSize: Theme.captionFont.pixelSize
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        // USB port requirement note
                        Text {
                            visible: BatteryManager.chargingMode !== 0
                            text: TranslationManager.translate("settings.preferences.chargingUsbNote", "Controls the USB port on the front of the DE1 to manage charging.")
                            color: Theme.warningColor
                            font.family: Theme.captionFont.family
                            font.pixelSize: Theme.captionFont.pixelSize
                            font.italic: true
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        // Manual charger toggle
                        RowLayout {
                            Layout.fillWidth: true
                            visible: BatteryManager.chargingMode === 0

                            Text {
                                text: TranslationManager.translate("settings.preferences.usbCharger", "USB Charger")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
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

                // Shot Map Settings
                Rectangle {
                    objectName: "shotMap"
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

                        Text {
                            text: TranslationManager.translate("settings.shotmap.title", "Shot Map")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.shotmap.description", "Share your shots on the global map at decenza.coffee")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.shotmap.enable", "Enable Shot Map")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
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
                            Layout.fillWidth: true
                            visible: MainController.shotReporter && MainController.shotReporter.enabled
                                 && MainController.shotReporter.hasLocation
                            text: {
                                if (!MainController.shotReporter) return ""
                                var city = MainController.shotReporter.currentCity()
                                var country = MainController.shotReporter.currentCountryCode()
                                var prefix = MainController.shotReporter.usingManualCity ? TranslationManager.translate("settings.preferences.manualPrefix", "Manual") + ": " : TranslationManager.translate("settings.preferences.gpsPrefix", "GPS") + ": "
                                var lat = MainController.shotReporter.latitude.toFixed(1)
                                var lon = MainController.shotReporter.longitude.toFixed(1)
                                return prefix + city + (country ? ", " + country : "") + " (" + lat + ", " + lon + ")"
                            }
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        // Location unavailable - clickable to request permission / open settings
                        Text {
                            Layout.fillWidth: true
                            visible: MainController.shotReporter && MainController.shotReporter.enabled
                                 && !MainController.shotReporter.hasLocation
                            text: {
                                if (Qt.platform.os === "android") {
                                    if (MainController.shotReporter.isGpsEnabled())
                                        return TranslationManager.translate("settings.preferences.gpsAcquiring", "Acquiring location…")
                                    return TranslationManager.translate("settings.preferences.gpsDisabled", "GPS disabled - tap to open Settings")
                                }
                                return TranslationManager.translate("settings.preferences.noLocation", "No location - tap to enable")
                            }
                            color: Theme.primaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            font.underline: true
                            wrapMode: Text.WordWrap

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (!MainController.shotReporter) return
                                    // refreshLocation() handles the permission prompt on macOS/iOS;
                                    // openLocationSettings() is for Android (GPS system toggle)
                                    MainController.shotReporter.refreshLocation()
                                    if (Qt.platform.os === "android" && !MainController.shotReporter.isGpsEnabled())
                                        MainController.shotReporter.openLocationSettings()
                                }
                            }
                        }

                        // Manual city input (shown when enabled)
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: MainController.shotReporter && MainController.shotReporter.enabled
                            spacing: Theme.scaled(5)

                            Text {
                                text: TranslationManager.translate("settings.preferences.manualCityLabel", "Manual city (fallback if GPS unavailable):")
                                color: Theme.textSecondaryColor
                                font.family: Theme.captionFont.family
                                font.pixelSize: Theme.captionFont.pixelSize
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(8)

                                StyledTextField {
                                    id: manualCityField
                                    Layout.fillWidth: true
                                    placeholder: TranslationManager.translate("settings.preferences.cityPlaceholder", "e.g. Copenhagen, Denmark")
                                    text: MainController.shotReporter ? MainController.shotReporter.manualCity : ""
                                    onEditingFinished: {
                                        if (MainController.shotReporter) {
                                            MainController.shotReporter.manualCity = text
                                        }
                                    }
                                }

                                AccessibleButton {
                                    text: MainController.shotReporter && MainController.shotReporter.hasLocation ? TranslationManager.translate("settings.preferences.test", "Test") : TranslationManager.translate("settings.preferences.retry", "Retry")
                                    accessibleName: MainController.shotReporter && MainController.shotReporter.hasLocation
                                        ? TranslationManager.translate("settings.options.testLocation", "Test location on shot map")
                                        : TranslationManager.translate("settings.preferences.retryLocation", "Retry location request")
                                    onClicked: {
                                        if (MainController.shotReporter && MainController.shotReporter.hasLocation) {
                                            mapTestPopup.open()
                                        } else if (MainController.shotReporter) {
                                            MainController.shotReporter.refreshLocation()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

            }

            // ========== MIDDLE COLUMN: App Behavior ==========
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(350)
                Layout.alignment: Qt.AlignTop
                spacing: Theme.scaled(15)

                // Theme mode preferences card
                Rectangle {
                    objectName: "themeMode"
                    Layout.fillWidth: true
                    implicitHeight: themeModeColumn.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: themeModeColumn
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(10)

                        Text {
                            text: TranslationManager.translate("settings.preferences.themeMode", "Theme Mode")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        // Follow system theme toggle
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(15)

                            Text {
                                text: TranslationManager.translate("settings.preferences.followSystem", "Follow system theme")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                id: followSystemSwitch
                                checked: Settings.themeMode === "system"
                                accessibleName: TranslationManager.translate("settings.preferences.followSystem", "Follow system theme")
                                onCheckedChanged: {
                                    if (checked) {
                                        Settings.themeMode = "system"
                                    } else {
                                        Settings.themeMode = Settings.isDarkMode ? "dark" : "light"
                                    }
                                }
                            }
                        }

                        // Dark theme selector
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(15)

                            Text {
                                text: TranslationManager.translate("settings.preferences.darkTheme", "Dark theme")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledComboBox {
                                id: darkThemeCombo
                                Layout.preferredWidth: Theme.scaled(180)
                                accessibleLabel: TranslationManager.translate("settings.preferences.darkTheme", "Dark theme")
                                model: Settings.themeNames
                                currentIndex: Math.max(0, Settings.themeNames.indexOf(Settings.darkThemeName))
                                onActivated: Settings.applyDarkTheme(Settings.themeNames[currentIndex])
                            }
                        }

                        // Light theme selector
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(15)

                            Text {
                                text: TranslationManager.translate("settings.preferences.lightTheme", "Light theme")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledComboBox {
                                id: lightThemeCombo
                                Layout.preferredWidth: Theme.scaled(180)
                                accessibleLabel: TranslationManager.translate("settings.preferences.lightTheme", "Light theme")
                                model: Settings.themeNames
                                currentIndex: Math.max(0, Settings.themeNames.indexOf(Settings.lightThemeName))
                                onActivated: Settings.applyLightTheme(Settings.themeNames[currentIndex])
                            }
                        }
                    }
                }

                // Extraction View Mode
                Rectangle {
                    objectName: "extractionView"
                    Layout.fillWidth: true
                    implicitHeight: extractionViewContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: extractionViewContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(10)

                        property string currentMode: Settings.value("espresso/extractionView", "chart")

                        Connections {
                            target: Settings
                            function onValueChanged(key) {
                                if (key === "espresso/extractionView") {
                                    extractionViewContent.currentMode = Settings.value("espresso/extractionView", "chart")
                                }
                            }
                        }

                        Text {
                            text: TranslationManager.translate("settings.preferences.extractionView", "Extraction View")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.preferences.extractionViewDesc", "Visualization during espresso extraction")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: ListModel {
                                ListElement { mode: "chart"; icon: "qrc:/icons/Graph.svg"; labelKey: "settings.preferences.viewChart"; labelFallback: "Shot Chart" }
                                ListElement { mode: "cupFill"; icon: "qrc:/icons/espresso.svg"; labelKey: "settings.preferences.viewCupFill"; labelFallback: "Cup Fill" }
                            }

                            delegate: Rectangle {
                                id: viewOptionCard
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(44)
                                radius: Theme.scaled(8)
                                color: extractionViewContent.currentMode === model.mode
                                    ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                                    : Theme.backgroundColor
                                border.color: extractionViewContent.currentMode === model.mode
                                    ? Theme.primaryColor : Theme.borderColor
                                border.width: extractionViewContent.currentMode === model.mode
                                    ? Theme.scaled(2) : Theme.scaled(1)

                                Accessible.ignored: true

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.scaled(12)
                                    anchors.rightMargin: Theme.scaled(12)
                                    spacing: Theme.scaled(10)

                                    Image {
                                        source: model.icon
                                        sourceSize.width: Theme.scaled(20)
                                        sourceSize.height: Theme.scaled(20)
                                        Layout.alignment: Qt.AlignVCenter
                                    }

                                    Text {
                                        text: TranslationManager.translate(model.labelKey, model.labelFallback)
                                        color: Theme.textColor
                                        font.family: Theme.bodyFont.family
                                        font.pixelSize: Theme.bodyFont.pixelSize
                                        Layout.fillWidth: true
                                        Accessible.ignored: true
                                    }

                                    // Radio indicator
                                    Rectangle {
                                        width: Theme.scaled(18)
                                        height: Theme.scaled(18)
                                        radius: Theme.scaled(9)
                                        border.color: extractionViewContent.currentMode === model.mode
                                            ? Theme.primaryColor : Theme.textSecondaryColor
                                        border.width: Theme.scaled(2)
                                        color: "transparent"
                                        Layout.alignment: Qt.AlignVCenter

                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: Theme.scaled(8)
                                            height: Theme.scaled(8)
                                            radius: Theme.scaled(4)
                                            color: Theme.primaryColor
                                            visible: extractionViewContent.currentMode === model.mode
                                        }
                                    }
                                }

                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    accessibleName: TranslationManager.translate(model.labelKey, model.labelFallback)
                                    accessibleItem: viewOptionCard
                                    onAccessibleClicked: {
                                        extractionViewContent.currentMode = model.mode
                                        Settings.setValue("espresso/extractionView", model.mode)
                                    }
                                }
                            }
                        }
                    }
                }

                // Post-shot review auto-close
                Rectangle {
                    objectName: "shotReviewTimer"
                    Layout.fillWidth: true
                    implicitHeight: postShotContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: postShotContent
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(10)

                        Text {
                            text: TranslationManager.translate("settings.machine.shotReviewTimer", "Shot Review Timer")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.machine.shotReviewTimerDesc", "Return to idle after reviewing shot")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        Item { Layout.fillHeight: true }

                        ValueInput {
                            id: postShotReviewInput
                            Layout.fillWidth: true
                            from: 0
                            to: 31
                            stepSize: 1
                            decimals: 0
                            value: machineTab.postShotReviewTimeout
                            displayText: value === 0
                                ? TranslationManager.translate("settings.preferences.instant", "Instant")
                                : value === 31
                                    ? TranslationManager.translate("settings.preferences.never", "Never")
                                    : (value + " " + TranslationManager.translate("settings.preferences.min", "min"))
                            accessibleName: TranslationManager.translate("settings.machine.shotReviewTimer", "Shot Review Timer")

                            onValueModified: function(newValue) {
                                machineTab.postShotReviewTimeout = newValue
                                Settings.setValue("postShotReviewTimeout", newValue)
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // Screen Zoom Configuration
                Rectangle {
                    objectName: "screenZoom"
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

                        Text {
                            text: TranslationManager.translate("settings.machine.screenZoom", "Screen Zoom")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.machine.screenZoomDesc", "Make text and controls larger or smaller on each screen")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(10)

                            Text {
                                text: TranslationManager.translate("settings.machine.configureZoomPerScreen", "Configure zoom per screen")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: machineTab.configurePageScaleEnabled
                                accessibleName: TranslationManager.translate("settings.machine.configureZoom", "Configure zoom per screen")
                                onClicked: {
                                    console.log("Switch clicked, checked =", checked)
                                    Settings.setValue("ui/configurePageScale", checked)
                                    // Also set Theme directly as fallback
                                    Theme.configurePageScaleEnabled = checked
                                    console.log("Theme.configurePageScaleEnabled =", Theme.configurePageScaleEnabled)
                                }
                            }
                        }

                        Text {
                            visible: machineTab.configurePageScaleEnabled
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.machine.zoomHint", "Navigate to each screen and use the floating control to adjust its zoom.")
                            color: Theme.primaryColor
                            font.family: Theme.captionFont.family
                            font.pixelSize: Theme.captionFont.pixelSize
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Launcher Mode (Android only)
                Rectangle {
                    objectName: "launcherMode"
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

                        Text {
                            text: TranslationManager.translate("settings.options.launcherMode", "Launcher Mode")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.options.launcherModeDesc", "Set Decenza as the Android home screen. Press Home to return here instead of the default launcher.")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.options.useAsLauncher", "Use as Home Screen")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
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

                        Text {
                            Layout.fillWidth: true
                            visible: Settings.launcherMode
                            text: TranslationManager.translate("settings.options.launcherModeHint", "Android will ask you to choose a default launcher. Select Decenza and tap \"Always\".")
                            color: Theme.warningColor
                            font.family: Theme.captionFont.family
                            font.pixelSize: Theme.captionFont.pixelSize
                            wrapMode: Text.WordWrap
                        }
                    }
                }

            }

            // ========== RIGHT COLUMN: Water & Features ==========
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(350)
                Layout.alignment: Qt.AlignTop
                spacing: Theme.scaled(15)

                // Water Level Status
                Rectangle {
                    id: waterLevelCard
                    objectName: "waterLevel"
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

                            Text {
                                text: TranslationManager.translate("settings.options.waterLevel", "Water Level")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
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

                                ColoredIcon {
                                    anchors.centerIn: parent
                                    source: "qrc:/icons/tick.svg"
                                    iconWidth: Theme.scaled(12)
                                    iconHeight: Theme.scaled(12)
                                    iconColor: Theme.successColor
                                }
                            }

                            Text {
                                text: TranslationManager.translate("settings.options.refillKitActive", "Auto-refill active")
                                color: Theme.successColor
                                font.family: Theme.bodyFont.family
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
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.scaled(4)
                                    visible: waterLevelCard.refillKitActive && DE1Device.waterLevelMm < 10

                                    Image {
                                        source: Theme.emojiToImage("\u26A0")
                                        sourceSize.width: Theme.scaled(12)
                                        sourceSize.height: Theme.scaled(12)
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: TranslationManager.translate("settings.options.refillKitMalfunction",
                                            "Water critically low despite refill kit - check kit connection")
                                        color: Theme.errorColor
                                        font.pixelSize: Theme.scaled(12)
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }

                        // Display unit toggle
                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.options.showInMl", "Show in milliliters (ml)")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
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
                    objectName: "waterRefillThreshold"
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
                            font.family: Theme.bodyFont.family
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

                // Refill Kit
                Rectangle {
                    objectName: "refillKit"
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

                        Text {
                            text: TranslationManager.translate("settings.preferences.refillKit", "Refill Kit")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.preferences.refillKitDesc", "Control whether the machine uses an automatic water refill kit")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
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
                            font.family: Theme.bodyFont.family
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

                // Headless Machine Settings (only visible on headless machines)
                Rectangle {
                    objectName: "headlessMachine"
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

                        Text {
                            text: TranslationManager.translate("settings.options.headlessMachine", "Headless Machine")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.options.singlePressStopPurge", "Single press to stop & purge")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
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

                // Steam Heater Settings
                Rectangle {
                    objectName: "steamHeater"
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

                        Text {
                            text: TranslationManager.translate("settings.preferences.steamHeater", "Steam Heater")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.preferences.steamHeaterDesc", "Pre-heat for faster steaming")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            property real temp: typeof DE1Device.steamTemperature === 'number' ? DE1Device.steamTemperature : 0
                            text: TranslationManager.translate("settings.preferences.current", "Current:") + " " + temp.toFixed(0) + "°C"
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.preferences.keepSteamHeaterOn", "Keep heater on when idle")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
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
                                text: TranslationManager.translate("settings.preferences.autoFlushAfter", "Auto flush wand after")
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
                                displayText: value === 0 ? TranslationManager.translate("common.off", "Off") : value + TranslationManager.translate("common.unit.seconds", "s")
                                accessibleName: TranslationManager.translate("settings.preferences.autoFlushDuration", "Auto flush duration")
                                onValueModified: function(newValue) {
                                    Settings.steamAutoFlushSeconds = newValue
                                }
                            }
                        }
                    }
                }

                // Simulation Mode
                Rectangle {
                    objectName: "simulationMode"
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

                        Text {
                            text: TranslationManager.translate("settings.machine.simulationModeTitle", "Simulation Mode")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.machine.simulationModeDesc", "Use the app without a connected DE1 machine")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.machine.simulationMode", "Simulation Mode")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
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
                                    font.family: Theme.captionFont.family
                                    font.pixelSize: Theme.captionFont.pixelSize
                                    font.bold: true
                                }

                                Accessible.role: Accessible.StaticText
                                Accessible.name: TranslationManager.translate("settings.preferences.simulationModeActive", "Simulation mode is active")
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: Settings.simulationMode
                                accessibleName: TranslationManager.translate("settings.machine.simulationMode", "Simulation Mode")
                                onToggled: {
                                    // Save to persistent Settings — takes effect on next launch
                                    Settings.simulationMode = checked
                                }
                            }
                        }

                        Text {
                            id: restartRequiredText
                            visible: Settings.simulationMode !== DE1Device.simulationMode
                            text: TranslationManager.translate("settings.preferences.restartRequired", "Restart required for this change to take effect")
                            color: Theme.warningColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            Layout.leftMargin: Theme.scaled(15)
                            Layout.bottomMargin: Theme.scaled(5)
                        }
                    }
                }

                // Pocket Integration (remote control via Pocket app)
                Rectangle {
                    objectName: "pocketIntegration"
                    Layout.fillWidth: true
                    implicitHeight: pocketContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: pocketContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(8)

                        Text {
                            text: TranslationManager.translate("settings.machine.pocketIntegrationTitle", "Pocket Integration")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.machine.pocketIntegrationDesc", "Allow the Pocket app to view and control your screen remotely. Requires an active Pocket pairing.")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.machine.pocketIntegration", "Enable Pocket Integration")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: Settings.screenCaptureEnabled
                                accessibleName: TranslationManager.translate("settings.machine.pocketIntegration", "Enable Pocket Integration")
                                onToggled: {
                                    Settings.screenCaptureEnabled = checked
                                }
                            }
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

        Accessible.role: Accessible.Button
        Accessible.name: TranslationManager.translate("accessibility.scrolldown", "Scroll down")
        Accessible.focusable: true
        Accessible.onPressAction: scrollDownArea.clicked(null)

        Text {
            anchors.centerIn: parent
            text: "\u2193"
            color: "white"
            font.pixelSize: Theme.scaled(16)
            font.bold: true
            Accessible.ignored: true
        }

        MouseArea {
            id: scrollDownArea
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
    Dialog {
        id: mapTestPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, Theme.scaled(800))
        height: Math.min(parent.height * 0.8, Theme.scaled(500))
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

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
                    text: TranslationManager.translate("settings.preferences.locationOnMap", "Your location on the Shot Map")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("common.button.close", "Close")
                    accessibleName: TranslationManager.translate("settings.options.closeMapTest", "Close map test popup")
                    onClicked: mapTestPopup.close()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.backgroundColor
                radius: Theme.cardRadius
                clip: true

                Loader {
                    id: mapLoader
                    anchors.fill: parent
                    active: mapTestPopup.visible && Settings.hasQuick3D
                    source: "qrc:/qt/qml/Decenza/qml/components/ShotMapScreensaver.qml"
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
                wrapMode: Text.WordWrap
            }
        }
    }

}

