import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    objectName: "hotWaterPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: {
        root.currentPageTitle = "Hot Water"
        MainController.applyHotWaterSettings()
    }
    StackView.onActivated: root.currentPageTitle = "Hot Water"

    property bool isDispensing: MachineState.phase === MachineStateType.Phase.HotWater
    property int editingVesselIndex: -1

    // Get current vessel's volume
    function getCurrentVesselVolume() {
        var preset = Settings.getWaterVesselPreset(Settings.selectedWaterVessel)
        return preset ? preset.volume : 200
    }

    function getCurrentVesselName() {
        var preset = Settings.getWaterVesselPreset(Settings.selectedWaterVessel)
        return preset ? preset.name : ""
    }

    // Save current vessel with new volume
    function saveCurrentVessel(volume) {
        var name = getCurrentVesselName()
        if (name) {
            Settings.updateWaterVesselPreset(Settings.selectedWaterVessel, name, volume)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.pageTopMargin  // Space for bottom bar
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

            // Vessel Presets Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(90)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Vessel Preset"
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

                    // Vessel preset buttons with drag-and-drop
                    Row {
                        id: vesselPresetsRow
                        Layout.fillWidth: true
                        spacing: 8

                        property int draggedIndex: -1

                        Repeater {
                            id: vesselRepeater
                            model: Settings.waterVesselPresets

                            Item {
                                id: vesselDelegate
                                width: vesselPill.width
                                height: 36

                                property int vesselIndex: index

                                Rectangle {
                                    id: vesselPill
                                    width: vesselText.implicitWidth + 24
                                    height: 36
                                    radius: 18
                                    color: vesselDelegate.vesselIndex === Settings.selectedWaterVessel ? Theme.primaryColor : Theme.backgroundColor
                                    border.color: vesselDelegate.vesselIndex === Settings.selectedWaterVessel ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: dragArea.drag.active ? 0.8 : 1.0

                                    Drag.active: dragArea.drag.active
                                    Drag.source: vesselDelegate
                                    Drag.hotSpot.x: width / 2
                                    Drag.hotSpot.y: height / 2

                                    states: State {
                                        when: dragArea.drag.active
                                        ParentChange { target: vesselPill; parent: vesselPresetsRow }
                                        AnchorChanges { target: vesselPill; anchors.verticalCenter: undefined }
                                    }

                                    Text {
                                        id: vesselText
                                        anchors.centerIn: parent
                                        text: modelData.name
                                        color: vesselDelegate.vesselIndex === Settings.selectedWaterVessel ? "white" : Theme.textColor
                                        font: Theme.bodyFont
                                    }

                                    MouseArea {
                                        id: dragArea
                                        anchors.fill: parent
                                        drag.target: vesselPill
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
                                                // Simple click - select the vessel
                                                Settings.selectedWaterVessel = vesselDelegate.vesselIndex
                                                volumeInput.value = modelData.volume
                                                Settings.waterVolume = modelData.volume
                                                MainController.applyHotWaterSettings()
                                            }
                                            vesselPill.Drag.drop()
                                            vesselPresetsRow.draggedIndex = -1
                                        }

                                        onPositionChanged: {
                                            if (drag.active) {
                                                moved = true
                                                vesselPresetsRow.draggedIndex = vesselDelegate.vesselIndex
                                            }
                                        }

                                        Timer {
                                            id: holdTimer
                                            interval: 500
                                            onTriggered: {
                                                if (!dragArea.moved) {
                                                    dragArea.held = true
                                                    editingVesselIndex = vesselDelegate.vesselIndex
                                                    editVesselNameInput.text = modelData.name
                                                    editVesselPopup.open()
                                                }
                                            }
                                        }
                                    }
                                }

                                DropArea {
                                    anchors.fill: parent
                                    onEntered: function(drag) {
                                        var fromIndex = drag.source.vesselIndex
                                        var toIndex = vesselDelegate.vesselIndex
                                        if (fromIndex !== toIndex) {
                                            Settings.moveWaterVesselPreset(fromIndex, toIndex)
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
                                onClicked: addVesselDialog.open()
                            }
                        }
                    }
                }
            }

            // Volume (per-vessel, auto-saves)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(100)
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
                        Layout.preferredWidth: Theme.scaled(150)
                        value: getCurrentVesselVolume()
                        from: 50
                        to: 500
                        stepSize: 10
                        suffix: " ml"
                        valueColor: Theme.primaryColor

                        onValueModified: function(newValue) {
                            volumeInput.value = newValue
                            Settings.waterVolume = newValue
                            saveCurrentVessel(newValue)
                            MainController.applyHotWaterSettings()
                        }
                    }
                }
            }

            // Temperature (global setting)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(100)
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
                        Layout.preferredWidth: Theme.scaled(150)
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
        }
    }

    // Bottom bar
    BottomBar {
        visible: !isDispensing
        title: getCurrentVesselName() || "No vessel"
        onBackClicked: {
            MainController.applyHotWaterSettings()
            root.goToIdle()
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingMedium

            Text {
                text: volumeInput.value.toFixed(0) + " ml"
                color: "white"
                font: Theme.bodyFont
            }

            Rectangle { width: 1; height: Theme.scaled(30); color: "white"; opacity: 0.3 }

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

    // Edit vessel preset popup
    Popup {
        id: editVesselPopup
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
                text: "Edit Vessel Preset"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            TextField {
                id: editVesselNameInput
                Layout.fillWidth: true
                placeholderText: "Vessel name"
                font: Theme.bodyFont
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Button {
                    text: "Delete"
                    onClicked: {
                        Settings.removeWaterVesselPreset(editingVesselIndex)
                        editVesselPopup.close()
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
                    onClicked: editVesselPopup.close()
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
                        var preset = Settings.getWaterVesselPreset(editingVesselIndex)
                        Settings.updateWaterVesselPreset(editingVesselIndex, editVesselNameInput.text, preset.volume)
                        editVesselPopup.close()
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

    // Add vessel dialog
    Dialog {
        id: addVesselDialog
        title: "Add Vessel Preset"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            spacing: 12

            TextField {
                id: newVesselNameInput
                Layout.preferredWidth: 200
                placeholderText: "Vessel name"
                font: Theme.bodyFont
            }
        }

        onAccepted: {
            if (newVesselNameInput.text.length > 0) {
                Settings.addWaterVesselPreset(newVesselNameInput.text, 200)
                newVesselNameInput.text = ""
            }
        }

        onOpened: {
            newVesselNameInput.text = ""
            newVesselNameInput.forceActiveFocus()
        }
    }
}
