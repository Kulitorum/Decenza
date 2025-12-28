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
        spacing: 15

        // === DISPENSING VIEW ===
        ColumnLayout {
            visible: isDispensing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 20

            // Preset pills for quick switching during dispensing
            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8

                Repeater {
                    model: Settings.waterVesselPresets

                    Rectangle {
                        width: liveVesselText.implicitWidth + 24
                        height: 36
                        radius: 18
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
                    spacing: 8

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
                        height: 8
                        radius: 4
                        color: Theme.surfaceColor

                        Rectangle {
                            width: parent.width * Math.min(1, Math.max(0, ScaleDevice.weight) / Settings.waterVolume)
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

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: Theme.scaled(20)

                    Text {
                        text: "Vessel Preset"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(24)
                    }

                    // Vessel preset buttons with drag-and-drop
                    Row {
                        id: vesselPresetsRow
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

                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: "Add new hot water preset"
                                accessibleItem: addVesselButton
                                onAccessibleClicked: addVesselDialog.open()
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

                    // Weight (per-vessel, auto-saves)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Text {
                            text: "Weight"
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
                        spacing: 16

                        Text {
                            text: "Temperature"
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

    // Bottom bar
    BottomBar {
        visible: !isDispensing
        title: getCurrentVesselName() || "No vessel"
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
            spacing: 15

            Text {
                text: "Edit Vessel Preset"
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
                    id: editVesselNameInput
                    anchors.fill: parent
                    anchors.margins: 10
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: "Vessel name"
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                    }
                }
            }

            RowLayout {
                spacing: 10

                AccessibleButton {
                    text: "Delete"
                    accessibleName: "Delete preset"
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

                AccessibleButton {
                    text: "Cancel"
                    accessibleName: "Cancel"
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

                AccessibleButton {
                    text: "Save"
                    accessibleName: "Save preset"
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
            spacing: 15

            Text {
                text: "Add Vessel Preset"
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
                    id: newVesselNameInput
                    anchors.fill: parent
                    anchors.margins: 10
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: "Vessel name"
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                    }
                }
            }

            RowLayout {
                spacing: 10

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: "Cancel"
                    accessibleName: "Cancel"
                    onClicked: addVesselDialog.close()
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

                AccessibleButton {
                    text: "Add"
                    accessibleName: "Add preset"
                    onClicked: {
                        if (newVesselNameInput.text.length > 0) {
                            Settings.addWaterVesselPreset(newVesselNameInput.text, 200)
                            newVesselNameInput.text = ""
                            addVesselDialog.close()
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
