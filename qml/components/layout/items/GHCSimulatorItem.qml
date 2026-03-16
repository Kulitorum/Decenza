import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "../.."

// Layout widget with 5 buttons to control the DE1 simulator:
// Espresso, Steam, Hot Water, Flush, and Stop.
// Only functional when simulation mode is active (Settings.simulationMode).
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // --- COMPACT MODE (status bar) ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        RowLayout {
            id: compactRow
            anchors.centerIn: parent
            spacing: Theme.scaled(4)

            Repeater {
                model: [
                    { icon: "qrc:/icons/espresso.svg", label: "E", action: function() { DE1Device.startEspresso() } },
                    { icon: "qrc:/icons/steam.svg", label: "S", action: function() { DE1Device.startSteam() } },
                    { icon: "qrc:/icons/hotwater.svg", label: "W", action: function() { DE1Device.startHotWater() } },
                    { icon: "qrc:/icons/flush.svg", label: "F", action: function() { DE1Device.startFlush() } },
                    { icon: "qrc:/icons/stop.svg", label: "X", action: function() { DE1Device.requestIdle() } }
                ]

                Rectangle {
                    width: Theme.scaled(32)
                    height: Theme.scaled(32)
                    radius: Theme.scaled(6)
                    color: compactArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    Accessible.ignored: true

                    Image {
                        anchors.centerIn: parent
                        source: modelData.icon
                        sourceSize.width: Theme.scaled(18)
                        sourceSize.height: Theme.scaled(18)
                        Accessible.ignored: true
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: Theme.surfaceColor
                        }
                    }

                    MouseArea {
                        id: compactArea
                        anchors.fill: parent
                        onClicked: modelData.action()
                    }
                }
            }
        }
    }

    // --- FULL MODE (grid) ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: Theme.scaled(200)
        implicitHeight: Theme.scaled(200)

        Rectangle {
            anchors.fill: parent
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                spacing: Theme.scaled(4)

                Text {
                    text: "GHC Simulator"
                    color: Theme.textColor
                    font: Theme.captionFont
                    Layout.alignment: Qt.AlignHCenter
                    Accessible.ignored: true
                }

                // Top row: Espresso, Steam
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    ActionButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        translationKey: "idle.button.espresso"
                        translationFallback: "Espresso"
                        iconSource: "qrc:/icons/espresso.svg"
                        backgroundColor: Theme.primaryColor
                        onClicked: DE1Device.startEspresso()
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        translationKey: "idle.button.steam"
                        translationFallback: "Steam"
                        iconSource: "qrc:/icons/steam.svg"
                        backgroundColor: Theme.primaryColor
                        onClicked: DE1Device.startSteam()
                    }
                }

                // Middle row: Hot Water, Flush
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    ActionButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        translationKey: "idle.button.hotwater"
                        translationFallback: "Water"
                        iconSource: "qrc:/icons/hotwater.svg"
                        backgroundColor: Theme.primaryColor
                        onClicked: DE1Device.startHotWater()
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        translationKey: "idle.button.flush"
                        translationFallback: "Flush"
                        iconSource: "qrc:/icons/flush.svg"
                        backgroundColor: Theme.primaryColor
                        onClicked: DE1Device.startFlush()
                    }
                }

                // Bottom: Stop
                ActionButton {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    translationKey: "common.button.stop"
                    translationFallback: "Stop"
                    iconSource: "qrc:/icons/stop.svg"
                    backgroundColor: Theme.errorColor
                    onClicked: DE1Device.requestIdle()
                }
            }
        }
    }
}
