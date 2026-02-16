import QtQuick
import QtQuick.Layouts
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: "Machine status: " + root.statusText
    Accessible.focusable: true

    readonly property color statusColor: {
        switch (MachineState.phase) {
            case MachineStateType.Phase.Disconnected:       return Theme.errorColor
            case MachineStateType.Phase.Sleep:               return Theme.textSecondaryColor
            case MachineStateType.Phase.Idle:                return Theme.textSecondaryColor
            case MachineStateType.Phase.Heating:             return Theme.errorColor
            case MachineStateType.Phase.Ready:               return Theme.successColor
            case MachineStateType.Phase.EspressoPreheating:  return Theme.warningColor
            case MachineStateType.Phase.Preinfusion:         return Theme.primaryColor
            case MachineStateType.Phase.Pouring:             return Theme.primaryColor
            case MachineStateType.Phase.Ending:              return Theme.textSecondaryColor
            case MachineStateType.Phase.Steaming:            return Theme.primaryColor
            case MachineStateType.Phase.HotWater:            return Theme.primaryColor
            case MachineStateType.Phase.Flushing:            return Theme.primaryColor
            case MachineStateType.Phase.Refill:              return Theme.warningColor
            case MachineStateType.Phase.Descaling:           return Theme.primaryColor
            case MachineStateType.Phase.Cleaning:            return Theme.primaryColor
            default:                                         return Theme.textSecondaryColor
        }
    }

    readonly property string statusText: {
        switch (MachineState.phase) {
            case MachineStateType.Phase.Disconnected:       return TranslationManager.translate("machineStatus.disconnected", "Disconnected")
            case MachineStateType.Phase.Sleep:               return TranslationManager.translate("machineStatus.sleep", "Sleep")
            case MachineStateType.Phase.Idle:                return TranslationManager.translate("machineStatus.idle", "Idle")
            case MachineStateType.Phase.Heating:             return TranslationManager.translate("machineStatus.heating", "Heating")
            case MachineStateType.Phase.Ready:               return TranslationManager.translate("machineStatus.ready", "Ready")
            case MachineStateType.Phase.EspressoPreheating:  return TranslationManager.translate("machineStatus.preheating", "Preheating")
            case MachineStateType.Phase.Preinfusion:         return TranslationManager.translate("machineStatus.preinfusion", "Preinfusion")
            case MachineStateType.Phase.Pouring:             return TranslationManager.translate("machineStatus.pouring", "Pouring")
            case MachineStateType.Phase.Ending:              return TranslationManager.translate("machineStatus.ending", "Ending")
            case MachineStateType.Phase.Steaming:            return TranslationManager.translate("machineStatus.steaming", "Steaming")
            case MachineStateType.Phase.HotWater:            return TranslationManager.translate("machineStatus.hotWater", "Hot Water")
            case MachineStateType.Phase.Flushing:            return TranslationManager.translate("machineStatus.flushing", "Flushing")
            case MachineStateType.Phase.Refill:              return TranslationManager.translate("machineStatus.refill", "Refill")
            case MachineStateType.Phase.Descaling:           return TranslationManager.translate("machineStatus.descaling", "Descaling")
            case MachineStateType.Phase.Cleaning:            return TranslationManager.translate("machineStatus.cleaning", "Cleaning")
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

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.scaled(10)
                height: Theme.scaled(10)
                radius: Theme.scaled(5)
                color: root.statusColor
            }

            Text {
                text: root.statusText
                color: root.statusColor
                font: Theme.bodyFont
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

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.statusText
                color: root.statusColor
                font: Theme.valueFont
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: TranslationManager.translate("machineStatus.label", "Machine Status")
                color: Theme.textSecondaryColor
                font: Theme.labelFont
            }
        }

        AccessibleMouseArea {
            anchors.fill: parent
            accessibleName: TranslationManager.translate("machineStatus.accessible", "Machine status: %1").arg(root.statusText)
        }
    }
}
