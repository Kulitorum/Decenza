import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App
import "../components"

Page {
    objectName: "steamPage"
    background: Rectangle { color: Theme.backgroundColor }

    property bool isSteaming: MachineState.phase === MachineState.Phase.Steaming
    property int editingCupIndex: -1  // For the edit popup

    // Helper to format flow as readable value (handles undefined/NaN)
    function flowToDisplay(flow) {
        if (flow === undefined || flow === null || isNaN(flow)) {
            return "7.0"  // Default
        }
        return (flow / 100).toFixed(1)
    }

    // Get current cup's values with defaults
    function getCurrentCupDuration() {
        var preset = Settings.getSteamCupPreset(Settings.selectedSteamCup)
        return preset ? preset.duration : 30
    }

    function getCurrentCupFlow() {
        var preset = Settings.getSteamCupPreset(Settings.selectedSteamCup)
        return (preset && preset.flow !== undefined) ? preset.flow : 700
    }

    function getCurrentCupName() {
        var preset = Settings.getSteamCupPreset(Settings.selectedSteamCup)
        return preset ? preset.name : ""
    }

    // Save current cup with new values
    function saveCurrentCup(duration, flow) {
        var name = getCurrentCupName()
        if (name) {
            Settings.updateSteamCupPreset(Settings.selectedSteamCup, name, duration, flow)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: 60
        spacing: 15

        // Header
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: isSteaming ? "Steaming" : "Steam Settings"
                color: Theme.textColor
                font: Theme.headingFont
            }
        }

        // === STEAMING VIEW ===
        ColumnLayout {
            visible: isSteaming
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
                minValue: 100
                maxValue: 180
                unit: "°C"
                color: Theme.temperatureColor
                label: "Steam Temp"
            }

            // Real-time flow slider (can adjust while steaming)
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 5

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "Steam Flow"
                        color: Theme.textColor
                        font: Theme.bodyFont
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: flowToDisplay(steamingFlowSlider.value)
                        color: Theme.primaryColor
                        font: Theme.bodyFont
                    }
                }

                Slider {
                    id: steamingFlowSlider
                    Layout.fillWidth: true
                    from: 200
                    to: 1200
                    stepSize: 10
                    value: Settings.steamFlow
                    onMoved: MainController.setSteamFlowImmediate(value)
                }
            }

            Item { Layout.fillHeight: true }

            // Stop button
            ActionButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 300
                Layout.preferredHeight: 100
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
            visible: !isSteaming
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
                            text: "Drag to reorder, long-press to edit"
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
                            model: Settings.steamCupPresets

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
                                    color: cupDelegate.cupIndex === Settings.selectedSteamCup ? Theme.primaryColor : Theme.backgroundColor
                                    border.color: cupDelegate.cupIndex === Settings.selectedSteamCup ? Theme.primaryColor : Theme.textSecondaryColor
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
                                        color: cupDelegate.cupIndex === Settings.selectedSteamCup ? "white" : Theme.textColor
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
                                                Settings.selectedSteamCup = cupDelegate.cupIndex
                                                var flow = modelData.flow !== undefined ? modelData.flow : 700
                                                durationSlider.value = modelData.duration
                                                flowSlider.value = flow
                                                Settings.steamTimeout = modelData.duration
                                                Settings.steamFlow = flow
                                                MainController.applySteamSettings()
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
                                            Settings.moveSteamCupPreset(fromIndex, toIndex)
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

            // Duration Slider (per-cup, auto-saves)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 80
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Duration"
                            color: Theme.textColor
                            font: Theme.bodyFont
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: durationSlider.value.toFixed(0) + "s"
                            color: Theme.primaryColor
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.bold: true
                        }
                    }

                    Slider {
                        id: durationSlider
                        Layout.fillWidth: true
                        from: 1
                        to: 120
                        stepSize: 1
                        value: getCurrentCupDuration()
                        onPressedChanged: {
                            if (!pressed) {
                                // Save when slider is released
                                Settings.steamTimeout = value
                                saveCurrentCup(value, flowSlider.value)
                            }
                        }
                    }
                }
            }

            // Steam Flow Slider (per-cup, auto-saves)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 80
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Steam Flow"
                            color: Theme.textColor
                            font: Theme.bodyFont
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: flowToDisplay(flowSlider.value)
                            color: Theme.primaryColor
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.bold: true
                        }
                    }

                    Slider {
                        id: flowSlider
                        Layout.fillWidth: true
                        from: 200
                        to: 1200
                        stepSize: 10
                        value: getCurrentCupFlow()
                        onMoved: MainController.setSteamFlowImmediate(value)
                        onPressedChanged: {
                            if (!pressed) {
                                // Save when slider is released
                                MainController.setSteamFlowImmediate(value)
                                saveCurrentCup(durationSlider.value, value)
                            }
                        }
                    }

                    Text {
                        text: "Low = flat milk, High = foamy"
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                    }
                }
            }

            // Temperature Slider (global setting)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 80
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Steam Temperature"
                            color: Theme.textColor
                            font: Theme.bodyFont
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: steamTempSlider.value.toFixed(0) + "°C"
                            color: Theme.primaryColor
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.bold: true
                        }
                    }

                    Slider {
                        id: steamTempSlider
                        Layout.fillWidth: true
                        from: 120
                        to: 170
                        stepSize: 1
                        value: Settings.steamTemperature
                        onMoved: MainController.setSteamTemperatureImmediate(value)
                    }

                    Text {
                        text: "Higher = drier steam (global setting)"
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        Item { Layout.fillHeight: true; visible: isSteaming }
    }

    // Bottom bar with back button and ready summary
    Rectangle {
        visible: !isSteaming
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

            // Back button
            RoundButton {
                Layout.preferredWidth: 50
                Layout.preferredHeight: 50
                icon.source: "qrc:/icons/back.svg"
                icon.width: 28
                icon.height: 28
                flat: true
                icon.color: "white"
                onClicked: {
                    MainController.applySteamSettings()
                    root.goToIdle()
                }
            }

            // Cup name
            Text {
                text: getCurrentCupName() || "No cup"
                color: "white"
                font.pixelSize: 20
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            // Duration
            Text {
                text: durationSlider.value.toFixed(0) + "s"
                color: "white"
                font: Theme.bodyFont
            }

            Rectangle { width: 1; height: 30; color: "white"; opacity: 0.3 }

            // Flow
            Text {
                text: "Flow " + flowToDisplay(flowSlider.value)
                color: "white"
                font: Theme.bodyFont
            }

            Rectangle { width: 1; height: 30; color: "white"; opacity: 0.3 }

            // Temp
            Text {
                text: steamTempSlider.value.toFixed(0) + "°C"
                color: "white"
                font: Theme.bodyFont
            }
        }
    }

    // Tap anywhere to stop (only when steaming)
    MouseArea {
        visible: isSteaming
        anchors.fill: parent
        z: -1
        onClicked: {
            DE1Device.stopOperation()
            root.goToIdle()
        }
    }

    // Edit Cup Popup (rename/delete)
    Popup {
        id: editCupPopup
        anchors.centerIn: parent
        width: 300
        height: 200
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: 10
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15

            Text {
                text: "Edit Cup"
                color: Theme.textColor
                font.pixelSize: 18
                font.bold: true
            }

            // Name input
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: 4

                TextInput {
                    id: editCupNameInput
                    anchors.fill: parent
                    anchors.margins: 10
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // Buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                // Delete button
                Rectangle {
                    Layout.preferredWidth: 70
                    Layout.preferredHeight: 36
                    color: Theme.accentColor
                    radius: 4

                    Text {
                        anchors.centerIn: parent
                        text: "Delete"
                        color: "white"
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            Settings.removeSteamCupPreset(editingCupIndex)
                            editCupPopup.close()
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Cancel button
                Rectangle {
                    Layout.preferredWidth: 70
                    Layout.preferredHeight: 36
                    color: "transparent"
                    border.color: Theme.textSecondaryColor
                    radius: 4

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        color: Theme.textColor
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: editCupPopup.close()
                    }
                }

                // Save button
                Rectangle {
                    Layout.preferredWidth: 70
                    Layout.preferredHeight: 36
                    color: Theme.primaryColor
                    radius: 4

                    Text {
                        anchors.centerIn: parent
                        text: "Save"
                        color: "white"
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (editCupNameInput.text.trim() !== "") {
                                var preset = Settings.getSteamCupPreset(editingCupIndex)
                                var duration = preset ? preset.duration : 30
                                var flow = (preset && preset.flow !== undefined) ? preset.flow : 700
                                Settings.updateSteamCupPreset(editingCupIndex, editCupNameInput.text.trim(), duration, flow)
                            }
                            editCupPopup.close()
                        }
                    }
                }
            }
        }
    }

    // Add Cup Dialog
    Popup {
        id: addCupDialog
        anchors.centerIn: parent
        width: 300
        height: 180
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: 10
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15

            Text {
                text: "Add Cup Preset"
                color: Theme.textColor
                font.pixelSize: 18
                font.bold: true
            }

            // Name input
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: 4

                TextInput {
                    id: newCupName
                    anchors.fill: parent
                    anchors.margins: 10
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: Text.AlignVCenter

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: "Cup name (e.g. Ikea Small)"
                        color: Theme.textSecondaryColor
                        visible: !parent.text && !parent.activeFocus
                    }
                }
            }

            // Buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item { Layout.fillWidth: true }

                Rectangle {
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 36
                    color: "transparent"
                    border.color: Theme.textSecondaryColor
                    radius: 4

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        color: Theme.textColor
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: addCupDialog.close()
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 36
                    color: Theme.primaryColor
                    radius: 4

                    Text {
                        anchors.centerIn: parent
                        text: "Add"
                        color: "white"
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (newCupName.text.trim() !== "") {
                                var presetCount = Settings.steamCupPresets.length
                                Settings.addSteamCupPreset(newCupName.text.trim(), 30, 700)
                                Settings.selectedSteamCup = presetCount  // Select the new cup
                                newCupName.text = ""
                                addCupDialog.close()
                            }
                        }
                    }
                }
            }
        }
    }

    // Update sliders when selected cup changes
    Connections {
        target: Settings
        function onSelectedSteamCupChanged() {
            durationSlider.value = getCurrentCupDuration()
            flowSlider.value = getCurrentCupFlow()
        }
        function onSteamCupPresetsChanged() {
            durationSlider.value = getCurrentCupDuration()
            flowSlider.value = getCurrentCupFlow()
        }
    }
}
