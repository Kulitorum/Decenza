import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"
import "../components/DateUtils.js" as DateUtils

Page {
    id: shotMetadataPage
    objectName: "shotMetadataPage"
    background: Rectangle { color: Theme.backgroundColor }

    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("beaninfo.title", "Beans")

    property bool hasPendingShot: false  // Set to true by goToShotMetadata() after a shot
    property int editShotId: 0  // Set > 0 to edit existing shot from history
    property var editShotData: ({})  // Loaded shot data when editing
    property bool isEditMode: editShotId > 0
    property Item focusedField: null
    // Incremented when async distinct cache refreshes; referenced in suggestion bindings
    // to force QML re-evaluation (the >= 0 condition is always true by design)
    property int _distinctCacheVersion: 0
    property string _pendingBeanAutoFill: ""  // Brand awaiting async bean-type fetch for auto-fill

    Connections {
        target: MainController.shotHistory
        function onDistinctCacheReady() {
            _distinctCacheVersion++
            if (_pendingBeanAutoFill.length > 0) {
                var types = MainController.shotHistory.getDistinctBeanTypesForBrand(_pendingBeanAutoFill)
                if (types.length > 0) {
                    _pendingBeanAutoFill = ""
                    if (types.length === 1) {
                        if (isEditMode) editBeanType = types[0]; else Settings.dyeBeanType = types[0];
                    }
                }
                // else: cache miss still pending, keep _pendingBeanAutoFill for next signal
            }
        }
        function onShotReady(shotId, shot) {
            if (shotId !== shotMetadataPage.editShotId) return
            editShotData = shot
            if (editShotData.id) {
                editBeanBrand = editShotData.beanBrand || ""
                editBeanType = editShotData.beanType || ""
                editRoastDate = DateUtils.normalizeDateString(editShotData.roastDate || "")
                editRoastLevel = editShotData.roastLevel || ""
                editGrinderBrand = editShotData.grinderBrand || ""
                editGrinderModel = editShotData.grinderModel || ""
                editGrinderBurrs = editShotData.grinderBurrs || ""
                editGrinderSetting = editShotData.grinderSetting || ""
                editBarista = editShotData.barista || ""
                editDoseWeight = editShotData.doseWeight ?? 0
                editDrinkWeight = editShotData.finalWeight ?? 0
                editDrinkTds = editShotData.drinkTds ?? 0
                editDrinkEy = editShotData.drinkEy ?? 0
                editEnjoyment = editShotData.enjoyment ?? 0
                editNotes = editShotData.espressoNotes || ""
            }
        }
    }

    // Snapshot of DYE values at page open (for Discard in unsaved-changes dialog)
    property string _snapBrand
    property string _snapType
    property string _snapRoastDate
    property string _snapRoastLevel
    property string _snapGrinderBrand
    property string _snapGrinderModel
    property string _snapGrinderBurrs
    property string _snapGrinderSetting
    property string _snapBarista
    property int _snapSelectedPreset: -1

    // Preset dialog properties
    property int editPresetIndex: -1
    property string editPresetName: ""

    // Pending preset switch (set when user taps a preset while dirty, -1 = none)
    property int _pendingPresetIndex: -1

    // Keyboard offset for popups - set when popup opens, reset when it closes
    property real popupKeyboardOffset: 0

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("beaninfo.title", "Beans")

        // Snapshot current DYE values BEFORE auto-match so Discard restores the true pre-page state
        if (!isEditMode) {
            // Silently fix roast date if stored in wrong format (e.g. m/d/yyyy from old data)
            var normalizedDate = DateUtils.normalizeDateString(Settings.dyeRoastDate)
            if (normalizedDate !== Settings.dyeRoastDate) Settings.dyeRoastDate = normalizedDate

            _snapBrand = Settings.dyeBeanBrand
            _snapType = Settings.dyeBeanType
            _snapRoastDate = Settings.dyeRoastDate
            _snapRoastLevel = Settings.dyeRoastLevel
            _snapGrinderBrand = Settings.dyeGrinderBrand
            _snapGrinderModel = Settings.dyeGrinderModel
            _snapGrinderBurrs = Settings.dyeGrinderBurrs
            _snapGrinderSetting = Settings.dyeGrinderSetting
            _snapBarista = Settings.dyeBarista
            _snapSelectedPreset = Settings.selectedBeanPreset
        }

        if (editShotId > 0) {
            loadShotForEditing()
        } else if (Settings.selectedBeanPreset === -1 && hasGuestBeanData()) {
            // Check if current bean data matches an existing preset
            var matchIndex = Settings.findBeanPresetByContent(Settings.dyeBeanBrand, Settings.dyeBeanType)
            if (matchIndex >= 0) {
                // Auto-select the matching preset instead of showing the dialog
                Settings.selectedBeanPreset = matchIndex
                _snapSelectedPreset = matchIndex
            } else {
                guestBeanDialog.open()
            }
        }
    }

    // Deselect preset when user edits any DYE field — presets are immutable favorites
    function deselectPresetOnEdit() {
        if (!isEditMode && Settings.selectedBeanPreset >= 0) {
            Settings.selectedBeanPreset = -1
        }
    }

    // Re-capture the unsaved-changes baseline from current Settings. Called after
    // "choose" actions (selecting a preset, saving a preset) that alter Settings but
    // aren't user-typed edits the back-button should prompt about.
    function refreshSnapshot() {
        _snapBrand = Settings.dyeBeanBrand
        _snapType = Settings.dyeBeanType
        _snapRoastDate = Settings.dyeRoastDate
        _snapRoastLevel = Settings.dyeRoastLevel
        _snapGrinderBrand = Settings.dyeGrinderBrand
        _snapGrinderModel = Settings.dyeGrinderModel
        _snapGrinderBurrs = Settings.dyeGrinderBurrs
        _snapGrinderSetting = Settings.dyeGrinderSetting
        _snapBarista = Settings.dyeBarista
        _snapSelectedPreset = Settings.selectedBeanPreset
    }

    // Check if there's actual bean data loaded (not just empty)
    function hasGuestBeanData() {
        return Settings.dyeBeanBrand.length > 0 || Settings.dyeBeanType.length > 0
    }

    // Persisted graph height (like ShotComparisonPage)
    property real graphHeight: Settings.value("shotMetadata/graphHeight", Theme.scaled(200))

    // Load shot data for editing (async)
    function loadShotForEditing() {
        if (editShotId <= 0) return
        MainController.shotHistory.requestShot(editShotId)
    }

    // Editing fields (separate from Settings.dye* to avoid polluting current session)
    property string editBeanBrand: ""
    property string editBeanType: ""
    property string editRoastDate: ""
    property string editRoastLevel: ""
    property string editGrinderBrand: ""
    property string editGrinderModel: ""
    property string editGrinderBurrs: ""
    property string editGrinderSetting: ""
    property string editBarista: ""
    property double editDoseWeight: 0
    property double editDrinkWeight: 0
    property double editDrinkTds: 0
    property double editDrinkEy: 0
    property int editEnjoyment: 0  // 0 = unrated
    property string editNotes: ""

    // Save edited shot back to history
    function saveEditedShot() {
        if (editShotId <= 0) return
        var metadata = {
            "beanBrand": editBeanBrand,
            "beanType": editBeanType,
            "roastDate": editRoastDate,
            "roastLevel": editRoastLevel,
            "grinderBrand": editGrinderBrand,
            "grinderModel": editGrinderModel,
            "grinderBurrs": editGrinderBurrs,
            "grinderSetting": editGrinderSetting,
            "barista": editBarista,
            "doseWeight": editDoseWeight,
            "finalWeight": editDrinkWeight,
            "drinkTds": editDrinkTds,
            "drinkEy": editDrinkEy,
            "enjoyment": editEnjoyment,
            "espressoNotes": editNotes
        }
        MainController.shotHistory.requestUpdateShotMetadata(editShotId, metadata)
        root.goBack()
    }

    function scrollToFocusedField() {
        if (!focusedField) return
        // Get field center position in content coordinates
        let fieldY = focusedField.mapToItem(flickable.contentItem, 0, 0).y
        let fieldCenter = fieldY + focusedField.height / 2
        // Get keyboard height (real or estimated)
        let kbHeight = Qt.inputMethod.keyboardRectangle.height / Screen.devicePixelRatio
        if (kbHeight <= 0) kbHeight = shotMetadataPage.height * 0.5
        // Calculate center of visible area above keyboard
        let visibleHeight = shotMetadataPage.height - kbHeight - Theme.pageTopMargin
        let visibleCenter = visibleHeight / 2
        // Scroll so field center is at center of visible area
        let targetY = fieldCenter - visibleCenter
        // Clamp to valid scroll range
        let maxScroll = flickable.contentHeight - flickable.height
        flickable.contentY = Math.max(0, Math.min(targetY, maxScroll))
    }

    function hideKeyboard() {
        Qt.inputMethod.hide()
        flickable.contentY = 0
        flickable.forceActiveFocus()
    }

    function applyPreset(index) {
        Settings.selectedBeanPreset = index
        Settings.applyBeanPreset(index)
        refreshSnapshot()
    }

    // Update the currently-selected preset's data with the current DYE field values.
    // Used by "Save" (not "Save As") when a preset was selected before editing.
    function saveToCurrentPreset(pendingPresetIndex, goBackAfter) {
        var idx = _snapSelectedPreset
        if (idx < 0) return
        var preset = Settings.getBeanPreset(idx)
        Settings.updateBeanPreset(idx,
            preset.name,
            Settings.dyeBeanBrand,
            Settings.dyeBeanType,
            Settings.dyeRoastDate,
            Settings.dyeRoastLevel,
            Settings.dyeGrinderBrand,
            Settings.dyeGrinderModel,
            Settings.dyeGrinderBurrs,
            Settings.dyeGrinderSetting,
            Settings.dyeBarista)
        Settings.selectedBeanPreset = idx
        refreshSnapshot()
        if (pendingPresetIndex >= 0) {
            applyPreset(pendingPresetIndex)
        } else if (goBackAfter) {
            root.goBack()
        }
    }

    function isDirty() {
        return Settings.dyeBeanBrand !== _snapBrand
            || Settings.dyeBeanType !== _snapType
            || Settings.dyeRoastDate !== _snapRoastDate
            || Settings.dyeRoastLevel !== _snapRoastLevel
            || Settings.dyeGrinderBrand !== _snapGrinderBrand
            || Settings.dyeGrinderModel !== _snapGrinderModel
            || Settings.dyeGrinderBurrs !== _snapGrinderBurrs
            || Settings.dyeGrinderSetting !== _snapGrinderSetting
            || Settings.dyeBarista !== _snapBarista
            || Settings.selectedBeanPreset !== _snapSelectedPreset
    }

    function handleBack() {
        shotMetadataPage.forceActiveFocus()
        if (isEditMode) {
            root.goBack()
            return
        }
        if (isDirty()) {
            _pendingPresetIndex = -1  // Back navigation, not preset switch
            unsavedChangesDialog.open()
        } else {
            root.goBack()
        }
    }

    // Intercept Android system back button / Escape key
    focus: true
    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
            event.accepted = true
            handleBack()
        }
    }

    // Scroll to focused field when it changes or when keyboard height changes
    onFocusedFieldChanged: {
        if (focusedField) {
            Qt.callLater(scrollToFocusedField)
        }
    }

    // Reset focusedField when focus leaves all text fields.
    // Uses Qt.callLater instead of a timer — by the next event loop iteration,
    // focus has settled on the new field (if any).
    function checkFocusReset() {
        if (focusedField && !focusedField.activeFocus) {
            focusedField = null
            flickable.contentY = 0
        }
    }

    // Announce upload status changes
    Connections {
        target: MainController.visualizer
        function onUploadingChanged() {
            if (AccessibilityManager.enabled) {
                if (MainController.visualizer.uploading) {
                    AccessibilityManager.announce(TranslationManager.translate("shotmetadata.accessible.uploadingtovisualizer", "Uploading to Visualizer"), true)
                }
            }
        }
        function onLastUploadStatusChanged() {
            if (AccessibilityManager.enabled && MainController.visualizer.lastUploadStatus.length > 0) {
                AccessibilityManager.announce(MainController.visualizer.lastUploadStatus, true)
            }
        }
    }

    // Tap background to dismiss keyboard
    MouseArea {
        anchors.fill: parent
        visible: focusedField !== null
        onClicked: {
            focusedField = null
            hideKeyboard()
        }
        z: -1
    }

    Flickable {
        id: flickable
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        // Add bottom padding for keyboard: use real height if available, else estimate when focused
        property real kbHeight: Qt.inputMethod.keyboardRectangle.height / Screen.devicePixelRatio
        onKbHeightChanged: if (focusedField) Qt.callLater(scrollToFocusedField)
        contentHeight: mainColumn.height + (kbHeight > 0 ? kbHeight : (focusedField ? shotMetadataPage.height * 0.5 : 0))
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: mainColumn
            width: parent.width
            spacing: Theme.scaled(6)

            // Resizable Graph (only visible in edit mode when editing a shot from history)
            Rectangle {
                id: graphCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), shotMetadataPage.graphHeight))
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: isEditMode && editShotData.pressure && editShotData.pressure.length > 0

                Accessible.role: Accessible.Graphic
                Accessible.name: TranslationManager.translate("beaninfo.graph.accessible", "Shot graph. Tap to inspect values")
                Accessible.focusable: true
                Accessible.onPressAction: beanGraphMouseArea.clicked(null)

                HistoryShotGraph {
                    id: beanGraph
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    anchors.bottomMargin: Theme.spacingSmall + resizeHandle.height
                    pressureData: editShotData.pressure || []
                    flowData: editShotData.flow || []
                    temperatureData: editShotData.temperature || []
                    weightData: editShotData.weight || []
                    weightFlowRateData: editShotData.weightFlowRate || []
                    resistanceData: editShotData.resistance || []
                    pressureGoalData: editShotData.pressureGoal || []
                    flowGoalData: editShotData.flowGoal || []
                    temperatureGoalData: editShotData.temperatureGoal || []
                    phaseMarkers: editShotData.phases || []
                    maxTime: editShotData.duration || 60
                }

                // Tap-to-announce overlay (reads out curve values at tapped position)
                MouseArea {
                    id: beanGraphMouseArea
                    anchors.fill: beanGraph
                    onClicked: function(mouse) {
                        beanGraph.announceAtPosition(mouse.x, mouse.y)
                    }
                }

                // Resize handle at bottom
                Rectangle {
                    id: resizeHandle
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Theme.scaled(16)
                    color: "transparent"
                    Accessible.ignored: true

                    // Visual indicator (three lines)
                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(2)

                        Repeater {
                            model: 3
                            Rectangle {
                                width: Theme.scaled(30)
                                height: 1
                                color: Theme.textSecondaryColor
                                opacity: resizeMouseArea.containsMouse || resizeMouseArea.pressed ? 0.8 : 0.4
                            }
                        }
                    }

                    MouseArea {
                        id: resizeMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SizeVerCursor
                        preventStealing: true

                        property real startY: 0
                        property real startHeight: 0

                        onPressed: function(mouse) {
                            startY = mouse.y + resizeHandle.mapToItem(shotMetadataPage, 0, 0).y
                            startHeight = graphCard.Layout.preferredHeight
                        }

                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                var currentY = mouse.y + resizeHandle.mapToItem(shotMetadataPage, 0, 0).y
                                var delta = currentY - startY
                                var newHeight = startHeight + delta
                                // Clamp between min and max
                                newHeight = Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), newHeight))
                                shotMetadataPage.graphHeight = newHeight
                            }
                        }

                        onReleased: {
                            Settings.setValue("shotMetadata/graphHeight", shotMetadataPage.graphHeight)
                            flickable.returnToBounds()
                        }
                    }
                }
            }

            // Bean presets + data-entry grid laid out side-by-side in non-edit mode.
            // In edit mode the presets card is hidden and the grid takes full width.
            RowLayout {
                id: beanEntryRow
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(
                    fieldsGrid.implicitHeight,
                    isEditMode ? 0 : (presetsCardColumn.implicitHeight + Theme.scaled(24)))
                spacing: Theme.scaled(12)

            // Bean presets section (only in non-edit mode)
            Rectangle {
                id: presetsCard
                Layout.preferredWidth: Theme.scaled(380)
                Layout.fillHeight: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: !isEditMode

                ColumnLayout {
                    id: presetsCardColumn
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(8)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "beaninfo.presets.title"
                            fallback: "Bean Preset"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(22)
                        }

                        Tr {
                            key: "beaninfo.preset.count"
                            fallback: "(%1)".arg(Settings.beanPresets.length)
                            color: Theme.textColor
                            font.pixelSize: Theme.bodyFont.pixelSize
                        }

                        Item { Layout.fillWidth: true }

                        // Save button — update current preset (visible when a preset was selected before editing)
                        Rectangle {
                            id: saveBeanButton
                            visible: _snapSelectedPreset >= 0
                            Layout.preferredWidth: saveBeanLabel.implicitWidth + Theme.scaled(16)
                            Layout.preferredHeight: Theme.scaled(36)
                            radius: Theme.scaled(4)
                            color: Theme.primaryColor

                            Text {
                                id: saveBeanLabel
                                anchors.centerIn: parent
                                text: TranslationManager.translate("beaninfo.button.savepreset", "Save")
                                color: Theme.primaryContrastColor
                                font.pixelSize: Theme.scaled(14)
                                Accessible.ignored: true
                            }

                            AccessibleTapHandler {
                                anchors.fill: parent
                                accessibleName: TranslationManager.translate("beaninfo.accessibility.savepreset", "Save changes to current preset")
                                accessibleItem: saveBeanButton
                                onAccessibleClicked: saveToCurrentPreset(-1, false)
                            }
                        }

                        // Add button
                        Rectangle {
                            id: addBeanButton
                            Layout.preferredWidth: Theme.scaled(36)
                            Layout.preferredHeight: Theme.scaled(36)
                            radius: Theme.scaled(18)
                            color: Theme.backgroundColor
                            border.color: Theme.textSecondaryColor
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "+"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(20)
                                Accessible.ignored: true
                            }

                            AccessibleTapHandler {
                                anchors.fill: parent
                                accessibleName: TranslationManager.translate("beaninfo.accessibility.addpreset", "Add new bean preset")
                                accessibleItem: addBeanButton
                                onAccessibleClicked: {
                                    savePresetDialog.suggestedName = [Settings.dyeBeanBrand, Settings.dyeBeanType].filter(Boolean).join(" ")
                                    savePresetDialog.open()
                                }
                            }
                        }
                    }

                    Tr {
                        Layout.fillWidth: true
                        visible: Settings.beanPresets.length > 0
                        key: "beaninfo.hint.reorder_vertical"
                        fallback: "Drag to reorder, double-click to rename, click star to show on home screen"
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.Wrap
                    }

                    // This list is bound to the FULL `beanPresets` (unfiltered), so the
                    // `index` passed to row callbacks is already an external index —
                    // no `originalIndex` translation needed here. BeansItem / IdlePage
                    // bind to the filtered `idleBeanPresets` view and map via originalIndex.
                    FavoritesListView {
                        id: beanPresetsList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: Settings.beanPresets.length > 0
                        model: Settings.beanPresets
                        selectedIndex: Settings.selectedBeanPreset
                        rowAccessibleDescription: TranslationManager.translate(
                            "beaninfo.accessible.row_hint",
                            "Double-tap or long-press to rename preset.")

                        displayTextFn: function(row, index) {
                            return row && row.name ? row.name : ""
                        }
                        accessibleNameFn: function(row, index) {
                            if (!row) return ""
                            var status = index === Settings.selectedBeanPreset
                                ? ", " + TranslationManager.translate("accessibility.selected", "selected") : ""
                            return row.name + " " + TranslationManager.translate("beaninfo.accessibility.preset", "preset") + status
                        }
                        deleteAccessibleNameFn: function(row, index) {
                            return TranslationManager.translate("beaninfo.accessibility.removepreset", "Remove preset") +
                                   (row && row.name ? " " + row.name : "")
                        }

                        // "Show on idle" star toggle in place of profiles' edit button
                        trailingActionDelegate: Component {
                            StyledIconButton {
                                anchors.fill: parent
                                active: parent.row ? (parent.row.showOnIdle !== false) : false
                                activeColor: parent.selected ? Theme.primaryContrastColor : Theme.primaryColor
                                inactiveColor: parent.selected
                                    ? Qt.rgba(Theme.primaryContrastColor.r, Theme.primaryContrastColor.g, Theme.primaryContrastColor.b, 0.5)
                                    : Theme.textSecondaryColor
                                icon.source: active ? "qrc:/icons/star.svg" : "qrc:/icons/star-outline.svg"
                                icon.width: Theme.scaled(18)
                                icon.height: Theme.scaled(18)
                                accessibleName: parent.row
                                    ? ((parent.row.showOnIdle !== false
                                        ? TranslationManager.translate("beaninfo.accessible.hide_from_idle", "Hide from idle page")
                                        : TranslationManager.translate("beaninfo.accessible.show_on_idle", "Show on idle page"))
                                        + " " + parent.row.name)
                                    : ""

                                onClicked: {
                                    if (!parent.row) return
                                    var currently = parent.row.showOnIdle !== false
                                    Settings.setBeanPresetShowOnIdle(parent.rowIndex, !currently)
                                }
                            }
                        }

                        onRowSelected: function(index) {
                            shotMetadataPage.forceActiveFocus()
                            if (isDirty()) {
                                // Defer the switch until user saves/discards/keeps
                                _pendingPresetIndex = index
                                unsavedChangesDialog.open()
                            } else {
                                applyPreset(index)
                            }
                        }
                        onRowLongPressed: function(index) {
                            editPresetIndex = index
                            var preset = Settings.getBeanPreset(index)
                            editPresetName = preset.name || ""
                            editPresetDialog.open()
                        }
                        onRowMoved: function(from, to) {
                            // Shift the "unsaved-changes" snapshot so a pure reorder
                            // of the selected item doesn't trip the back-button dialog.
                            // Mirrors the shift logic in Settings::moveBeanPreset
                            // (src/core/settings.cpp, Settings::moveBeanPreset).
                            var s = _snapSelectedPreset
                            if (s === from) _snapSelectedPreset = to
                            else if (from < s && to >= s) _snapSelectedPreset = s - 1
                            else if (from > s && to <= s) _snapSelectedPreset = s + 1
                            Settings.moveBeanPreset(from, to)
                        }
                        onRowDeleted: function(index) {
                            // Mirror Settings::removeBeanPreset's selection adjustment
                            // (src/core/settings.cpp, Settings::removeBeanPreset) on the
                            // snapshot so deleting an unrelated preset doesn't trip the
                            // back-button unsaved-changes dialog.
                            var s = _snapSelectedPreset
                            var newLen = Settings.beanPresets.length - 1
                            if (newLen <= 0) _snapSelectedPreset = -1
                            else if (s >= newLen) _snapSelectedPreset = newLen - 1
                            else if (s > index) _snapSelectedPreset = s - 1
                            Settings.removeBeanPreset(index)
                        }
                    }
                }
            }

            // 2-column grid grouped into Bean / Grinder / Barista sections.
            // Wider fields, better use of vertical space, less text cropping.
            GridLayout {
                id: fieldsGrid
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                columns: 2
                columnSpacing: Theme.scaled(10)
                rowSpacing: Theme.scaled(2)

                // === BEAN SECTION ===
                Tr {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    key: "beaninfo.section.bean"
                    fallback: "Bean"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                }

                SuggestionField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.roaster", "Roaster")
                    text: isEditMode ? editBeanBrand : Settings.dyeBeanBrand
                    suggestions: {
                        var list = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctBeanBrands() : []
                        var current = isEditMode ? editBeanBrand : Settings.dyeBeanBrand
                        if (current.length > 0 && list.indexOf(current) === -1) list = [current].concat(list)
                        return list
                    }
                    onTextEdited: function(t) { if (isEditMode) editBeanBrand = t; else { Settings.dyeBeanBrand = t; deselectPresetOnEdit(); } }
                    onSuggestionSelected: function(t) {
                        if (isEditMode) { editBeanType = ""; editRoastDate = ""; }
                        else { Settings.dyeBeanType = ""; Settings.dyeRoastDate = ""; deselectPresetOnEdit(); }
                        var types = MainController.shotHistory.getDistinctBeanTypesForBrand(t)
                        if (types.length === 1) {
                            if (isEditMode) editBeanType = types[0]; else Settings.dyeBeanType = types[0];
                        } else if (types.length === 0) {
                            _pendingBeanAutoFill = t  // Cache miss — retry when async fetch completes
                        }
                    }
                    onInputFocused: function(field) { focusedField = field }
                    onInputBlurred: Qt.callLater(checkFocusReset)
                }

                SuggestionField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.coffee", "Coffee")
                    text: isEditMode ? editBeanType : Settings.dyeBeanType
                    suggestions: {
                        var list = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctBeanTypesForBrand(
                            isEditMode ? editBeanBrand : Settings.dyeBeanBrand) : []
                        var current = isEditMode ? editBeanType : Settings.dyeBeanType
                        if (current.length > 0 && list.indexOf(current) === -1) list = [current].concat(list)
                        return list
                    }
                    onTextEdited: function(t) { if (isEditMode) editBeanType = t; else { Settings.dyeBeanType = t; deselectPresetOnEdit(); } }
                    onSuggestionSelected: function(t) {
                        if (isEditMode) editRoastDate = ""; else { Settings.dyeRoastDate = ""; deselectPresetOnEdit(); }
                    }
                    onInputFocused: function(field) { focusedField = field }
                    onInputBlurred: Qt.callLater(checkFocusReset)
                }

                Item {
                    Layout.fillWidth: true
                    implicitHeight: beanRoastDateField.implicitHeight

                    LabeledField {
                        id: beanRoastDateField
                        anchors.left: parent.left
                        anchors.right: beanCalendarBtn.left
                        anchors.rightMargin: Theme.scaled(4)
                        label: TranslationManager.translate("shotmetadata.label.roastdate.format", "Roast date (yyyy-mm-dd)")
                        text: isEditMode ? editRoastDate : Settings.dyeRoastDate
                        inputHints: Qt.ImhDate
                        inputMask: "9999-99-99"
                        onTextEdited: function(t) { if (isEditMode) editRoastDate = t; else { Settings.dyeRoastDate = t; deselectPresetOnEdit(); } }
                    }

                    AccessibleButton {
                        id: beanCalendarBtn
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        width: Theme.scaled(44)
                        height: Theme.scaled(44)
                        accessibleName: TranslationManager.translate("datepicker.openCalendar", "Open calendar")
                        leftPadding: Theme.scaled(8)
                        rightPadding: Theme.scaled(8)
                        icon.source: "qrc:/emoji/1f4c5.svg"
                        icon.width: Theme.scaled(20)
                        icon.height: Theme.scaled(20)
                        text: ""
                        onClicked: beanDatePicker.openWithDate(isEditMode ? editRoastDate : Settings.dyeRoastDate)
                    }

                    DatePickerDialog {
                        id: beanDatePicker
                        onDateSelected: function(dateString) {
                            if (isEditMode) editRoastDate = dateString; else { Settings.dyeRoastDate = dateString; deselectPresetOnEdit(); }
                        }
                    }
                }

                LabeledComboBox {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.roastlevel", "Roast level")
                    model: ["",
                        TranslationManager.translate("shotmetadata.roastlevel.light", "Light"),
                        TranslationManager.translate("shotmetadata.roastlevel.mediumlight", "Medium-Light"),
                        TranslationManager.translate("shotmetadata.roastlevel.medium", "Medium"),
                        TranslationManager.translate("shotmetadata.roastlevel.mediumdark", "Medium-Dark"),
                        TranslationManager.translate("shotmetadata.roastlevel.dark", "Dark")]
                    currentValue: isEditMode ? editRoastLevel : Settings.dyeRoastLevel
                    onValueChanged: function(v) { if (isEditMode) editRoastLevel = v; else { Settings.dyeRoastLevel = v; deselectPresetOnEdit(); } }
                }

                // === GRINDER SECTION ===
                Tr {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.scaled(6)
                    key: "beaninfo.section.grinder"
                    fallback: "Grinder"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                }

                SuggestionField {
                    id: grinderBrandField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.grinderbrand", "Grinder brand")
                    text: isEditMode ? editGrinderBrand : Settings.dyeGrinderBrand
                    suggestions: {
                        var history = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctGrinderBrands() : []
                        var known = Settings.knownGrinderBrands()
                        // Merge: history first, then known brands not already in history
                        var merged = history.slice()
                        for (var i = 0; i < known.length; i++) {
                            if (merged.indexOf(known[i]) < 0) merged.push(known[i])
                        }
                        return merged
                    }
                    onTextEdited: function(t) {
                        if (isEditMode) editGrinderBrand = t; else { Settings.dyeGrinderBrand = t; deselectPresetOnEdit(); }
                    }
                    onSuggestionSelected: function(t) {
                        // Clear model and burrs, then auto-fill if only one option
                        if (isEditMode) { editGrinderModel = ""; editGrinderBurrs = ""; }
                        else { Settings.dyeGrinderModel = ""; Settings.dyeGrinderBurrs = ""; deselectPresetOnEdit(); }
                        var models = Settings.knownGrinderModels(t)
                        if (models.length === 1) {
                            if (isEditMode) editGrinderModel = models[0]; else Settings.dyeGrinderModel = models[0];
                            var burrs = Settings.suggestedBurrs(t, models[0])
                            if (burrs.length === 1) {
                                if (isEditMode) editGrinderBurrs = burrs[0]; else Settings.dyeGrinderBurrs = burrs[0];
                            }
                        }
                    }
                    onInputFocused: function(field) { focusedField = field }
                    onInputBlurred: Qt.callLater(checkFocusReset)
                }

                SuggestionField {
                    id: grinderModelField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.grindermodel", "Model")
                    text: isEditMode ? editGrinderModel : Settings.dyeGrinderModel
                    property string currentBrand: isEditMode ? editGrinderBrand : Settings.dyeGrinderBrand
                    suggestions: {
                        var history = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctGrinderModelsForBrand(currentBrand) : []
                        var known = Settings.knownGrinderModels(currentBrand)
                        var merged = history.slice()
                        for (var i = 0; i < known.length; i++) {
                            if (merged.indexOf(known[i]) < 0) merged.push(known[i])
                        }
                        return merged
                    }
                    onTextEdited: function(t) {
                        if (isEditMode) editGrinderModel = t; else { Settings.dyeGrinderModel = t; deselectPresetOnEdit(); }
                    }
                    onSuggestionSelected: function(t) {
                        // Auto-fill burrs if only one option
                        var brand = isEditMode ? editGrinderBrand : Settings.dyeGrinderBrand
                        var burrs = Settings.suggestedBurrs(brand, t)
                        if (burrs.length === 1) {
                            if (isEditMode) editGrinderBurrs = burrs[0]; else Settings.dyeGrinderBurrs = burrs[0];
                        }
                        if (!isEditMode) deselectPresetOnEdit();
                    }
                    onInputFocused: function(field) { focusedField = field }
                    onInputBlurred: Qt.callLater(checkFocusReset)
                }

                SuggestionField {
                    id: grinderBurrsField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.grinderburrs", "Burrs")
                    text: isEditMode ? editGrinderBurrs : Settings.dyeGrinderBurrs
                    property string currentBrand: isEditMode ? editGrinderBrand : Settings.dyeGrinderBrand
                    property string currentModel: isEditMode ? editGrinderModel : Settings.dyeGrinderModel
                    suggestions: {
                        var history = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctGrinderBurrsForModel(currentBrand, currentModel) : []
                        var known = Settings.suggestedBurrs(currentBrand, currentModel)
                        var merged = history.slice()
                        for (var i = 0; i < known.length; i++) {
                            if (merged.indexOf(known[i]) < 0) merged.push(known[i])
                        }
                        return merged
                    }
                    onTextEdited: function(t) { if (isEditMode) editGrinderBurrs = t; else { Settings.dyeGrinderBurrs = t; deselectPresetOnEdit(); } }
                    onInputFocused: function(field) { focusedField = field }
                    onInputBlurred: Qt.callLater(checkFocusReset)
                }

                SuggestionField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.setting", "Setting")
                    text: isEditMode ? editGrinderSetting : Settings.dyeGrinderSetting
                    suggestions: {
                        var list = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctGrinderSettingsForGrinder(
                            isEditMode ? editGrinderModel : Settings.dyeGrinderModel) : []
                        var current = isEditMode ? editGrinderSetting : Settings.dyeGrinderSetting
                        if (current.length > 0 && list.indexOf(current) === -1) list = [current].concat(list)
                        return list
                    }
                    onTextEdited: function(t) { if (isEditMode) editGrinderSetting = t; else { Settings.dyeGrinderSetting = t; deselectPresetOnEdit(); } }
                    onInputFocused: function(field) { focusedField = field }
                    onInputBlurred: Qt.callLater(checkFocusReset)
                }

                Tr {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.scaled(6)
                    key: "beaninfo.section.barista"
                    fallback: "Barista"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                }

                SuggestionField {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.barista", "Barista")
                    text: isEditMode ? editBarista : Settings.dyeBarista
                    suggestions: {
                        var list = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctBaristas() : []
                        var current = isEditMode ? editBarista : Settings.dyeBarista
                        if (current.length > 0 && list.indexOf(current) === -1) list = [current].concat(list)
                        return list
                    }
                    onTextEdited: function(t) { if (isEditMode) editBarista = t; else Settings.dyeBarista = t; }
                    onInputFocused: function(field) { focusedField = field }
                    onInputBlurred: Qt.callLater(checkFocusReset)
                }
            }
            }  // end beanEntryRow

            Item { Layout.preferredHeight: 10 }
        }
    }

    // Hide keyboard button - only on mobile (Qt.inputMethod.visible is unreliable on Android)
    Rectangle {
        id: hideKeyboardButton
        visible: focusedField !== null && (Qt.platform.os === "android" || Qt.platform.os === "ios")
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin + 4
        width: hideKeyboardText.width + 24
        height: Theme.scaled(28)
        radius: Theme.scaled(14)
        color: Theme.primaryColor
        z: 100

        Accessible.role: Accessible.Button
        Accessible.name: TranslationManager.translate("shotmetadata.button.hidekeyboard", "Hide keyboard")
        Accessible.focusable: true
        Accessible.onPressAction: hideKeyboardMa.clicked(null)

        Tr {
            id: hideKeyboardText
            anchors.centerIn: parent
            key: "shotmetadata.button.hidekeyboard"
            fallback: "Hide keyboard"
            color: Theme.primaryContrastColor
            font.pixelSize: Theme.scaled(13)
            font.bold: true
            Accessible.ignored: true
        }

        MouseArea {
            id: hideKeyboardMa
            anchors.fill: parent
            onClicked: {
                focusedField = null
                hideKeyboard()
            }
        }
    }

    // Bottom bar — back button always visible (replaces old Cancel/Done buttons;
    // in non-edit mode, handleBack() prompts to keep or discard unsaved changes)
    BottomBar {
        barColor: "transparent"

        onBackClicked: handleBack()

        // Save button - visible in edit mode only
        Rectangle {
            id: saveButton
            visible: isEditMode
            Layout.preferredWidth: saveButtonContent.width + 40
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: saveArea.pressed ? Qt.darker(Theme.successButtonColor, 1.2) : Theme.successButtonColor  // Dark green

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("beaninfo.button.save", "Save Changes")
            Accessible.focusable: true
            Accessible.onPressAction: saveArea.clicked(null)

            Row {
                id: saveButtonContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Image {
                    source: "qrc:/icons/tick.svg"
                    sourceSize.width: Theme.scaled(16)
                    sourceSize.height: Theme.scaled(16)
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }

                Tr {
                    key: "beaninfo.button.save"
                    fallback: "Save Changes"
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

            MouseArea {
                id: saveArea
                anchors.fill: parent
                onClicked: saveEditedShot()
            }
        }
    }

    // === Inline Components ===

    component LabeledField: Item {
        property string label: ""
        property string text: ""
        property int inputHints: Qt.ImhNone
        property string inputMask: ""
        signal textEdited(string text)

        implicitHeight: fieldLabel.height + Theme.scaled(48) + 2

        Text {
            id: fieldLabel
            anchors.left: parent.left
            anchors.top: parent.top
            text: parent.label
            color: Theme.textColor
            font.pixelSize: Theme.scaled(14)
        }

        StyledTextField {
            id: fieldInput
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: fieldLabel.bottom
            anchors.topMargin: Theme.scaled(2)
            height: Theme.scaled(48)
            text: parent.text
            inputMethodHints: parent.inputHints
            inputMask: parent.inputMask
            EnterKey.type: Qt.EnterKeyNext
            Keys.onReturnPressed: nextItemInFocusChain().forceActiveFocus()
            onTextChanged: parent.textEdited(text)
            onActiveFocusChanged: {
                if (activeFocus) {
                    focusedField = fieldInput
                    if (AccessibilityManager.enabled) {
                        var stripped = text.replace(/[\s\-]/g, "")
                        let announcement = parent.label + ". " + (stripped.length > 0 ? text : TranslationManager.translate("shotmetadata.accessible.empty", "Empty"))
                        AccessibilityManager.announce(announcement)
                    }
                } else {
                    Qt.callLater(checkFocusReset)
                }
            }

            Accessible.role: Accessible.EditableText
            Accessible.name: parent.label
            Accessible.description: {
                var stripped = text.replace(/[\s\-]/g, "")
                return stripped.length > 0 ? text : TranslationManager.translate("shotmetadata.accessible.empty", "Empty")
            }
        }
    }

    component LabeledComboBox: Item {
        property string label: ""
        property var model: []
        property string currentValue: ""
        signal valueChanged(string value)

        implicitHeight: comboLabel.height + Theme.scaled(48) + 2

        Text {
            id: comboLabel
            anchors.left: parent.left
            anchors.top: parent.top
            text: parent.label
            color: Theme.textColor
            font.pixelSize: Theme.scaled(14)
        }

        StyledComboBox {
            id: combo
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: comboLabel.bottom
            anchors.topMargin: Theme.scaled(2)
            height: Theme.scaled(48)
            model: parent.model
            currentIndex: Math.max(0, model.indexOf(parent.currentValue))
            font.pixelSize: Theme.labelFont.pixelSize
            accessibleLabel: parent.label
            emptyItemText: TranslationManager.translate("shotmetadata.option.none", "(None)")

            Accessible.description: currentIndex > 0 ? currentText : TranslationManager.translate("shotmetadata.accessible.notset", "Not set")

            onActiveFocusChanged: {
                if (activeFocus && AccessibilityManager.enabled) {
                    let value = currentIndex > 0 ? currentText : TranslationManager.translate("shotmetadata.accessible.notset", "Not set")
                    AccessibilityManager.announce(parent.label + ". " + value)
                }
            }

            background: Rectangle {
                color: Theme.backgroundColor
                radius: Theme.scaled(4)
                border.color: combo.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                border.width: 1
            }

            contentItem: Text {
                text: combo.currentIndex === 0 && combo.model[0] === "" ? parent.parent.label : combo.displayText
                color: Theme.textColor
                font.pixelSize: Theme.scaled(18)
                verticalAlignment: Text.AlignVCenter
                leftPadding: Theme.scaled(12)
            }

            indicator: Text {
                anchors.right: parent.right
                anchors.rightMargin: Theme.scaled(12)
                anchors.verticalCenter: parent.verticalCenter
                text: "▼"
                color: Theme.textColor
                font.pixelSize: Theme.scaled(10)
            }

            onActivated: function(index) { parent.valueChanged(currentText) }
        }
    }

    // Add Preset Popup
    Dialog {
        id: savePresetDialog
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2 - popupKeyboardOffset
        padding: 20
        modal: true
        focus: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        property string suggestedName: ""  // Set before opening to pre-fill
        property bool goBackAfterSave: false  // Navigate back after saving (unsaved changes flow)
        property int pendingPresetAfterSave: -1  // Apply this preset after saving (-1 = none)

        onOpened: {
            popupKeyboardOffset = shotMetadataPage.height * 0.25
            // Use suggested name if provided, otherwise clear
            newBeanNameInput.text = suggestedName
            suggestedName = ""
            newBeanNameInput.forceActiveFocus()
        }

        onClosed: {
            popupKeyboardOffset = 0
            goBackAfterSave = false
            pendingPresetAfterSave = -1
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
                key: "beaninfo.popup.addPreset"
                fallback: "Add Bean Preset"
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
                    id: newBeanNameInput
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("beaninfo.accessible.newPresetName", "New bean preset name")
                    Accessible.description: text
                    Accessible.focusable: true

                    Tr {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        key: "beaninfo.placeholder.presetName"
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
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("beanInfo.cancelSavingPreset", "Cancel saving bean preset")
                    onClicked: savePresetDialog.close()
                }

                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("common.add", "Add")
                    accessibleName: TranslationManager.translate("beanInfo.addNewPreset", "Add new bean preset with current settings")
                    onClicked: {
                        if (newBeanNameInput.text.trim().length > 0) {
                            var presetName = newBeanNameInput.text.trim()
                            Settings.saveBeanPresetFromCurrent(presetName)
                            // Find the index of the saved/updated preset
                            var savedIndex = Settings.findBeanPresetByName(presetName)
                            if (savedIndex >= 0) {
                                Settings.selectedBeanPreset = savedIndex
                            }
                            // Update snapshot so handleBack() doesn't show spurious unsaved changes dialog
                            refreshSnapshot()
                            var shouldGoBack = savePresetDialog.goBackAfterSave
                            var pendingPreset = savePresetDialog.pendingPresetAfterSave
                            newBeanNameInput.text = ""
                            savePresetDialog.goBackAfterSave = false
                            savePresetDialog.pendingPresetAfterSave = -1
                            savePresetDialog.close()
                            if (pendingPreset >= 0) {
                                applyPreset(pendingPreset)
                            } else if (shouldGoBack) {
                                root.goBack()
                            }
                        }
                    }
                }
            }
        }
    }

    // Edit Preset Popup
    Dialog {
        id: editPresetDialog
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2 - popupKeyboardOffset
        padding: 20
        modal: true
        focus: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        onOpened: {
            popupKeyboardOffset = shotMetadataPage.height * 0.25
            editBeanNameInput.text = editPresetName
            editBeanNameInput.forceActiveFocus()
        }

        onClosed: {
            popupKeyboardOffset = 0
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
                key: "beaninfo.popup.editPreset"
                fallback: "Edit Bean Preset"
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
                    id: editBeanNameInput
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("beaninfo.accessible.renamePreset", "Rename bean preset")
                    Accessible.description: text
                    Accessible.focusable: true

                    Tr {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        key: "beaninfo.placeholder.presetName"
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
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("beanInfo.cancelEditingPreset", "Cancel editing bean preset")
                    onClicked: editPresetDialog.close()
                }

                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("common.save", "Save")
                    accessibleName: TranslationManager.translate("beanInfo.saveChangesToPreset", "Save changes to bean preset")
                    onClicked: {
                        if (editBeanNameInput.text.trim().length > 0 && editPresetIndex >= 0) {
                            var preset = Settings.getBeanPreset(editPresetIndex)
                            Settings.updateBeanPreset(editPresetIndex,
                                editBeanNameInput.text.trim(),
                                preset.brand || "",
                                preset.type || "",
                                preset.roastDate || "",
                                preset.roastLevel || "",
                                preset.grinderBrand || "",
                                preset.grinderModel || "",
                                preset.grinderBurrs || "",
                                preset.grinderSetting || "",
                                preset.barista || "")
                        }
                        editPresetDialog.close()
                    }
                }
            }
        }
    }

    // Unsaved changes confirmation dialog
    Dialog {
        id: unsavedChangesDialog
        anchors.centerIn: parent
        width: Theme.scaled(360)
        modal: true
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            Text {
                text: TranslationManager.translate("beaninfo.unsaved.title", "Unsaved Changes")
                font: Theme.titleFont
                color: Theme.textColor
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                Layout.bottomMargin: 0
            }

            Text {
                text: {
                    if (_snapSelectedPreset >= 0) {
                        return _pendingPresetIndex >= 0
                            ? TranslationManager.translate("beaninfo.unsaved.message.preset.hassaved", "Save changes to this preset before switching, save as new, or discard?")
                            : TranslationManager.translate("beaninfo.unsaved.message.hassaved", "Save changes to this preset, save as new, keep as-is, or discard?")
                    }
                    return _pendingPresetIndex >= 0
                        ? TranslationManager.translate("beaninfo.unsaved.message.preset", "Save your changes as a favorite before switching, or discard them?")
                        : TranslationManager.translate("beaninfo.unsaved.message", "Save as a favorite, keep as-is, or discard?")
                }
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                spacing: Theme.scaled(10)

                // "Save" — update the existing preset (only when a preset was selected before editing)
                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(44)
                    visible: _snapSelectedPreset >= 0
                    text: TranslationManager.translate("beaninfo.unsaved.savePreset", "Save")
                    accessibleName: _pendingPresetIndex >= 0
                        ? TranslationManager.translate("beaninfo.unsaved.savePreset.preset.accessible", "Save changes to current preset and switch")
                        : TranslationManager.translate("beaninfo.unsaved.savePreset.accessible", "Save changes to current preset and go back")
                    onClicked: {
                        unsavedChangesDialog.close()
                        var pending = _pendingPresetIndex
                        _pendingPresetIndex = -1
                        saveToCurrentPreset(pending, pending < 0)
                    }
                    background: Rectangle {
                        radius: Theme.buttonRadius
                        color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: Theme.primaryContrastColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        Accessible.ignored: true
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(44)
                    text: _snapSelectedPreset >= 0
                        ? TranslationManager.translate("beaninfo.unsaved.saveAsNew", "Save as New")
                        : TranslationManager.translate("beaninfo.unsaved.saveFavorite", "Save Favorite")
                    accessibleName: _pendingPresetIndex >= 0
                        ? TranslationManager.translate("beaninfo.unsaved.saveFavorite.preset.accessible", "Save as a new bean favorite and switch preset")
                        : TranslationManager.translate("beaninfo.unsaved.saveFavorite.accessible", "Save as a new bean favorite and go back")
                    onClicked: {
                        unsavedChangesDialog.close()
                        savePresetDialog.suggestedName = [Settings.dyeBeanBrand, Settings.dyeBeanType].filter(Boolean).join(" ")
                        savePresetDialog.goBackAfterSave = _pendingPresetIndex < 0  // Only go back if not switching presets
                        savePresetDialog.pendingPresetAfterSave = _pendingPresetIndex
                        savePresetDialog.open()
                    }
                    background: Rectangle {
                        radius: Theme.buttonRadius
                        color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: Theme.primaryContrastColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        Accessible.ignored: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    AccessibleButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(44)
                        text: TranslationManager.translate("beaninfo.unsaved.discard", "Discard")
                        accessibleName: _pendingPresetIndex >= 0
                            ? TranslationManager.translate("beaninfo.unsaved.discard.preset.accessible", "Discard changes and switch preset")
                            : TranslationManager.translate("beaninfo.unsaved.discard.accessible", "Discard changes and go back")
                        onClicked: {
                            unsavedChangesDialog.close()
                            if (_pendingPresetIndex >= 0) {
                                // Discard edits and switch to the tapped preset
                                var idx = _pendingPresetIndex
                                _pendingPresetIndex = -1
                                applyPreset(idx)
                            } else {
                                // Discard edits and go back
                                Settings.dyeBeanBrand = _snapBrand
                                Settings.dyeBeanType = _snapType
                                Settings.dyeRoastDate = _snapRoastDate
                                Settings.dyeRoastLevel = _snapRoastLevel
                                Settings.dyeGrinderBrand = _snapGrinderBrand
                                Settings.dyeGrinderModel = _snapGrinderModel
                                Settings.dyeGrinderBurrs = _snapGrinderBurrs
                                Settings.dyeGrinderSetting = _snapGrinderSetting
                                Settings.dyeBarista = _snapBarista
                                Settings.selectedBeanPreset = _snapSelectedPreset
                                root.goBack()
                            }
                        }
                        background: Rectangle {
                            radius: Theme.buttonRadius
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.primaryColor
                        }
                        contentItem: Text {
                            text: parent.text
                            font: Theme.bodyFont
                            color: Theme.primaryColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            Accessible.ignored: true
                        }
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(44)
                        visible: _pendingPresetIndex < 0  // Hide when switching presets
                        text: TranslationManager.translate("beaninfo.unsaved.keep", "Keep")
                        accessibleName: TranslationManager.translate("beaninfo.unsaved.keep.accessible", "Keep changes and go back")
                        onClicked: {
                            unsavedChangesDialog.close()
                            // DYE fields now diverge from the preset — clear the association
                            // rather than corrupting the preset with the edited values
                            if (Settings.selectedBeanPreset >= 0) {
                                Settings.selectedBeanPreset = -1
                            }
                            root.goBack()
                        }
                        background: Rectangle {
                            radius: Theme.buttonRadius
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.primaryColor
                        }
                        contentItem: Text {
                            text: parent.text
                            font: Theme.bodyFont
                            color: Theme.primaryColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            Accessible.ignored: true
                        }
                    }
                }
            }
        }
    }

    // Guest Bean Dialog - shown when opening with unsaved bean data
    Dialog {
        id: guestBeanDialog
        anchors.centerIn: parent
        width: Theme.scaled(400)
        modal: true
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.topMargin: Theme.scaled(10)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("beaninfo.guestbean.title", "Guest Bean")
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Message
            Text {
                text: TranslationManager.translate("beaninfo.guestbean.message",
                    "You have bean info loaded that isn't saved as a preset.\nWould you like to save it?")
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
            }

            // Current bean info preview
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.preferredHeight: guestBeanPreview.implicitHeight + Theme.scaled(16)
                color: Theme.backgroundColor
                radius: Theme.scaled(4)

                Column {
                    id: guestBeanPreview
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.scaled(8)
                    spacing: Theme.scaled(4)

                    Text {
                        text: Settings.dyeBeanBrand + (Settings.dyeBeanType ? " - " + Settings.dyeBeanType : "")
                        font: Theme.bodyFont
                        color: Theme.textColor
                        elide: Text.ElideRight
                        width: parent.width
                        visible: Settings.dyeBeanBrand || Settings.dyeBeanType
                    }

                    Text {
                        text: Settings.dyeGrinderModel + (Settings.dyeGrinderSetting ? " @ " + Settings.dyeGrinderSetting : "")
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        elide: Text.ElideRight
                        width: parent.width
                        visible: Settings.dyeGrinderModel || Settings.dyeGrinderSetting
                    }
                }
            }

            // Buttons
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(10)

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(50)
                    text: TranslationManager.translate("beaninfo.guestbean.notNow", "Not Now")
                    accessibleName: TranslationManager.translate("beaninfo.guestbean.notNow", "Not Now")
                    onClicked: guestBeanDialog.close()
                    background: Rectangle {
                        radius: Theme.buttonRadius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.primaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: Theme.primaryColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        Accessible.ignored: true
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(50)
                    text: TranslationManager.translate("beaninfo.guestbean.save", "Save as Preset")
                    accessibleName: TranslationManager.translate("beaninfo.guestbean.save", "Save as Preset")
                    onClicked: {
                        guestBeanDialog.close()
                        // Open the save preset dialog with a suggested name
                        savePresetDialog.suggestedName = [Settings.dyeBeanBrand, Settings.dyeBeanType].filter(Boolean).join(" ")
                        savePresetDialog.open()
                    }
                    background: Rectangle {
                        radius: Theme.buttonRadius
                        color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: Theme.primaryContrastColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        Accessible.ignored: true
                    }
                }
            }
        }
    }

}
