// The extraction-view Repeater delegate reads this file's `extractionViewCard` id;
// Bound makes it statically resolvable. It and the charging-mode and refill-kit
// delegates each declare the injected role they use required in the same edit (`model`
// for the ListModel-backed extraction-view list, `modelData` for the other two) --
// without that, Bound stops role injection and all three option rows render blank at
// RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

KeyboardAwareContainer {
    id: machineTab
    textFields: [manualCityField]
    targetFlickable: contentFlickable

    // Local properties
    readonly property int postShotReviewTimeout: Number(RecipeContext.preferences.postShotReviewTimeout ?? 31)
    readonly property bool configurePageScaleEnabled: appPreferenceWrite.displayedValue(
        "configurePageScale", machineTab.scalarPreference("app.configurePageScale", Theme.configurePageScaleEnabled))
    readonly property var scalarPreferences: SettingsPreferencesService.preferences

    function scalarPreference(key: string, fallback: var): var {
        var value = scalarPreferences[key]
        return value === undefined ? fallback : value
    }

    property var pendingThemeRequests: ({})

    // Maintenance card actions. Tabs load through a Loader, which breaks `root`
    // id resolution, so we emit signals that SettingsPage forwards to the global
    // navigation functions (mirrors the themes tab's openSaveThemeDialog).
    signal openDescaling()
    signal openTransport()

    function submitThemeSetting(operation, value) {
        if (!ThemeManagementService.ready) return
        const id = ThemeManagementService.newRequestId()
        const pending = Object.assign({}, pendingThemeRequests)
        pending[id] = operation
        pendingThemeRequests = pending
        ThemeManagementService.executeSetting(operation, { value: value },
                                             ThemeManagementService.revision, id)
    }

    PreferenceWrite {
        id: appPreferenceWrite
        domain: "app"
        onFailed: function(message) {
            Theme.configurePageScaleEnabled = machineTab.scalarPreference("app.configurePageScale", false)
            preferenceError.show(message)
        }
    }
    PreferenceWrite {
        id: hardwarePreferenceWrite
        domain: "hardware"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: brewPreferenceWrite
        domain: "brew"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: visualizerPreferenceWrite
        domain: "visualizer"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: batteryPreferenceWrite
        domain: "battery"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: shotMapPreferenceWrite
        domain: "shotMap"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: contextPreferenceWrite
        onFailed: function(message) { preferenceError.show(message) }
    }

    StatusToast { id: preferenceError; error: true }

    Connections {
        target: ThemeManagementService
        function onOperationCompleted(requestId, operation, success, revision, result, reason) {
            const pending = Object.assign({}, machineTab.pendingThemeRequests)
            if (pending[requestId] === undefined) return
            delete pending[requestId]
            machineTab.pendingThemeRequests = pending
            if (!success) preferenceError.show(reason)
        }
    }

    Connections {
        target: SettingsPreferencesService
        function onPreferencesChanged() {
            Theme.configurePageScaleEnabled = machineTab.scalarPreference("app.configurePageScale", false)
        }
    }

    Component.onCompleted: Theme.configurePageScaleEnabled = configurePageScaleEnabled

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
                SettingsCard {
                    searchId: "batteryCharging"
                    title: TranslationManager.translate("settings.preferences.batteryCharging", "Battery Charging")
                    description: TranslationManager.translate("settings.search.batteryDesc", "Smart charging mode for battery health")
                    keywords: ["battery", "charge", "usb", "power", "smart charging"]
                    contentMargins: Theme.scaled(10)
                    spacing: Theme.scaled(4)

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
                            text: HostStatusService.state.batteryPercent + "%"
                            color: HostStatusService.state.batteryPercent < 20 ? Theme.errorColor :
                                   HostStatusService.state.batteryPercent < 50 ? Theme.warningColor :
                                   Theme.successColor
                            font.bold: true
                        }

                        Rectangle {
                            Layout.preferredWidth: Theme.scaled(30)
                            Layout.preferredHeight: Theme.scaled(14)
                            radius: Theme.scaled(2)
                            color: "transparent"
                            border.color: Theme.textSecondaryColor
                            border.width: 1

                            Rectangle {
                                x: 2
                                y: 2
                                width: (parent.width - 4) * HostStatusService.state.batteryPercent / 100
                                height: parent.height - 4
                                radius: Theme.scaled(1)
                                color: HostStatusService.state.batteryPercent < 20 ? Theme.errorColor :
                                       HostStatusService.state.batteryPercent < 50 ? Theme.warningColor :
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
                                HostStatusService.state.isCharging ? "settings.preferences.charging" : "settings.preferences.notCharging",
                                HostStatusService.state.isCharging ? "Charging" : "Not charging")
                            color: HostStatusService.state.isCharging ? Theme.successColor : Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                        }
                    }

                    // USB charger switch — off keeps the DE1 USB port off and hides the modes
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: TranslationManager.translate("settings.preferences.usbCharger", "USB Charger")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            checked: batteryPreferenceWrite.displayedValue(
                                "usbChargerEnabled",
                                machineTab.scalarPreference("battery.usbChargerEnabled",
                                    HostStatusService.state.usbChargerEnabled))
                            enabled: batteryPreferenceWrite.ready
                            accessibleName: TranslationManager.translate("settings.preferences.usbCharger", "USB Charger")
                            onClicked: batteryPreferenceWrite.submit("usbChargerEnabled", checked)
                        }
                    }

                    // Smart charging mode selector
                    Text {
                        visible: batteryPreferenceWrite.displayedValue(
                                     "usbChargerEnabled",
                                     machineTab.scalarPreference("battery.usbChargerEnabled",
                                         HostStatusService.state.usbChargerEnabled))
                        text: TranslationManager.translate("settings.preferences.smartChargingMode", "Smart Charging Mode")
                        color: Theme.textSecondaryColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(12)
                    }

                    RowLayout {
                        visible: batteryPreferenceWrite.displayedValue(
                                     "usbChargerEnabled",
                                     machineTab.scalarPreference("battery.usbChargerEnabled",
                                         HostStatusService.state.usbChargerEnabled))
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(42)
                        spacing: Theme.scaled(8)

                        Repeater {
                            SettingsSearch.title: TranslationManager.translate("settings.preferences.smartChargingMode", "Smart Charging Mode")
                            model: [
                                { value: 0, label: TranslationManager.translate("settings.preferences.chargingOff", "Off"), desc: TranslationManager.translate("settings.preferences.alwaysCharging", "Always charging") },
                                { value: 1, label: TranslationManager.translate("settings.preferences.chargingOn", "On"), desc: "55-65%" },
                                { value: 2, label: TranslationManager.translate("settings.preferences.chargingNight", "Night"), desc: "90-95%" }
                            ]

                            delegate: Rectangle {
                                id: chargingModeButton
                                required property var modelData

                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: Theme.scaled(6)
                                color: batteryPreferenceWrite.displayedValue(
                                            "chargingMode",
                                            machineTab.scalarPreference("battery.chargingMode",
                                                HostStatusService.state.chargingMode)) === chargingModeButton.modelData.value ?
                                       Theme.primaryColor : Theme.backgroundColor
                                border.color: batteryPreferenceWrite.displayedValue(
                                                   "chargingMode",
                                                   machineTab.scalarPreference("battery.chargingMode",
                                                       HostStatusService.state.chargingMode)) === chargingModeButton.modelData.value ?
                                              Theme.primaryColor : Theme.textSecondaryColor
                                border.width: 1

                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: Theme.scaled(2)

                                    Text {
                                        text: chargingModeButton.modelData.label
                                        color: batteryPreferenceWrite.displayedValue(
                                                    "chargingMode",
                                                    machineTab.scalarPreference("battery.chargingMode",
                                                        HostStatusService.state.chargingMode)) === chargingModeButton.modelData.value ?
                                               Theme.primaryContrastColor : Theme.textColor
                                        font.pixelSize: Theme.scaled(14)
                                        font.bold: true
                                        Layout.alignment: Qt.AlignHCenter
                                    }

                                    Text {
                                        text: chargingModeButton.modelData.desc
                                        color: batteryPreferenceWrite.displayedValue(
                                                    "chargingMode",
                                                    machineTab.scalarPreference("battery.chargingMode",
                                                        HostStatusService.state.chargingMode)) === chargingModeButton.modelData.value ?
                                               Qt.rgba(1, 1, 1, 0.7) : Theme.textSecondaryColor
                                        font.pixelSize: Theme.scaled(10)
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                }

                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    accessibleName: TranslationManager.translate("settings.machine.accessible.chargingMode", "%1 charging mode. %2")
                                                        .arg(chargingModeButton.modelData.label).arg(chargingModeButton.modelData.desc)
                                                    + (batteryPreferenceWrite.displayedValue(
                                                           "chargingMode",
                                                           machineTab.scalarPreference("battery.chargingMode",
                                                               HostStatusService.state.chargingMode)) === chargingModeButton.modelData.value
                                                       ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                                    accessibleItem: chargingModeButton
                                    enabled: batteryPreferenceWrite.ready
                                    onAccessibleClicked: batteryPreferenceWrite.submit(
                                                             "chargingMode", chargingModeButton.modelData.value)
                                }
                            }
                        }
                    }

                    // Explanation text
                    Text {
                        text: !batteryPreferenceWrite.displayedValue(
                                  "usbChargerEnabled",
                                  machineTab.scalarPreference("battery.usbChargerEnabled",
                                      HostStatusService.state.usbChargerEnabled)) ?
                              TranslationManager.translate("settings.preferences.usbChargerOffDesc", "The DE1's USB port is kept off. Use this when this device is not charged from the DE1.") :
                              batteryPreferenceWrite.displayedValue(
                                  "chargingMode",
                                  machineTab.scalarPreference("battery.chargingMode",
                                      HostStatusService.state.chargingMode)) === 0 ?
                              TranslationManager.translate("settings.preferences.chargingOffDesc", "Charger is always on. Battery stays at 100%.") :
                              batteryPreferenceWrite.displayedValue(
                                  "chargingMode",
                                  machineTab.scalarPreference("battery.chargingMode",
                                      HostStatusService.state.chargingMode)) === 1 ?
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
                        visible: batteryPreferenceWrite.displayedValue(
                                     "usbChargerEnabled",
                                     machineTab.scalarPreference("battery.usbChargerEnabled",
                                         HostStatusService.state.usbChargerEnabled))
                              && batteryPreferenceWrite.displayedValue(
                                     "chargingMode",
                                     machineTab.scalarPreference("battery.chargingMode",
                                         HostStatusService.state.chargingMode)) !== 0
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

                }

                // Steam Heater Settings
                SettingsCard {
                    searchId: "steamHeater"
                    title: TranslationManager.translate("settings.preferences.steamHeater", "Steam Heater")
                    description: TranslationManager.translate("settings.preferences.steamHeaterDesc", "Pre-heat for faster steaming")
                    showDescription: true
                    keywords: ["steam", "heater", "flush", "auto", "temperature", "two-tap", "two tap", "purge", "stop", "headless"]
                    spacing: Theme.scaled(10)

                    Text {
                        property real temp: typeof DE1Device.steamTemperature === 'number' ? DE1Device.steamTemperature : 0
                        // "Off" from the resolved state, not from the measured
                        // temperature — a cooling boiler still reads hot.
                        text: TranslationManager.translate("settings.preferences.current", "Current:") + " "
                              + SteamLabels.temperatureText(!MainController.steamHeaterOn, temp)
                        color: Theme.textSecondaryColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(12)
                    }

                    // Two settings, not one three-way choice: they answer
                    // different questions and compose. "Keep warm when idle"
                    // is the baseline; "Let the recipe decide" is whether an
                    // active recipe's pitcher may override it.
                    //
                    // Each carries a caption because neither name says what
                    // it DOES to the boiler, and the pair's combined effect is
                    // the part users get wrong. The captions are the same
                    // sentences as the manual's four-state table, in the
                    // reader's own two rows rather than as a grid.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.preferences.keepWarmWhenIdle", "Keep warm when idle")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(14)

                                Accessible.ignored: true
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                id: steamHeaterSwitch
                                checked: brewPreferenceWrite.displayedValue(
                                             "keepWarmWhenIdle",
                                             machineTab.scalarPreference("brew.keepWarmWhenIdle", true))
                                enabled: brewPreferenceWrite.ready
                                accessibleName: TranslationManager.translate("settings.preferences.keepWarmWhenIdle", "Keep warm when idle")
                                // Spoken after the name, so a screen-reader user
                                // gets the same explanation the caption gives.
                                Accessible.description: keepWarmCaption.text
                                onClicked: {
                                    brewPreferenceWrite.submit("keepWarmWhenIdle", checked)
                                }
                            }
                        }

                        Text {
                            id: keepWarmCaption
                            Layout.fillWidth: true
                            Layout.rightMargin: Theme.scaled(60)
                            text: TranslationManager.translate(
                                "settings.preferences.keepWarmWhenIdleDesc",
                                "Holds the steam boiler at temperature the whole time the machine is awake. Off, it heats only when you steam.")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.preferences.letRecipeDecide", "Let the recipe decide")
                                color: Theme.textColor
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(14)

                                Accessible.ignored: true
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                id: letRecipeDecideSwitch
                                checked: brewPreferenceWrite.displayedValue(
                                             "letRecipeDecide",
                                             machineTab.scalarPreference("brew.letRecipeDecide", true))
                                enabled: brewPreferenceWrite.ready
                                accessibleName: TranslationManager.translate("settings.preferences.letRecipeDecide", "Let the recipe decide")
                                Accessible.description: letRecipeDecideCaption.text
                                onClicked: {
                                    brewPreferenceWrite.submit("letRecipeDecide", checked)
                                }
                            }
                        }

                        Text {
                            id: letRecipeDecideCaption
                            Layout.fillWidth: true
                            Layout.rightMargin: Theme.scaled(60)
                            text: TranslationManager.translate(
                                "settings.preferences.letRecipeDecideDesc",
                                "The active recipe overrules the setting above: one with a pitcher warms the boiler when its shot starts, one without keeps it cold.")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }

                    // What the two settings resolve to RIGHT NOW. The pair is
                    // where users get lost — the combination is not readable
                    // off two independent switches — so state the outcome
                    // rather than leaving it to be inferred.
                    Text {
                        Layout.fillWidth: true
                        text: {
                            var letRecipeDecide = brewPreferenceWrite.displayedValue(
                                        "letRecipeDecide", machineTab.scalarPreference("brew.letRecipeDecide", true))
                            var keepWarmWhenIdle = brewPreferenceWrite.displayedValue(
                                        "keepWarmWhenIdle", machineTab.scalarPreference("brew.keepWarmWhenIdle", true))
                            if (!letRecipeDecide) {
                                return keepWarmWhenIdle
                                    ? TranslationManager.translate("settings.preferences.steamSummaryAlways",
                                        "Now: the heater stays warm, and recipes are ignored.")
                                    : TranslationManager.translate("settings.preferences.steamSummaryManual",
                                        "Now: the heater runs only while you steam.")
                            }
                            return keepWarmWhenIdle
                                ? TranslationManager.translate("settings.preferences.steamSummaryWarmUnlessRecipe",
                                    "Now: the heater stays warm unless the active recipe has no pitcher.")
                                : TranslationManager.translate("settings.preferences.steamSummaryRecipeOnly",
                                    "Now: the heater warms when a recipe with a pitcher starts its shot.")
                        }
                        color: Theme.textSecondaryColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(12)
                        font.italic: true
                        wrapMode: Text.WordWrap
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
                            enabled: brewPreferenceWrite.ready
                            value: brewPreferenceWrite.displayedValue(
                                       "steamAutoFlushSeconds",
                                       machineTab.scalarPreference("brew.steamAutoFlushSeconds", 0))
                            valueColor: value > 0 ? Theme.primaryColor : Theme.textSecondaryColor
                            displayText: value === 0 ? TranslationManager.translate("common.off", "Off") : value + TranslationManager.translate("common.unit.seconds", "s")
                            accessibleName: TranslationManager.translate("settings.preferences.autoFlushDuration", "Auto flush duration")
                            onValueModified: function(newValue) {
                                brewPreferenceWrite.submit("steamAutoFlushSeconds", newValue)
                            }
                        }
                    }

                    // Two-tap stop: first tap puffs/soft-stops, second tap purges.
                    // Drives both the GHC firmware (via MMR) and the headless
                    // on-screen stop button. Default off (matches de1app firmware default).
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: TranslationManager.translate("settings.preferences.steamTwoTapStop", "Two-tap to stop steaming")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(14)

                            Accessible.ignored: true
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            id: steamTwoTapSwitch
                            checked: hardwarePreferenceWrite.displayedValue(
                                         "steamTwoTapStop",
                                         machineTab.scalarPreference("hardware.steamTwoTapStop", false))
                            enabled: hardwarePreferenceWrite.ready
                            accessibleName: TranslationManager.translate("settings.preferences.steamTwoTapStop", "Two-tap to stop steaming")
                            onClicked: hardwarePreferenceWrite.submit("steamTwoTapStop", checked)
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.preferences.steamTwoTapStopDesc", "First tap stops steam, second tap purges the wand")
                        color: Theme.textSecondaryColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }
                }

                // Shot Map Settings
                SettingsCard {
                    searchId: "shotMap"
                    title: TranslationManager.translate("settings.shotmap.title", "Shot Map")
                    description: TranslationManager.translate("settings.shotmap.description", "Share your shots on the global map at decenza.coffee")
                    showDescription: true
                    keywords: ["map", "location", "gps", "share", "global"]
                    spacing: Theme.scaled(10)

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
                            checked: shotMapPreferenceWrite.displayedValue(
                                         "enabled", machineTab.scalarPreference("shotMap.enabled", false))
                            enabled: shotMapPreferenceWrite.ready
                            accessibleName: TranslationManager.translate("settings.shotmap.enable", "Enable Shot Map")
                            onClicked: {
                                shotMapPreferenceWrite.submit("enabled", checked)
                                if (checked && !machineTab.scalarPreference("shotMap.gpsEnabled", false))
                                    SettingsPreferencesService.openShotMapLocationSettings()
                            }
                        }
                    }

                    // These fields are host-owned. Coordinates are rounded by
                    // ShotReporter before they cross the authenticated session.
                    Text {
                        Layout.fillWidth: true
                        visible: shotMapPreferenceWrite.displayedValue(
                                     "enabled", machineTab.scalarPreference("shotMap.enabled", false))
                                 && machineTab.scalarPreference("shotMap.hasLocation", false)
                        text: {
                            var city = machineTab.scalarPreference("shotMap.city", "")
                            var country = machineTab.scalarPreference("shotMap.countryCode", "")
                            var prefix = machineTab.scalarPreference("shotMap.usingManualCity", false)
                                ? TranslationManager.translate("settings.preferences.manualPrefix", "Manual") + ": "
                                : TranslationManager.translate("settings.preferences.gpsPrefix", "GPS") + ": "
                            var lat = Number(machineTab.scalarPreference("shotMap.latitude", 0)).toFixed(1)
                            var lon = Number(machineTab.scalarPreference("shotMap.longitude", 0)).toFixed(1)
                            return prefix + city + (country ? ", " + country : "") + " (" + lat + ", " + lon + ")"
                        }
                        color: Theme.textColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

                    // Location access and OS permissions stay on the host.
                    Text {
                        Layout.fillWidth: true
                        visible: shotMapPreferenceWrite.displayedValue(
                                     "enabled", machineTab.scalarPreference("shotMap.enabled", false))
                                 && !machineTab.scalarPreference("shotMap.hasLocation", false)
                        text: machineTab.scalarPreference("shotMap.gpsEnabled", false)
                            ? TranslationManager.translate("settings.preferences.gpsAcquiring", "Acquiring location…")
                            : TranslationManager.translate("settings.preferences.gpsDisabled", "Location is off - tap to open host Settings")
                        color: Theme.primaryColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(12)
                        font.underline: true
                        wrapMode: Text.WordWrap

                        AccessibleMouseArea {
                            id: locationEnableArea
                            anchors.fill: parent
                            accessibleName: parent.text
                            SettingsSearch.title: TranslationManager.translate("settings.search.locationAccess", "Location access")
                            onAccessibleClicked: {
                                SettingsPreferencesService.refreshShotMapLocation()
                                if (!machineTab.scalarPreference("shotMap.gpsEnabled", false))
                                    SettingsPreferencesService.openShotMapLocationSettings()
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: shotMapPreferenceWrite.displayedValue(
                                     "enabled", machineTab.scalarPreference("shotMap.enabled", false))
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
                                enabled: shotMapPreferenceWrite.ready
                                accessibleName: TranslationManager.translate("settings.preferences.manualCity.accessible", "Manual city for the shot map")
                                placeholder: TranslationManager.translate("settings.preferences.cityPlaceholder", "e.g. Copenhagen, Denmark")
                                text: shotMapPreferenceWrite.displayedValue(
                                          "manualCity", machineTab.scalarPreference("shotMap.manualCity", ""))
                                onEditingFinished: shotMapPreferenceWrite.submit("manualCity", text)
                            }

                            AccessibleButton {
                                text: machineTab.scalarPreference("shotMap.hasLocation", false)
                                    ? TranslationManager.translate("settings.preferences.test", "Test")
                                    : TranslationManager.translate("settings.preferences.retry", "Retry")
                                accessibleName: machineTab.scalarPreference("shotMap.hasLocation", false)
                                    ? TranslationManager.translate("settings.options.testLocation", "Test location on shot map")
                                    : TranslationManager.translate("settings.preferences.retryLocation", "Retry location request")
                                SettingsSearch.title: TranslationManager.translate("settings.options.testLocation", "Test location on shot map")
                                enabled: shotMapPreferenceWrite.ready
                                onClicked: {
                                    if (machineTab.scalarPreference("shotMap.hasLocation", false))
                                        mapTestPopup.open()
                                    else
                                        SettingsPreferencesService.refreshShotMapLocation()
                                }
                            }
                        }
                    }
                }

                // Maintenance card
                SettingsCard {
                    searchId: "maintenance"
                    title: TranslationManager.translate("settings.maintenance.title", "Maintenance")
                    description: TranslationManager.translate("settings.maintenance.description", "Keep your machine clean and ready for storage.")
                    keywords: ["maintenance", "descale", "descaling", "transport", "drain", "air purge", "clean", "storage", "travel", "vacation", "empty", "water"]
                    showHeader: false
                    spacing: Theme.scaled(10)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Image {
                            source: Theme.emojiToImage("🧰")
                            sourceSize.width: Theme.scaled(20)
                            sourceSize.height: Theme.scaled(20)
                            Accessible.ignored: true
                        }

                        Text {
                            text: TranslationManager.translate("settings.maintenance.title", "Maintenance")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.maintenance.description", "Keep your machine clean and ready for storage.")
                        color: Theme.textSecondaryColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

                    SettingsActionRow {
                        emoji: "🧽"
                        title: TranslationManager.translate("settings.maintenance.descale.title", "Descaling Wizard")
                        description: TranslationManager.translate("settings.maintenance.descale.desc", "Remove scale buildup from the boiler")
                        accessibleName: TranslationManager.translate("settings.maintenance.descale.accessible", "Open descaling wizard")
                        onTriggered: machineTab.openDescaling()
                    }

                    SettingsActionRow {
                        emoji: "🧳"
                        title: TranslationManager.translate("settings.maintenance.transport.title", "Transport Mode")
                        description: TranslationManager.translate("settings.maintenance.transport.desc", "Drain all water before storage or transport")
                        accessibleName: TranslationManager.translate("settings.maintenance.transport.accessible", "Open transport mode")
                        onTriggered: machineTab.openTransport()
                    }
                }

            }

            // ========== MIDDLE COLUMN: App Behavior ==========
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(350)
                Layout.alignment: Qt.AlignTop
                spacing: Theme.scaled(15)

                // Theme mode preferences card
                SettingsCard {
                    searchId: "themeMode"
                    title: TranslationManager.translate("settings.preferences.themeMode", "Theme Mode")
                    description: TranslationManager.translate("settings.search.themeModeDesc", "Dark mode, light mode, follow system")
                    keywords: ["dark", "light", "theme", "mode", "appearance", "system", "background", "wallpaper", "image", "photo", "screensaver", "idle"]
                    spacing: Theme.scaled(10)

                    // Follow system theme toggle
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(15)

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: TranslationManager.translate("settings.preferences.followSystem", "Follow system theme")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledSwitch {
                            id: followSystemSwitch
                            checked: ThemeManagementService.themeMode === "system"
                            accessibleName: TranslationManager.translate("settings.preferences.followSystem", "Follow system theme")
                            enabled: ThemeManagementService.ready
                            onClicked: machineTab.submitThemeSetting("setThemeMode",
                                checked ? "system" : (ThemeManagementService.darkMode ? "dark" : "light"))
                        }
                    }

                    // Dark theme selector
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(15)

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: TranslationManager.translate("settings.preferences.darkTheme", "Dark theme")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledComboBox {
                            id: darkThemeCombo
                            Layout.preferredWidth: Theme.scaled(170)
                            Layout.maximumWidth: Theme.scaled(170)
                            accessibleLabel: TranslationManager.translate("settings.preferences.darkTheme", "Dark theme")
                            model: ThemeManagementService.themeNames
                            currentIndex: Math.max(0, ThemeManagementService.themeNames.indexOf(ThemeManagementService.darkThemeName))
                            onActivated: machineTab.submitThemeSetting("applyDarkTheme", ThemeManagementService.themeNames[currentIndex])
                        }
                    }

                    // Light theme selector
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(15)

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: TranslationManager.translate("settings.preferences.lightTheme", "Light theme")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledComboBox {
                            id: lightThemeCombo
                            Layout.preferredWidth: Theme.scaled(170)
                            Layout.maximumWidth: Theme.scaled(170)
                            accessibleLabel: TranslationManager.translate("settings.preferences.lightTheme", "Light theme")
                            model: ThemeManagementService.themeNames
                            currentIndex: Math.max(0, ThemeManagementService.themeNames.indexOf(ThemeManagementService.lightThemeName))
                            onActivated: machineTab.submitThemeSetting("applyLightTheme", ThemeManagementService.themeNames[currentIndex])
                        }
                    }

                    // Glass chrome — an option rather than a theme, because
                    // translucency is orthogonal to light/dark: any theme can be
                    // glass. Works with the user's own colours, not just a built-in.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(15)

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: TranslationManager.translate("settings.preferences.glassChrome", "Glass chrome")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledSwitch {
                            checked: ThemeManagementService.glassChrome
                            accessibleName: TranslationManager.translate("settings.preferences.glassChrome", "Glass chrome")
                            enabled: ThemeManagementService.ready
                            onClicked: machineTab.submitThemeSetting("setGlassChrome", checked)
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.preferences.glassChromeHint",
                            "Softens the bars, tiles and controls. With a background image or the last-shot chart set, cards and dialogs also become translucent so the picture shows through — and it is always on in those cases.")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.Wrap
                    }

                    // Background (applied app-wide, both light and dark mode)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(15)

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: TranslationManager.translate("settings.preferences.background", "Background")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(14)
                        }

                        AccessibleButton {
                            // "Change" once anything is set — colour, image or shot
                            // chart; they are one choice made in one chooser. Asked of
                            // the SOURCE, not the individual parameters: testing the
                            // two legacy properties said "Choose…" while the shot-chart
                            // background was active, because that source sets neither.
                            text: ThemeManagementService.backgroundSource !== "none"
                                ? TranslationManager.translate("settings.preferences.backgroundChange", "Change…")
                                : TranslationManager.translate("settings.preferences.backgroundChoose", "Choose…")
                            accessibleName: TranslationManager.translate("settings.preferences.background", "Background")
                            onClicked: backgroundPickerDialog.open()
                        }
                    }

                    BackgroundPickerDialog {
                        id: backgroundPickerDialog
                    }
                }

                // Extraction View Mode
                SettingsCard {
                    id: extractionViewCard
                    searchId: "extractionView"
                    title: TranslationManager.translate("settings.preferences.extractionView", "Extraction View")
                    description: TranslationManager.translate("settings.preferences.extractionViewDesc", "Visualization during espresso extraction")
                    keywords: ["chart", "cup", "graph", "extraction", "view"]
                    spacing: Theme.scaled(10)
                    readonly property string currentMode: contextPreferenceWrite.displayedValue(
                                "espressoExtractionView", RecipeContext.preferences.espressoExtractionView || "chart")

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.preferences.extractionViewDesc", "Visualization during espresso extraction")
                        color: Theme.textSecondaryColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        SettingsSearch.title: TranslationManager.translate("settings.preferences.extractionView", "Extraction View")
                        model: ListModel {
                            ListElement { mode: "chart"; icon: "qrc:/icons/Graph.svg"; labelKey: "settings.preferences.viewChart"; labelFallback: "Shot Chart" }
                            ListElement { mode: "cupFill"; icon: "qrc:/icons/espresso.svg"; labelKey: "settings.preferences.viewCupFill"; labelFallback: "Cup Fill" }
                        }

                        delegate: Rectangle {
                            id: viewOptionCard
                            required property var model

                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(44)
                            radius: Theme.scaled(8)
                            color: extractionViewCard.currentMode === viewOptionCard.model.mode
                                ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                                : Theme.backgroundColor
                            border.color: extractionViewCard.currentMode === viewOptionCard.model.mode
                                ? Theme.primaryColor : Theme.borderColor
                            border.width: extractionViewCard.currentMode === viewOptionCard.model.mode
                                ? Theme.scaled(2) : Theme.scaled(1)

                            Accessible.ignored: true

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.scaled(12)
                                anchors.rightMargin: Theme.scaled(12)
                                spacing: Theme.scaled(10)

                                Image {
                                    source: viewOptionCard.model.icon
                                    sourceSize.width: Theme.scaled(20)
                                    sourceSize.height: Theme.scaled(20)
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                Text {
                                    text: TranslationManager.translate(viewOptionCard.model.labelKey, viewOptionCard.model.labelFallback)
                                    color: Theme.textColor
                                    font.family: Theme.bodyFont.family
                                    font.pixelSize: Theme.bodyFont.pixelSize
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }

                                // Radio indicator
                                Rectangle {
                                    Layout.preferredWidth: Theme.scaled(18)
                                    Layout.preferredHeight: Theme.scaled(18)
                                    radius: Theme.scaled(9)
                                    border.color: extractionViewCard.currentMode === viewOptionCard.model.mode
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
                                        visible: extractionViewCard.currentMode === viewOptionCard.model.mode
                                    }
                                }
                            }

                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: TranslationManager.translate(viewOptionCard.model.labelKey, viewOptionCard.model.labelFallback)
                                accessibleItem: viewOptionCard
                                onAccessibleClicked: {
                                    contextPreferenceWrite.submit("espressoExtractionView", viewOptionCard.model.mode)
                                }
                            }
                        }
                    }
                }

                // Shot Review: the review page's timer and what happens around it
                SettingsCard {
                    searchId: "shotReviewTimer"
                    title: TranslationManager.translate("settings.machine.shotReview", "Shot Review")
                    description: TranslationManager.translate("settings.machine.shotReviewTimerDesc", "Return to idle after reviewing shot")
                    keywords: ["review", "post-shot", "timeout", "close", "auto", "edit", "shot info", "after", "notes", "clear", "reset", "start"]
                    spacing: Theme.scaled(10)

                    Text {
                        text: TranslationManager.translate("settings.machine.shotReviewTimer", "Shot Review Timer")
                        color: Theme.textColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(14)
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.machine.shotReviewTimerDesc", "Return to idle after reviewing shot")
                        color: Theme.textSecondaryColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

                    ValueInput {
                        id: postShotReviewInput
                        Layout.fillWidth: true
                        from: 0
                        to: 31
                        stepSize: 1
                        decimals: 0
                        value: contextPreferenceWrite.displayedValue(
                                   "postShotReviewTimeout", machineTab.postShotReviewTimeout)
                        enabled: contextPreferenceWrite.ready
                        displayText: value === 0
                            ? TranslationManager.translate("settings.preferences.instant", "Instant")
                            : value === 31
                                ? TranslationManager.translate("settings.preferences.never", "Never")
                                : (value + " " + TranslationManager.translate("settings.preferences.min", "min"))
                        accessibleName: TranslationManager.translate("settings.machine.shotReviewTimer", "Shot Review Timer")

                        onValueModified: function(newValue) {
                            contextPreferenceWrite.submit("postShotReviewTimeout", newValue)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(15)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "settings.visualizer.editAfterShot"
                                fallback: "Edit After Shot"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }
                            Tr {
                                Layout.fillWidth: true
                                key: "settings.machine.editAfterShotDesc"
                                fallback: "Open shot review page after each extraction"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                                wrapMode: Text.WordWrap
                            }
                        }
                        StyledSwitch {
                            checked: visualizerPreferenceWrite.displayedValue(
                                         "visualizerShowAfterShot",
                                         machineTab.scalarPreference("visualizer.visualizerShowAfterShot", true))
                            enabled: visualizerPreferenceWrite.ready
                            accessibleName: TranslationManager.translate("settings.visualizer.editAfterShot", "Edit After Shot")
                            onClicked: visualizerPreferenceWrite.submit("visualizerShowAfterShot", checked)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(15)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "settings.visualizer.clearNotesOnStart"
                                fallback: "Clear Notes on Start"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }
                            Tr {
                                Layout.fillWidth: true
                                key: "settings.visualizer.clearNotesOnStartDesc"
                                fallback: "Clear shot notes when starting a new shot"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                                wrapMode: Text.WordWrap
                            }
                        }
                        StyledSwitch {
                            checked: visualizerPreferenceWrite.displayedValue(
                                         "visualizerClearNotesOnStart",
                                         machineTab.scalarPreference("visualizer.visualizerClearNotesOnStart", false))
                            enabled: visualizerPreferenceWrite.ready
                            accessibleName: TranslationManager.translate("settings.visualizer.clearNotesOnStart", "Clear Notes on Start")
                            onClicked: visualizerPreferenceWrite.submit("visualizerClearNotesOnStart", checked)
                        }
                    }
                }

                // Screen Zoom Configuration
                SettingsCard {
                    searchId: "screenZoom"
                    title: TranslationManager.translate("settings.machine.screenZoom", "Screen Zoom")
                    description: TranslationManager.translate("settings.machine.screenZoomDesc", "Make text and controls larger or smaller on each screen")
                    showDescription: true
                    keywords: ["zoom", "scale", "size", "ui", "display", "dpi"]
                    spacing: Theme.scaled(10)

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
                            enabled: appPreferenceWrite.ready
                            accessibleName: TranslationManager.translate("settings.machine.configureZoom", "Configure zoom per screen")
                            onClicked: {
                                WebDebugLogger.debug("App", "SettingsMachineTab", ["Switch clicked, checked =", checked].map(String).join(" "))
                                appPreferenceWrite.submit("configurePageScale", checked)
                                Theme.configurePageScaleEnabled = checked
                                WebDebugLogger.debug("App", "SettingsMachineTab", ["Theme.configurePageScaleEnabled =", Theme.configurePageScaleEnabled].map(String).join(" "))
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

                // Launcher Mode (Android only)
                SettingsCard {
                    searchId: "launcherMode"
                    title: TranslationManager.translate("settings.options.launcherMode", "Launcher Mode")
                    description: TranslationManager.translate("settings.options.launcherModeDesc", "Set Decenza as the Android home screen. Press Home to return here instead of the default launcher.")
                    showDescription: true
                    keywords: ["launcher", "home", "android", "kiosk"]
                    availability: "android"

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
                            checked: appPreferenceWrite.displayedValue(
                                         "launcherMode", machineTab.scalarPreference("app.launcherMode", false))
                            enabled: appPreferenceWrite.ready
                            accessibleName: TranslationManager.translate(
                                "settings.options.useAsLauncher", "Use as Home Screen")
                            onClicked: appPreferenceWrite.submit("launcherMode", checked)
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: appPreferenceWrite.displayedValue(
                                     "launcherMode", machineTab.scalarPreference("app.launcherMode", false))
                        text: TranslationManager.translate("settings.options.launcherModeHint", "Android will ask you to choose a default launcher. Select Decenza and tap \"Always\".")
                        color: Theme.warningColor
                        font.family: Theme.captionFont.family
                        font.pixelSize: Theme.captionFont.pixelSize
                        wrapMode: Text.WordWrap
                    }
                }

            }

            // ========== RIGHT COLUMN: Water & Features ==========
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(350)
                Layout.alignment: Qt.AlignTop
                spacing: Theme.scaled(15)

                // Water Level Status
                SettingsCard {
                    id: waterLevelCard
                    searchId: "waterLevel"
                    title: TranslationManager.translate("settings.options.waterLevel", "Water Level")
                    description: TranslationManager.translate("settings.search.waterLevelDesc", "Water tank level and refill threshold")
                    keywords: ["water", "tank", "level", "refill"]
                    showHeader: false
                    spacing: Theme.scaled(12)
                    property bool refillKitActive: appPreferenceWrite.displayedValue(
                                                       "refillKitOverride", machineTab.scalarPreference("app.refillKitOverride", 2)) === 1 ||
                                                    (appPreferenceWrite.displayedValue(
                                                       "refillKitOverride", machineTab.scalarPreference("app.refillKitOverride", 2)) === 2
                                                     && DE1Device.refillKitDetected === 1)

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
                            Layout.preferredWidth: Theme.scaled(20)
                            Layout.preferredHeight: Theme.scaled(20)
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
                            checked: appPreferenceWrite.displayedValue(
                                         "waterLevelDisplayUnit",
                                         machineTab.scalarPreference("app.waterLevelDisplayUnit", "percent")) === "ml"
                            enabled: appPreferenceWrite.ready
                            accessibleName: TranslationManager.translate("settings.options.showInMl", "Show in milliliters (ml)")
                            onClicked: {
                                appPreferenceWrite.submit("waterLevelDisplayUnit", checked ? "ml" : "percent")
                            }
                        }
                    }
                }

                // Temperature display unit (Celsius / Fahrenheit). Storage stays Celsius;
                // this only changes how temps are shown and entered across the app.
                SettingsCard {
                    searchId: "temperatureUnit"
                    title: TranslationManager.translate("settings.options.temperatureUnit", "Temperature unit")
                    description: TranslationManager.translate("settings.search.temperatureUnitDesc", "Show temperatures in Celsius or Fahrenheit")
                    keywords: ["fahrenheit", "celsius", "units", "°f", "°c", "degrees", "temperature"]

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.options.showInFahrenheit", "Show temperatures in Fahrenheit (°F)")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(14)
                            wrapMode: Text.WordWrap
                        }

                        StyledSwitch {
                            checked: appPreferenceWrite.displayedValue(
                                         "temperatureUnit",
                                         machineTab.scalarPreference("app.temperatureUnit", "celsius")) === "fahrenheit"
                            enabled: appPreferenceWrite.ready
                            accessibleName: TranslationManager.translate("settings.options.showInFahrenheitAccessible", "Show temperatures in Fahrenheit")
                            onClicked: {
                                appPreferenceWrite.submit("temperatureUnit", checked ? "fahrenheit" : "celsius")
                            }
                        }
                    }
                }

                // Water Refill Threshold (only when refill kit is not active)
                SettingsCard {
                    id: waterRefillThresholdCard
                    searchId: "waterRefillThreshold"
                    title: TranslationManager.translate("settings.options.waterRefillLevel", "Water Refill Threshold")
                    description: TranslationManager.translate("settings.options.waterRefillLevelDesc", "Water level at which the machine warns you to refill")
                    showDescription: true
                    keywords: ["water", "refill", "threshold", "warning"]
                    shown: {
                        var override = appPreferenceWrite.displayedValue(
                                    "refillKitOverride", machineTab.scalarPreference("app.refillKitOverride", 2))
                        var detected = DE1Device.refillKitDetected === 1
                        return override === 0 || (override === 2 && !detected)
                    }

                    ValueInput {
                        Layout.fillWidth: true
                        from: 3
                        to: 70
                        stepSize: 1
                        decimals: 0
                        enabled: appPreferenceWrite.ready
                        value: appPreferenceWrite.displayedValue(
                                   "waterRefillPoint", machineTab.scalarPreference("app.waterRefillPoint", 5))
                        suffix: " mm"
                        accessibleName: TranslationManager.translate("settings.options.waterRefillLevelAccessible", "Water refill level")
                        onValueModified: function(newValue) {
                            appPreferenceWrite.submit("waterRefillPoint", newValue)
                        }
                    }
                }

                // Refill Kit
                SettingsCard {
                    searchId: "refillKit"
                    title: TranslationManager.translate("settings.preferences.refillKit", "Refill Kit")
                    description: TranslationManager.translate("settings.preferences.refillKitDesc", "Control whether the machine uses an automatic water refill kit")
                    showDescription: true
                    keywords: ["refill", "kit", "plumb", "water", "auto"]
                    contentOpacity: kitAvailable ? 1.0 : 0.5
                    property bool kitAvailable: DE1Device.refillKitDetected > 0

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

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(42)
                        spacing: Theme.scaled(8)

                        Repeater {
                            SettingsSearch.title: TranslationManager.translate("settings.preferences.refillKit", "Refill Kit")
                            model: [
                                { value: 2, label: TranslationManager.translate("settings.preferences.refillKitAuto", "Auto"), desc: TranslationManager.translate("settings.preferences.refillKitAutoDesc", "Auto-detect") },
                                { value: 0, label: TranslationManager.translate("settings.preferences.refillKitOff", "Off"), desc: TranslationManager.translate("settings.preferences.refillKitOffDesc", "Force off") },
                                { value: 1, label: TranslationManager.translate("settings.preferences.refillKitOn", "On"), desc: TranslationManager.translate("settings.preferences.refillKitOnDesc", "Force on") }
                            ]

                            delegate: Rectangle {
                                id: refillKitButton
                                required property var modelData

                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: Theme.scaled(6)
                                color: appPreferenceWrite.displayedValue("refillKitOverride", machineTab.scalarPreference("app.refillKitOverride", 2)) === refillKitButton.modelData.value ?
                                       Theme.primaryColor : Theme.backgroundColor
                                border.color: appPreferenceWrite.displayedValue("refillKitOverride", machineTab.scalarPreference("app.refillKitOverride", 2)) === refillKitButton.modelData.value ?
                                              Theme.primaryColor : Theme.textSecondaryColor
                                border.width: 1

                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: Theme.scaled(2)

                                    Text {
                                        text: refillKitButton.modelData.label
                                        color: appPreferenceWrite.displayedValue("refillKitOverride", machineTab.scalarPreference("app.refillKitOverride", 2)) === refillKitButton.modelData.value ?
                                               Theme.primaryContrastColor : Theme.textColor
                                        font.pixelSize: Theme.scaled(14)
                                        font.bold: true
                                        Layout.alignment: Qt.AlignHCenter
                                    }

                                    Text {
                                        text: refillKitButton.modelData.desc
                                        color: appPreferenceWrite.displayedValue("refillKitOverride", machineTab.scalarPreference("app.refillKitOverride", 2)) === refillKitButton.modelData.value ?
                                               Qt.rgba(1, 1, 1, 0.7) : Theme.textSecondaryColor
                                        font.pixelSize: Theme.scaled(10)
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                }

                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    enabled: appPreferenceWrite.ready
                                    accessibleName: TranslationManager.translate("settings.machine.accessible.refillKitMode", "%1 refill kit mode. %2")
                                                        .arg(refillKitButton.modelData.label).arg(refillKitButton.modelData.desc)
                                                    + (appPreferenceWrite.displayedValue("refillKitOverride", machineTab.scalarPreference("app.refillKitOverride", 2)) === refillKitButton.modelData.value
                                                       ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                                    accessibleItem: refillKitButton
                                    onAccessibleClicked: appPreferenceWrite.submit("refillKitOverride", refillKitButton.modelData.value)
                                }
                            }
                        }
                    }
                }

                // Pocket Integration (remote control via Pocket app)
                SettingsCard {
                    searchId: "pocketIntegration"
                    title: TranslationManager.translate("settings.machine.pocketIntegrationTitle", "Pocket Integration")
                    description: TranslationManager.translate("settings.machine.pocketIntegrationDesc", "Allow the Pocket app to view and control your screen remotely. Requires an active Pocket pairing.")
                    showDescription: true
                    keywords: ["pocket", "remote", "pair", "control", "screen"]

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
                            checked: appPreferenceWrite.displayedValue(
                                         "screenCaptureEnabled",
                                         machineTab.scalarPreference("app.screenCaptureEnabled", false))
                            enabled: appPreferenceWrite.ready
                            accessibleName: TranslationManager.translate("settings.machine.pocketIntegration", "Enable Pocket Integration")
                            onClicked: {
                                appPreferenceWrite.submit("screenCaptureEnabled", checked)
                            }
                        }
                    }
                }

                // Simulation Mode — absent on builds with no simulator compiled in (tablet release).
                SettingsCard {
                    searchId: "simulationMode"
                    title: TranslationManager.translate("settings.machine.simulationModeTitle", "Simulation Mode")
                    description: TranslationManager.translate("settings.machine.simulationModeDesc", "Use the app without a connected DE1 machine")
                    showDescription: true
                    keywords: ["offline", "simulation", "demo", "unlock", "gui", "disconnect"]
                    availability: "simulator"

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
                            visible: appPreferenceWrite.displayedValue(
                                "simulationMode", machineTab.scalarPreference("app.simulationMode", DE1Device.simulationMode))
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
                            checked: appPreferenceWrite.displayedValue(
                                "simulationMode", machineTab.scalarPreference("app.simulationMode", DE1Device.simulationMode))
                            enabled: appPreferenceWrite.ready
                                  && machineTab.scalarPreference("app.simulatorAvailable", false)
                            accessibleName: TranslationManager.translate("settings.machine.simulationMode", "Simulation Mode")
                            onToggled: {
                                // Save to persistent Settings — takes effect on next launch
                                appPreferenceWrite.submit("simulationMode", checked)
                            }
                        }
                    }

                    Text {
                        id: restartRequiredText
                        visible: appPreferenceWrite.displayedValue(
                                     "simulationMode", machineTab.scalarPreference("app.simulationMode", DE1Device.simulationMode))
                                 !== DE1Device.simulationMode
                        text: TranslationManager.translate("settings.preferences.restartRequired", "Restart required for this change to take effect")
                        color: Theme.warningColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(12)
                        Layout.leftMargin: Theme.scaled(15)
                        Layout.bottomMargin: Theme.scaled(5)
                    }
                }

            }

                        // Spacer
            Item { Layout.fillWidth: true }
        }
    }

    // Scroll indicator — shows when more content is below
    ScrollDownIndicator {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.scaled(8)
        flickable: contentFlickable
    }

    // Map Test Popup
    DecenzaDialog {
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
                    active: mapTestPopup.visible && Settings.app.hasQuick3D
                    sourceComponent: ShotMapScreensaver {
                        testMode: true
                        testLatitude: Number(machineTab.scalarPreference("shotMap.latitude", 0))
                        testLongitude: Number(machineTab.scalarPreference("shotMap.longitude", 0))
                    }
                }

                // Fallback when Quick3D is not available
                Text {
                    anchors.centerIn: parent
                    visible: !Settings.app.hasQuick3D
                    text: TranslationManager.translate("settings.options.quick3dRequired", "3D Map requires Qt Quick3D\n(not available on this platform)")
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Text {
                Layout.fillWidth: true
                text: {
                    var lat = Number(machineTab.scalarPreference("shotMap.latitude", 0)).toFixed(1)
                    var lon = Number(machineTab.scalarPreference("shotMap.longitude", 0)).toFixed(1)
                    var city = machineTab.scalarPreference("shotMap.city", "")
                    return TranslationManager.translate("settings.machine.locationSummary",
                                                        "Location: %1 at coordinates %2, %3")
                           .arg(city).arg(lat).arg(lon)
                }
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(12)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
    }

}
