import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    objectName: "hotWaterPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: {
        root.currentPageTitle = pageTitleText.text
        // Sync Settings with selected preset
        Settings.waterVolume = getCurrentVesselVolume()
        Settings.waterVolumeMode = getCurrentVesselMode()
        MainController.applyHotWaterSettings()
        // Tare immediately so display shows 0g instead of current scale weight
        // (scale will tare again when hot water flow actually starts)
        if (!isVolumeMode) {
            MachineState.tareScale()
        }
    }
    StackView.onActivated: root.currentPageTitle = pageTitleText.text

    // Hidden Tr component for page title (used by root.currentPageTitle)
    Tr { id: pageTitleText; key: "hotwater.title"; fallback: "Hot Water"; visible: false }

    property bool isDispensing: MachineState.phase === MachineStateType.Phase.HotWater || root.debugLiveView
    property int editingVesselIndex: -1

    property bool isVolumeMode: Settings.waterVolumeMode === "volume"

    // Get current vessel's volume
    function getCurrentVesselVolume() {
        var preset = Settings.getWaterVesselPreset(Settings.selectedWaterVessel)
        return preset ? preset.volume : 200
    }

    function getCurrentVesselName() {
        var preset = Settings.getWaterVesselPreset(Settings.selectedWaterVessel)
        return preset ? preset.name : ""
    }

    function getCurrentVesselMode() {
        var preset = Settings.getWaterVesselPreset(Settings.selectedWaterVessel)
        return (preset && preset.mode) ? preset.mode : "weight"
    }

    // Save current vessel with new volume
    function saveCurrentVessel(volume) {
        var name = getCurrentVesselName()
        if (name) {
            Settings.updateWaterVesselPreset(Settings.selectedWaterVessel, name, volume, Settings.waterVolumeMode)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.pageTopMargin  // Space for bottom bar
        spacing: Theme.scaled(15)

        // === DISPENSING VIEW ===
        ColumnLayout {
            visible: isDispensing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(20)

            // Preset pills for quick switching during dispensing
            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(8)

                Repeater {
                    model: Settings.waterVesselPresets

                    Rectangle {
                        width: liveVesselText.implicitWidth + 24
                        height: Theme.scaled(36)
                        radius: Theme.scaled(18)
                        color: index === Settings.selectedWaterVessel ? Theme.primaryColor : Theme.surfaceColor
                        border.color: index === Settings.selectedWaterVessel ? Theme.primaryColor : Theme.textSecondaryColor
                        border.width: 1

                        Text {
                            id: liveVesselText
                            anchors.centerIn: parent
                            text: modelData.name
                            color: index === Settings.selectedWaterVessel ? "white" : Theme.textColor
                            font: Theme.bodyFont
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                Settings.selectedWaterVessel = index
                                Settings.waterVolume = modelData.volume
                                Settings.waterVolumeMode = (modelData.mode || "weight")
                                MainController.applyHotWaterSettings()
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // Progress display — adapts to weight vs volume mode
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.scaled(8)

                    // Weight mode: show live weight progress
                    Text {
                        id: hotWaterProgressText
                        visible: !isVolumeMode
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Math.max(0, ScaleDevice ? ScaleDevice.weight : 0).toFixed(0) + "g / " + Settings.waterVolume + "g"
                        color: Theme.textColor
                        font: Theme.timerFont
                    }

                    // Volume mode: show target
                    Text {
                        id: hotWaterVolumeText
                        visible: isVolumeMode
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Settings.waterVolume + " ml"
                        color: Theme.textColor
                        font: Theme.timerFont
                    }

                    Tr {
                        visible: isVolumeMode
                        anchors.horizontalCenter: parent.horizontalCenter
                        key: "hotwater.dispensing.flowmeter"
                        fallback: "Dispensing (flowmeter)"
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                    }

                    // Progress bar (weight mode only — scale provides live data)
                    Rectangle {
                        visible: !isVolumeMode
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: hotWaterProgressText.width
                        height: Theme.scaled(8)
                        radius: Theme.scaled(4)
                        color: Theme.surfaceColor

                        Rectangle {
                            width: parent.width * Math.min(1, Math.max(0, ScaleDevice ? ScaleDevice.weight : 0) / Settings.waterVolume)
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
                id: hotWaterStopButton
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Theme.scaled(200)
                Layout.preferredHeight: Theme.scaled(60)
                visible: DE1Device.isHeadless
                radius: Theme.cardRadius
                color: stopTapHandler.isPressed ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor
                border.color: "white"
                border.width: Theme.scaled(2)

                Text {
                    anchors.centerIn: parent
                    text: "STOP"
                    color: "white"
                    font.pixelSize: Theme.scaled(24)
                    font.weight: Font.Bold
                }

                // Using TapHandler for better touch responsiveness
                AccessibleTapHandler {
                    id: stopTapHandler
                    anchors.fill: parent
                    accessibleName: "Stop hot water"
                    accessibleItem: hotWaterStopButton
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
            visible: !isDispensing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(12)

            // Vessel Presets Section
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
                        key: "hotwater.label.vesselPreset"
                        fallback: "Vessel Preset"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(24)
                    }

                    // Vessel preset buttons with drag-and-drop
                    Row {
                        id: vesselPresetsRow
                        spacing: Theme.scaled(8)

                        property int draggedIndex: -1

                        Repeater {
                            id: vesselRepeater
                            model: Settings.waterVesselPresets

                            Item {
                                id: vesselDelegate
                                width: vesselPill.width
                                height: Theme.scaled(36)

                                property int vesselIndex: index

                                Rectangle {
                                    id: vesselPill
                                    width: vesselText.implicitWidth + 24
                                    height: Theme.scaled(36)
                                    radius: Theme.scaled(18)
                                    color: vesselDelegate.vesselIndex === Settings.selectedWaterVessel ? Theme.primaryColor : Theme.backgroundColor
                                    border.color: vesselDelegate.vesselIndex === Settings.selectedWaterVessel ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: dragArea.drag.active ? 0.8 : 1.0

                                    Accessible.role: Accessible.Button
                                    Accessible.name: modelData.name + " " + TranslationManager.translate("hotwater.accessibility.preset", "preset") +
                                                     (vesselDelegate.vesselIndex === Settings.selectedWaterVessel ?
                                                      ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                                    Accessible.focusable: true

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
                                                Settings.waterVolumeMode = (modelData.mode || "weight")
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

                                        onDoubleClicked: {
                                            holdTimer.stop()
                                            held = true  // Prevent single-click selection on release
                                            editingVesselIndex = vesselDelegate.vesselIndex
                                            editVesselNameInput.text = modelData.name
                                            editVesselPopup.open()
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
                            id: addVesselButton
                            width: Theme.scaled(36)
                            height: Theme.scaled(36)
                            radius: Theme.scaled(18)
                            color: Theme.backgroundColor
                            border.color: Theme.textSecondaryColor
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "+"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(20)
                            }

                            // Using TapHandler for better touch responsiveness
                            AccessibleTapHandler {
                                anchors.fill: parent
                                accessibleName: "Add new hot water preset"
                                accessibleItem: addVesselButton
                                onAccessibleClicked: addVesselDialog.open()
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Tr {
                        key: "hotwater.hint.reorder"
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

                    // Mode toggle + target value (per-vessel, auto-saves)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)

                        // Weight / Volume mode toggle pills
                        Row {
                            spacing: Theme.scaled(4)

                            Rectangle {
                                width: weightModeText.implicitWidth + Theme.scaled(20)
                                height: Theme.scaled(36)
                                radius: Theme.scaled(18)
                                color: !isVolumeMode ? Theme.primaryColor : Theme.backgroundColor
                                border.color: !isVolumeMode ? Theme.primaryColor : Theme.textSecondaryColor
                                border.width: 1

                                Text {
                                    id: weightModeText
                                    anchors.centerIn: parent
                                    text: TranslationManager.translate("hotwater.mode.weight", "Weight (g)")
                                    color: !isVolumeMode ? "white" : Theme.textColor
                                    font: Theme.bodyFont
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        Settings.waterVolumeMode = "weight"
                                        saveCurrentVessel(volumeInput.value)
                                        MainController.applyHotWaterSettings()
                                    }
                                }
                            }

                            Rectangle {
                                width: volumeModeText.implicitWidth + Theme.scaled(20)
                                height: Theme.scaled(36)
                                radius: Theme.scaled(18)
                                color: isVolumeMode ? Theme.primaryColor : Theme.backgroundColor
                                border.color: isVolumeMode ? Theme.primaryColor : Theme.textSecondaryColor
                                border.width: 1

                                Text {
                                    id: volumeModeText
                                    anchors.centerIn: parent
                                    text: TranslationManager.translate("hotwater.mode.volume", "Volume (ml)")
                                    color: isVolumeMode ? "white" : Theme.textColor
                                    font: Theme.bodyFont
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        Settings.waterVolumeMode = "volume"
                                        // Clamp value to 255ml max for volume mode
                                        if (volumeInput.value > 255) {
                                            volumeInput.value = 255
                                            Settings.waterVolume = 255
                                            saveCurrentVessel(255)
                                        } else {
                                            saveCurrentVessel(volumeInput.value)
                                        }
                                        MainController.applyHotWaterSettings()
                                    }
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: volumeInput
                            Layout.preferredWidth: Theme.scaled(180)
                            value: getCurrentVesselVolume()
                            from: 10
                            to: isVolumeMode ? 255 : 500
                            stepSize: 10
                            suffix: isVolumeMode ? " ml" : " g"
                            valueColor: Theme.primaryColor
                            accessibleName: isVolumeMode
                                ? TranslationManager.translate("hotwater.label.volume", "Volume")
                                : TranslationManager.translate("hotwater.label.weight", "Weight")

                            onValueModified: function(newValue) {
                                volumeInput.value = newValue
                                Settings.waterVolume = newValue
                                saveCurrentVessel(newValue)
                                MainController.applyHotWaterSettings()
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3 }

                    // Temperature (global setting)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)

                        Tr {
                            key: "hotwater.label.temperature"
                            fallback: "Temperature"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(24)
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: temperatureInput
                            Layout.preferredWidth: Theme.scaled(180)
                            value: Settings.waterTemperature
                            from: 40
                            to: 100
                            stepSize: 1
                            suffix: "°C"
                            valueColor: Theme.temperatureColor
                            accessibleName: TranslationManager.translate("hotwater.label.temperature", "Temperature")

                            onValueModified: function(newValue) {
                                temperatureInput.value = newValue
                                Settings.waterTemperature = newValue
                                MainController.applyHotWaterSettings()
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }

    // Hidden Tr for "No vessel" fallback
    Tr { id: noVesselText; key: "hotwater.label.noVessel"; fallback: "No vessel"; visible: false }

    // Bottom bar
    BottomBar {
        visible: !isDispensing
        title: getCurrentVesselName() || noVesselText.text
        onBackClicked: {
            MainController.applyHotWaterSettings()
            root.goToIdle()
        }

        Text {
            text: volumeInput.value.toFixed(0) + (isVolumeMode ? " ml" : " g")
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


    // Edit vessel preset popup
    Popup {
        id: editVesselPopup
        x: (parent.width - width) / 2
        y: editVesselPopupAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true

        property bool editVesselPopupAtTop: false
        onOpened: {
            editVesselPopupAtTop = false
            editVesselNameInput.forceActiveFocus()
        }
        onClosed: editVesselPopupAtTop = false

        Connections {
            target: Qt.inputMethod
            function onVisibleChanged() {
                if (Qt.inputMethod.visible && editVesselPopup.opened) {
                    editVesselPopup.editVesselPopupAtTop = true
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
                key: "hotwater.popup.editVesselPreset"
                fallback: "Edit Vessel Preset"
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
                    id: editVesselNameInput
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("hotwater.accessible.renameVessel", "Rename water vessel")
                    Accessible.description: text
                    Accessible.focusable: true

                    Tr {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        key: "hotwater.placeholder.vesselName"
                        fallback: "Vessel name"
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                    }
                }
            }

            RowLayout {
                spacing: Theme.scaled(10)

                AccessibleButton {
                    text: TranslationManager.translate("hotwater.button.delete", "Delete")
                    accessibleName: TranslationManager.translate("hotWater.deleteVesselPreset", "Delete this water vessel preset")
                    destructive: true
                    onClicked: {
                        Settings.removeWaterVesselPreset(editingVesselIndex)
                        editVesselPopup.close()
                    }
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("hotwater.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("hotWater.cancelEditingVessel", "Cancel editing water vessel")
                    onClicked: editVesselPopup.close()
                }

                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("hotwater.button.save", "Save")
                    accessibleName: TranslationManager.translate("hotWater.saveVesselChanges", "Save changes to water vessel preset")
                    onClicked: {
                        var preset = Settings.getWaterVesselPreset(editingVesselIndex)
                        Settings.updateWaterVesselPreset(editingVesselIndex, editVesselNameInput.text, preset.volume, preset.mode || "weight")
                        editVesselPopup.close()
                    }
                }
            }
        }
    }

    // Add vessel dialog
    Popup {
        id: addVesselDialog
        x: (parent.width - width) / 2
        y: addVesselDialogAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true

        property bool addVesselDialogAtTop: false
        onOpened: {
            addVesselDialogAtTop = false
            newVesselNameInput.text = ""
            newVesselNameInput.forceActiveFocus()
        }
        onClosed: addVesselDialogAtTop = false

        Connections {
            target: Qt.inputMethod
            function onVisibleChanged() {
                if (Qt.inputMethod.visible && addVesselDialog.opened) {
                    addVesselDialog.addVesselDialogAtTop = true
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
                key: "hotwater.popup.addVesselPreset"
                fallback: "Add Vessel Preset"
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
                    id: newVesselNameInput
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("hotwater.accessible.newVesselName", "New water vessel name")
                    Accessible.description: text
                    Accessible.focusable: true

                    Tr {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        key: "hotwater.placeholder.vesselName"
                        fallback: "Vessel name"
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                    }
                }
            }

            RowLayout {
                spacing: Theme.scaled(10)

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("hotwater.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("hotWater.cancelAddingVessel", "Cancel adding new water vessel")
                    onClicked: addVesselDialog.close()
                }

                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("hotwater.button.add", "Add")
                    accessibleName: TranslationManager.translate("hotWater.addNewVessel", "Add new water vessel with entered name")
                    onClicked: {
                        if (newVesselNameInput.text.length > 0) {
                            Settings.addWaterVesselPreset(newVesselNameInput.text, 200)
                            newVesselNameInput.text = ""
                            addVesselDialog.close()
                        }
                    }
                }
            }
        }
    }
}
