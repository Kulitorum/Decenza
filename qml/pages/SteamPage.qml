import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    objectName: "steamPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = "Steam"
    StackView.onActivated: root.currentPageTitle = "Steam"

    property bool isSteaming: MachineState.phase === MachineState.Phase.Steaming
    property int editingPitcherIndex: -1  // For the edit popup

    // Helper to format flow as readable value (handles undefined/NaN)
    // Steam flow is stored as 0.01 ml/s units (e.g., 150 = 1.5 ml/s)
    function flowToDisplay(flow) {
        if (flow === undefined || flow === null || isNaN(flow)) {
            return "1.5"  // Default
        }
        return (flow / 100).toFixed(1)
    }

    // Get current pitcher's values with defaults
    function getCurrentPitcherDuration() {
        var preset = Settings.getSteamPitcherPreset(Settings.selectedSteamPitcher)
        return preset ? preset.duration : 30
    }

    function getCurrentPitcherFlow() {
        var preset = Settings.getSteamPitcherPreset(Settings.selectedSteamPitcher)
        return (preset && preset.flow !== undefined) ? preset.flow : 150
    }

    function getCurrentPitcherName() {
        var preset = Settings.getSteamPitcherPreset(Settings.selectedSteamPitcher)
        return preset ? preset.name : ""
    }

    // Save current pitcher with new values
    function saveCurrentPitcher(duration, flow) {
        var name = getCurrentPitcherName()
        if (name) {
            Settings.updateSteamPitcherPreset(Settings.selectedSteamPitcher, name, duration, flow)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.pageTopMargin  // Space for bottom bar
        spacing: 15

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

                ValueInput {
                    id: steamingFlowSlider
                    Layout.preferredWidth: Theme.scaled(150)
                    from: 40
                    to: 250
                    stepSize: 5
                    decimals: 0
                    value: Settings.steamFlow
                    displayText: flowToDisplay(value)
                    onValueModified: function(newValue) {
                        steamingFlowSlider.value = newValue
                        MainController.setSteamFlowImmediate(newValue)
                    }
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

            // Pitcher Presets Section
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
                            text: "Pitcher Preset"
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

                    // Pitcher preset buttons with drag-and-drop
                    Row {
                        id: pitcherPresetsRow
                        Layout.fillWidth: true
                        spacing: 8

                        property int draggedIndex: -1

                        Repeater {
                            id: pitcherRepeater
                            model: Settings.steamPitcherPresets

                            Item {
                                id: pitcherDelegate
                                width: pitcherPill.width
                                height: 36

                                property int pitcherIndex: index

                                Rectangle {
                                    id: pitcherPill
                                    width: pitcherText.implicitWidth + 24
                                    height: 36
                                    radius: 18
                                    color: pitcherDelegate.pitcherIndex === Settings.selectedSteamPitcher ? Theme.primaryColor : Theme.backgroundColor
                                    border.color: pitcherDelegate.pitcherIndex === Settings.selectedSteamPitcher ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: dragArea.drag.active ? 0.8 : 1.0

                                    Drag.active: dragArea.drag.active
                                    Drag.source: pitcherDelegate
                                    Drag.hotSpot.x: width / 2
                                    Drag.hotSpot.y: height / 2

                                    states: State {
                                        when: dragArea.drag.active
                                        ParentChange { target: pitcherPill; parent: pitcherPresetsRow }
                                        AnchorChanges { target: pitcherPill; anchors.verticalCenter: undefined }
                                    }

                                    Text {
                                        id: pitcherText
                                        anchors.centerIn: parent
                                        text: modelData.name
                                        color: pitcherDelegate.pitcherIndex === Settings.selectedSteamPitcher ? "white" : Theme.textColor
                                        font: Theme.bodyFont
                                    }

                                    MouseArea {
                                        id: dragArea
                                        anchors.fill: parent
                                        drag.target: pitcherPill
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
                                                // Simple click - select the pitcher
                                                Settings.selectedSteamPitcher = pitcherDelegate.pitcherIndex
                                                var flow = modelData.flow !== undefined ? modelData.flow : 150
                                                durationSlider.value = modelData.duration
                                                flowSlider.value = flow
                                                Settings.steamTimeout = modelData.duration
                                                Settings.steamFlow = flow
                                                MainController.applySteamSettings()
                                            }
                                            pitcherPill.Drag.drop()
                                            pitcherPresetsRow.draggedIndex = -1
                                        }

                                        onPositionChanged: {
                                            if (drag.active) {
                                                moved = true
                                                pitcherPresetsRow.draggedIndex = pitcherDelegate.pitcherIndex
                                            }
                                        }

                                        Timer {
                                            id: holdTimer
                                            interval: 500
                                            onTriggered: {
                                                if (!dragArea.moved) {
                                                    dragArea.held = true
                                                    editingPitcherIndex = pitcherDelegate.pitcherIndex
                                                    editPitcherNameInput.text = modelData.name
                                                    editPitcherPopup.open()
                                                }
                                            }
                                        }
                                    }
                                }

                                DropArea {
                                    anchors.fill: parent
                                    onEntered: function(drag) {
                                        var fromIndex = drag.source.pitcherIndex
                                        var toIndex = pitcherDelegate.pitcherIndex
                                        if (fromIndex !== toIndex) {
                                            Settings.moveSteamPitcherPreset(fromIndex, toIndex)
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
                                onClicked: addPitcherDialog.open()
                            }
                        }
                    }
                }
            }

            // Duration (per-pitcher, auto-saves)
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
                        text: "Duration"
                        color: Theme.textColor
                        font: Theme.bodyFont
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        id: durationSlider
                        Layout.preferredWidth: Theme.scaled(150)
                        from: 1
                        to: 120
                        stepSize: 1
                        decimals: 0
                        suffix: " s"
                        value: getCurrentPitcherDuration()
                        valueColor: Theme.primaryColor
                        onValueModified: function(newValue) {
                            durationSlider.value = newValue
                            Settings.steamTimeout = newValue
                            saveCurrentPitcher(newValue, flowSlider.value)
                        }
                    }
                }
            }

            // Steam Flow (per-pitcher, auto-saves)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(100)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    Column {
                        Text {
                            text: "Steam Flow"
                            color: Theme.textColor
                            font: Theme.bodyFont
                        }
                        Text {
                            text: "Low = flat, High = foamy"
                            color: Theme.textSecondaryColor
                            font: Theme.labelFont
                        }
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        id: flowSlider
                        Layout.preferredWidth: Theme.scaled(150)
                        from: 40
                        to: 250
                        stepSize: 5
                        decimals: 0
                        value: getCurrentPitcherFlow()
                        displayText: flowToDisplay(value)
                        valueColor: Theme.primaryColor
                        onValueModified: function(newValue) {
                            flowSlider.value = newValue
                            MainController.setSteamFlowImmediate(newValue)
                            saveCurrentPitcher(durationSlider.value, newValue)
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

                    Column {
                        Text {
                            text: "Temperature"
                            color: Theme.textColor
                            font: Theme.bodyFont
                        }
                        Text {
                            text: "Higher = drier steam"
                            color: Theme.textSecondaryColor
                            font: Theme.labelFont
                        }
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        id: steamTempSlider
                        Layout.preferredWidth: Theme.scaled(150)
                        from: 120
                        to: 170
                        stepSize: 1
                        decimals: 0
                        suffix: "°C"
                        value: Settings.steamTemperature
                        valueColor: Theme.temperatureColor
                        onValueModified: function(newValue) {
                            steamTempSlider.value = newValue
                            MainController.setSteamTemperatureImmediate(newValue)
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        Item { Layout.fillHeight: true; visible: isSteaming }
    }

    // Bottom bar with back button and ready summary
    BottomBar {
        visible: !isSteaming
        title: getCurrentPitcherName() || "No pitcher"
        onBackClicked: {
            MainController.applySteamSettings()
            root.goToIdle()
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingMedium

            Text {
                text: durationSlider.value.toFixed(0) + "s"
                color: "white"
                font: Theme.bodyFont
            }

            Rectangle { width: 1; height: Theme.scaled(30); color: "white"; opacity: 0.3 }

            Text {
                text: "Flow " + flowToDisplay(flowSlider.value)
                color: "white"
                font: Theme.bodyFont
            }

            Rectangle { width: 1; height: Theme.scaled(30); color: "white"; opacity: 0.3 }

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

    // Edit Pitcher Popup (rename/delete)
    Popup {
        id: editPitcherPopup
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
                text: "Edit Pitcher"
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
                    id: editPitcherNameInput
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
                            Settings.removeSteamPitcherPreset(editingPitcherIndex)
                            editPitcherPopup.close()
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
                        onClicked: editPitcherPopup.close()
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
                            if (editPitcherNameInput.text.trim() !== "") {
                                var preset = Settings.getSteamPitcherPreset(editingPitcherIndex)
                                var duration = preset ? preset.duration : 30
                                var flow = (preset && preset.flow !== undefined) ? preset.flow : 150
                                Settings.updateSteamPitcherPreset(editingPitcherIndex, editPitcherNameInput.text.trim(), duration, flow)
                            }
                            editPitcherPopup.close()
                        }
                    }
                }
            }
        }
    }

    // Add Pitcher Dialog
    Popup {
        id: addPitcherDialog
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
                text: "Add Pitcher Preset"
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
                    id: newPitcherName
                    anchors.fill: parent
                    anchors.margins: 10
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: Text.AlignVCenter

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: "Pitcher name (e.g. Ikea Small)"
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
                        onClicked: addPitcherDialog.close()
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
                            if (newPitcherName.text.trim() !== "") {
                                var presetCount = Settings.steamPitcherPresets.length
                                Settings.addSteamPitcherPreset(newPitcherName.text.trim(), 30, 150)
                                Settings.selectedSteamPitcher = presetCount  // Select the new pitcher
                                newPitcherName.text = ""
                                addPitcherDialog.close()
                            }
                        }
                    }
                }
            }
        }
    }

    // Update sliders when selected pitcher changes
    Connections {
        target: Settings
        function onSelectedSteamCupChanged() {
            durationSlider.value = getCurrentPitcherDuration()
            flowSlider.value = getCurrentPitcherFlow()
        }
        function onSteamPitcherPresetsChanged() {
            durationSlider.value = getCurrentPitcherDuration()
            flowSlider.value = getCurrentPitcherFlow()
        }
    }
}
