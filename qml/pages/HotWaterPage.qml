// Vessel delegates declare their model roles explicitly so bindings remain
// statically resolvable in both native and remote engines.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

T.Page {
    id: page

    objectName: "hotWaterPage"

    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: pageTitleText.text
    background: ThemedPageBackground {}

    SharedPresetDraft { id: presetDraft; domain: "waterVessel" }
    readonly property bool browserHasUnsavedChanges: presetDraft.dirty
    StatusToast { id: actionToast; error: true }
    property string pendingPageActionId: ""
    property string pendingPageAction: ""
    property string editingExpectedName: ""
    property double editingRevision: 0
    property double addDialogRevision: 0
    property int pendingSelectionIndex: -1
    property bool pendingBackAfterDraft: false
    property real liveFlowRateDraft: Number(RecipeContext.preferences.hotWaterFlowRate ?? 10)
    property bool liveFlowEditing: false

    function syncLiveFlowRate() {
        if (RecipeContext.ready && !liveFlowEditing)
            liveFlowRateDraft = Number(RecipeContext.preferences.hotWaterFlowRate)
    }

    Connections {
        target: RecipeContext
        function onPreferencesChanged() { page.syncLiveFlowRate() }
        function onStateChanged() { page.syncLiveFlowRate() }
    }

    function reportActionFailure() {
        actionToast.show(TranslationManager.translate(
            "hotwater.action.failed", "Couldn't update hot water settings. Please try again."))
    }

    function applySelection() {
        if (pendingPageActionId || presetDraft.pending)
            return
        if (presetDraft.selectedIndex >= 0) {
            const id = MachineControlService.selectWaterVessel(
                presetDraft.selectedIndex, presetDraft.selectedPreset)
            pendingPageActionId = id
            pendingPageAction = "apply"
        } else {
            const id = MachineControlService.applyHotWaterSettings(SteamService.state.controlContextId)
            pendingPageActionId = id
            pendingPageAction = "apply"
        }
    }

    function requestBack() {
        if (pendingPageActionId)
            return
        if (AppShell.debugLiveView && MachineState.phase !== MachineState.Phase.HotWater) {
            AppShell.dismissRequested()
            return
        }
        if (!isDispensing && presetDraft.dirty) {
            if (presetDraft.stale) {
                pendingBackAfterDraft = true
                staleDraftDialog.open()
                return
            }
            pendingBackAfterDraft = true
            presetDraft.commit()
            return
        }
        const id = isDispensing
                ? MachineControlService.stopHotWater(SteamService.state.controlContextId)
                : MachineControlService.applyHotWaterSettings(SteamService.state.controlContextId)
        pendingPageActionId = id
        pendingPageAction = isDispensing ? "stop" : "back"
    }

    function adjustTarget(delta) {
        if (pendingPageActionId)
            return
        pendingPageActionId = MachineControlService.bumpHotWaterTarget(
                    SteamService.state.controlContextId, delta)
        pendingPageAction = "adjust-target"
    }

    // Tab order while pouring: vessels, -/+ target, flow rate, stop.
    function liveFocusChain() {
        const chain = []
        for (let i = 0; i < liveVesselRepeater.count; ++i) {
            const pill = liveVesselRepeater.itemAt(i)
            if (pill)
                chain.push(pill)
        }
        if (hotWaterSubtractButton.visible)
            chain.push(hotWaterSubtractButton, hotWaterAddButton)
        chain.push(liveFlowRateInput)
        if (hotWaterStopButton.visible)
            chain.push(hotWaterStopButton)
        return chain
    }

    function liveFocusNeighbour(item, forward) {
        const chain = liveFocusChain()
        const index = chain.indexOf(item)
        return chain[(index + (forward ? 1 : chain.length - 1)) % chain.length]
    }

    function requestStop() {
        if (pendingPageActionId)
            return
        if (AppShell.debugLiveView && MachineState.phase !== MachineState.Phase.HotWater) {
            AppShell.dismissRequested()
            return
        }
        pendingPageActionId = MachineControlService.stopHotWater(
                    SteamService.state.controlContextId)
        pendingPageAction = "stop"
    }

    Connections {
        target: MachineControlService
        function onCompleted(requestId, operation, success, message, result) {
            if (requestId !== page.pendingPageActionId)
                return
            const action = page.pendingPageAction
            page.pendingPageActionId = ""
            page.pendingPageAction = ""
            if (!success) {
                if (action === "back" || action === "stop")
                    AppShell.browserBackCancelled()
                if (action === "flow")
                    page.syncLiveFlowRate()
                page.reportActionFailure()
                return
            }
            if (action === "back" || action === "stop")
                AppShell.dismissRequested()
        }
    }

    Connections {
        target: presetDraft
        function onCompleted(operation, success, reason) {
            if (!success) {
                if (reason === "stalePreferences") {
                    page.pendingSelectionIndex = -1
                    staleDraftDialog.open()
                    return
                }
                page.pendingSelectionIndex = -1
                if (page.pendingBackAfterDraft)
                    AppShell.browserBackCancelled()
                page.pendingBackAfterDraft = false
                page.reportActionFailure()
                return
            }
            if (operation === "commit" && page.pendingSelectionIndex >= 0) {
                const index = page.pendingSelectionIndex
                page.pendingSelectionIndex = -1
                page.selectVessel(index, presetDraft.presetAt(index))
                return
            }
            if (operation === "commit" && page.pendingBackAfterDraft) {
                page.pendingBackAfterDraft = false
                AppShell.dismissRequested()
                return
            }
            if (operation === "update") editVesselPopup.close()
            else if (operation === "remove") editVesselPopup.close()
            else if (operation === "add") {
                addVesselDialog.close()
                const index = presetDraft.presets.length - 1
                if (index >= 0)
                    page.selectVessel(index, presetDraft.presetAt(index))
            }
        }
    }

    function resolveStaleDraftDiscard() {
        if (!presetDraft.reload()) {
            reportActionFailure()
            staleDraftDialog.open()
            return
        }
        const continueBack = pendingBackAfterDraft
        pendingBackAfterDraft = false
        pendingSelectionIndex = -1
        editVesselPopup.close()
        addVesselDialog.close()
        if (continueBack)
            requestBack()
    }

    function cancelStaleDraftResolution() {
        pendingBackAfterDraft = false
        pendingSelectionIndex = -1
    }

    // Use StackView.onActivated (not Component.onCompleted) so side effects
    // run when the page is actually shown, not during construction. This
    // also re-fires on pop-back if the page is ever pushed below another.
    // Skip the preset apply and tare while dispensing so a
    // re-activation mid-session doesn't clobber in-progress state.
    StackView.onActivated: {
        if (!page.isDispensing) {
            page.applySelection()
            // Tare immediately so display shows 0g instead of current scale weight.
            if (!page.isVolumeMode) {
                SteamService.request("tare", {}, SteamService.state.controlContextId)
            }
            volumeInput.forceActiveFocus()
        }
    }

    // Hidden Tr component for page title (used by root.currentPageTitle)
    Tr { id: pageTitleText; key: "hotwater.title"; fallback: "Hot Water"; visible: false }

    property bool isDispensing: MachineState.phase === MachineState.Phase.HotWater || AppShell.debugLiveView
    property int editingVesselIndex: -1

    property bool isVolumeMode: String(presetDraft.values.mode || RecipeContext.preferences.waterVolumeMode) === "volume"
    // The -/+ target buttons need a scale to stop on; without one the pour stops on volume.
    readonly property bool canAdjustTarget: !isVolumeMode && ScaleDevice.connected && !ScaleDevice.isFlowScale
    readonly property real vesselTemperature: Number(presetDraft.values.temperature
        ?? RecipeContext.preferences.waterTemperature ?? 85)

    // Get current vessel's volume
    function getCurrentVesselVolume() {
        var preset = presetDraft.values
        return Number(preset.volume ?? 200)
    }

    function getCurrentVesselName() {
        var preset = presetDraft.values
        return String(preset.name ?? "")
    }

    // Save current vessel with all parameters. Temperature defaults to the
    // current temperature input so callers that only change volume/flow keep it.
    function saveCurrentVessel(volume, flowRate, temperature) {
        presetDraft.setValue("volume", volume)
        presetDraft.setValue("flowRate", flowRate)
        presetDraft.setValue("temperature", temperature !== undefined ? temperature
                            : page.vesselTemperature)
    }

    // Repeater.itemAt() is typed QQuickItem, so reaching the delegate's own `focusTarget`
    // needs the cast to its declared root type. Kept to this one place rather than
    // repeated at the six sites that need it.
    function vesselFocusTarget(i: int): Item {
        // Range-checked as well as null-checked: Repeater.count is the MODEL size and is
        // emitted before the delegates exist (regenerate() returns early until
        // componentComplete(), qquickrepeater.cpp:434-438), so `count > 0` with a null
        // itemAt() is normal while a creation-time binding first evaluates.
        if (i < 0 || i >= vesselRepeater.count) return null
        var it = vesselRepeater.itemAt(i) as RepeaterDelegateItem
        return it ? it.focusTarget : null
    }

    function focusVesselAt(i: int) {
        var target = page.vesselFocusTarget(i)
        if (target)
            target.forceActiveFocus()
    }

    // Selection is applied by the native host; the page reflects its confirmed
    // settings snapshot after the request completes.
    function selectVessel(index, vesselData) {
        // Ignore malformed or out-of-range model rows.
        if (!vesselData || vesselData.volume === undefined)
            return
        if (presetDraft.dirty && !presetDraft.pending) {
            pendingSelectionIndex = index === presetDraft.selectedIndex ? -1 : index
            presetDraft.commit()
            return
        }
        if (pendingPageActionId || presetDraft.pending)
            return
        pendingPageActionId = MachineControlService.selectWaterVessel(index, vesselData)
        pendingPageAction = "select"
    }

    function openEditVessel(index, name) {
        editingVesselIndex = index
        editingExpectedName = String(name)
        editingRevision = Number(RecipeContext.revision)
        editVesselNameInput.text = String(name)
        editVesselPopup.open()
    }

    function openAddVessel() {
        addDialogRevision = Number(RecipeContext.revision)
        addVesselDialog.open()
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
                    model: presetDraft.presets

                    Rectangle {
                        id: liveVesselPill

                        required property int index
                        required property var modelData

                        width: liveVesselText.implicitWidth + 24
                        height: Theme.scaled(36)
                        radius: Theme.scaled(18)
                        color: liveVesselPill.index === presetDraft.selectedIndex ? Theme.primaryColor : Theme.cardBackgroundColor
                        border.color: liveVesselPill.index === presetDraft.selectedIndex ? Theme.primaryColor : Theme.textSecondaryColor
                        border.width: 1

                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: liveVesselPill.modelData.name + " " + TranslationManager.translate("hotwater.accessibility.vessel", "vessel") +
                                         (liveVesselPill.index === presetDraft.selectedIndex ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                        Accessible.focusable: true
                        Accessible.onPressAction: liveVesselArea.clicked(null)

                        Keys.onReturnPressed: function(event) { liveVesselArea.clicked(null); event.accepted = true }
                        Keys.onSpacePressed:  function(event) { liveVesselArea.clicked(null); event.accepted = true }
                        Keys.onLeftPressed: function(event) {
                            if (liveVesselPill.index > 0) liveVesselRepeater.itemAt(liveVesselPill.index - 1).forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        }
                        Keys.onRightPressed: function(event) {
                            if (liveVesselPill.index < liveVesselRepeater.count - 1) liveVesselRepeater.itemAt(liveVesselPill.index + 1).forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        }
                        Keys.onTabPressed: function(event) {
                            page.liveFocusNeighbour(liveVesselPill, true).forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        }
                        Keys.onBacktabPressed: function(event) {
                            page.liveFocusNeighbour(liveVesselPill, false).forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        }

                        Text {
                            id: liveVesselText
                            anchors.centerIn: parent
                            text: liveVesselPill.modelData.name
                            color: liveVesselPill.index === presetDraft.selectedIndex ? Theme.primaryContrastColor : Theme.textColor
                            font: Theme.bodyFont
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: liveVesselArea
                            anchors.fill: parent
                            onClicked: page.selectVessel(liveVesselPill.index, liveVesselPill.modelData)
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

                    // Weight mode: live weight progress, flanked by -10 g / +10 g like the flush timer
                    RowLayout {
                        id: hotWaterProgressRow
                        visible: !page.isVolumeMode
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.spacingMedium

                        LiveAdjustButton {
                            id: hotWaterSubtractButton
                            visible: page.canAdjustTarget
                            text: TranslationManager.translate("hotwater.button.subtract10", "-10 g")
                            accessibleName: TranslationManager.translate("hotwater.accessible.subtract10", "Subtract 10 grams from this pour")
                            KeyNavigation.tab: page.liveFocusNeighbour(hotWaterSubtractButton, true)
                            KeyNavigation.backtab: page.liveFocusNeighbour(hotWaterSubtractButton, false)
                            enabled: !page.pendingPageActionId
                            onClicked: page.adjustTarget(-10)
                        }

                        Text {
                            id: hotWaterProgressText
                            text: Math.max(0, MachineState.scaleWeight).toFixed(0) + "g / " + MachineState.hotWaterTargetWeight.toFixed(0) + "g"
                            color: Theme.textColor
                            font: Theme.timerFont
                        }

                        LiveAdjustButton {
                            id: hotWaterAddButton
                            visible: page.canAdjustTarget
                            text: TranslationManager.translate("hotwater.button.add10", "+10 g")
                            accessibleName: TranslationManager.translate("hotwater.accessible.add10", "Add 10 grams to this pour")
                            KeyNavigation.tab: page.liveFocusNeighbour(hotWaterAddButton, true)
                            KeyNavigation.backtab: page.liveFocusNeighbour(hotWaterAddButton, false)
                            enabled: !page.pendingPageActionId
                            onClicked: page.adjustTarget(10)
                        }
                    }

                    // Volume mode: show target
                    Text {
                        id: hotWaterVolumeText
                        visible: page.isVolumeMode
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: RecipeContext.preferences.waterVolume + " ml"
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
                        width: hotWaterProgressRow.width
                        height: Theme.scaled(8)
                        radius: Theme.scaled(4)
                        color: Theme.surfaceColor

                        Rectangle {
                            width: parent.width * Math.min(1, Math.max(0, MachineState.scaleWeight) / MachineState.hotWaterTargetWeight)
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
                enabled: RecipeContext.ready && MachineControlService.ready
                    && !page.pendingPageActionId
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Theme.scaled(180)
                value: page.liveFlowRateDraft
                from: 5
                to: 100
                stepSize: 5
                displayText: (value / 10).toFixed(1) + " mL/s"
                valueColor: Theme.flowColor
                accessibleName: TranslationManager.translate("hotwater.label.flowRate", "Flow Rate")
                KeyNavigation.tab: page.liveFocusNeighbour(liveFlowRateInput, true)
                KeyNavigation.backtab: page.liveFocusNeighbour(liveFlowRateInput, false)

                // Keep the local control responsive while the native host
                // applies the value once on release.
                onValueModified: function(newValue) {
                    page.liveFlowEditing = true
                    page.liveFlowRateDraft = Math.round(newValue)
                }
                // Submit one host operation after the user releases the control.
                onValueCommitted: function(newValue) {
                    page.liveFlowEditing = false
                    if (page.pendingPageActionId)
                        return
                    page.pendingPageActionId = MachineControlService.setHotWaterFlow(
                                SteamService.state.controlContextId, Math.round(newValue))
                    page.pendingPageAction = "flow"
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
                Keys.onReturnPressed: function(event) { page.requestStop(); event.accepted = true }
                Keys.onSpacePressed:  function(event) { page.requestStop(); event.accepted = true }
                Keys.onTabPressed: function(event) {
                    page.liveFocusNeighbour(hotWaterStopButton, true).forceActiveFocus(Qt.TabFocusReason)
                    event.accepted = true
                }
                Keys.onBacktabPressed: function(event) {
                    page.liveFocusNeighbour(hotWaterStopButton, false).forceActiveFocus(Qt.TabFocusReason)
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
                    onAccessibleClicked: page.requestStop()
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
                            model: presetDraft.presets

                            RepeaterDelegateItem {
                                id: vesselDelegate

                                required property int index
                                required property var modelData

                                width: vesselPill.width
                                height: Theme.scaled(36)

                                itemIndex: vesselDelegate.index
                                focusTarget: vesselPill

                                Rectangle {
                                    id: vesselPill
                                    width: vesselText.implicitWidth + 24
                                    height: Theme.scaled(36)
                                    radius: Theme.scaled(18)
                                    color: vesselDelegate.itemIndex === presetDraft.selectedIndex ? Theme.primaryColor : Theme.insetBackgroundColor
                                    border.color: vesselDelegate.itemIndex === presetDraft.selectedIndex ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: dragArea.drag.active ? 0.8 : 1.0

                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.Button
                                    Accessible.name: vesselDelegate.modelData.name + " " + TranslationManager.translate("hotwater.accessibility.preset", "preset") +
                                                     (vesselDelegate.itemIndex === presetDraft.selectedIndex ?
                                                      ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                                    Accessible.description: TranslationManager.translate("hotwater.accessibility.presetHint", "Double-tap or long-press to rename.")
                                    Accessible.focusable: true
                                    Accessible.onPressAction: {
                                        page.selectVessel(vesselDelegate.itemIndex, vesselDelegate.modelData)
                                    }

                                    Keys.onReturnPressed: function(event) {
                                        page.selectVessel(vesselDelegate.itemIndex, vesselDelegate.modelData)
                                        event.accepted = true
                                    }
                                    Keys.onSpacePressed: function(event) {
                                        page.selectVessel(vesselDelegate.itemIndex, vesselDelegate.modelData)
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
                                            addVesselButton.forceActiveFocus(Qt.TabFocusReason)
                                        event.accepted = true
                                    }
                                    Keys.onBacktabPressed: function(event) {
                                        if (vesselDelegate.index > 0)
                                            page.focusVesselAt(vesselDelegate.index - 1)
                                        else
                                            flowRateInput.forceActiveFocus(Qt.TabFocusReason)
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
                                        color: vesselDelegate.itemIndex === presetDraft.selectedIndex ? Theme.primaryContrastColor : Theme.textColor
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
                                                page.selectVessel(vesselDelegate.itemIndex, vesselDelegate.modelData)
                                            }
                                            vesselPill.Drag.drop()
                                            vesselPresetsRow.draggedIndex = -1
                                        }

                                        onPositionChanged: {
                                            if (dragArea.drag.active) {
                                                dragArea.moved = true
                                                vesselPresetsRow.draggedIndex = vesselDelegate.itemIndex
                                            }
                                        }

                                        onDoubleClicked: {
                                            holdTimer.stop()
                                            dragArea.held = true  // Prevent single-click selection on release
                                            page.openEditVessel(vesselDelegate.itemIndex, vesselDelegate.modelData.name)
                                        }

                                        Timer {
                                            id: holdTimer
                                            interval: 500
                                            onTriggered: {
                                                if (!dragArea.moved) {
                                                    dragArea.held = true
                                                    page.openEditVessel(vesselDelegate.itemIndex, vesselDelegate.modelData.name)
                                                }
                                            }
                                        }
                                    }
                                }

                                DropArea {
                                    anchors.fill: parent
                                    onEntered: function(drag) {
                                        // Guarded like the DelegateModel drop targets in
                                        // FavoritesListView / LayoutEditorZone. `itemIndex` is a
                                        // SHARED property now, so a drag from an unrelated
                                        // reorderable list would answer with a plausible integer
                                        // instead of undefined and silently reorder this list.
                                        var src = drag.source as RepeaterDelegateItem
                                        if (!src || src === vesselDelegate) return
                                        var fromIndex = src.itemIndex
                                        var toIndex = vesselDelegate.itemIndex
                                        if (fromIndex !== toIndex) {
                                            presetDraft.move(fromIndex, toIndex)
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
                            Keys.onReturnPressed: function(event) { page.openAddVessel(); event.accepted = true }
                            Keys.onSpacePressed:  function(event) { page.openAddVessel(); event.accepted = true }

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
                                onAccessibleClicked: page.openAddVessel()
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
                                        presetDraft.setValue("mode", "weight")
                                        page.saveCurrentVessel(volumeInput.value, flowRateInput.value)
                                        presetDraft.commit()
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
                                        presetDraft.setValue("mode", "volume")
                                        // Clamp value to 255ml max for volume mode
                                        if (volumeInput.value > 255) {
                                            page.saveCurrentVessel(255, flowRateInput.value)
                                        } else {
                                            page.saveCurrentVessel(volumeInput.value, flowRateInput.value)
                                        }
                                        presetDraft.commit()
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
                                page.saveCurrentVessel(newValue, flowRateInput.value)
                            }
                            onValueCommitted: presetDraft.commit()
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
                            value: Theme.cToDisplay(page.vesselTemperature)
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
                                page.saveCurrentVessel(volumeInput.value, flowRateInput.value, celsius)
                            }
                            onValueCommitted: presetDraft.commit()
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
                            value: Number(presetDraft.values.flowRate ?? 40)
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
                                page.saveCurrentVessel(volumeInput.value, Math.round(newValue))
                            }
                            onValueCommitted: presetDraft.commit()
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
        onBackClicked: page.requestBack()

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
    DecenzaDialog {
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
            target: Keyboard
            function onVisibleChanged() {
                if (Keyboard.visible && editVesselPopup.opened) {
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
                    enabled: RecipeContext.ready && !presetDraft.pending
                    KeyNavigation.tab: cancelEditVesselButton
                    KeyNavigation.backtab: editVesselNameInput
                    onClicked: {
                        presetDraft.remove(page.editingVesselIndex, page.editingExpectedName, page.editingRevision)
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
                        && RecipeContext.ready && !presetDraft.pending
                        && !presetDraft.nameTaken(editVesselNameInput.text, page.editingVesselIndex)
                    onClicked: {
                        Keyboard.commit()
                        if (editVesselNameInput.text.trim().length === 0
                                || presetDraft.nameTaken(editVesselNameInput.text, page.editingVesselIndex))
                            return
                        presetDraft.updateName(page.editingVesselIndex, editVesselNameInput.text.trim(), page.editingExpectedName, page.editingRevision)
                    }
                }
            }
        }
    }

    // Add vessel dialog
    DecenzaDialog {
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
            target: Keyboard
            function onVisibleChanged() {
                if (Keyboard.visible && addVesselDialog.opened) {
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
                    && presetDraft.nameTaken(newVesselNameInput.text, -1)
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
                    enabled: RecipeContext.ready && !presetDraft.pending
                        && newVesselNameInput.text.trim().length > 0
                        && !presetDraft.nameTaken(newVesselNameInput.text, -1)
                    KeyNavigation.tab: newVesselNameInput
                    KeyNavigation.backtab: cancelAddVesselButton
                    onClicked: {
                        Keyboard.commit()
                        if (newVesselNameInput.text.length > 0) {
                            presetDraft.add(newVesselNameInput.text.trim(), {
                                volume: 200, mode: "weight", flowRate: 40,
                                temperature: Number(RecipeContext.preferences.waterTemperature)
                            }, page.addDialogRevision)
                        }
                    }
                }
            }
        }
    }

    UnsavedChangesDialog {
        id: staleDraftDialog
        itemType: "water vessel preset"
        titleText: TranslationManager.translate("presetDraft.staleTitle", "Settings changed")
        messageText: TranslationManager.translate(
            "presetDraft.staleMessage",
            "These presets changed elsewhere. Discard your draft and reload the latest settings?")
        canDiscard: RecipeContext.ready
        showSave: false
        showSaveAs: false
        showCancel: true
        onLeaveCancelled: AppShell.browserBackCancelled()
        onDiscardClicked: page.resolveStaleDraftDiscard()
        onCancelClicked: page.cancelStaleDraftResolution()
    }
}
