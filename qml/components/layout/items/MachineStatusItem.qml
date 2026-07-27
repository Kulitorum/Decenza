import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    // Per-instance display mode (composable-status-bar): "text" (default, status
    // dot + text) or "icon" (DE1 icon ahead of the text). Read from stored props.
    readonly property string displayMode: (modelData && modelData.displayMode) ? modelData.displayMode : "text"

    // Per-instance color override. "default"/unset keeps the dynamic phase color
    // (statusColor); a named choice forces a static tint over every phase.
    readonly property string colorChoice: (modelData && modelData.color) ? modelData.color : "default"
    readonly property color readoutColor: WidgetColor.resolve(colorChoice, root.statusColor)

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: TranslationManager.translate("machineStatus.accessible", "Machine status: %1").arg(root.statusText)
    Accessible.focusable: true

    readonly property color statusColor: {
        switch (MachineState.phase) {
            case MachineState.Phase.Disconnected:       return Theme.errorColor
            case MachineState.Phase.Sleep:               return Theme.textSecondaryColor
            case MachineState.Phase.Idle:                return Theme.textSecondaryColor
            case MachineState.Phase.Heating:             return Theme.errorColor
            case MachineState.Phase.Ready:               return Theme.successColor
            case MachineState.Phase.EspressoPreheating:  return Theme.warningColor
            case MachineState.Phase.Preinfusion:         return Theme.accentColor
            case MachineState.Phase.Pouring:             return Theme.accentColor
            case MachineState.Phase.Ending:              return Theme.textSecondaryColor
            case MachineState.Phase.Steaming:            return Theme.accentColor
            case MachineState.Phase.HotWater:            return Theme.accentColor
            case MachineState.Phase.Flushing:            return Theme.accentColor
            case MachineState.Phase.Refill:              return Theme.warningColor
            case MachineState.Phase.Descaling:           return Theme.accentColor
            case MachineState.Phase.Cleaning:            return Theme.accentColor
            case MachineState.Phase.Transport:           return Theme.accentColor
            default:                                         return Theme.textSecondaryColor
        }
    }

    readonly property string statusText: {
        switch (MachineState.phase) {
            case MachineState.Phase.Disconnected:       return TranslationManager.translate("machineStatus.disconnected", "Disconnected")
            case MachineState.Phase.Sleep:               return TranslationManager.translate("machineStatus.sleep", "Sleep")
            case MachineState.Phase.Idle:                return TranslationManager.translate("machineStatus.idle", "Idle")
            case MachineState.Phase.Heating:             return TranslationManager.translate("machineStatus.heating", "Heating")
            case MachineState.Phase.Ready:               return TranslationManager.translate("machineStatus.ready", "Ready")
            case MachineState.Phase.EspressoPreheating:  return TranslationManager.translate("machineStatus.preheating", "Preheating")
            case MachineState.Phase.Preinfusion:         return TranslationManager.translate("machineStatus.preinfusion", "Preinfusion")
            case MachineState.Phase.Pouring:             return TranslationManager.translate("machineStatus.pouring", "Pouring")
            case MachineState.Phase.Ending:              return TranslationManager.translate("machineStatus.ending", "Ending")
            case MachineState.Phase.Steaming:            return TranslationManager.translate("machineStatus.steaming", "Steaming")
            case MachineState.Phase.HotWater:            return TranslationManager.translate("machineStatus.hotWater", "Hot Water")
            case MachineState.Phase.Flushing:            return TranslationManager.translate("machineStatus.flushing", "Flushing")
            case MachineState.Phase.Refill:              return TranslationManager.translate("machineStatus.refill", "Refill")
            case MachineState.Phase.Descaling:           return TranslationManager.translate("machineStatus.descaling", "Descaling")
            case MachineState.Phase.Cleaning:            return TranslationManager.translate("machineStatus.cleaning", "Cleaning")
            case MachineState.Phase.Transport:           return TranslationManager.translate("machineStatus.transport", "Transport")
            default:                                         return TranslationManager.translate("machineStatus.unknown", "Unknown")
        }
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth
        implicitHeight: compactRow.implicitHeight

        Row {
            id: compactRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            ThemedIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.displayMode === "icon"
                source: "qrc:/icons/decent-de1.svg"
                iconSize: Theme.scaled(20)
                color: root.readoutColor
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.displayMode !== "icon"
                width: Theme.scaled(10)
                height: Theme.scaled(10)
                radius: Theme.scaled(5)
                color: root.readoutColor
            }

            Text {
                text: root.statusText
                color: root.readoutColor
                font: Theme.bodyFont
                Accessible.ignored: true
            }
        }

        AccessibleMouseArea {
            anchors.fill: parent
            accessibleName: TranslationManager.translate("machineStatus.accessible", "Machine status: %1").arg(root.statusText)
        }
    }

    // --- FULL MODE ---
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
                source: "qrc:/icons/decent-de1.svg"
                iconSize: Theme.scaled(28)
                color: root.readoutColor
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.statusText
                color: root.readoutColor
                font: Theme.valueFont
                Accessible.ignored: true
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: TranslationManager.translate("machineStatus.label", "Machine Status")
                color: Theme.textSecondaryColor
                font: Theme.labelFont
                Accessible.ignored: true
            }
        }

        AccessibleMouseArea {
            anchors.fill: parent
            accessibleName: TranslationManager.translate("machineStatus.accessible", "Machine status: %1").arg(root.statusText)
        }
    }
}
