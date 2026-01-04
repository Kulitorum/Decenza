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
        // Sync Settings.waterVolume with selected preset
        Settings.waterVolume = getCurrentVesselVolume()
        MainController.applyHotWaterSettings()
    }
    StackView.onActivated: root.currentPageTitle = pageTitleText.text

    // Hidden Tr component for page title (used by root.currentPageTitle)
    Tr { id: pageTitleText; key: "hotwater.title"; fallback: "Hot Water"; visible: false }

    property bool isDispensing: MachineState.phase === MachineStateType.Phase.HotWater || root.debugLiveView
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
                                MainController.applyHotWaterSettings()
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // Weight progress with progress bar
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.scaled(8)

                    Text {
                        id: hotWaterProgressText
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Math.max(0, ScaleDevice.weight).toFixed(0) + "g / " + Settings.waterVolume + "g"
                        color: Theme.textColor
                        font: Theme.timerFont
                    }

                    // Progress bar
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: hotWaterProgressText.width
                        height: Theme.scaled(8)
                        radius: Theme.scaled(4)
                        color: Theme.surfaceColor

                        Rectangle {
                            width: parent.width * Math.min(1, Math.max(0, ScaleDevice.weight) / Settings.waterVolume)
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

                    // Weight (per-vessel, auto-saves)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)

                        Tr {
                            key: "hotwater.label.weight"
                            fallback: "Weight"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(24)
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: volumeInput
                            Layout.preferredWidth: Theme.scaled(180)
                            value: getCurrentVesselVolume()
                            from: 50
                            to: 500
                            stepSize: 10
                            suffix: " g"
                            valueColor: Theme.primaryColor

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
            text: volumeInput.value.toFixed(0) + " g"
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
                    text: deleteButtonText.text
                    accessibleName: "Delete preset"
                    onClicked: {
                        Settings.removeWaterVesselPreset(editingVesselIndex)
                        editVesselPopup.close()
                    }
                    Tr { id: deleteButtonText; key: "hotwater.button.delete"; fallback: "Delete"; visible: false }
                    background: Rectangle {
                        implicitWidth: Theme.scaled(80)
                        implicitHeight: Theme.scaled(36)
                        radius: Theme.scaled(6)
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

                AccessibleButton {
                    text: cancelButtonText.text
                    accessibleName: "Cancel"
                    onClicked: editVesselPopup.close()
                    Tr { id: cancelButtonText; key: "hotwater.button.cancel"; fallback: "Cancel"; visible: false }
                    background: Rectangle {
                        implicitWidth: Theme.scaled(70)
                        implicitHeight: Theme.scaled(36)
                        radius: Theme.scaled(6)
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

                AccessibleButton {
                    text: saveButtonText.text
                    accessibleName: "Save preset"
                    onClicked: {
                        var preset = Settings.getWaterVesselPreset(editingVesselIndex)
                        Settings.updateWaterVesselPreset(editingVesselIndex, editVesselNameInput.text, preset.volume)
                        editVesselPopup.close()
                    }
                    Tr { id: saveButtonText; key: "hotwater.button.save"; fallback: "Save"; visible: false }
                    background: Rectangle {
                        implicitWidth: Theme.scaled(70)
                        implicitHeight: Theme.scaled(36)
                        radius: Theme.scaled(6)
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
                    text: addCancelButtonText.text
                    accessibleName: "Cancel"
                    onClicked: addVesselDialog.close()
                    Tr { id: addCancelButtonText; key: "hotwater.button.cancel"; fallback: "Cancel"; visible: false }
                    background: Rectangle {
                        implicitWidth: Theme.scaled(70)
                        implicitHeight: Theme.scaled(36)
                        radius: Theme.scaled(6)
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

                AccessibleButton {
                    text: addButtonText.text
                    accessibleName: "Add preset"
                    onClicked: {
                        if (newVesselNameInput.text.length > 0) {
                            Settings.addWaterVesselPreset(newVesselNameInput.text, 200)
                            newVesselNameInput.text = ""
                            addVesselDialog.close()
                        }
                    }
                    Tr { id: addButtonText; key: "hotwater.button.add"; fallback: "Add"; visible: false }
                    background: Rectangle {
                        implicitWidth: Theme.scaled(70)
                        implicitHeight: Theme.scaled(36)
                        radius: Theme.scaled(6)
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
