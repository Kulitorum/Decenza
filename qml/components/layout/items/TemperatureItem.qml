import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    // Per-instance display mode (composable-status-bar): "text" (default) or
    // "icon" (a temperature icon ahead of the value). Read from stored props.
    readonly property string displayMode: (modelData && modelData.displayMode) ? modelData.displayMode : "text"

    // Per-instance color override; "default"/unset keeps the temperature color.
    readonly property string colorChoice: (modelData && modelData.color) ? modelData.color : "default"
    readonly property color readoutColor: WidgetColor.resolve(colorChoice, Theme.temperatureColor)

    readonly property double effectiveTargetTemp: Settings.brew.hasTemperatureOverride
        ? Settings.brew.temperatureOverride
        : ProfileManager.profileTargetTemperature
    readonly property bool isRealOverride: Settings.brew.hasTemperatureOverride &&
        Math.abs(Settings.brew.temperatureOverride - ProfileManager.profileTargetTemperature) > 0.1

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: {
        var text = "Group temperature: " + DE1Device.temperature.toFixed(1) + " degrees, target: " + root.effectiveTargetTemp.toFixed(0) + " degrees"
        if (root.isRealOverride) text += " (override active)"
        return text
    }
    Accessible.focusable: true

    // --- COMPACT MODE (bar rendering) ---
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
                source: "qrc:/icons/temperature.svg"
                iconSize: Theme.scaled(20)
                color: root.readoutColor
            }

            Text {
                id: compactTemp
                anchors.verticalCenter: parent.verticalCenter
                text: DE1Device.temperature.toFixed(1) + "\u00B0C"
                color: root.readoutColor
                font: Theme.bodyFont
                Accessible.ignored: true
            }
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -Theme.spacingSmall
            onClicked: {
                MachineState.tareScale()
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    var announcement = "Group temperature: " + DE1Device.temperature.toFixed(1) + " degrees, target: " + root.effectiveTargetTemp.toFixed(0) + " degrees"
                    if (root.isRealOverride) announcement += " (override active)"
                    AccessibilityManager.announceLabel(announcement)
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
                source: "qrc:/icons/temperature.svg"
                iconSize: Theme.scaled(28)
                color: root.readoutColor
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(4)
                Text {
                    text: DE1Device.temperature.toFixed(1) + "\u00B0C"
                    color: root.readoutColor
                    font: Theme.valueFont
                    Accessible.ignored: true
                }
                Text {
                    anchors.baseline: parent.children[0].baseline
                    text: "/ " + root.effectiveTargetTemp.toFixed(1) + "\u00B0C"
                    color: root.isRealOverride ? Theme.accentColor : Theme.textSecondaryColor
                    font.family: Theme.valueFont.family
                    font.pixelSize: Theme.valueFont.pixelSize / 2
                    Accessible.ignored: true
                }
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(4)
                Tr {
                    key: "idle.label.grouptemp"
                    fallback: "Group Temp"
                    color: Theme.textSecondaryColor
                    font: Theme.labelFont
                    Accessible.ignored: true
                }
                Text {
                    visible: root.isRealOverride
                    text: "(override)"
                    color: Theme.accentColor
                    font: Theme.labelFont
                    Accessible.ignored: true
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    var announcement = "Group temperature: " + DE1Device.temperature.toFixed(1) + " degrees, target: " + root.effectiveTargetTemp.toFixed(0) + " degrees"
                    if (root.isRealOverride) announcement += " (override active)"
                    AccessibilityManager.announceLabel(announcement)
                }
            }
        }
    }
}
