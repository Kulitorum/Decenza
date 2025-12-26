import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App
import "../components"

Page {
    objectName: "flushPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: {
        root.currentPageTitle = "Flush"
        MainController.applyFlushSettings()
    }

    property bool isFlushing: MachineState.phase === MachineStateType.Phase.Flushing
    property int editingPresetIndex: -1

    // Get current preset values
    function getCurrentPresetFlow() {
        var preset = Settings.getFlushPreset(Settings.selectedFlushPreset)
        return preset ? preset.flow : 6.0
    }

    function getCurrentPresetSeconds() {
        var preset = Settings.getFlushPreset(Settings.selectedFlushPreset)
        return preset ? preset.seconds : 5.0
    }

    function getCurrentPresetName() {
        var preset = Settings.getFlushPreset(Settings.selectedFlushPreset)
        return preset ? preset.name : ""
    }

    // Save current preset with new values
    function saveCurrentPreset(flow, seconds) {
        var name = getCurrentPresetName()
        if (name) {
            Settings.updateFlushPreset(Settings.selectedFlushPreset, name, flow, seconds)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: 80
        anchors.bottomMargin: 80  // Space for bottom bar
        spacing: 15

        // === FLUSHING VIEW ===
        ColumnLayout {
            visible: isFlushing
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

            // Progress indicator
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Rinsing group head..."
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
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
            visible: !isFlushing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // Presets Section
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
                            text: "Flush Preset"
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

                    // Preset buttons with drag-and-drop
                    Row {
                        id: presetsRow
                        Layout.fillWidth: true
                        spacing: 8

                        property int draggedIndex: -1

                        Repeater {
                            id: presetRepeater
                            model: Settings.flushPresets

                            Item {
                                id: presetDelegate
                                width: presetPill.width
                                height: 36

                                property int presetIndex: index

                                Rectangle {
                                    id: presetPill
                                    width: presetText.implicitWidth + 24
                                    height: 36
                                    radius: 18
                                    color: presetDelegate.presetIndex === Settings.selectedFlushPreset ? Theme.primaryColor : Theme.backgroundColor
                                    border.color: presetDelegate.presetIndex === Settings.selectedFlushPreset ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: dragArea.drag.active ? 0.8 : 1.0

                                    Drag.active: dragArea.drag.active
                                    Drag.source: presetDelegate
                                    Drag.hotSpot.x: width / 2
                                    Drag.hotSpot.y: height / 2

                                    states: State {
                                        when: dragArea.drag.active
                                        ParentChange { target: presetPill; parent: presetsRow }
                                        AnchorChanges { target: presetPill; anchors.verticalCenter: undefined }
                                    }

                                    Text {
                                        id: presetText
                                        anchors.centerIn: parent
                                        text: modelData.name
                                        color: presetDelegate.presetIndex === Settings.selectedFlushPreset ? "white" : Theme.textColor
                                        font: Theme.bodyFont
                                    }

                                    MouseArea {
                                        id: dragArea
                                        anchors.fill: parent
                                        drag.target: presetPill
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
                                                // Simple click - select the preset
                                                Settings.selectedFlushPreset = presetDelegate.presetIndex
                                                flowInput.value = modelData.flow
                                                secondsInput.value = modelData.seconds
                                                Settings.flushFlow = modelData.flow
                                                Settings.flushSeconds = modelData.seconds
                                                MainController.applyFlushSettings()
                                            }
                                            presetPill.Drag.drop()
                                            presetsRow.draggedIndex = -1
                                        }

                                        onPositionChanged: {
                                            if (drag.active) {
                                                moved = true
                                                presetsRow.draggedIndex = presetDelegate.presetIndex
                                            }
                                        }

                                        Timer {
                                            id: holdTimer
                                            interval: 500
                                            onTriggered: {
                                                if (!dragArea.moved) {
                                                    dragArea.held = true
                                                    editingPresetIndex = presetDelegate.presetIndex
                                                    editPresetNameInput.text = modelData.name
                                                    editPresetPopup.open()
                                                }
                                            }
                                        }
                                    }
                                }

                                DropArea {
                                    anchors.fill: parent
                                    onEntered: function(drag) {
                                        var fromIndex = drag.source.presetIndex
                                        var toIndex = presetDelegate.presetIndex
                                        if (fromIndex !== toIndex) {
                                            Settings.moveFlushPreset(fromIndex, toIndex)
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
                                onClicked: addPresetDialog.open()
                            }
                        }
                    }
                }
            }

            // Duration (per-preset, auto-saves)
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
                        text: "Duration"
                        color: Theme.textColor
                        font: Theme.bodyFont
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        id: secondsInput
                        value: getCurrentPresetSeconds()
                        from: 1
                        to: 30
                        stepSize: 0.5
                        suffix: " s"
                        valueColor: Theme.primaryColor

                        onValueModified: function(newValue) {
                            secondsInput.value = newValue
                            Settings.flushSeconds = newValue
                            saveCurrentPreset(flowInput.value, newValue)
                            MainController.applyFlushSettings()
                        }
                    }
                }
            }

            // Flow Rate (per-preset, auto-saves)
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
                        text: "Flow Rate"
                        color: Theme.textColor
                        font: Theme.bodyFont
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        id: flowInput
                        value: getCurrentPresetFlow()
                        from: 2
                        to: 10
                        stepSize: 0.5
                        suffix: " mL/s"
                        valueColor: Theme.flowColor

                        onValueModified: function(newValue) {
                            flowInput.value = newValue
                            Settings.flushFlow = newValue
                            saveCurrentPreset(newValue, secondsInput.value)
                            MainController.applyFlushSettings()
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
                text: "START FLUSH"
                enabled: MachineState.isReady
                onClicked: {
                    MainController.applyFlushSettings()
                    DE1Device.startFlush()
                }
            }
        }
    }

    // Bottom bar
    Rectangle {
        visible: !isFlushing
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

            RoundButton {
                Layout.preferredWidth: 100
                Layout.preferredHeight: 70
                icon.source: "qrc:/icons/back.svg"
                icon.width: 28
                icon.height: 28
                flat: true
                icon.color: "white"
                onClicked: {
                    MainController.applyFlushSettings()
                    root.goToIdle()
                }
            }

            Text {
                text: getCurrentPresetName() || "Flush"
                color: "white"
                font.pixelSize: 20
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Text {
                text: secondsInput.value.toFixed(1) + "s"
                color: "white"
                font: Theme.bodyFont
            }

            Rectangle { width: 1; height: 30; color: "white"; opacity: 0.3 }

            Text {
                text: flowInput.value.toFixed(1) + " mL/s"
                color: "white"
                font: Theme.bodyFont
            }
        }
    }

    // Tap anywhere to stop (when flushing)
    MouseArea {
        anchors.fill: parent
        z: -1
        visible: isFlushing
        onClicked: {
            DE1Device.stopOperation()
            root.goToIdle()
        }
    }

    // Edit preset popup
    Popup {
        id: editPresetPopup
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
                text: "Edit Flush Preset"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            TextField {
                id: editPresetNameInput
                Layout.fillWidth: true
                placeholderText: "Preset name"
                font: Theme.bodyFont
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Button {
                    text: "Delete"
                    onClicked: {
                        Settings.removeFlushPreset(editingPresetIndex)
                        editPresetPopup.close()
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
                    onClicked: editPresetPopup.close()
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
                        var preset = Settings.getFlushPreset(editingPresetIndex)
                        Settings.updateFlushPreset(editingPresetIndex, editPresetNameInput.text, preset.flow, preset.seconds)
                        editPresetPopup.close()
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

    // Add preset dialog
    Dialog {
        id: addPresetDialog
        title: "Add Flush Preset"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            spacing: 12

            TextField {
                id: newPresetNameInput
                Layout.preferredWidth: 200
                placeholderText: "Preset name"
                font: Theme.bodyFont
            }
        }

        onAccepted: {
            if (newPresetNameInput.text.length > 0) {
                Settings.addFlushPreset(newPresetNameInput.text, 6.0, 5.0)
                newPresetNameInput.text = ""
            }
        }

        onOpened: {
            newPresetNameInput.text = ""
            newPresetNameInput.forceActiveFocus()
        }
    }
}
