import QtQuick
import QtQuick.Layouts
import Decenza

ColumnLayout {
    id: connectionIndicator

    property bool machineConnected: false
    property bool scaleConnected: false
    property bool isFlowScale: false

    spacing: Theme.spacingSmall

    // Connection status (Online/Offline)
    Item {
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: connectionIndicator.machineConnected ? onlineText.implicitWidth : offlineText.implicitWidth
        implicitHeight: connectionIndicator.machineConnected ? onlineText.implicitHeight : offlineText.implicitHeight

        Tr {
            id: onlineText
            key: "connection.online"
            fallback: "Online"
            visible: connectionIndicator.machineConnected
            color: Theme.successColor
            font: Theme.valueFont
            Accessible.ignored: true
        }

        Tr {
            id: offlineText
            key: "connection.offline"
            fallback: "Offline"
            visible: !connectionIndicator.machineConnected
            color: Theme.errorColor
            font: Theme.valueFont
            Accessible.ignored: true
        }
    }

    // Connection details (Machine, Machine + Scale, etc.)
    Item {
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: Math.max(machineOnlyText.visible ? machineOnlyText.implicitWidth : 0,
                                machineScaleText.visible ? machineScaleText.implicitWidth : 0,
                                machineSimulatedText.visible ? machineSimulatedText.implicitWidth : 0)
        implicitHeight: machineOnlyText.implicitHeight

        Tr {
            id: machineOnlyText
            key: "connection.machine"
            fallback: "Machine"
            visible: !connectionIndicator.machineConnected || (connectionIndicator.machineConnected && !connectionIndicator.scaleConnected && !connectionIndicator.isFlowScale)
            color: Theme.textSecondaryColor
            font: Theme.labelFont
            Accessible.ignored: true
        }

        Tr {
            id: machineScaleText
            key: "connection.machineScale"
            fallback: "Machine + Scale"
            visible: connectionIndicator.machineConnected && connectionIndicator.scaleConnected && !connectionIndicator.isFlowScale
            color: Theme.textSecondaryColor
            font: Theme.labelFont
            Accessible.ignored: true
        }

        Tr {
            id: machineSimulatedText
            key: "connection.machineSimulatedScale"
            fallback: "Machine + Simulated Scale"
            visible: connectionIndicator.machineConnected && connectionIndicator.isFlowScale
            color: Theme.textSecondaryColor
            font: Theme.labelFont
            Accessible.ignored: true
        }
    }
}
