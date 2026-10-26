// Delegates below declare their model roles with `required property`; the ids in this file
// (livePresetRepeater, flowInput, presetsRow, ...) then resolve statically inside them.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

T.Page {
    id: flushPage

    objectName: "flushPage"
    background: ThemedPageBackground {}

    SharedPresetDraft { id: presetDraft; domain: "flush" }
    readonly property bool browserHasUnsavedChanges: presetDraft.dirty
    StatusToast { id: actionToast; error: true }
    property string pendingPageActionId: ""
    property string pendingPageAction: ""
    property string editingExpectedName: ""
    property double editingRevision: 0
    property double addDialogRevision: 0
    property int pendingSelectionIndex: -1
    property bool pendingBackAfterDraft: false

    function reportActionFailure() {
        actionToast.show(TranslationManager.translate(
            "flush.action.failed", "Couldn't update flush settings. Please try again."))
    }

    function applySelection() {
        if (pendingPageActionId)
            return
        if (presetDraft.selectedIndex >= 0) {
            pendingPageActionId = MachineControlService.selectFlushPreset(
                        presetDraft.selectedIndex, presetDraft.selectedPreset)
        } else {
            pendingPageActionId = MachineControlService.applyFlushSettings(
                        SteamService.state.controlContextId)
        }
        pendingPageAction = "apply"
    }

    function requestBack() {
        if (pendingPageActionId || presetDraft.pending)
            return
        const stopping = isFlushing
        if (AppShell.debugLiveView && MachineState.phase !== MachineState.Phase.Flushing) {
            AppShell.dismissRequested()
            return
        }
        if (!stopping && presetDraft.dirty) {
            if (presetDraft.stale) {
                pendingBackAfterDraft = true
                staleDraftDialog.open()
                return
            }
            pendingBackAfterDraft = true
            presetDraft.commit()
            return
        }
        if (stopping)
            AppShell.userExitedFlush = true
        pendingPageActionId = stopping
                ? MachineControlService.stopFlush(SteamService.state.controlContextId)
                : MachineControlService.applyFlushSettings(SteamService.state.controlContextId)
        pendingPageAction = stopping ? "stop" : "back"
    }

    function adjustTime(delta) {
        if (pendingPageActionId || !isFlushing)
            return
        pendingPageActionId = MachineControlService.bumpFlushSeconds(
                    SteamService.state.controlContextId, delta)
        pendingPageAction = "adjust-time"
    }

    function requestStop() {
        if (pendingPageActionId)
            return
        if (AppShell.debugLiveView && MachineState.phase !== MachineState.Phase.Flushing) {
            AppShell.dismissRequested()
            return
        }
        AppShell.userExitedFlush = true
        pendingPageActionId = MachineControlService.stopFlush(
                    SteamService.state.controlContextId)
        pendingPageAction = "stop"
    }

    Connections {
        target: MachineControlService
        function onCompleted(requestId, operation, success, message, result) {
            if (requestId !== flushPage.pendingPageActionId)
                return
            const action = flushPage.pendingPageAction
            flushPage.pendingPageActionId = ""
            flushPage.pendingPageAction = ""
            if (!success) {
                if (action === "back" || action === "stop")
                    AppShell.browserBackCancelled()
                if (action === "stop") AppShell.userExitedFlush = false
                flushPage.reportActionFailure()
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
                    flushPage.pendingSelectionIndex = -1
                    staleDraftDialog.open()
                    return
                }
                flushPage.pendingSelectionIndex = -1
                if (flushPage.pendingBackAfterDraft)
                    AppShell.browserBackCancelled()
                flushPage.pendingBackAfterDraft = false
                flushPage.reportActionFailure()
                return
            }
            if (operation === "commit" && flushPage.pendingSelectionIndex >= 0) {
                const index = flushPage.pendingSelectionIndex
                flushPage.pendingSelectionIndex = -1
                flushPage.selectPreset(index)
                return
            }
            if (operation === "commit" && flushPage.pendingBackAfterDraft) {
                flushPage.pendingBackAfterDraft = false
                AppShell.dismissRequested()
                return
            }
            if (operation === "update" || operation === "remove")
                editPresetPopup.close()
            else if (operation === "add")
                addPresetDialog.close()
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
        editPresetPopup.close()
        addPresetDialog.close()
        if (continueBack)
            requestBack()
    }

    function cancelStaleDraftResolution() {
        pendingBackAfterDraft = false
        pendingSelectionIndex = -1
    }

    property string pageTitle: TranslationManager.translate("flush.title", "Flush")

    // Use StackView.onActivated (not Component.onCompleted) so side effects
    // run when the page is actually shown, not during construction. This
    // also re-fires on pop-back if the page is ever pushed below another.
    // Skip the preset-reset and settings push while flushing so a
    // re-activation mid-session doesn't clobber in-progress state.
    StackView.onActivated: {
        if (!isFlushing) {
            applySelection()
            secondsInput.forceActiveFocus()
        }
    }

    property bool isFlushing: MachineState.phase === MachineState.Phase.Flushing || AppShell.debugLiveView
    property int editingPresetIndex: -1

    onIsFlushingChanged: WebDebugLogger.debug("App", "FlushPage",
                                              isFlushing ? "Showing the flushing view" : "Showing the flush settings view")

    // Get current preset values
    // Repeater.itemAt() is typed QQuickItem, so reaching the delegate's own `focusTarget`
    // needs the cast to its declared root type. Guarded and kept to one place, matching
    // SteamPage.pitcherFocusTarget() / HotWaterPage.vesselFocusTarget().
    //
    // The null check is not defensive padding: Repeater.count is the MODEL size and is
    // emitted before the delegates exist — regenerate() returns early until
    // componentComplete() (qquickrepeater.cpp:434-438) — so `count > 0` with a null
    // itemAt() is the normal state while a creation-time KeyNavigation binding first
    // evaluates. Dereferencing through the cast there threw on every page open.
    function presetFocusTarget(i: int): Item {
        if (i < 0 || i >= presetRepeater.count) return null
        var it = presetRepeater.itemAt(i) as RepeaterDelegateItem
        return it ? it.focusTarget : null
    }

    function focusPresetAt(i: int): void {
        var target = flushPage.presetFocusTarget(i)
        if (target)
            target.forceActiveFocus()
    }

    function getCurrentPresetFlow() {
        var preset = presetDraft.values
        return Number(preset.flow ?? 6.0)
    }

    function getCurrentPresetSeconds() {
        var preset = presetDraft.values
        return Number(preset.seconds ?? 5.0)
    }

    function getCurrentPresetName() {
        var preset = presetDraft.values
        return String(preset.name || "")
    }

    // Save current preset with new values
    function saveCurrentPreset(flow, seconds) {
        presetDraft.setValue("flow", flow)
        presetDraft.setValue("seconds", seconds)
    }

    function selectPreset(index) {
        const preset = presetDraft.presetAt(index)
        if (!preset || !preset.name)
            return
        if (presetDraft.dirty && !presetDraft.pending) {
            pendingSelectionIndex = index === presetDraft.selectedIndex ? -1 : index
            presetDraft.commit()
            return
        }
        if (pendingPageActionId || presetDraft.pending)
            return
        pendingPageActionId = MachineControlService.selectFlushPreset(index, preset)
        pendingPageAction = "select"
    }

    function openEditPreset(index, name) {
        editingPresetIndex = index
        editingExpectedName = String(name)
        editingRevision = Number(RecipeContext.revision)
        editPresetNameInput.text = String(name)
        editPresetPopup.open()
    }

    function openAddPreset() {
        addDialogRevision = Number(RecipeContext.revision)
        addPresetDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.pageTopMargin  // Space for bottom bar
        spacing: Theme.scaled(15)

        // === FLUSHING VIEW ===
        ColumnLayout {
            visible: flushPage.isFlushing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(20)

            // Preset pills for quick switching during flushing
            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(8)

                Repeater {
                    id: livePresetRepeater
                    model: presetDraft.presets

                    Rectangle {
                        id: livePresetDelegate

                        required property var modelData
                        required property int index

                        width: livePresetText.implicitWidth + 24
                        height: Theme.scaled(36)
                        radius: Theme.scaled(18)
                        color: index === presetDraft.selectedIndex ? Theme.primaryColor : Theme.cardBackgroundColor
                        border.color: index === presetDraft.selectedIndex ? Theme.primaryColor : Theme.textSecondaryColor
                        border.width: 1

                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.name + " " + TranslationManager.translate("flush.accessibility.preset", "preset") +
                                         (index === presetDraft.selectedIndex ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                        Accessible.focusable: true
                        Accessible.onPressAction: livePresetArea.clicked(null)

                        Keys.onReturnPressed: function(event) { livePresetArea.clicked(null); event.accepted = true }
                        Keys.onSpacePressed: function(event) { livePresetArea.clicked(null); event.accepted = true }
                        Keys.onLeftPressed: function(event) {
                            if (index > 0) livePresetRepeater.itemAt(index - 1).forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        }
                        Keys.onRightPressed: function(event) {
                            if (index < livePresetRepeater.count - 1) livePresetRepeater.itemAt(index + 1).forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        }
                        Keys.onTabPressed: function(event) {
                            if (index < livePresetRepeater.count - 1)
                                livePresetRepeater.itemAt(index + 1).forceActiveFocus(Qt.TabFocusReason)
                            else
                                flushSubtractFiveButton.forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        }
                        Keys.onBacktabPressed: function(event) {
                            if (index > 0)
                                livePresetRepeater.itemAt(index - 1).forceActiveFocus(Qt.TabFocusReason)
                            else if (flushStopButton.visible)
                                flushStopButton.forceActiveFocus(Qt.TabFocusReason)
                            else
                                flushAddFiveButton.forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        }

                        Text {
                            id: livePresetText
                            anchors.centerIn: parent
                            text: livePresetDelegate.modelData.name
                            color: livePresetDelegate.index === presetDraft.selectedIndex ? Theme.primaryContrastColor : Theme.textColor
                            font: Theme.bodyFont
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: livePresetArea
                            anchors.fill: parent
                            onClicked: {
                                flushPage.selectPreset(livePresetDelegate.index)
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // Timer with progress bar, flanked by -5 s / +5 s like the steam timer
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.scaled(8)

                    RowLayout {
                        id: flushTimerRow
                        spacing: Theme.spacingMedium

                        LiveAdjustButton {
                            id: flushSubtractFiveButton
                            text: TranslationManager.translate("flush.button.subtract5", "-5 s")
                            accessibleName: TranslationManager.translate("flush.accessible.subtract5", "Subtract 5 seconds from flush time")
                            KeyNavigation.tab: flushAddFiveButton
                            KeyNavigation.backtab: livePresetRepeater.count > 0
                                                   ? livePresetRepeater.itemAt(livePresetRepeater.count - 1)
                                                   : flushAddFiveButton
                            enabled: !flushPage.pendingPageActionId
                            onClicked: flushPage.adjustTime(-5)
                        }

                        Text {
                            id: flushProgressText
                            text: MachineState.shotTime.toFixed(1) + "s / " + Number(RecipeContext.preferences.flushSeconds ?? 0).toFixed(0) + "s"
                            color: Theme.textColor
                            font: Theme.timerFont
                        }

                        LiveAdjustButton {
                            id: flushAddFiveButton
                            text: TranslationManager.translate("flush.button.add5", "+5 s")
                            accessibleName: TranslationManager.translate("flush.accessible.add5", "Add 5 seconds to flush time")
                            KeyNavigation.tab: flushStopButton.visible
                                               ? flushStopButton
                                               : (livePresetRepeater.count > 0 ? livePresetRepeater.itemAt(0) : flushSubtractFiveButton)
                            KeyNavigation.backtab: flushSubtractFiveButton
                            enabled: !flushPage.pendingPageActionId
                            onClicked: flushPage.adjustTime(5)
                        }
                    }

                    // Progress bar
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: flushTimerRow.width
                        height: Theme.scaled(8)
                        radius: Theme.scaled(4)
                        color: Theme.surfaceColor

                        Rectangle {
                            width: parent.width * Math.min(1, MachineState.shotTime / RecipeContext.preferences.flushSeconds)
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
                Keys.onReturnPressed: function(event) { flushPage.requestStop(); event.accepted = true }
                Keys.onSpacePressed: function(event) { flushPage.requestStop(); event.accepted = true }
                Keys.onTabPressed: function(event) {
                    if (livePresetRepeater.count > 0) livePresetRepeater.itemAt(0).forceActiveFocus(Qt.TabFocusReason)
                    event.accepted = true
                }
                Keys.onBacktabPressed: function(event) {
                    flushAddFiveButton.forceActiveFocus(Qt.TabFocusReason)
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
                    onAccessibleClicked: flushPage.requestStop()
                }
            }

            Item { Layout.preferredHeight: Theme.scaled(20) }
        }

        // === SETTINGS VIEW ===
        ColumnLayout {
            visible: !flushPage.isFlushing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(12)

            // Presets Section
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
                            model: presetDraft.presets

                            RepeaterDelegateItem {
                                id: presetDelegate

                                required property var modelData
                                required property int index
                                width: presetPill.width
                                height: Theme.scaled(36)

                                itemIndex: presetDelegate.index
                                focusTarget: presetPill

                                Rectangle {
                                    id: presetPill
                                    width: presetText.implicitWidth + 24
                                    height: Theme.scaled(36)
                                    radius: Theme.scaled(18)
                                    color: presetDelegate.itemIndex === presetDraft.selectedIndex ? Theme.primaryColor : Theme.insetBackgroundColor
                                    border.color: presetDelegate.itemIndex === presetDraft.selectedIndex ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: dragArea.drag.active ? 0.8 : 1.0

                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.Button
                                    Accessible.name: presetDelegate.modelData.name + " " + TranslationManager.translate("flush.accessibility.preset", "preset") +
                                                     (presetDelegate.itemIndex === presetDraft.selectedIndex ?
                                                      ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                                    Accessible.description: TranslationManager.translate("flush.accessibility.presetHint", "Double-tap or long-press to rename.")
                                    Accessible.focusable: true
                                    Accessible.onPressAction: {
                                        flushPage.selectPreset(presetDelegate.itemIndex)
                                    }

                                    Keys.onReturnPressed: function(event) {
                                        flushPage.selectPreset(presetDelegate.itemIndex)
                                        event.accepted = true
                                    }
                                    Keys.onSpacePressed: function(event) {
                                        flushPage.selectPreset(presetDelegate.itemIndex)
                                        event.accepted = true
                                    }
                                    Keys.onLeftPressed: function(event) {
                                        if (presetDelegate.index > 0) flushPage.focusPresetAt(presetDelegate.index - 1)
                                        event.accepted = true
                                    }
                                    Keys.onRightPressed: function(event) {
                                        if (presetDelegate.index < presetRepeater.count - 1) flushPage.focusPresetAt(presetDelegate.index + 1)
                                        event.accepted = true
                                    }
                                    Keys.onTabPressed: function(event) {
                                        if (presetDelegate.index < presetRepeater.count - 1)
                                            flushPage.focusPresetAt(presetDelegate.index + 1)
                                        else
                                            addPresetButton.forceActiveFocus(Qt.TabFocusReason)
                                        event.accepted = true
                                    }
                                    Keys.onBacktabPressed: function(event) {
                                        if (presetDelegate.index > 0)
                                            flushPage.focusPresetAt(presetDelegate.index - 1)
                                        else
                                            flowInput.forceActiveFocus(Qt.TabFocusReason)
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
                                        text: presetDelegate.modelData.name
                                        color: presetDelegate.itemIndex === presetDraft.selectedIndex ? Theme.primaryContrastColor : Theme.textColor
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
                                                flushPage.selectPreset(presetDelegate.itemIndex)
                                            }
                                            presetPill.Drag.drop()
                                            presetsRow.draggedIndex = -1
                                        }

                                        onPositionChanged: {
                                            if (drag.active) {
                                                moved = true
                                                presetsRow.draggedIndex = presetDelegate.itemIndex
                                            }
                                        }

                                        onDoubleClicked: {
                                            holdTimer.stop()
                                            held = true  // Prevent single-click selection on release
                                            flushPage.openEditPreset(presetDelegate.itemIndex, presetDelegate.modelData.name)
                                        }

                                        Timer {
                                            id: holdTimer
                                            interval: 500
                                            onTriggered: {
                                                if (!dragArea.moved) {
                                                    dragArea.held = true
                                                    flushPage.openEditPreset(presetDelegate.itemIndex, presetDelegate.modelData.name)
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
                                        if (!src || src === presetDelegate) return
                                        var fromIndex = src.itemIndex
                                        var toIndex = presetDelegate.itemIndex
                                        if (fromIndex !== toIndex) {
                                            presetDraft.move(fromIndex, toIndex)
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
                            KeyNavigation.backtab: flushPage.presetFocusTarget(presetRepeater.count - 1)
                                                   || secondsInput
                            Keys.onReturnPressed: function(event) { flushPage.openAddPreset(); event.accepted = true }
                            Keys.onSpacePressed: function(event) { flushPage.openAddPreset(); event.accepted = true }

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
                                onAccessibleClicked: flushPage.openAddPreset()
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
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(16)
                    spacing: Theme.scaled(8)

                    // Duration (per-preset; the host commits once on release)
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
                            value: flushPage.getCurrentPresetSeconds()
                            from: 1
                            to: 45
                            stepSize: 0.5
                            suffix: " s"
                            valueColor: Theme.primaryColor
                            accessibleName: TranslationManager.translate("flush.label.duration", "Duration")
                            KeyNavigation.tab: flowInput
                            KeyNavigation.backtab: addPresetButton

                            // Keep each tick in the local draft; commit the row once on release.
                            onValueModified: function(newValue) {
                                flushPage.saveCurrentPreset(flowInput.value, newValue)
                            }
                            onValueCommitted: presetDraft.commit()
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.textSecondaryColor; opacity: 0.3 }

                    // Flow Rate (per-preset; the host commits once on release)
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
                            value: flushPage.getCurrentPresetFlow()
                            from: 2
                            to: 10
                            stepSize: 0.5
                            suffix: " mL/s"
                            valueColor: Theme.flowColor
                            accessibleName: TranslationManager.translate("flush.label.flowRate", "Flow Rate")
                            KeyNavigation.tab: flushPage.presetFocusTarget(0) || addPresetButton
                            KeyNavigation.backtab: secondsInput

                            // Keep each tick in the local draft; commit the row once on release.
                            onValueModified: function(newValue) {
                                flushPage.saveCurrentPreset(newValue, secondsInput.value)
                            }
                            onValueCommitted: presetDraft.commit()
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }

    // Always visible: back stays reachable during active flush. Chips collapse
    // during flush (live timer above shows state); back stops + suppresses overlay.
    BottomBar {
        title: flushPage.getCurrentPresetName() || flushPage.pageTitle
        onBackClicked: flushPage.requestBack()

        Text {
            visible: !flushPage.isFlushing
            text: secondsInput.value.toFixed(1) + "s"
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
            Accessible.ignored: true
        }
        Rectangle {
            visible: !flushPage.isFlushing
            width: 1; height: Theme.scaled(30); color: Theme.primaryContrastColor; opacity: 0.3
        }
        Text {
            visible: !flushPage.isFlushing
            text: flowInput.value.toFixed(1) + " mL/s"
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
            Accessible.ignored: true
        }
    }


    // Edit preset popup
    DecenzaDialog {
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
            target: Keyboard
            function onVisibleChanged() {
                if (Keyboard.visible && editPresetPopup.opened) {
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
                    enabled: RecipeContext.ready && !presetDraft.pending
                    KeyNavigation.tab: cancelEditButton
                    KeyNavigation.backtab: editPresetNameInput
                    onClicked: {
                        presetDraft.remove(flushPage.editingPresetIndex, flushPage.editingExpectedName, flushPage.editingRevision)
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
                    enabled: RecipeContext.ready && !presetDraft.pending
                    KeyNavigation.tab: editPresetNameInput
                    KeyNavigation.backtab: cancelEditButton
                    onClicked: {
                        Keyboard.commit()
                        presetDraft.updateName(flushPage.editingPresetIndex, editPresetNameInput.text.trim(), flushPage.editingExpectedName, flushPage.editingRevision)
                    }
                }
            }
        }
    }

    // Add preset dialog
    DecenzaDialog {
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
            target: Keyboard
            function onVisibleChanged() {
                if (Keyboard.visible && addPresetDialog.opened) {
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
                    enabled: RecipeContext.ready && !presetDraft.pending
                    KeyNavigation.tab: newPresetNameInput
                    KeyNavigation.backtab: cancelAddButton
                    onClicked: {
                        Keyboard.commit()
                        if (newPresetNameInput.text.length > 0) {
                            presetDraft.add(newPresetNameInput.text.trim(), {flow: 6.0, seconds: 5.0}, flushPage.addDialogRevision)
                        }
                    }
                }
            }
        }
    }

    UnsavedChangesDialog {
        id: staleDraftDialog
        itemType: "flush preset"
        titleText: TranslationManager.translate("presetDraft.staleTitle", "Settings changed")
        messageText: TranslationManager.translate(
            "presetDraft.staleMessage",
            "These presets changed elsewhere. Discard your draft and reload the latest settings?")
        canDiscard: RecipeContext.ready
        showSave: false
        showSaveAs: false
        showCancel: true
        onLeaveCancelled: AppShell.browserBackCancelled()
        onDiscardClicked: flushPage.resolveStaleDraftDiscard()
        onCancelClicked: flushPage.cancelStaleDraftResolution()
    }
}
