import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "layout"

// Top status bar. By default it renders the configurable `statusBar` layout zone
// (the project's zone-driven system). When Settings.theme.compactStatusBar is on
// (opt-in), it instead shows a fixed, icon-led summary of live device state with a
// centred Sleep button — kept out of the bottom-left corner (shared with Back),
// which caused accidental sleeps.
Rectangle {
    id: statusBarRoot
    color: Theme.surfaceColor

    readonly property bool compact: Settings.theme.compactStatusBar
    readonly property bool de1Connected: DE1Device.connected
    readonly property bool scaleConnected: ScaleDevice.connected

    function batteryIcon(lvl) {
        if (lvl <= 10) return "qrc:/icons/battery-0.svg"
        if (lvl <= 37) return "qrc:/icons/battery-25.svg"
        if (lvl <= 62) return "qrc:/icons/battery-50.svg"
        if (lvl <= 87) return "qrc:/icons/battery-75.svg"
        return "qrc:/icons/battery-100.svg"
    }

    function doSleep() {
        if (ScaleDevice && ScaleDevice.connected)
            ScaleDevice.disableLcd()
        DE1Device.goToSleep()
        var win = Window.window
        if (win && typeof win.goToScreensaver === "function")
            win.goToScreensaver()
    }

    // ---------------------------------------------------------------------------
    // Default: zone-driven status bar (configurable via the layout editor).
    // ---------------------------------------------------------------------------
    property var statusBarItems: {
        var raw = Settings.network.layoutConfiguration
        try {
            var parsed = JSON.parse(raw)
            return (parsed.zones && parsed.zones.statusBar) || []
        } catch(e) {
            return []
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.chartMarginSmall
        anchors.rightMargin: Theme.spacingLarge
        spacing: Theme.spacingMedium
        visible: !statusBarRoot.compact

        Repeater {
            model: statusBarRoot.compact ? [] : statusBarRoot.statusBarItems
            delegate: LayoutItemDelegate {
                zoneName: "statusBar"
                Layout.fillWidth: modelData.type === "spacer"
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Opt-in: compact, icon-led device-status bar with centred Sleep button.
    // ---------------------------------------------------------------------------

    // One labelled, icon-led datapoint.
    component StatusItem: RowLayout {
        property url iconSource
        property string value
        property bool active: true
        property string accessibleName: value
        spacing: Theme.scaled(6)

        Accessible.role: Accessible.StaticText
        Accessible.name: accessibleName
        Accessible.focusable: true

        ThemedIcon {
            source: iconSource
            iconSize: Theme.scaled(20)
            color: Theme.textColor
            opacity: active ? 1.0 : 0.4
            Layout.alignment: Qt.AlignVCenter
            Accessible.ignored: true  // the RowLayout exposes the combined accessibleName
        }
        Text {
            text: value
            color: active ? Theme.textColor : Theme.textSecondaryColor
            font: Theme.labelFont
            Layout.alignment: Qt.AlignVCenter
            Accessible.ignored: true  // avoid a double announce (parent already names it)
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingLarge
        spacing: Theme.scaled(24)
        visible: statusBarRoot.compact

        // --- Machine cluster ---
        StatusItem {
            iconSource: "qrc:/icons/decent-de1.svg"
            active: statusBarRoot.de1Connected
            value: statusBarRoot.de1Connected ? DE1Device.stateString
                                              : TranslationManager.translate("status.de1Offline", "Offline")
            accessibleName: TranslationManager.translate("status.de1", "Machine") + ": " + value
        }
        StatusItem {
            iconSource: "qrc:/icons/temperature.svg"
            active: statusBarRoot.de1Connected
            value: statusBarRoot.de1Connected ? DE1Device.temperature.toFixed(1) + "°C"
                                              : TranslationManager.translate("status.none", "—")
            accessibleName: TranslationManager.translate("status.groupTemp", "Group temperature") + ": " + value
        }
        StatusItem {
            iconSource: "qrc:/icons/steam.svg"
            active: statusBarRoot.de1Connected
            value: statusBarRoot.de1Connected ? DE1Device.steamTemperature.toFixed(0) + "°C"
                                              : TranslationManager.translate("status.none", "—")
            accessibleName: TranslationManager.translate("status.steamTemp", "Steam temperature") + ": " + value
        }

        Item { Layout.fillWidth: true }

        // --- Sleep / power button (dead-centre, clearly a button). Single tap only;
        //     deliberate quit lives in the QuitItem layout widget, so there's no
        //     hidden long-press-to-quit footgun on every page. ---
        Rectangle {
            id: sleepButton
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: Theme.scaled(40)
            Layout.preferredWidth: sleepRow.implicitWidth + Theme.spacingMedium * 2
            radius: height / 2
            color: sleepMa.pressed ? Qt.darker(Theme.surfaceColor, 1.4) : Qt.lighter(Theme.surfaceColor, 1.3)
            border.width: Theme.scaled(1)
            border.color: Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("idle.accessible.sleep", "Sleep") + ". "
                             + TranslationManager.translate("idle.accessible.sleep.description", "Put the machine to sleep")
            Accessible.focusable: true
            Accessible.onPressAction: sleepMa.clicked(null)

            Row {
                id: sleepRow
                anchors.centerIn: parent
                spacing: Theme.scaled(6)
                ThemedIcon {
                    source: "qrc:/icons/sleep.svg"
                    iconSize: Theme.scaled(22)
                    color: Theme.textColor
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true  // the Button parent provides the name
                }
                Text {
                    text: TranslationManager.translate("idle.button.sleep", "Sleep")
                    color: Theme.textColor
                    font: Theme.labelFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true  // the Button parent provides the name
                }
            }
            MouseArea {
                id: sleepMa
                anchors.fill: parent
                onClicked: statusBarRoot.doSleep()
            }
        }

        Item { Layout.fillWidth: true }

        // --- Scale cluster ---
        StatusItem {
            iconSource: "qrc:/icons/scale.svg"
            active: statusBarRoot.scaleConnected
            value: statusBarRoot.scaleConnected ? TranslationManager.translate("status.scaleConnected", "Connected")
                                                : TranslationManager.translate("status.scaleOff", "Off")
            accessibleName: TranslationManager.translate("status.scale", "Scale") + ": " + value
        }
        StatusItem {
            // Device/tablet battery (BatteryManager). Only shown on mobile, where the
            // reading is real — on desktop BatteryManager reports a constant 100%,
            // which would be a misleading fake full-battery icon.
            readonly property int deviceBattery: BatteryManager.batteryPercent
            visible: Qt.platform.os === "android" || Qt.platform.os === "ios"
            iconSource: statusBarRoot.batteryIcon(deviceBattery)
            value: deviceBattery + "%"
            accessibleName: TranslationManager.translate("status.battery", "Battery") + ": " + value
        }
    }
}
