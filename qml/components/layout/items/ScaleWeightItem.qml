import QtQuick
import QtQuick.Layouts
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    property bool isFlowScale: ScaleDevice && ScaleDevice.name === "Flow Scale"
    property bool scaleConnected: ScaleDevice && ScaleDevice.connected

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactScaleRow.implicitWidth
        implicitHeight: compactScaleRow.implicitHeight

        Row {
            id: compactScaleRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall
            visible: root.scaleConnected

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.scaled(8)
                height: Theme.scaled(8)
                radius: Theme.scaled(4)
                color: root.isFlowScale ? Theme.textSecondaryColor : Theme.weightColor
            }

            Text {
                text: {
                    var weight = MachineState.scaleWeight.toFixed(1)
                    var suffix = root.isFlowScale ? "g~" : "g"
                    if (Settings.hasBrewYieldOverride) {
                        return weight + suffix + " 1:" + MainController.brewByRatio.toFixed(1)
                    }
                    return weight + suffix
                }
                color: Settings.hasBrewYieldOverride ? Theme.primaryColor
                     : root.isFlowScale ? Theme.textSecondaryColor
                     : Theme.weightColor
                font: Theme.bodyFont
            }
        }

        Text {
            anchors.centerIn: parent
            visible: !root.scaleConnected
            text: "--"
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
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
                visible: root.scaleConnected
                text: {
                    var weight = MachineState.scaleWeight.toFixed(1)
                    var suffix = root.isFlowScale ? "g~" : "g"
                    if (Settings.hasBrewYieldOverride) {
                        return weight + suffix + " 1:" + MainController.brewByRatio.toFixed(1)
                    }
                    return weight + suffix
                }
                color: Settings.hasBrewYieldOverride ? Theme.primaryColor
                     : root.isFlowScale ? Theme.textSecondaryColor
                     : Theme.weightColor
                font: Theme.valueFont
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: !root.scaleConnected
                text: "--"
                color: Theme.textSecondaryColor
                font: Theme.valueFont
            }

            Tr {
                Layout.alignment: Qt.AlignHCenter
                key: "idle.label.scaleweight"
                fallback: "Scale Weight"
                color: Theme.textSecondaryColor
                font: Theme.labelFont
            }
        }
    }
}
