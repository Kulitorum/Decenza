// The two vessel Repeaters below declare their model roles with `required property`
// instead of taking them from an injected delegate context, so `page` and the file's
// other ids resolve statically inside them. See PresetPillRow.qml for the same
// treatment. `root` is NOT this file's id — it is main.qml's, reached through the
// creation context StackView pushed this page with, and neither the pragma nor a
// required property can make that statically resolvable from here.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

Page {
    id: page

    objectName: "hotWaterPage"

    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: pageTitleText.text
    background: ThemedPageBackground {}

    // Use StackView.onActivated (not Component.onCompleted) so side effects
    // run when the page is actually shown, not during construction. This
    // also re-fires on pop-back if the page is ever pushed below another.
    // Skip the preset-reset, settings push, and tare while dispensing so a
    // re-activation mid-session doesn't clobber in-progress state.
    StackView.onActivated: {
        if (!page.isDispensing) {
            // Sync Settings with selected preset
            Settings.brew.waterVolume = page.getCurrentVesselVolume()
            Settings.brew.waterVolumeMode = page.getCurrentVesselMode()
            Settings.hardware.hotWaterFlowRate = page.getCurrentVesselFlowRate()
            Settings.brew.waterTemperature = page.getCurrentVesselTemperature()
            MainController.applyHotWaterSettings()
            // Tare immediately so display shows 0g instead of current scale weight.
            if (!page.isVolumeMode) {
                MachineState.tareScale()
            }
            volumeInput.forceActiveFocus()
        }
    }

    // Hidden Tr component for page title (used by root.currentPageTitle)
    Tr { id: pageTitleText; key: "hotwater.title"; fallback: "Hot Water"; visible: false }

    property bool isDispensing: MachineState.phase === MachineState.Phase.HotWater || AppShell.debugLiveView
    property int editingVesselIndex: -1

    property bool isVolumeMode: Settings.brew.waterVolumeMode === "volume"

    // Get current vessel's volume
    function getCurrentVesselVolume() {
        var preset = Settings.brew.getWaterVesselPreset(Settings.brew.selectedWaterVessel)
        return preset ? preset.volume : 200
    }

    function getCurrentVesselName() {
        var preset = Settings.brew.getWaterVesselPreset(Settings.brew.selectedWaterVessel)
        return preset ? preset.name : ""
    }

    function getCurrentVesselMode() {
        var preset = Settings.brew.getWaterVesselPreset(Settings.brew.selectedWaterVessel)
        return (preset && preset.mode) ? preset.mode : "weight"
    }

    function getCurrentVesselFlowRate() {
        var preset = Settings.brew.getWaterVesselPreset(Settings.brew.selectedWaterVessel)
        return (preset && preset.flowRate !== undefined) ? preset.flowRate : 40
    }

    function getCurrentVesselTemperature() {
        var preset = Settings.brew.getWaterVesselPreset(Settings.brew.selectedWaterVessel)
        return (preset && preset.temperature !== undefined) ? preset.temperature : Settings.brew.waterTemperature
    }

    // Save current vessel with all parameters. Temperature defaults to the
    // current temperature input so callers that only change volume/flow keep it.
    function saveCurrentVessel(volume, flowRate, temperature) {
        var name = page.getCurrentVesselName()
        if (name) {
            var temp = (temperature !== undefined) ? temperature : temperatureInput.value
            Settings.brew.updateWaterVesselPreset(Settings.brew.selectedWaterVessel, name, volume, Settings.brew.waterVolumeMode, flowRate, temp)
        }
    }

    // Repeater.itemAt() is typed QQuickItem, so the delegate's own `focusTarget`
    // is not statically known and every call site was an unchecked member access.
    // Kept to this one place rather than repeated at the six sites that need it.
    function vesselFocusTarget(i: int): Item {
        var it = vesselRepeater.itemAt(i)
        return it ? it.focusTarget : null
    }

    function focusVesselAt(i: int) {
        var target = page.vesselFocusTarget(i)
        if (target)
            target.forceActiveFocus()
    }

    // Select a vessel preset and load all of its parameters into the inputs.
    // Legacy presets with no stored temperature fall back to the current value.
    function selectVessel(index, vesselData) {
        // getWaterVesselPreset() returns an empty {} for an out-of-range index;
        // bail rather than write undefined volume/mode into Settings.
        if (!vesselData || vesselData.volume === undefined)
            return
        var flow = (vesselData.flowRate !== undefined) ? vesselData.flowRate : 40
        var temp = (vesselData.temperature !== undefined) ? vesselData.temperature : Settings.brew.waterTemperature
        Settings.brew.selectedWaterVessel = index
        volumeInput.value = vesselData.volume
        flowRateInput.value = flow
        temperatureInput.value = temp
        Settings.brew.waterVolume = vesselData.volume
        Settings.brew.waterVolumeMode = (vesselData.mode || "weight")
        Settings.hardware.hotWaterFlowRate = flow
        Settings.brew.waterTemperature = temp
        MainController.applyHotWaterSettings()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.pageTopMargin  // Space for bottom bar
        spacing: Theme.scaled(15)

        // === DISPENSING VIEW ===
        ColumnLayout {
            visible: page.isDispensing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(20)

            // Preset pills for quick switching during dispensing
            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(8)

                Repeater {
                    id: liveVesselRepeater
                    model: Settings.brew.waterVesselPresets

                    Rectangle {
                        id: liveVesselPill

                        required property int index
                        required property var modelData

                        width: liveVesselText.implicitWidth + 24
                        height: Theme.scaled(36)
                        radius: Theme.scaled(18)
                        color: liveVesselPill.index === Settings.brew.selectedWaterVessel ? Theme.primaryColor : Theme.cardBackgroundColor
                        border.color: liveVesselPill.index === Settings.brew.selectedWaterVessel ? Theme.primaryColor : Theme.textSecondaryColor
                        border.width: 1

                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: liveVesselPill.modelData.name + " " + TranslationManager.translate("hotwater.accessibility.vessel", "vessel") +
                                         (liveVesselPill.index === Settings.brew.selectedWaterVessel ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                        Accessible.focusable: true
                        Accessible.onPressAction: liveVesselArea.clicked(null)

                        Keys.onReturnPressed: function(event) { liveVesselArea.clicked(null); event.accepted = true }
                        Keys.onSpacePressed:  function(event) { liveVesselArea.clicked(null); event.accepted = true }
                        Keys.onLeftPressed: function(event) {
                            if (liveVesselPill.index > 0) liveVesselRepeater.itemAt(liveVesselPill.index - 1).forceActiveFocus()
                            event.accepted = true
                        }
                        Keys.onRightPressed: function(event) {
                            if (liveVesselPill.index < liveVesselRepeater.count - 1) liveVesselRepeater.itemAt(liveVesselPill.index + 1).forceActiveFocus()
                            event.accepted = true
                        }
                        Keys.onTabPressed: function(event) {
                            if (liveVesselPill.index < liveVesselRepeater.count - 1)
                                liveVesselRepeater.itemAt(liveVesselPill.index + 1).forceActiveFocus()
                            else if (hotWaterStopButton.visible)
                                hotWaterStopButton.forceActiveFocus()
                            else
                                liveVesselRepeater.itemAt(0).forceActiveFocus()
                            event.accepted = true
                        }
                        Keys.onBacktabPressed: function(event) {
                            if (liveVesselPill.index > 0)
                                liveVesselRepeater.itemAt(liveVesselPill.index - 1).forceActiveFocus()
                            else if (hotWaterStopButton.visible)
                                hotWaterStopButton.forceActiveFocus()
                            else
                                liveVesselRepeater.itemAt(liveVesselRepeater.count - 1).forceActiveFocus()
                            event.accepted = true
                        }

                        Text {
                            id: liveVesselText
                            anchors.centerIn: parent
                            text: liveVesselPill.modelData.name
                            color: liveVesselPill.index === Settings.brew.selectedWaterVessel ? Theme.primaryContrastColor : Theme.textColor
                            font: Theme.bodyFont
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: liveVesselArea
                            anchors.fill: parent
                            onClicked: {
                                Settings.brew.selectedWaterVessel = liveVesselPill.index
                                Settings.brew.waterVolume = liveVesselPill.modelData.volume
                                Settings.brew.waterVolumeMode = (liveVesselPill.modelData.mode ?? "weight")
                                Settings.hardware.hotWaterFlowRate = (liveVesselPill.modelData.flowRate !== undefined) ? liveVesselPill.modelData.flowRate : 40
                                Settings.brew.waterTemperature = (liveVesselPill.modelData.temperature !== undefined) ? liveVesselPill.modelData.temperature : Settings.brew.waterTemperature
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
                        visible: !page.isVolumeMode
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Math.max(0, MachineState.scaleWeight).toFixed(0) + "g / " + Settings.brew.waterVolume + "g"
                        color: Theme.textColor
                        font: Theme.timerFont
                    }

                    // Volume mode: show target
                    Text {
                        id: hotWaterVolumeText
                        visible: page.isVolumeMode
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Settings.brew.waterVolume + " ml"
                        color: Theme.textColor
                        font: Theme.timerFont
                    }

                    Tr {
                        visible: page.isVolumeMode
                        anchors.horizontalCenter: parent.horizontalCenter
                        key: "hotwater.dispensing.flowmeter"
                        fallback: "Dispensing (flowmeter)"
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                    }

                    // Progress bar (weight mode only — scale provides live data)
                    Rectangle {
                        visible: !page.isVolumeMode
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: hotWaterProgressText.width
                        height: Theme.scaled(8)
                        radius: Theme.scaled(4)
                        color: Theme.surfaceColor

                        Rectangle {
                            width: parent.width * Math.min(1, Math.max(0, MachineState.scaleWeight) / Settings.brew.waterVolume)
                            height: parent.height
                            radius: Theme.scaled(4)
                            color: Theme.primaryColor
                        }
                    }
                }
            }

            // Live flow rate control
            ValueInput {
                id: liveFlowRateInput
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Theme.scaled(180)
                value: Settings.hardware.hotWaterFlowRate
                from: 5
                to: 100
                stepSize: 5
                displayText: (value / 10).toFixed(1) + " mL/s"
                valueColor: Theme.flowColor
                accessibleName: TranslationManager.translate("hotwater.label.flowRate", "Flow Rate")
                KeyNavigation.tab: hotWaterStopButton.visible ? hotWaterStopButton : (liveVesselRepeater.count > 0 ? liveVesselRepeater.itemAt(0) : liveFlowRateInput)
                KeyNavigation.backtab: liveVesselRepeater.count > 0 ? liveVesselRepeater.itemAt(liveVesselRepeater.count - 1) : liveFlowRateInput

                // Cheap Settings write per tick so the slider's `value:`
                // binding re-evaluates and the displayed number tracks the
                // user's adjustment live during a hold. ValueInput doesn't
                // self-mutate root.value — without a consumer writing the
                // bound source, the displayed value stays pinned.
                onValueModified: function(newValue) {
                    Settings.hardware.hotWaterFlowRate = Math.round(newValue)
                }
                // BLE write deferred to commit so holding +/- doesn't
                // spam the flow-rate MMR register every 80 ms.
                onValueCommitted: function(newValue) {
                    MainController.setHotWaterFlowRateImmediate(Math.round(newValue))
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
                border.color: Theme.primaryContrastColor
                border.width: Theme.scaled(2)

                activeFocusOnTab: true
                Keys.onReturnPressed: function(event) { DE1Device.stopOperation(); AppShell.idleRequested(); event.accepted = true }
                Keys.onSpacePressed:  function(event) { DE1Device.stopOperation(); AppShell.idleRequested(); event.accepted = true }
                Keys.onTabPressed: function(event) {
                    if (liveVesselRepeater.count > 0) liveVesselRepeater.itemAt(0).forceActiveFocus()
                    event.accepted = true
                }
                Keys.onBacktabPressed: function(event) {
                    if (liveVesselRepeater.count > 0) liveVesselRepeater.itemAt(liveVesselRepeater.count - 1).forceActiveFocus()
                    event.accepted = true
                }

                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("hotwater.button.stop", "STOP")
                    color: Theme.primaryContrastColor
                    font.pixelSize: Theme.scaled(24)
                    font.weight: Font.Bold
                    Accessible.ignored: true
                }

                // Using TapHandler for better touch responsiveness
                AccessibleTapHandler {
                    id: stopTapHandler
                    anchors.fill: parent
                    accessibleName: TranslationManager.translate("hotwater.accessible.stopHotWater", "Stop hot water")
                    accessibleItem: hotWaterStopButton
                    onAccessibleClicked: {
                        DE1Device.stopOperation()
                        AppShell.idleRequested()
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.scaled(20) }
        }

        // === SETTINGS VIEW ===
        ColumnLayout {
            visible: !page.isDispensing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(12)

            // Vessel Presets Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(90)
                color: Theme.cardBackgroundColor
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
                            model: Settings.brew.waterVesselPresets

                            Item {
                                id: vesselDelegate

                                required property int index
                                required property var modelData

                                width: vesselPill.width
                                height: Theme.scaled(36)

                                property int vesselIndex: vesselDelegate.index
                                property Item focusTarget: vesselPill

                                Rectangle {
                                    id: vesselPill
                                    width: vesselText.implicitWidth + 24
                                    height: Theme.scaled(36)
                                    radius: Theme.scaled(18)
                                    color: vesselDelegate.vesselIndex === Settings.brew.selectedWaterVessel ? Theme.primaryColor : Theme.insetBackgroundColor
                                    border.color: vesselDelegate.vesselIndex === Settings.brew.selectedWaterVessel ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: dragArea.drag.active ? 0.8 : 1.0

                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.Button
                                    Accessible.name: vesselDelegate.modelData.name + " " + TranslationManager.translate("hotwater.accessibility.preset", "preset") +
                                                     (vesselDelegate.vesselIndex === Settings.brew.selectedWaterVessel ?
                                                      ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                                    Accessible.description: TranslationManager.translate("hotwater.accessibility.presetHint", "Double-tap or long-press to rename.")
                                    Accessible.focusable: true
                                    Accessible.onPressAction: {
                                        page.selectVessel(vesselDelegate.vesselIndex, vesselDelegate.modelData)
                                    }

                                    Keys.onReturnPressed: function(event) {
                                        page.selectVessel(vesselDelegate.vesselIndex, vesselDelegate.modelData)
                                        event.accepted = true
                                    }
                                    Keys.onSpacePressed: function(event) {
                                        page.selectVessel(vesselDelegate.vesselIndex, vesselDelegate.modelData)
                                        event.accepted = true
                                    }
                                    Keys.onLeftPressed: function(event) {
                                        if (vesselDelegate.index > 0) page.focusVesselAt(vesselDelegate.index - 1)
                                        event.accepted = true
                                    }
                                    Keys.onRightPressed: function(event) {
                                        if (vesselDelegate.index < vesselRepeater.count - 1) page.focusVesselAt(vesselDelegate.index + 1)
                                        event.accepted = true
                                    }
                                    Keys.onTabPressed: function(event) {
                                        if (vesselDelegate.index < vesselRepeater.count - 1)
                                            page.focusVesselAt(vesselDelegate.index + 1)
                                        else
                                            addVesselButton.forceActiveFocus()
                                        event.accepted = true
                                    }
                                    Keys.onBacktabPressed: function(event) {
                                        if (vesselDelegate.index > 0)
                                            page.focusVesselAt(vesselDelegate.index - 1)
                                        else
                                            flowRateInput.forceActiveFocus()
                                        event.accepted = true
                                    }

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
                                        text: vesselDelegate.modelData.name
                                        color: vesselDelegate.vesselIndex === Settings.brew.selectedWaterVessel ? Theme.primaryContrastColor : Theme.textColor
                                        font: Theme.bodyFont
                                        Accessible.ignored: true
                                    }

                                    MouseArea {
                                        id: dragArea
                                        anchors.fill: parent
                                        drag.target: vesselPill
                                        drag.axis: Drag.XAxis

                                        property bool held: false
                                        property bool moved: false

                                        onPressed: {
                                            dragArea.held = false
                                            dragArea.moved = false
                                            holdTimer.start()
                                        }

                                        onReleased: {
                                            holdTimer.stop()
                                            if (!dragArea.moved && !dragArea.held) {
                                                // Simple click - select the vessel
                                                page.selectVessel(vesselDelegate.vesselIndex, vesselDelegate.modelData)
                                            }
                                            vesselPill.Drag.drop()
                                            vesselPresetsRow.draggedIndex = -1
                                        }

                                        onPositionChanged: {
                                            if (dragArea.drag.active) {
                                                dragArea.moved = true
                                                vesselPresetsRow.draggedIndex = vesselDelegate.vesselIndex
                                            }
                                        }

                                        onDoubleClicked: {
                                            holdTimer.stop()
                                            dragArea.held = true  // Prevent single-click selection on release
                                            page.editingVesselIndex = vesselDelegate.vesselIndex
                                            editVesselNameInput.text = vesselDelegate.modelData.name
                                            editVesselPopup.open()
                                        }

                                        Timer {
                                            id: holdTimer
                                            interval: 500
                                            onTriggered: {
                                                if (!dragArea.moved) {
                                                    dragArea.held = true
                                                    page.editingVesselIndex = vesselDelegate.vesselIndex
                                                    editVesselNameInput.text = vesselDelegate.modelData.name
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
                                            Settings.brew.moveWaterVesselPreset(fromIndex, toIndex)
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

                            activeFocusOnTab: true
                            KeyNavigation.tab: volumeInput
                            KeyNavigation.backtab: vesselRepeater.count > 0
                                ? page.vesselFocusTarget(vesselRepeater.count - 1)
                                : volumeInput
                            Keys.onReturnPressed: function(event) { addVesselDialog.open(); event.accepted = true }
                            Keys.onSpacePressed:  function(event) { addVesselDialog.open(); event.accepted = true }

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
                                accessibleName: TranslationManager.translate("hotwater.accessible.addPreset", "Add new hot water preset")
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
                color: Theme.cardBackgroundColor
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
                                id: weightModeButton
                                width: weightModeText.implicitWidth + Theme.scaled(20)
                                height: Theme.scaled(36)
                                radius: Theme.scaled(18)
                                color: !page.isVolumeMode ? Theme.primaryColor : Theme.insetBackgroundColor
                                border.color: !page.isVolumeMode ? Theme.primaryColor : Theme.textSecondaryColor
                                border.width: 1

                                activeFocusOnTab: true
                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate("hotwater.mode.weight", "Weight (g)") +
                                                 (!page.isVolumeMode ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                                Accessible.focusable: true
                                Accessible.onPressAction: weightModeArea.clicked(null)
                                Keys.onReturnPressed: function(event) { weightModeArea.clicked(null); event.accepted = true }
                                Keys.onSpacePressed:  function(event) { weightModeArea.clicked(null); event.accepted = true }
                                KeyNavigation.tab: volumeModeButton
                                KeyNavigation.backtab: temperatureInput

                                Text {
                                    id: weightModeText
                                    anchors.centerIn: parent
                                    text: TranslationManager.translate("hotwater.mode.weight", "Weight (g)")
                                    color: !page.isVolumeMode ? Theme.primaryContrastColor : Theme.textColor
                                    font: Theme.bodyFont
                                    Accessible.ignored: true
                                }

                                MouseArea {
                                    id: weightModeArea
                                    anchors.fill: parent
                                    onClicked: {
                                        Settings.brew.waterVolumeMode = "weight"
                                        page.saveCurrentVessel(volumeInput.value, flowRateInput.value)
                                        MainController.applyHotWaterSettings()
                                    }
                                }
                            }

                            Rectangle {
                                id: volumeModeButton
                                width: volumeModeText.implicitWidth + Theme.scaled(20)
                                height: Theme.scaled(36)
                                radius: Theme.scaled(18)
                                color: page.isVolumeMode ? Theme.primaryColor : Theme.insetBackgroundColor
                                border.color: page.isVolumeMode ? Theme.primaryColor : Theme.textSecondaryColor
                                border.width: 1

                                activeFocusOnTab: true
                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate("hotwater.mode.volume", "Volume (ml)") +
                                                 (page.isVolumeMode ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                                Accessible.focusable: true
                                Accessible.onPressAction: volumeModeArea.clicked(null)
                                Keys.onReturnPressed: function(event) { volumeModeArea.clicked(null); event.accepted = true }
                                Keys.onSpacePressed:  function(event) { volumeModeArea.clicked(null); event.accepted = true }
                                KeyNavigation.tab: volumeInput
                                KeyNavigation.backtab: weightModeButton

                                Text {
                                    id: volumeModeText
                                    anchors.centerIn: parent
                                    text: TranslationManager.translate("hotwater.mode.volume", "Volume (ml)")
                                    color: page.isVolumeMode ? Theme.primaryContrastColor : Theme.textColor
                                    font: Theme.bodyFont
                                    Accessible.ignored: true
                                }

                                MouseArea {
                                    id: volumeModeArea
                                    anchors.fill: parent
                                    onClicked: {
                                        Settings.brew.waterVolumeMode = "volume"
                                        // Clamp value to 255ml max for volume mode
                                        if (volumeInput.value > 255) {
                                            volumeInput.value = 255
                                            Settings.brew.waterVolume = 255
                                            page.saveCurrentVessel(255, flowRateInput.value)
                                        } else {
                                            page.saveCurrentVessel(volumeInput.value, flowRateInput.value)
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
                            value: page.getCurrentVesselVolume()
                            from: 10
                            to: page.isVolumeMode ? 255 : 500
                            stepSize: 10
                            fineStepSize: 1
                            suffix: page.isVolumeMode ? " ml" : " g"
                            valueColor: Theme.primaryColor
                            accessibleName: page.isVolumeMode
                                ? TranslationManager.translate("hotwater.label.volume", "Volume")
                                : TranslationManager.translate("hotwater.label.weight", "Weight")
                            KeyNavigation.tab: temperatureInput
                            KeyNavigation.backtab: volumeModeButton

                            onValueModified: function(newValue) {
                                volumeInput.value = newValue
                                Settings.brew.waterVolume = newValue
                                page.saveCurrentVessel(newValue, flowRateInput.value)
                            }
                            onValueCommitted: MainController.applyHotWaterSettings()
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.textSecondaryColor; opacity: 0.3 }

                    // Temperature (per-preset)
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
                            // Stored in Celsius; shown and entered in the user's unit.
                            value: Theme.cToDisplay(Settings.brew.waterTemperature)
                            from: Theme.cToDisplay(40)
                            to: Theme.cToDisplay(100)
                            stepSize: 1
                            suffix: Theme.tempUnitSuffix()
                            valueColor: Theme.temperatureColor
                            accessibleName: TranslationManager.translate("hotwater.label.temperature", "Temperature")
                            KeyNavigation.tab: flowRateInput
                            KeyNavigation.backtab: volumeInput

                            onValueModified: function(newValue) {
                                // Convert the entered display value back to Celsius for storage;
                                // the bound value re-derives from the setting via cToDisplay.
                                var celsius = Theme.displayToC(newValue)
                                Settings.brew.waterTemperature = celsius
                                page.saveCurrentVessel(volumeInput.value, flowRateInput.value, celsius)
                            }
                            onValueCommitted: MainController.applyHotWaterSettings()
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.textSecondaryColor; opacity: 0.3 }

                    // Flow Rate (per-preset, stored as tenths of mL/s)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)

                        Tr {
                            key: "hotwater.label.flowRate"
                            fallback: "Flow Rate"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(24)
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: flowRateInput
                            Layout.preferredWidth: Theme.scaled(180)
                            value: page.getCurrentVesselFlowRate()
                            from: 5
                            to: 100
                            stepSize: 5
                            displayText: (value / 10).toFixed(1) + " mL/s"
                            valueColor: Theme.flowColor
                            accessibleName: TranslationManager.translate("hotwater.label.flowRate", "Flow Rate")
                            KeyNavigation.tab: vesselRepeater.count > 0
                                ? page.vesselFocusTarget(0)
                                : addVesselButton
                            KeyNavigation.backtab: temperatureInput

                            onValueModified: function(newValue) {
                                flowRateInput.value = Math.round(newValue)
                                Settings.hardware.hotWaterFlowRate = Math.round(newValue)
                                page.saveCurrentVessel(volumeInput.value, Math.round(newValue))
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
        id: hotWaterBottomBar
        visible: !page.isDispensing
        title: page.getCurrentVesselName() || noVesselText.text
        onBackClicked: {
            MainController.applyHotWaterSettings()
            AppShell.idleRequested()
        }

        Text {
            text: volumeInput.value.toFixed(0) + (page.isVolumeMode ? " ml" : " g")
            color: hotWaterBottomBar.contentColor
            font: Theme.bodyFont
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: hotWaterBottomBar.contentColor; opacity: 0.3 }
        Text {
            text: temperatureInput.value.toFixed(0) + Theme.tempUnitSuffix()
            color: hotWaterBottomBar.contentColor
            font: Theme.bodyFont
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: hotWaterBottomBar.contentColor; opacity: 0.3 }
        Text {
            text: (flowRateInput.value / 10).toFixed(1) + " mL/s"
            color: hotWaterBottomBar.contentColor
            font: Theme.bodyFont
        }
    }


    // Edit vessel preset popup
    Dialog {
        id: editVesselPopup
        x: (parent.width - width) / 2
        y: editVesselPopup.editVesselPopupAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        property bool editVesselPopupAtTop: false
        onOpened: {
            editVesselPopup.editVesselPopupAtTop = false
            editVesselNameInput.forceActiveFocus()
        }
        onClosed: editVesselPopup.editVesselPopupAtTop = false

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
                    activeFocusOnTab: true
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("hotwater.accessible.renameVessel", "Rename water vessel")
                    Accessible.description: text
                    Accessible.focusable: true
                    KeyNavigation.tab: deleteVesselButton
                    KeyNavigation.backtab: saveVesselButton

                    Tr {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        key: "hotwater.placeholder.vesselName"
                        fallback: "Vessel name"
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
                    id: deleteVesselButton
                    text: TranslationManager.translate("hotwater.button.delete", "Delete")
                    accessibleName: TranslationManager.translate("hotWater.deleteVesselPreset", "Delete this water vessel preset")
                    destructive: true
                    KeyNavigation.tab: cancelEditVesselButton
                    KeyNavigation.backtab: editVesselNameInput
                    onClicked: {
                        Settings.brew.removeWaterVesselPreset(page.editingVesselIndex)
                        editVesselPopup.close()
                    }
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    id: cancelEditVesselButton
                    text: TranslationManager.translate("hotwater.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("hotWater.cancelEditingVessel", "Cancel editing water vessel")
                    KeyNavigation.tab: saveVesselButton
                    KeyNavigation.backtab: deleteVesselButton
                    onClicked: editVesselPopup.close()
                }

                AccessibleButton {
                    id: saveVesselButton
                    primary: true
                    text: TranslationManager.translate("hotwater.button.save", "Save")
                    accessibleName: TranslationManager.translate("hotWater.saveVesselChanges", "Save changes to water vessel preset")
                    KeyNavigation.tab: editVesselNameInput
                    KeyNavigation.backtab: cancelEditVesselButton
                    // A vessel is identified by its NAME everywhere it is used —
                    // recipes snapshot it by name, and the recipe wizard matches
                    // its tiles on it. So a blank name (nothing can refer to it)
                    // and a name another vessel already holds (two vessels that
                    // cannot be told apart) are both refused, as in the Add
                    // dialog. ignoreIndex is this preset: keeping its own name is
                    // not a clash.
                    enabled: editVesselNameInput.text.trim().length > 0
                        && !Settings.brew.waterVesselNameTaken(editVesselNameInput.text, page.editingVesselIndex)
                    onClicked: {
                        Qt.inputMethod.commit()
                        if (editVesselNameInput.text.trim().length === 0
                                || Settings.brew.waterVesselNameTaken(editVesselNameInput.text, page.editingVesselIndex))
                            return
                        var preset = Settings.brew.getWaterVesselPreset(page.editingVesselIndex)
                        Settings.brew.updateWaterVesselPreset(page.editingVesselIndex, editVesselNameInput.text, preset.volume, preset.mode || "weight", (preset.flowRate !== undefined) ? preset.flowRate : 40, (preset.temperature !== undefined) ? preset.temperature : Settings.brew.waterTemperature)
                        editVesselPopup.close()
                    }
                }
            }
        }
    }

    // Add vessel dialog
    Dialog {
        id: addVesselDialog
        x: (parent.width - width) / 2
        y: addVesselDialog.addVesselDialogAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        property bool addVesselDialogAtTop: false
        onOpened: {
            addVesselDialog.addVesselDialogAtTop = false
            newVesselNameInput.text = ""
            newVesselNameInput.forceActiveFocus()
        }
        onClosed: addVesselDialog.addVesselDialogAtTop = false

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
                    activeFocusOnTab: true
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("hotwater.accessible.newVesselName", "New water vessel name")
                    Accessible.description: text
                    Accessible.focusable: true
                    KeyNavigation.tab: cancelAddVesselButton
                    KeyNavigation.backtab: addVesselConfirmButton

                    Tr {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        key: "hotwater.placeholder.vesselName"
                        fallback: "Vessel name"
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                        Accessible.ignored: true
                    }
                }
            }

            // Two vessels sharing a name are indistinguishable everywhere they
            // are used — recipes snapshot the vessel BY NAME — so the clash is
            // named here rather than letting the setter drop the write silently.
            Label {
                visible: newVesselNameInput.text.trim().length > 0
                    && Settings.brew.waterVesselNameTaken(newVesselNameInput.text, -1)
                Layout.fillWidth: true
                text: TranslationManager.translate("hotwater.vesselNameInUse",
                          "That name is already used by another vessel — choose a different one.")
                font: Theme.captionFont
                color: Theme.warningColor
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            RowLayout {
                spacing: Theme.scaled(10)

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    id: cancelAddVesselButton
                    text: TranslationManager.translate("hotwater.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("hotWater.cancelAddingVessel", "Cancel adding new water vessel")
                    KeyNavigation.tab: addVesselConfirmButton
                    KeyNavigation.backtab: newVesselNameInput
                    onClicked: addVesselDialog.close()
                }

                AccessibleButton {
                    id: addVesselConfirmButton
                    primary: true
                    text: TranslationManager.translate("hotwater.button.add", "Add")
                    accessibleName: TranslationManager.translate("hotWater.addNewVessel", "Add new water vessel with entered name")
                    enabled: newVesselNameInput.text.trim().length > 0
                        && !Settings.brew.waterVesselNameTaken(newVesselNameInput.text, -1)
                    KeyNavigation.tab: newVesselNameInput
                    KeyNavigation.backtab: cancelAddVesselButton
                    onClicked: {
                        Qt.inputMethod.commit()
                        if (newVesselNameInput.text.length > 0) {
                            Settings.brew.addWaterVesselPreset(newVesselNameInput.text, 200, "weight", 40, Settings.brew.waterTemperature)
                            // Select the just-added preset (appended at the end) and load its
                            // values into the inputs, so edits apply to the new preset rather
                            // than the previously-selected one.
                            var newIndex = Settings.brew.waterVesselPresets.length - 1
                            page.selectVessel(newIndex, Settings.brew.getWaterVesselPreset(newIndex))
                            newVesselNameInput.text = ""
                            addVesselDialog.close()
                        }
                    }
                }
            }
        }
    }
}
