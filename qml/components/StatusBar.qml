import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza

// Top status bar — a clean, icon-led summary of live device state, with the
// Sleep button moved here (dead-centre) so it no longer shares the bottom-left
// corner with the Back button (which caused accidental sleeps during nav lag).
// Machine cluster (DE1 status · group-head temp · steam temp) on the left,
// scale cluster (scale status · battery) on the right.
Rectangle {
    id: statusBarRoot
    color: Theme.surfaceColor

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

    // One labelled, icon-led datapoint.
    component StatusItem: RowLayout {
        property url iconSource
        property string value
        property bool active: true
        property string accessibleName: value
        spacing: Theme.scaled(6)

        Accessible.role: Accessible.StaticText
        Accessible.name: accessibleName

        ThemedIcon {
            source: iconSource
            iconSize: Theme.scaled(20)
            color: Theme.textColor
            opacity: active ? 1.0 : 0.4
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            text: value
            color: active ? Theme.textColor : Theme.textSecondaryColor
            font: Theme.labelFont
            Layout.alignment: Qt.AlignVCenter
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingLarge
        spacing: Theme.scaled(24)

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

        // --- Sleep / power button (dead-centre, clearly a button) ---
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
            Accessible.description: TranslationManager.translate("idle.accessible.sleep.hint", "Long-press to quit the app.")
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
                }
                Text {
                    text: TranslationManager.translate("idle.button.sleep", "Sleep")
                    color: Theme.textColor
                    font: Theme.labelFont
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            MouseArea {
                id: sleepMa
                anchors.fill: parent
                onClicked: statusBarRoot.doSleep()
                onPressAndHold: Qt.quit()
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
            // Device/tablet battery (BatteryManager) — the scale's own battery is
            // often not reported over BLE (ScaleDevice.batteryLevel == -1).
            readonly property int deviceBattery: BatteryManager.batteryPercent
            iconSource: statusBarRoot.batteryIcon(deviceBattery)
            active: deviceBattery >= 0
            value: deviceBattery >= 0 ? deviceBattery + "%"
                                      : TranslationManager.translate("status.none", "—")
            accessibleName: TranslationManager.translate("status.battery", "Battery") + ": " + value
        }
    }
}
