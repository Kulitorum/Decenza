import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    objectName: "flushPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: {
        root.currentPageTitle = "Flush"
        MainController.applyFlushSettings()
    }
    StackView.onActivated: root.currentPageTitle = "Flush"

    property bool isFlushing: MachineState.phase === MachineStateType.Phase.Flushing || root.debugLiveView
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
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.pageTopMargin  // Space for bottom bar
        spacing: 15

        // === FLUSHING VIEW ===
        ColumnLayout {
            visible: isFlushing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 20

            Item { Layout.fillHeight: true }

            // Timer with progress bar
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8

                    Text {
                        id: flushProgressText
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: MachineState.shotTime.toFixed(1) + "s / " + Settings.flushSeconds.toFixed(0) + "s"
                        color: Theme.textColor
                        font: Theme.timerFont
                    }

                    // Progress bar
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: flushProgressText.width
                        height: 8
                        radius: 4
                        color: Theme.surfaceColor

                        Rectangle {
                            width: parent.width * Math.min(1, MachineState.shotTime / Settings.flushSeconds)
                            height: parent.height
                            radius: 4
                            color: Theme.primaryColor
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
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
                Layout.preferredHeight: Theme.scaled(90)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: Theme.scaled(20)

                    Text {
                        text: "Flush Preset"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(24)
                    }

                    // Preset buttons with drag-and-drop
                    Row {
                        id: presetsRow
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

                                        onDoubleClicked: {
                                            holdTimer.stop()
                                            held = true  // Prevent single-click selection on release
                                            editingPresetIndex = presetDelegate.presetIndex
                                            editPresetNameInput.text = modelData.name
                                            editPresetPopup.open()
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

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "Drag to reorder, hold or double-click to edit"
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                    }
                }
            }

            // Settings frame
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(16)
                    spacing: Theme.scaled(8)

                    // Duration (per-preset, auto-saves)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Text {
                            text: "Duration"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(24)
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: secondsInput
                            Layout.preferredWidth: Theme.scaled(180)
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

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3 }

                    // Flow Rate (per-preset, auto-saves)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Text {
                            text: "Flow Rate"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(24)
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: flowInput
                            Layout.preferredWidth: Theme.scaled(180)
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

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }

    // Bottom bar
    BottomBar {
        visible: !isFlushing
        title: getCurrentPresetName() || "Flush"
        onBackClicked: {
            MainController.applyFlushSettings()
            root.goToIdle()
        }

        Text {
            text: secondsInput.value.toFixed(1) + "s"
            color: "white"
            font: Theme.bodyFont
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: "white"; opacity: 0.3 }
        Text {
            text: flowInput.value.toFixed(1) + " mL/s"
            color: "white"
            font: Theme.bodyFont
        }
    }


    // Edit preset popup
    Popup {
        id: editPresetPopup
        x: (parent.width - width) / 2
        y: editPresetPopupAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true

        property bool editPresetPopupAtTop: false
        onOpened: {
            editPresetPopupAtTop = false
            editPresetNameInput.forceActiveFocus()
        }
        onClosed: editPresetPopupAtTop = false

        Connections {
            target: Qt.inputMethod
            function onVisibleChanged() {
                if (Qt.inputMethod.visible && editPresetPopup.opened) {
                    editPresetPopup.editPresetPopupAtTop = true
                }
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: 15

            Text {
                text: "Edit Flush Preset"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            Rectangle {
                Layout.preferredWidth: 280
                Layout.preferredHeight: 44
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: 4

                TextInput {
                    id: editPresetNameInput
                    anchors.fill: parent
                    anchors.margins: 10
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: "Preset name"
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                    }
                }
            }

            RowLayout {
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
    Popup {
        id: addPresetDialog
        x: (parent.width - width) / 2
        y: addPresetDialogAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true

        property bool addPresetDialogAtTop: false
        onOpened: {
            addPresetDialogAtTop = false
            newPresetNameInput.text = ""
            newPresetNameInput.forceActiveFocus()
        }
        onClosed: addPresetDialogAtTop = false

        Connections {
            target: Qt.inputMethod
            function onVisibleChanged() {
                if (Qt.inputMethod.visible && addPresetDialog.opened) {
                    addPresetDialog.addPresetDialogAtTop = true
                }
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: 15

            Text {
                text: "Add Flush Preset"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            Rectangle {
                Layout.preferredWidth: 280
                Layout.preferredHeight: 44
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: 4

                TextInput {
                    id: newPresetNameInput
                    anchors.fill: parent
                    anchors.margins: 10
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: "Preset name"
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                    }
                }
            }

            RowLayout {
                spacing: 10

                Item { Layout.fillWidth: true }

                Button {
                    text: "Cancel"
                    onClicked: addPresetDialog.close()
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
                    text: "Add"
                    onClicked: {
                        if (newPresetNameInput.text.length > 0) {
                            Settings.addFlushPreset(newPresetNameInput.text, 6.0, 5.0)
                            newPresetNameInput.text = ""
                            addPresetDialog.close()
                        }
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
}
