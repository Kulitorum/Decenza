import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"

Page {
    objectName: "flushPage"
    background: Rectangle { color: Theme.backgroundColor }

    property string pageTitle: TranslationManager.translate("flush.title", "Flush")

    // Use StackView.onActivated (not Component.onCompleted) so side effects
    // run when the page is actually shown, not during construction. This
    // also re-fires on pop-back if the page is ever pushed below another.
    // Skip the preset-reset and settings push while flushing so a
    // re-activation mid-session doesn't clobber in-progress state.
    StackView.onActivated: {
        root.currentPageTitle = pageTitle
        if (!isFlushing) {
            // Sync Settings with selected preset
            Settings.flushFlow = getCurrentPresetFlow()
            Settings.flushSeconds = getCurrentPresetSeconds()
            MainController.applyFlushSettings()
            secondsInput.forceActiveFocus()
        }
    }

    property bool isFlushing: MachineState.phase === MachineStateType.Phase.Flushing || root.debugLiveView
    property int editingPresetIndex: -1

    onIsFlushingChanged: {
        console.log("FlushPage: isFlushing changed to", isFlushing, "phase=", MachineState.phase)
        if (!isFlushing) {
            console.log("FlushPage: Settings view now visible (isFlushing=false)")
        }
    }

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
        spacing: Theme.scaled(15)

        // === FLUSHING VIEW ===
        ColumnLayout {
            visible: isFlushing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(20)

            // Preset pills for quick switching during flushing
            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(8)

                Repeater {
                    id: livePresetRepeater
                    model: Settings.flushPresets

                    Rectangle {
                        width: livePresetText.implicitWidth + 24
                        height: Theme.scaled(36)
                        radius: Theme.scaled(18)
                        color: index === Settings.selectedFlushPreset ? Theme.primaryColor : Theme.surfaceColor
                        border.color: index === Settings.selectedFlushPreset ? Theme.primaryColor : Theme.textSecondaryColor
                        border.width: 1

                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.name + " " + TranslationManager.translate("flush.accessibility.preset", "preset") +
                                         (index === Settings.selectedFlushPreset ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                        Accessible.focusable: true
                        Accessible.onPressAction: livePresetArea.clicked(null)

                        Keys.onReturnPressed: { livePresetArea.clicked(null); event.accepted = true }
                        Keys.onSpacePressed:  { livePresetArea.clicked(null); event.accepted = true }
                        Keys.onLeftPressed: {
                            if (index > 0) livePresetRepeater.itemAt(index - 1).forceActiveFocus()
                            event.accepted = true
                        }
                        Keys.onRightPressed: {
                            if (index < livePresetRepeater.count - 1) livePresetRepeater.itemAt(index + 1).forceActiveFocus()
                            event.accepted = true
                        }
                        Keys.onTabPressed: {
                            if (index < livePresetRepeater.count - 1)
                                livePresetRepeater.itemAt(index + 1).forceActiveFocus()
                            else if (flushStopButton.visible)
                                flushStopButton.forceActiveFocus()
                            else
                                livePresetRepeater.itemAt(0).forceActiveFocus()
                            event.accepted = true
                        }
                        Keys.onBacktabPressed: {
                            if (index > 0)
                                livePresetRepeater.itemAt(index - 1).forceActiveFocus()
                            else if (flushStopButton.visible)
                                flushStopButton.forceActiveFocus()
                            else
                                livePresetRepeater.itemAt(livePresetRepeater.count - 1).forceActiveFocus()
                            event.accepted = true
                        }

                        Text {
                            id: livePresetText
                            anchors.centerIn: parent
                            text: modelData.name
                            color: index === Settings.selectedFlushPreset ? Theme.primaryContrastColor : Theme.textColor
                            font: Theme.bodyFont
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: livePresetArea
                            anchors.fill: parent
                            onClicked: {
                                Settings.selectedFlushPreset = index
                                Settings.flushFlow = modelData.flow
                                Settings.flushSeconds = modelData.seconds
                                MainController.applyFlushSettings()
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // Timer with progress bar
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.scaled(8)

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
                        height: Theme.scaled(8)
                        radius: Theme.scaled(4)
                        color: Theme.surfaceColor

                        Rectangle {
                            width: parent.width * Math.min(1, MachineState.shotTime / Settings.flushSeconds)
                            height: parent.height
                            radius: Theme.scaled(4)
                            color: Theme.primaryColor
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // Stop button for headless machines
            Rectangle {
                id: flushStopButton
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Theme.scaled(200)
                Layout.preferredHeight: Theme.scaled(60)
                visible: DE1Device.isHeadless
                radius: Theme.cardRadius
                color: stopTapHandler.isPressed ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor
                border.color: Theme.primaryContrastColor
                border.width: Theme.scaled(2)

                activeFocusOnTab: true
                Keys.onReturnPressed: { DE1Device.stopOperation(); root.goToIdle(); event.accepted = true }
                Keys.onSpacePressed:  { DE1Device.stopOperation(); root.goToIdle(); event.accepted = true }
                Keys.onTabPressed: {
                    if (livePresetRepeater.count > 0) livePresetRepeater.itemAt(0).forceActiveFocus()
                    event.accepted = true
                }
                Keys.onBacktabPressed: {
                    if (livePresetRepeater.count > 0) livePresetRepeater.itemAt(livePresetRepeater.count - 1).forceActiveFocus()
                    event.accepted = true
                }

                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("flush.button.stop", "STOP")
                    color: Theme.primaryContrastColor
                    font.pixelSize: Theme.scaled(24)
                    font.weight: Font.Bold
                    Accessible.ignored: true
                }

                // Using TapHandler for better touch responsiveness
                AccessibleTapHandler {
                    id: stopTapHandler
                    anchors.fill: parent
                    accessibleName: TranslationManager.translate("flush.accessible.stopFlushing", "Stop flushing")
                    accessibleItem: flushStopButton
                    onAccessibleClicked: {
                        DE1Device.stopOperation()
                        root.goToIdle()
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.scaled(20) }
        }

        // === SETTINGS VIEW ===
        ColumnLayout {
            visible: !isFlushing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(12)

            // Presets Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(90)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(20)

                    Tr {
                        key: "flush.label.preset"
                        fallback: "Flush Preset"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(24)
                    }

                    // Preset buttons with drag-and-drop
                    Row {
                        id: presetsRow
                        spacing: Theme.scaled(8)

                        property int draggedIndex: -1

                        Repeater {
                            id: presetRepeater
                            model: Settings.flushPresets

                            Item {
                                id: presetDelegate
                                width: presetPill.width
                                height: Theme.scaled(36)

                                property int presetIndex: index
                                property Item focusTarget: presetPill

                                Rectangle {
                                    id: presetPill
                                    width: presetText.implicitWidth + 24
                                    height: Theme.scaled(36)
                                    radius: Theme.scaled(18)
                                    color: presetDelegate.presetIndex === Settings.selectedFlushPreset ? Theme.primaryColor : Theme.backgroundColor
                                    border.color: presetDelegate.presetIndex === Settings.selectedFlushPreset ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: dragArea.drag.active ? 0.8 : 1.0

                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.Button
                                    Accessible.name: modelData.name + " " + TranslationManager.translate("flush.accessibility.preset", "preset") +
                                                     (presetDelegate.presetIndex === Settings.selectedFlushPreset ?
                                                      ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                                    Accessible.description: TranslationManager.translate("flush.accessibility.presetHint", "Double-tap or long-press to rename.")
                                    Accessible.focusable: true
                                    Accessible.onPressAction: {
                                        Settings.selectedFlushPreset = presetDelegate.presetIndex
                                        flowInput.value = modelData.flow
                                        secondsInput.value = modelData.seconds
                                        Settings.flushFlow = modelData.flow
                                        Settings.flushSeconds = modelData.seconds
                                        MainController.applyFlushSettings()
                                    }

                                    Keys.onReturnPressed: {
                                        Settings.selectedFlushPreset = presetDelegate.presetIndex
                                        flowInput.value = modelData.flow
                                        secondsInput.value = modelData.seconds
                                        Settings.flushFlow = modelData.flow
                                        Settings.flushSeconds = modelData.seconds
                                        MainController.applyFlushSettings()
                                        event.accepted = true
                                    }
                                    Keys.onSpacePressed: {
                                        Settings.selectedFlushPreset = presetDelegate.presetIndex
                                        flowInput.value = modelData.flow
                                        secondsInput.value = modelData.seconds
                                        Settings.flushFlow = modelData.flow
                                        Settings.flushSeconds = modelData.seconds
                                        MainController.applyFlushSettings()
                                        event.accepted = true
                                    }
                                    Keys.onLeftPressed: {
                                        if (index > 0) presetRepeater.itemAt(index - 1).focusTarget.forceActiveFocus()
                                        event.accepted = true
                                    }
                                    Keys.onRightPressed: {
                                        if (index < presetRepeater.count - 1) presetRepeater.itemAt(index + 1).focusTarget.forceActiveFocus()
                                        event.accepted = true
                                    }
                                    Keys.onTabPressed: {
                                        if (index < presetRepeater.count - 1)
                                            presetRepeater.itemAt(index + 1).focusTarget.forceActiveFocus()
                                        else
                                            addPresetButton.forceActiveFocus()
                                        event.accepted = true
                                    }
                                    Keys.onBacktabPressed: {
                                        if (index > 0)
                                            presetRepeater.itemAt(index - 1).focusTarget.forceActiveFocus()
                                        else
                                            flowInput.forceActiveFocus()
                                        event.accepted = true
                                    }

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
                                        color: presetDelegate.presetIndex === Settings.selectedFlushPreset ? Theme.primaryContrastColor : Theme.textColor
                                        font: Theme.bodyFont
                                        Accessible.ignored: true
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
                            id: addPresetButton
                            width: Theme.scaled(36)
                            height: Theme.scaled(36)
                            radius: Theme.scaled(18)
                            color: Theme.backgroundColor
                            border.color: Theme.textSecondaryColor
                            border.width: 1

                            activeFocusOnTab: true
                            KeyNavigation.tab: secondsInput
                            KeyNavigation.backtab: presetRepeater.count > 0
                                ? presetRepeater.itemAt(presetRepeater.count - 1).focusTarget
                                : secondsInput
                            Keys.onReturnPressed: { addPresetDialog.open(); event.accepted = true }
                            Keys.onSpacePressed:  { addPresetDialog.open(); event.accepted = true }

                            Text {
                                anchors.centerIn: parent
                                text: "+"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(20)
                                Accessible.ignored: true
                            }

                            // Using TapHandler for better touch responsiveness
                            AccessibleTapHandler {
                                anchors.fill: parent
                                accessibleName: TranslationManager.translate("flush.accessible.addPreset", "Add new flush preset")
                                accessibleItem: addPresetButton
                                onAccessibleClicked: addPresetDialog.open()
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Tr {
                        key: "flush.hint.reorder"
                        fallback: "Drag to reorder, hold or double-click to edit"
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
                        spacing: Theme.scaled(16)

                        Tr {
                            key: "flush.label.duration"
                            fallback: "Duration"
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
                            accessibleName: TranslationManager.translate("flush.label.duration", "Duration")
                            KeyNavigation.tab: flowInput
                            KeyNavigation.backtab: addPresetButton

                            // onValueModified fires on every +/- tick during hold (up to
                            // 12 Hz). Do cheap bookkeeping here, and defer the BLE write
                            // to onValueCommitted which fires once on release.
                            onValueModified: function(newValue) {
                                secondsInput.value = newValue
                                Settings.flushSeconds = newValue
                                saveCurrentPreset(flowInput.value, newValue)
                            }
                            onValueCommitted: MainController.applyFlushSettings()
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3 }

                    // Flow Rate (per-preset, auto-saves)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)

                        Tr {
                            key: "flush.label.flowRate"
                            fallback: "Flow Rate"
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
                            accessibleName: TranslationManager.translate("flush.label.flowRate", "Flow Rate")
                            KeyNavigation.tab: presetRepeater.count > 0
                                ? presetRepeater.itemAt(0).focusTarget
                                : addPresetButton
                            KeyNavigation.backtab: secondsInput

                            // onValueModified: cheap bookkeeping per tick.
                            // onValueCommitted: BLE write once at interaction end.
                            onValueModified: function(newValue) {
                                flowInput.value = newValue
                                Settings.flushFlow = newValue
                                saveCurrentPreset(newValue, secondsInput.value)
                            }
                            onValueCommitted: MainController.applyFlushSettings()
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
        title: getCurrentPresetName() || pageTitle
        onBackClicked: {
            MainController.applyFlushSettings()
            // Handle both pushed (user nav) and replaced (auto nav) cases
            if (pageStack.depth > 1) {
                root.goBack()
            } else {
                root.goToIdle()
            }
        }

        Text {
            text: secondsInput.value.toFixed(1) + "s"
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: Theme.primaryContrastColor; opacity: 0.3 }
        Text {
            text: flowInput.value.toFixed(1) + " mL/s"
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
    }


    // Edit preset popup
    Dialog {
        id: editPresetPopup
        x: (parent.width - width) / 2
        y: editPresetPopupAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

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
            spacing: Theme.scaled(15)

            Tr {
                key: "flush.popup.editPreset"
                fallback: "Edit Flush Preset"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            Rectangle {
                Layout.preferredWidth: Theme.scaled(280)
                Layout.preferredHeight: Theme.scaled(44)
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: Theme.scaled(4)

                TextInput {
                    id: editPresetNameInput
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText
                    activeFocusOnTab: true
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("flush.accessible.renamePreset", "Rename flush preset")
                    Accessible.description: text
                    Accessible.focusable: true
                    KeyNavigation.tab: deleteButton
                    KeyNavigation.backtab: saveButton

                    Tr {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        key: "flush.placeholder.presetName"
                        fallback: "Preset name"
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                        Accessible.ignored: true
                    }
                }
            }

            RowLayout {
                spacing: Theme.scaled(10)

                AccessibleButton {
                    id: deleteButton
                    text: TranslationManager.translate("flush.button.delete", "Delete")
                    accessibleName: TranslationManager.translate("flush.deletePreset", "Delete this flush preset")
                    destructive: true
                    KeyNavigation.tab: cancelEditButton
                    KeyNavigation.backtab: editPresetNameInput
                    onClicked: {
                        Settings.removeFlushPreset(editingPresetIndex)
                        editPresetPopup.close()
                    }
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    id: cancelEditButton
                    text: TranslationManager.translate("flush.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("flush.cancelEditingPreset", "Cancel editing flush preset")
                    KeyNavigation.tab: saveButton
                    KeyNavigation.backtab: deleteButton
                    onClicked: editPresetPopup.close()
                }

                AccessibleButton {
                    id: saveButton
                    primary: true
                    text: TranslationManager.translate("flush.button.save", "Save")
                    accessibleName: TranslationManager.translate("flush.savePresetChanges", "Save changes to flush preset")
                    KeyNavigation.tab: editPresetNameInput
                    KeyNavigation.backtab: cancelEditButton
                    onClicked: {
                        Qt.inputMethod.commit()
                        var preset = Settings.getFlushPreset(editingPresetIndex)
                        Settings.updateFlushPreset(editingPresetIndex, editPresetNameInput.text, preset.flow, preset.seconds)
                        editPresetPopup.close()
                    }
                }
            }
        }
    }

    // Add preset dialog
    Dialog {
        id: addPresetDialog
        x: (parent.width - width) / 2
        y: addPresetDialogAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

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
            spacing: Theme.scaled(15)

            Tr {
                key: "flush.popup.addPreset"
                fallback: "Add Flush Preset"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            Rectangle {
                Layout.preferredWidth: Theme.scaled(280)
                Layout.preferredHeight: Theme.scaled(44)
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: Theme.scaled(4)

                TextInput {
                    id: newPresetNameInput
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText
                    activeFocusOnTab: true
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("flush.accessible.newPresetName", "New flush preset name")
                    Accessible.description: text
                    Accessible.focusable: true
                    KeyNavigation.tab: cancelAddButton
                    KeyNavigation.backtab: addButton

                    Tr {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        key: "flush.placeholder.presetName"
                        fallback: "Preset name"
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                        Accessible.ignored: true
                    }
                }
            }

            RowLayout {
                spacing: Theme.scaled(10)

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    id: cancelAddButton
                    text: TranslationManager.translate("flush.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("flush.cancelAddingPreset", "Cancel adding new flush preset")
                    KeyNavigation.tab: addButton
                    KeyNavigation.backtab: newPresetNameInput
                    onClicked: addPresetDialog.close()
                }

                AccessibleButton {
                    id: addButton
                    primary: true
                    text: TranslationManager.translate("flush.button.add", "Add")
                    accessibleName: TranslationManager.translate("flush.addNewPreset", "Add new flush preset with entered name")
                    KeyNavigation.tab: newPresetNameInput
                    KeyNavigation.backtab: cancelAddButton
                    onClicked: {
                        Qt.inputMethod.commit()
                        if (newPresetNameInput.text.length > 0) {
                            Settings.addFlushPreset(newPresetNameInput.text, 6.0, 5.0)
                            newPresetNameInput.text = ""
                            addPresetDialog.close()
                        }
                    }
                }
            }
        }
    }
}
