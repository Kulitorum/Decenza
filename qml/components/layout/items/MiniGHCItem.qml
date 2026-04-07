import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "../.."

// Layout widget with 5 buttons to control the DE1 simulator:
// Espresso, Steam, Hot Water, Flush, and Stop.
// Disabled when simulation mode is off to prevent sending real BLE commands.
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    enabled: DE1Device.simulationMode || DE1Device.isHeadless
    opacity: (DE1Device.simulationMode || DE1Device.isHeadless) ? 1.0 : 0.4

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
                    { icon: "qrc:/icons/espresso.svg", name: "Espresso", nameKey: "idle.button.espresso", action: function() { DE1Device.startEspresso() } },
                    { icon: "qrc:/icons/steam.svg", name: "Steam", nameKey: "idle.button.steam", action: function() { DE1Device.startSteam() } },
                    { icon: "qrc:/icons/water.svg", name: "Hot Water", nameKey: "idle.button.hotwater", action: function() { DE1Device.startHotWater() } },
                    { icon: "qrc:/icons/flush.svg", name: "Flush", nameKey: "idle.button.flush", action: function() { DE1Device.startFlush() } },
                    { icon: "qrc:/icons/hand.svg", name: "Stop", nameKey: "common.button.stop", action: function() { DE1Device.requestIdle() } }
                ]

                Rectangle {
                    id: compactBtn
                    width: Theme.scaled(32)
                    height: Theme.scaled(32)
                    radius: Theme.scaled(6)
                    color: compactArea.isPressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
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

                    AccessibleMouseArea {
                        id: compactArea
                        anchors.fill: parent
                        accessibleName: TranslationManager.translate(modelData.nameKey, modelData.name)
                        accessibleItem: compactBtn
                        onAccessibleClicked: modelData.action()
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

                Tr {
                    key: "ghcSimulator.title"
                    fallback: "Mini GHC"
                    color: Theme.textColor
                    font: Theme.captionFont
                    Layout.alignment: Qt.AlignHCenter
                    Accessible.ignored: true
                }

                // Top row: Espresso, Steam
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.scaled(4)
                    MiniGHCButton { translationKey: "idle.button.espresso"; translationFallback: "Espresso"; iconSource: "qrc:/icons/espresso.svg"; buttonColor: Theme.primaryColor; onTapped: DE1Device.startEspresso() }
                    MiniGHCButton { translationKey: "idle.button.steam";    translationFallback: "Steam";    iconSource: "qrc:/icons/steam.svg";    buttonColor: Theme.primaryColor; onTapped: DE1Device.startSteam() }
                }

                // Middle row: Hot Water, Flush
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.scaled(4)
                    MiniGHCButton { translationKey: "idle.button.hotwater"; translationFallback: "Water"; iconSource: "qrc:/icons/water.svg"; buttonColor: Theme.primaryColor; onTapped: DE1Device.startHotWater() }
                    MiniGHCButton { translationKey: "idle.button.flush";    translationFallback: "Flush"; iconSource: "qrc:/icons/flush.svg"; buttonColor: Theme.primaryColor; onTapped: DE1Device.startFlush() }
                }

                // Bottom: Stop
                MiniGHCButton {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    translationKey: "common.button.stop"
                    translationFallback: "Stop"
                    iconSource: "qrc:/icons/hand.svg"
                    buttonColor: Theme.errorColor
                    onTapped: DE1Device.requestIdle()
                }
            }

            component MiniGHCButton: Rectangle {
                id: btn
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.buttonRadius
                property string translationKey: ""
                property string translationFallback: ""
                property string iconSource: ""
                property color buttonColor: Theme.primaryColor
                signal tapped()

                readonly property string _label: {
                    var _ = TranslationManager.translationVersion
                    return TranslationManager.translate(translationKey, translationFallback)
                }

                color: btnArea.isPressed ? Qt.darker(buttonColor, 1.2) : buttonColor
                Accessible.ignored: true
                clip: true

                Image {
                    id: btnIcon
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -Math.round(btnLabel.implicitHeight / 2) - Theme.scaled(1)
                    source: btn.iconSource
                    sourceSize.width: Theme.scaled(18)
                    sourceSize.height: Theme.scaled(18)
                    Accessible.ignored: true
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.surfaceColor
                    }
                }

                Text {
                    id: btnLabel
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: btnIcon.bottom
                    anchors.topMargin: Theme.scaled(2)
                    text: btn._label
                    color: Theme.actionButtonContentColor
                    font: Theme.captionFont
                    Accessible.ignored: true
                }

                AccessibleMouseArea {
                    id: btnArea
                    anchors.fill: parent
                    accessibleName: btn._label
                    accessibleItem: btn
                    onAccessibleClicked: btn.tapped()
                }
            }
        }
    }
}
