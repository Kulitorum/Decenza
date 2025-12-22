import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App
import "../components"

Page {
    objectName: "idlePage"
    background: Rectangle { color: Theme.backgroundColor }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: 60  // Leave room for status bar
        spacing: 30

        // Profile selector
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 400
            Layout.preferredHeight: 60
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.smallMargin

                Text {
                    text: "Profile:"
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                }

                Text {
                    Layout.fillWidth: true
                    text: MainController.currentProfileName
                    color: Theme.textColor
                    font: Theme.bodyFont
                    elide: Text.ElideRight
                }

                Button {
                    text: "Change"
                    onClicked: profileDialog.open()
                }
            }
        }

        // Status section
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 50

            // Temperature
            Column {
                spacing: 5
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: DE1Device.temperature.toFixed(1) + "°C"
                    color: Theme.temperatureColor
                    font: Theme.valueFont
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Group Temp"
                    color: Theme.textSecondaryColor
                    font: Theme.labelFont
                }
            }

            // Water level
            Column {
                spacing: 5
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: DE1Device.waterLevel.toFixed(0) + "%"
                    color: DE1Device.waterLevel > 20 ? Theme.primaryColor : Theme.warningColor
                    font: Theme.valueFont
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Water Level"
                    color: Theme.textSecondaryColor
                    font: Theme.labelFont
                }
            }

            // Connection status
            ConnectionIndicator {
                machineConnected: DE1Device.connected
            }
        }

        Item { Layout.fillHeight: true }

        // Main action buttons
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 30

            ActionButton {
                text: "Espresso"
                iconSource: "qrc:/icons/espresso.svg"
                enabled: MachineState.isReady && DE1Device.connected
                onClicked: {
                    MainController.uploadCurrentProfile()
                    DE1Device.startEspresso()
                }
            }

            ActionButton {
                text: "Steam"
                iconSource: "qrc:/icons/steam.svg"
                enabled: DE1Device.connected
                onClicked: root.goToSteam()
            }

            ActionButton {
                text: "Hot Water"
                iconSource: "qrc:/icons/water.svg"
                enabled: DE1Device.connected
                onClicked: root.goToHotWater()
            }

            ActionButton {
                text: "Flush"
                iconSource: "qrc:/icons/flush.svg"
                enabled: MachineState.isReady && DE1Device.connected
                onClicked: DE1Device.startFlush()
            }
        }

        Item { Layout.fillHeight: true }

        // Target weight slider
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 500
            spacing: 20

            Text {
                text: "Target:"
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
            }

            Slider {
                Layout.fillWidth: true
                from: 20
                to: 60
                stepSize: 1
                value: MainController.targetWeight
                onMoved: MainController.targetWeight = value
            }

            Text {
                text: MainController.targetWeight.toFixed(0) + "g"
                color: Theme.textColor
                font: Theme.bodyFont
                Layout.preferredWidth: 50
            }
        }
    }

    // Settings button
    RoundButton {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        width: 60
        height: 60
        icon.source: "qrc:/icons/settings.svg"
        icon.width: 32
        icon.height: 32
        onClicked: root.goToSettings()
    }

    // Profile selection dialog
    Dialog {
        id: profileDialog
        title: "Select Profile"
        anchors.centerIn: parent
        width: 400
        height: 500
        modal: true
        standardButtons: Dialog.Cancel

        ListView {
            anchors.fill: parent
            model: MainController.availableProfiles
            delegate: ItemDelegate {
                width: parent.width
                text: modelData
                highlighted: modelData === MainController.currentProfileName
                onClicked: {
                    MainController.loadProfile(modelData)
                    profileDialog.close()
                }
            }
        }
    }
}
