import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App
import "../components"

Page {
    objectName: "hotWaterPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: {
        root.currentPageTitle = "Hot Water"
        MainController.applyHotWaterSettings()
    }

    property bool isDispensing: MachineState.phase === MachineStateType.Phase.HotWater
    property int editingCupIndex: -1

    // Get current cup's volume
    function getCurrentCupVolume() {
        var preset = Settings.getWaterCupPreset(Settings.selectedWaterCup)
        return preset ? preset.volume : 200
    }

    function getCurrentCupName() {
        var preset = Settings.getWaterCupPreset(Settings.selectedWaterCup)
        return preset ? preset.name : ""
    }

    // Save current cup with new volume
    function saveCurrentCup(volume) {
        var name = getCurrentCupName()
        if (name) {
            Settings.updateWaterCupPreset(Settings.selectedWaterCup, name, volume)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: 80
        anchors.bottomMargin: 80  // Space for bottom bar
        spacing: 15

        // === DISPENSING VIEW ===
        ColumnLayout {
            visible: isDispensing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 20

            // Timer
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: MachineState.shotTime.toFixed(1) + "s"
                color: Theme.textColor
                font: Theme.timerFont
            }

            // Temperature display
            CircularGauge {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 200
                Layout.preferredHeight: 200
                value: DE1Device.temperature
                minValue: 60
                maxValue: 100
                unit: "°C"
                color: Theme.primaryColor
                label: "Water Temp"
            }

            Item { Layout.fillHeight: true }

            // Stop button
            ActionButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 300
                Layout.preferredHeight: 80
                text: "STOP"
                backgroundColor: Theme.accentColor
                onClicked: {
                    DE1Device.stopOperation()
                    root.goToIdle()
                }
            }
        }

        // === SETTINGS VIEW ===
        ColumnLayout {
            visible: !isDispensing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // Cup Presets Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 90
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Cup Preset"
                            color: Theme.textColor
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.bold: true
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "Drag to reorder, hold to rename"
                            color: Theme.textSecondaryColor
                            font: Theme.labelFont
                        }
                    }

                    // Cup preset buttons with drag-and-drop
                    Row {
                        id: cupPresetsRow
                        Layout.fillWidth: true
                        spacing: 8

                        property int draggedIndex: -1

                        Repeater {
                            id: cupRepeater
                            model: Settings.waterCupPresets

                            Item {
                                id: cupDelegate
                                width: cupPill.width
                                height: 36

                                property int cupIndex: index

                                Rectangle {
                                    id: cupPill
                                    width: cupText.implicitWidth + 24
                                    height: 36
                                    radius: 18
                                    color: cupDelegate.cupIndex === Settings.selectedWaterCup ? Theme.primaryColor : Theme.backgroundColor
                                    border.color: cupDelegate.cupIndex === Settings.selectedWaterCup ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: dragArea.drag.active ? 0.8 : 1.0

                                    Drag.active: dragArea.drag.active
                                    Drag.source: cupDelegate
                                    Drag.hotSpot.x: width / 2
                                    Drag.hotSpot.y: height / 2

                                    states: State {
                                        when: dragArea.drag.active
                                        ParentChange { target: cupPill; parent: cupPresetsRow }
                                        AnchorChanges { target: cupPill; anchors.verticalCenter: undefined }
                                    }

                                    Text {
                                        id: cupText
                                        anchors.centerIn: parent
                                        text: modelData.name
                                        color: cupDelegate.cupIndex === Settings.selectedWaterCup ? "white" : Theme.textColor
                                        font: Theme.bodyFont
                                    }

                                    MouseArea {
                                        id: dragArea
                                        anchors.fill: parent
                                        drag.target: cupPill
                                        drag.axis: Drag.XAxis

                                        property bool held: false
                                        property bool moved: false

                                        onPressed: {
                                            held = false
                                            moved = false
                                            holdTimer.start()
                                        }

                                        onReleased: {
                                            holdTimer.stop()
                                            if (!moved && !held) {
                                                // Simple click - select the cup
                                                Settings.selectedWaterCup = cupDelegate.cupIndex
                                                volumeInput.value = modelData.volume
                                                Settings.waterVolume = modelData.volume
                                                MainController.applyHotWaterSettings()
                                            }
                                            cupPill.Drag.drop()
                                            cupPresetsRow.draggedIndex = -1
                                        }

                                        onPositionChanged: {
                                            if (drag.active) {
                                                moved = true
                                                cupPresetsRow.draggedIndex = cupDelegate.cupIndex
                                            }
                                        }

                                        Timer {
                                            id: holdTimer
                                            interval: 500
                                            onTriggered: {
                                                if (!dragArea.moved) {
                                                    dragArea.held = true
                                                    editingCupIndex = cupDelegate.cupIndex
                                                    editCupNameInput.text = modelData.name
                                                    editCupPopup.open()
                                                }
                                            }
                                        }
                                    }
                                }

                                DropArea {
                                    anchors.fill: parent
                                    onEntered: function(drag) {
                                        var fromIndex = drag.source.cupIndex
                                        var toIndex = cupDelegate.cupIndex
                                        if (fromIndex !== toIndex) {
                                            Settings.moveWaterCupPreset(fromIndex, toIndex)
                                        }
                                    }
                                }
                            }
                        }

                        // Add button
                        Rectangle {
                            width: 36
                            height: 36
                            radius: 18
                            color: Theme.backgroundColor
                            border.color: Theme.textSecondaryColor
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "+"
                                color: Theme.textColor
                                font.pixelSize: 20
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: addCupDialog.open()
                            }
                        }
                    }
                }
            }

            // Volume (per-cup, auto-saves)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    Text {
                        text: "Volume"
                        color: Theme.textColor
                        font: Theme.bodyFont
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        id: volumeInput
                        value: getCurrentCupVolume()
                        from: 50
                        to: 500
                        stepSize: 10
                        suffix: " ml"
                        valueColor: Theme.primaryColor

                        onValueModified: function(newValue) {
                            volumeInput.value = newValue
                            Settings.waterVolume = newValue
                            saveCurrentCup(newValue)
                            MainController.applyHotWaterSettings()
                        }
                    }
                }
            }

            // Temperature (global setting)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    Text {
                        text: "Temperature"
                        color: Theme.textColor
                        font: Theme.bodyFont
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        id: temperatureInput
                        value: Settings.waterTemperature
                        from: 40
                        to: 100
                        stepSize: 1
                        suffix: "°C"
                        valueColor: Theme.temperatureColor

                        onValueModified: function(newValue) {
                            temperatureInput.value = newValue
                            Settings.waterTemperature = newValue
                            MainController.applyHotWaterSettings()
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // Start button
            ActionButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 300
                Layout.preferredHeight: 80
                text: "START"
                enabled: MachineState.isReady
                onClicked: DE1Device.startHotWater()
            }
        }
    }

    // Bottom bar
    Rectangle {
        visible: !isDispensing
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 70
        color: Theme.primaryColor

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 20
            spacing: 15

            // Back button (large hitbox, icon aligned left)
            RoundButton {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 70
                flat: true
                icon.source: "qrc:/icons/back.svg"
                icon.width: 28
                icon.height: 28
                icon.color: "white"
                display: AbstractButton.IconOnly
                leftPadding: 0
                rightPadding: 52
                onClicked: {
                    MainController.applyHotWaterSettings()
                    root.goToIdle()
                }
            }

            Text {
                text: getCurrentCupName() || "No cup"
                color: "white"
                font.pixelSize: 20
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Text {
                text: volumeInput.value.toFixed(0) + " ml"
                color: "white"
                font: Theme.bodyFont
            }

            Rectangle { width: 1; height: 30; color: "white"; opacity: 0.3 }

            Text {
                text: temperatureInput.value.toFixed(0) + "°C"
                color: "white"
                font: Theme.bodyFont
            }
        }
    }

    // Tap anywhere to stop (when dispensing)
    MouseArea {
        anchors.fill: parent
        z: -1
        visible: isDispensing
        onClicked: {
            DE1Device.stopOperation()
            root.goToIdle()
        }
    }

    // Edit cup preset popup
    Popup {
        id: editCupPopup
        anchors.centerIn: parent
        width: 300
        height: 180
        modal: true
        focus: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            Text {
                text: "Edit Cup Preset"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            TextField {
                id: editCupNameInput
                Layout.fillWidth: true
                placeholderText: "Cup name"
                font: Theme.bodyFont
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Button {
                    text: "Delete"
                    onClicked: {
                        Settings.removeWaterCupPreset(editingCupIndex)
                        editCupPopup.close()
                    }
                    background: Rectangle {
                        implicitWidth: 80
                        implicitHeight: 36
                        radius: 6
                        color: Theme.errorColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Cancel"
                    onClicked: editCupPopup.close()
                    background: Rectangle {
                        implicitWidth: 70
                        implicitHeight: 36
                        radius: 6
                        color: Theme.backgroundColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: Theme.textColor
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    text: "Save"
                    onClicked: {
                        var preset = Settings.getWaterCupPreset(editingCupIndex)
                        Settings.updateWaterCupPreset(editingCupIndex, editCupNameInput.text, preset.volume)
                        editCupPopup.close()
                    }
                    background: Rectangle {
                        implicitWidth: 70
                        implicitHeight: 36
                        radius: 6
                        color: Theme.primaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }

    // Add cup dialog
    Dialog {
        id: addCupDialog
        title: "Add Cup Preset"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            spacing: 12

            TextField {
                id: newCupNameInput
                Layout.preferredWidth: 200
                placeholderText: "Cup name"
                font: Theme.bodyFont
            }
        }

        onAccepted: {
            if (newCupNameInput.text.length > 0) {
                Settings.addWaterCupPreset(newCupNameInput.text, 200)
                newCupNameInput.text = ""
            }
        }

        onOpened: {
            newCupNameInput.text = ""
            newCupNameInput.forceActiveFocus()
        }
    }
}
