import QtQuick
import QtQuick.Layouts
import Decenza

LayoutWidgetItem {
    id: root

    // Per-instance display mode (composable-status-bar): "text" (default) or
    // "icon" (a steam icon ahead of the value). Read from stored props.
    readonly property string displayMode: (modelData && modelData.displayMode) ? modelData.displayMode : "text"

    // Per-instance color override; "default"/unset keeps the steam warning color.
    readonly property string colorChoice: (modelData && modelData.color) ? modelData.color : "default"
    readonly property color readoutColor: WidgetColor.resolve(colorChoice, Theme.warningColor)

    readonly property real currentTemp: DE1Device.steamTemperature
    readonly property real targetTemp: Settings.brew.steamTemperature

    // The heater being off is NOT inferable from currentTemp: a boiler that was
    // switched off five minutes ago still reads 130°. Ask the resolved state.
    readonly property bool heaterOff: !MainController.steamHeaterOn
    readonly property string offLabel: SteamLabels.offReadout
    // What the readout shows: "Off" when the heater is off, the measured
    // temperature otherwise, an em dash when there is no machine to read.
    readonly property string readoutText: !DE1Device.connected
        ? "\u2014"
        : SteamLabels.temperatureText(root.heaterOff, root.currentTemp)

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: root.heaterOff
                     ? "Steam heater: " + root.offLabel
                     : "Steam temperature: " + Theme.cToDisplay(root.currentTemp).toFixed(0) +
                       " degrees, target: " + Theme.cToDisplay(root.targetTemp).toFixed(0) + " degrees"
    Accessible.focusable: true

    // --- COMPACT MODE (bar / status bar rendering) ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth
        implicitHeight: compactRow.implicitHeight

        Row {
            id: compactRow
            anchors.centerIn: parent
            spacing: Theme.scaled(6)

            ThemedIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.displayMode === "icon"
                source: "qrc:/icons/steam.svg"
                iconSize: Theme.scaled(20)
                color: root.readoutColor
            }

            Text {
                id: compactTemp
                anchors.verticalCenter: parent.verticalCenter
                text: root.readoutText
                color: root.readoutColor
                font: Theme.bodyFont
            }
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -Theme.spacingSmall
            onClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled) {
                    AccessibilityManager.announceLabel(root.Accessible.name)
                }
            }
        }
    }

    // --- FULL MODE (center rendering) ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: fullColumn.implicitWidth
        implicitHeight: fullColumn.implicitHeight

        ColumnLayout {
            id: fullColumn
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            ThemedIcon {
                Layout.alignment: Qt.AlignHCenter
                visible: root.displayMode === "icon"
                source: "qrc:/icons/steam.svg"
                iconSize: Theme.scaled(28)
                color: root.readoutColor
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(4)
                Text {
                    text: root.readoutText
                    color: root.readoutColor
                    font: Theme.valueFont
                }
                Text {
                    anchors.baseline: parent.children[0].baseline
                    text: "/ " + Theme.formatTemperature(root.targetTemp, 0)
                    color: Theme.textSecondaryColor
                    font.family: Theme.valueFont.family
                    font.pixelSize: Theme.valueFont.pixelSize / 2
                }
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: TranslationManager.translate("steamTemp.steamTemp", "Steam Temp")
                color: Theme.textSecondaryColor
                font: Theme.labelFont
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled) {
                    AccessibilityManager.announceLabel(root.Accessible.name)
                }
            }
        }
    }
}
