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
            case MachineStateType.Phase.Disconnected:       return "Disconnected"
            case MachineStateType.Phase.Sleep:               return "Sleep"
            case MachineStateType.Phase.Idle:                return "Idle"
            case MachineStateType.Phase.Heating:             return "Heating"
            case MachineStateType.Phase.Ready:               return "Ready"
            case MachineStateType.Phase.EspressoPreheating:  return "Preheating"
            case MachineStateType.Phase.Preinfusion:         return "Preinfusion"
            case MachineStateType.Phase.Pouring:             return "Pouring"
            case MachineStateType.Phase.Ending:              return "Ending"
            case MachineStateType.Phase.Steaming:            return "Steaming"
            case MachineStateType.Phase.HotWater:            return "Hot Water"
            case MachineStateType.Phase.Flushing:            return "Flushing"
            case MachineStateType.Phase.Refill:              return "Refill"
            case MachineStateType.Phase.Descaling:           return "Descaling"
            case MachineStateType.Phase.Cleaning:            return "Cleaning"
            default:                                         return "Unknown"
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
            accessibleName: "Machine status: " + root.statusText
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
                text: "Machine Status"
                color: Theme.textSecondaryColor
                font: Theme.labelFont
            }
        }

        AccessibleMouseArea {
            anchors.fill: parent
            accessibleName: "Machine status: " + root.statusText
        }
    }
}
