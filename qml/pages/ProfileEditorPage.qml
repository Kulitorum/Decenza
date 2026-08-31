pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

T.Page {
    id: profileEditorPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: profile ? profile.title : TranslationManager.translate("profileEditor.title", "Profile Editor")

    objectName: "profileEditorPage"
    // suppressShotChart: this page draws its own graph, and the last-shot chart
    // background would put a second set of curves behind it.
    background: ThemedPageBackground { suppressShotChart: true }

    property var profile: null
    property int selectedStepIndex: -1
    property bool profileModified: ProfileManager.profileModified
    property string originalProfileName: ""
    property int stepVersion: 0  // Increment to force step editor refresh

    function handleBack() {
        flushPendingEdits()
        if (profileModified) {
            exitDialog.open()
        } else {
            AppShell.backRequested()
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

    // For accessibility: track previously announced frame to only speak differences
    property var lastAnnouncedFrame: null

    // Announce frame when selected
    onSelectedStepIndexChanged: announceFrame()

    function announceFrame() {
        if (typeof AccessibilityManager === "undefined" || AccessibilityManager === null || !AccessibilityManager.enabled) return
        if (!profile || selectedStepIndex < 0 || selectedStepIndex >= profile.steps.length) return

        var step = profile.steps[selectedStepIndex]
        var prev = lastAnnouncedFrame
        var parts = []

        // Frame number and name
        parts.push("Frame " + (selectedStepIndex + 1) + ", " + (step.name || "unnamed"))

        // Only announce differences from previous frame (or all if no previous)
        if (!prev) {
            // First frame - announce everything
            parts.push(step.pump === "flow" ? "flow mode" : "pressure mode")
            if (step.pump === "flow") {
                parts.push(step.flow.toFixed(2) + " mL per second")
            } else {
                parts.push(step.pressure.toFixed(2) + " bar")
            }
            parts.push(Theme.cToDisplay(step.temperature).toFixed(0) + " degrees")
            parts.push(step.seconds.toFixed(0) + " seconds")
            parts.push(step.transition === "smooth" ? "smooth transition" : "fast transition")
        } else {
            // Announce only differences
            if (step.pump !== prev.pump) {
                parts.push(step.pump === "flow" ? "flow mode" : "pressure mode")
            }
            if (step.pump === "flow") {
                if (step.flow !== prev.flow || step.pump !== prev.pump) {
                    parts.push(step.flow.toFixed(2) + " mL per second")
                }
            } else {
                if (step.pressure !== prev.pressure || step.pump !== prev.pump) {
                    parts.push(step.pressure.toFixed(2) + " bar")
                }
            }
            if (step.temperature !== prev.temperature) {
                parts.push(Theme.cToDisplay(step.temperature).toFixed(0) + " degrees")
            }
            if (step.seconds !== prev.seconds) {
                parts.push(step.seconds.toFixed(0) + " seconds")
            }
            if (step.transition !== prev.transition) {
                parts.push(step.transition === "smooth" ? "smooth transition" : "fast transition")
            }
        }

        // Remember this frame for next comparison
        lastAnnouncedFrame = {
            pump: step.pump,
            flow: step.flow,
            pressure: step.pressure,
            temperature: step.temperature,
            seconds: step.seconds,
            transition: step.transition
        }

        AccessibilityManager.announce(parts.join(". "))
    }

    function updatePageTitle() {
    }

    // Commit any text fields that use onEditingFinished (which won't fire on navigation)
    function flushPendingEdits() {
        if (profile) {
            if (profileNotesFieldInline.text !== (profile.profile_notes || "")) {
                profile.profile_notes = profileNotesFieldInline.text
                uploadProfile()
            }
        }
    }

    // Update profile state and refresh UI (BLE upload deferred to editor exit, see #557)
    function uploadProfile() {
        if (profile) {
            ProfileManager.uploadProfile(profile)
            // Force step editor bindings to re-evaluate
            stepVersion++
            // Force graph to update by creating a new array reference
            // (assigning same reference doesn't trigger onFramesChanged)
            profileGraph.frames = profile.steps.slice()
        }
    }

    // Save profile to file. Returns true on success.
    function saveProfile() {
        if (!profile || !originalProfileName) {
            saveAsDialog.open()
            return false
        }
        return ProfileManager.saveProfile(originalProfileName)
    }

    // Save profile with new name. Returns true on success.
    function saveProfileAs(filename, title) {
        if (!profile) return false
        if (ProfileManager.saveProfileAs(filename, title)) {
            originalProfileName = filename
            return true
        }
        return false
    }

    KeyboardAwareContainer {
        id: keyboardContainer
        anchors.fill: parent
        textFields: [profileNotesFieldInline.textField, profileNameField]

    // Editor mode header
    Rectangle {
        id: editorModeHeader
        anchors.top: parent.top
        anchors.topMargin: Theme.pageTopMargin
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        height: Theme.scaled(50)
        color: Theme.warningColor
        radius: Theme.cardRadius

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.scaled(15)
            anchors.rightMargin: Theme.scaled(15)

            Text {
                text: TranslationManager.translate("profileEditor.advancedEditor", "Advanced Editor")
                font.family: Theme.titleFont.family
                font.pixelSize: Theme.titleFont.pixelSize
                font.bold: true
                color: Theme.primaryContrastColor
                Accessible.role: Accessible.Heading
                Accessible.name: text
                Accessible.focusable: true
            }

            Text {
                text: TranslationManager.translate("profileEditor.advancedEditorHint", "Full frame-by-frame control • Click frames to edit")
                font: Theme.captionFont
                color: Qt.rgba(1, 1, 1, 0.8)
                Layout.fillWidth: true
            }
        }
    }

    // Main content area
    Item {
        anchors.top: editorModeHeader.bottom
        anchors.topMargin: Theme.scaled(10)
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin

        RowLayout {
            anchors.fill: parent
            spacing: Theme.scaled(15)

            // Left side: Profile graph with frame visualization
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(0)
                    spacing: Theme.scaled(0)

                    // Frame toolbar wrapper
                    Rectangle {
                        color: Theme.cardBackgroundColor
                        Layout.fillWidth: true
                        implicitHeight: toolbarRow.implicitHeight
                        z: 1  // Above graph

                        RowLayout {
                            id: toolbarRow
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(8)
                            anchors.rightMargin: Theme.scaled(8)
                            spacing: Theme.scaled(6)

                            Tr {
                                key: "profileeditor.frames.title"
                                fallback: "Frames"
                                font: Theme.captionFont
                                color: Theme.textColor
                            }

                            Item { Layout.fillWidth: true }

                            AccessibleButton {
                                primary: true
                                text: TranslationManager.translate("profileEditor.addFrame", "+ Add")
                                accessibleName: TranslationManager.translate("profileEditor.addFrameAccessible", "Add new frame to profile")
                                onClicked: profileEditorPage.addStep()
                            }

                            AccessibleButton {
                                text: TranslationManager.translate("profileEditor.deleteFrame", "Delete")
                                accessibleName: TranslationManager.translate("profileEditor.deleteFrameAccessible", "Delete selected frame")
                                destructive: true
                                enabled: profileEditorPage.selectedStepIndex >= 0 && profileEditorPage.profile && profileEditorPage.profile.steps.length > 1
                                onClicked: profileEditorPage.deleteStep(profileEditorPage.selectedStepIndex)
                            }

                            AccessibleButton {
                                primary: true
                                text: TranslationManager.translate("profileEditor.copyFrame", "Copy")
                                accessibleName: TranslationManager.translate("profileEditor.duplicateFrameAccessible", "Duplicate selected frame")
                                enabled: profileEditorPage.selectedStepIndex >= 0 && profileEditorPage.profile && profileEditorPage.profile.steps.length < 20
                                onClicked: profileEditorPage.duplicateStep(profileEditorPage.selectedStepIndex)
                            }

                            StyledIconButton {
                                text: "\u2190"
                                accessibleName: TranslationManager.translate("profileEditor.moveFrameLeft", "Move frame left")
                                accessibleDescription: {
                                    if (profileEditorPage.selectedStepIndex < 0) return TranslationManager.translate("profileEditor.selectFrameFirst", "Select a frame first")
                                    if (profileEditorPage.selectedStepIndex === 0) return TranslationManager.translate("profileEditor.frameAlreadyFirst", "Frame is already first")
                                    return ""
                                }
                                enabled: profileEditorPage.selectedStepIndex > 0
                                onClicked: profileEditorPage.moveStep(profileEditorPage.selectedStepIndex, profileEditorPage.selectedStepIndex - 1)
                            }

                            StyledIconButton {
                                text: "\u2192"
                                accessibleName: TranslationManager.translate("profileEditor.moveFrameRight", "Move frame right")
                                accessibleDescription: {
                                    if (profileEditorPage.selectedStepIndex < 0) return TranslationManager.translate("profileEditor.selectFrameFirst", "Select a frame first")
                                    if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= profileEditorPage.profile.steps.length - 1) return TranslationManager.translate("profileEditor.frameAlreadyLast", "Frame is already last")
                                    return ""
                                }
                                enabled: profileEditorPage.selectedStepIndex >= 0 && profileEditorPage.selectedStepIndex < (profileEditorPage.profile ? profileEditorPage.profile.steps.length - 1 : 0)
                                onClicked: profileEditorPage.moveStep(profileEditorPage.selectedStepIndex, profileEditorPage.selectedStepIndex + 1)
                            }
                        }
                    }

                    // Profile graph
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: -Theme.scaled(10)

                        ProfileGraph {
                            id: profileGraph
                            anchors.fill: parent
                            frames: profileEditorPage.profile ? profileEditorPage.profile.steps : []
                            selectedFrameIndex: profileEditorPage.selectedStepIndex
                            targetWeight: profileEditorPage.profile ? (profileEditorPage.profile.target_weight || 0) : 0
                            targetVolume: profileEditorPage.profile ? (profileEditorPage.profile.target_volume || 0) : 0

                            onFrameSelected: function(index) {
                                profileEditorPage.selectedStepIndex = index
                            }
                        }
                    }

                    // Profile description
                    ExpandableTextArea {
                        id: profileNotesFieldInline
                        inlineHeight: Theme.scaled(80)
                        text: profileEditorPage.profile ? (profileEditorPage.profile.profile_notes || "") : ""
                        accessibleName: TranslationManager.translate("profileEditor.accessible.profileDescription", "Profile description")
                        textFont: Theme.labelFont
                        onEditingFinished: {
                            if (profileEditorPage.profile && text !== (profileEditorPage.profile.profile_notes || "")) {
                                profileEditorPage.profile.profile_notes = text
                                profileEditorPage.uploadProfile()
                            }
                        }
                    }
                }
            }

            // Right side: Frame editor panel
            Rectangle {
                Layout.preferredWidth: Theme.scaled(320)
                Layout.fillHeight: true
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(12)

                    // Profile settings + limits buttons
                    RowLayout {
                        Layout.fillWidth: true
                        visible: profileEditorPage.profile !== null
                        spacing: Theme.scaled(6)

                        AccessibleButton {
                            id: openProfileSettingsButton
                            Layout.fillWidth: true
                            text: {
                                profileEditorPage.stepVersion
                                if (!profileEditorPage.profile) return TranslationManager.translate("profileEditor.settings", "Settings")
                                var temp = profileEditorPage.profile.steps.length > 0 ? Theme.formatTemperature(profileEditorPage.profile.steps[0].temperature, 0) : Theme.formatTemperature(93, 0)
                                return TranslationManager.translate("profileEditor.settings", "Settings") + " (" + temp + ")"
                            }
                            accessibleName: TranslationManager.translate("profileEditor.openProfileSettings", "Open profile settings")
                            onClicked: profileSettingsPopup.open()
                            background: Rectangle {
                                color: openProfileSettingsButton.down || openProfileSettingsButton.isPressed ? Qt.darker(Theme.surfaceColor, 1.2) : Qt.rgba(1, 1, 1, 0.05)
                                radius: Theme.scaled(8)
                                border.width: 1
                                border.color: Theme.textSecondaryColor
                            }
                            contentItem: Text {
                                text: openProfileSettingsButton.text
                                font: Theme.captionFont
                                color: Theme.textColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        AccessibleButton {
                            id: openLimitsButton
                            Layout.fillWidth: true
                            text: {
                                profileEditorPage.stepVersion
                                if (!profileEditorPage.profile) return TranslationManager.translate("profileEditor.limits", "Limits")
                                var parts = []
                                if (profileEditorPage.profile.target_weight > 0) parts.push(profileEditorPage.profile.target_weight.toFixed(0) + "g")
                                if (profileEditorPage.profile.target_volume > 0) parts.push(profileEditorPage.profile.target_volume.toFixed(0) + "ml")
                                var stopAtValue = parts.length > 0 ? parts.join(" / ") : TranslationManager.translate("profileEditor.off", "off")
                                return TranslationManager.translate("profileEditor.limits", "Limits") + " (" + stopAtValue + ")"
                            }
                            accessibleName: TranslationManager.translate("profileEditor.openLimits", "Open limits settings")
                            onClicked: limitsPopup.open()
                            background: Rectangle {
                                color: openLimitsButton.down || openLimitsButton.isPressed ? Qt.darker(Theme.surfaceColor, 1.2) : Qt.rgba(1, 1, 1, 0.05)
                                radius: Theme.scaled(8)
                                border.width: 1
                                border.color: Theme.textSecondaryColor
                            }
                            contentItem: Text {
                                text: openLimitsButton.text
                                font: Theme.captionFont
                                color: Theme.textColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    // Frame editor
                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        sourceComponent: profileEditorPage.selectedStepIndex >= 0 ? stepEditorComponent : noSelectionComponent
                    }
                }
            }
        }
    }

    // Profile Settings Popup
    DecenzaDialog {
        id: profileSettingsPopup
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))
        padding: Theme.scaled(15)
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        onOpened: {
            profileNameField.text = profileEditorPage.profile ? profileEditorPage.profile.title : ""
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.width: 1
            border.color: Theme.textSecondaryColor
        }

        contentItem: ColumnLayout {
            id: contentColumn
            spacing: Theme.scaled(12)

            Text {
                text: TranslationManager.translate("profileEditor.profileSettingsTitle", "Profile Settings")
                font: Theme.titleFont
                color: Theme.textColor
            }

            // Profile name
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)
                Text { text: TranslationManager.translate("profileEditor.name", "Name"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                StyledTextField {
                    id: profileNameField
                    Accessible.name: TranslationManager.translate("profileEditor.profileName", "Profile name")
                    Layout.fillWidth: true
                    text: profileEditorPage.profile ? profileEditorPage.profile.title : ""
                    font: Theme.bodyFont
                    color: Theme.textColor
                    placeholder: TranslationManager.translate("profileEditor.profileNamePlaceholder", "Profile name")
                    leftPadding: Theme.scaled(12)
                    rightPadding: Theme.scaled(12)
                    topPadding: Theme.scaled(10)
                    bottomPadding: Theme.scaled(10)
                    background: Rectangle {
                        color: Theme.backgroundColor
                        radius: Theme.scaled(4)
                        border.color: profileNameField.activeFocus ? Theme.primaryColor : Theme.borderColor
                        border.width: 1
                    }
                    onEditingFinished: {
                        if (profileEditorPage.profile && text.length > 0 && text !== profileEditorPage.profile.title) {
                            profileEditorPage.profile.title = text
                            profileEditorPage.updatePageTitle()
                            profileEditorPage.uploadProfile()
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderColor }

            // Global temperature (applies to all frames)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)
                RowLayout { Layout.fillWidth: true
                    Text { text: TranslationManager.translate("profileEditor.allTemps", "All temps"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    Item { Layout.fillWidth: true }
                    Text { text: profileEditorPage.stepVersion >= 0 && profileEditorPage.profile && profileEditorPage.profile.steps.length > 0 ? Theme.formatTemperature(profileEditorPage.profile.steps[0].temperature, 1) : Theme.formatTemperature(93.0, 1); font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.temperatureColor }
                }
                ValueInput {
                    Layout.fillWidth: true; valueColor: Theme.temperatureColor
                    accessibleName: TranslationManager.translate("profileEditor.globalTemperature", "Global temperature"); from: Theme.cToDisplay(70); to: Theme.cToDisplay(100); stepSize: 0.1; suffix: Theme.tempUnitSuffix()
                    // Stored in Celsius; shown and entered in the user's unit.
                    value: { profileEditorPage.stepVersion; return Theme.cToDisplay(profileEditorPage.profile && profileEditorPage.profile.steps.length > 0 ? profileEditorPage.profile.steps[0].temperature : 93) }
                    // onValueModified mutates the profile per adjustment tick so the UI
                    // reflects the change live; onValueCommitted fires the BLE upload
                    // once on release instead of per tick.
                    onValueModified: function(newValue) {
                        if (profileEditorPage.profile && profileEditorPage.profile.steps.length > 0) {
                            // newValue is in the display unit; convert to Celsius for storage.
                            var rounded = Math.round(Theme.displayToC(newValue) * 10) / 10
                            var delta = rounded - profileEditorPage.profile.steps[0].temperature
                            for (var i = 0; i < profileEditorPage.profile.steps.length; i++) {
                                profileEditorPage.profile.steps[i].temperature += delta
                            }
                            profileEditorPage.profile.espresso_temperature = rounded
                        }
                    }
                    onValueCommitted: profileEditorPage.uploadProfile()
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderColor }

            // Recommended dose
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)
                RowLayout { Layout.fillWidth: true
                    Text { text: TranslationManager.translate("profileEditor.dose", "Recommended dose"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    Item { Layout.fillWidth: true }
                    StyledSwitch {
                        id: recommendedDoseSwitch
                        checked: profileEditorPage.profile ? !!profileEditorPage.profile.has_recommended_dose : false
                        onToggled: { if (profileEditorPage.profile) { profileEditorPage.profile.has_recommended_dose = checked; profileEditorPage.uploadProfile() } }
                        accessibleName: TranslationManager.translate("profileEditor.toggleRecommendedDose", "Toggle recommended dose")
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: recommendedDoseSwitch.checked
                    spacing: Theme.scaled(2)
                    ValueInput {
                        Layout.fillWidth: true; valueColor: Theme.weightColor
                        accessibleName: TranslationManager.translate("profileEditor.recommendedDose", "Recommended dose"); from: 5; to: 100; stepSize: 0.1; suffix: " g"
                        value: { profileEditorPage.stepVersion; return profileEditorPage.profile ? (profileEditorPage.profile.recommended_dose ?? 18) : 18 }
                        onValueModified: function(newValue) { if (profileEditorPage.profile) { profileEditorPage.profile.recommended_dose = Math.round(newValue * 10) / 10 } }
                        onValueCommitted: profileEditorPage.uploadProfile()
                    }
                }
            }

            // Close button
            AccessibleButton {
                id: doneButton
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(6)
                text: TranslationManager.translate("profileEditor.done", "Done")
                accessibleName: TranslationManager.translate("profileEditor.closeProfileSettings", "Close profile settings")
                onClicked: profileSettingsPopup.close()
                background: Rectangle {
                    implicitHeight: Theme.scaled(44)
                    radius: Theme.buttonRadius
                    color: doneButton.down || doneButton.isPressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                }
                contentItem: Text {
                    text: doneButton.text
                    font: Theme.bodyFont
                    color: Theme.primaryContrastColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // Limits Popup
    DecenzaDialog {
        id: limitsPopup
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(450))
        padding: Theme.scaled(15)
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.width: 1
            border.color: Theme.textSecondaryColor
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)

            Text {
                text: TranslationManager.translate("profileEditor.limitsTitle", "Limits")
                font: Theme.titleFont
                color: Theme.textColor
            }

            // Preheat water tank
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Text { Layout.fillWidth: true; text: TranslationManager.translate("profileEditor.preheatTank", "Preheat tank"); font: Theme.captionFont; color: Theme.temperatureColor; verticalAlignment: Text.AlignVCenter }
                ValueInput {
                    Layout.preferredWidth: Theme.scaled(160); valueColor: Theme.temperatureColor
                    accessibleName: TranslationManager.translate("profileEditor.preheatTankAccessible", "Preheat water tank temperature")
                    from: Theme.cToDisplay(0); to: Theme.cToDisplay(45); stepSize: 1; suffix: Theme.tempUnitSuffix()
                    // Stored in Celsius; shown and entered in the user's unit.
                    value: { profileEditorPage.stepVersion; return Theme.cToDisplay(profileEditorPage.profile ? (profileEditorPage.profile.tank_desired_water_temperature ?? 0) : 0) }
                    onValueModified: function(newValue) {
                        if (profileEditorPage.profile) {
                            profileEditorPage.profile.tank_desired_water_temperature = Math.round(Theme.displayToC(newValue))
                        }
                    }
                    onValueCommitted: profileEditorPage.uploadProfile()
                }
            }

            // Preinfusion ends after
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Text { Layout.fillWidth: true; text: TranslationManager.translate("profileEditor.preinfusionEnds", "Preinfusion ends after step"); font: Theme.captionFont; color: Theme.textSecondaryColor; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap }
                ValueInput {
                    Layout.preferredWidth: Theme.scaled(160)
                    accessibleName: TranslationManager.translate("profileEditor.preinfusionEndsAccessible", "Preinfusion ends after step")
                    from: 0; to: profileEditorPage.profile ? profileEditorPage.profile.steps.length : 0; stepSize: 1
                    value: { profileEditorPage.stepVersion; return profileEditorPage.profile ? (profileEditorPage.profile.preinfuse_frame_count ?? 0) : 0 }
                    onValueModified: function(newValue) {
                        if (profileEditorPage.profile) {
                            profileEditorPage.profile.preinfuse_frame_count = Math.round(newValue)
                        }
                    }
                    onValueCommitted: profileEditorPage.uploadProfile()
                }
            }

            // Stop at volume
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileEditor.stopAtVolume", "Stop at volume")
                    font: Theme.captionFont; color: profileEditorPage.profile && profileEditorPage.profile.target_volume > 0 ? Theme.flowColor : Theme.textSecondaryColor
                    verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap
                }
                ValueInput {
                    Layout.preferredWidth: Theme.scaled(160); valueColor: Theme.flowColor
                    accessibleName: TranslationManager.translate("profileEditor.afterPreinfusionStopAccessible", "After preinfusion, stop the shot at volume")
                    from: 0; to: 500; stepSize: 1; suffix: " mL"
                    displayText: { profileEditorPage.stepVersion; return profileEditorPage.profile && profileEditorPage.profile.target_volume <= 0 ? TranslationManager.translate("profileEditor.off", "off") : "" }
                    value: { profileEditorPage.stepVersion; return profileEditorPage.profile ? (profileEditorPage.profile.target_volume || 0) : 0 }
                    onValueModified: function(newValue) {
                        if (profileEditorPage.profile) {
                            profileEditorPage.profile.target_volume = Math.round(newValue)
                            profileEditorPage.stepVersion++
                        }
                    }
                    onValueCommitted: profileEditorPage.uploadProfile()
                }
            }

            // Stop at weight
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileEditor.stopAtWeight", "Stop at weight")
                    font: Theme.captionFont; color: profileEditorPage.profile && profileEditorPage.profile.target_weight > 0 ? Theme.weightColor : Theme.textSecondaryColor
                    verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap
                }
                ValueInput {
                    Layout.preferredWidth: Theme.scaled(160); valueColor: Theme.weightColor
                    accessibleName: TranslationManager.translate("profileEditor.stopAtWeightAccessible", "Stop at weight")
                    from: 0; to: 500; stepSize: 0.1; suffix: " g"
                    displayText: { profileEditorPage.stepVersion; return profileEditorPage.profile && profileEditorPage.profile.target_weight <= 0 ? TranslationManager.translate("profileEditor.off", "off") : "" }
                    value: { profileEditorPage.stepVersion; return profileEditorPage.profile ? (profileEditorPage.profile.target_weight || 0) : 0 }
                    onValueModified: function(newValue) {
                        if (profileEditorPage.profile) {
                            profileEditorPage.profile.target_weight = Math.round(newValue * 10) / 10
                            profileEditorPage.stepVersion++
                        }
                    }
                    onValueCommitted: profileEditorPage.uploadProfile()
                }
            }

            // Limit flow range (applied to pressure-pump steps)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Text { Layout.fillWidth: true; text: TranslationManager.translate("profileEditor.limitFlowRange", "Flow range limit"); font: Theme.captionFont; color: Theme.flowColor; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap }
                ValueInput {
                    Layout.preferredWidth: Theme.scaled(160); valueColor: Theme.flowColor
                    accessibleName: TranslationManager.translate("profileEditor.limitFlowRangeAccessible", "Limit flow range for pressure steps")
                    from: 0; to: 8; stepSize: 0.01; suffix: " mL/s"
                    value: { profileEditorPage.stepVersion; return profileEditorPage.profile ? (profileEditorPage.profile.maximum_flow_range_advanced ?? 0.6) : 0.6 }
                    onValueModified: function(newValue) {
                        if (profileEditorPage.profile) {
                            var newRange = Math.round(newValue * 100) / 100
                            profileEditorPage.profile.maximum_flow_range_advanced = newRange
                            profileEditorPage.applyRangeToAllSteps()
                        }
                    }
                    onValueCommitted: profileEditorPage.uploadProfile()
                }
            }

            // Limit pressure range (applied to flow-pump steps)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Text { Layout.fillWidth: true; text: TranslationManager.translate("profileEditor.limitPressureRange", "Pressure range limit"); font: Theme.captionFont; color: Theme.pressureColor; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap }
                ValueInput {
                    Layout.preferredWidth: Theme.scaled(160); valueColor: Theme.pressureColor
                    accessibleName: TranslationManager.translate("profileEditor.limitPressureRangeAccessible", "Limit pressure range for flow steps")
                    from: 0; to: 8; stepSize: 0.01; suffix: " bar"
                    value: { profileEditorPage.stepVersion; return profileEditorPage.profile ? (profileEditorPage.profile.maximum_pressure_range_advanced ?? 0.6) : 0.6 }
                    onValueModified: function(newValue) {
                        if (profileEditorPage.profile) {
                            var newRange = Math.round(newValue * 100) / 100
                            profileEditorPage.profile.maximum_pressure_range_advanced = newRange
                            profileEditorPage.applyRangeToAllSteps()
                        }
                    }
                    onValueCommitted: profileEditorPage.uploadProfile()
                }
            }

            // Close button
            AccessibleButton {
                Layout.alignment: Qt.AlignRight
                text: TranslationManager.translate("profileEditor.done", "Done")
                primary: true
                accessibleName: TranslationManager.translate("profileEditor.closeLimits", "Close limits settings")
                onClicked: limitsPopup.close()
            }
        }
    }

    // Bottom bar — counteract keyboard shift so it stays at screen bottom (behind keyboard)
    BottomBar {
        id: bottomBar
        transform: Translate { y: keyboardContainer.keyboardOffset }
        title: profileEditorPage.profile ? profileEditorPage.profile.title : TranslationManager.translate("profileEditor.profile", "Profile")
        onBackClicked: profileEditorPage.handleBack()

        // Read-only indicator
        Text {
            text: TranslationManager.translate("profileEditor.readOnly", "Read-Only")
            color: Theme.warningColor
            font: Theme.bodyFont
            visible: ProfileManager.isCurrentProfileReadOnly
        }

        // Modified indicator
        Tr {
            key: "profileeditor.status.modified"
            fallback: "\u2022 Modified"
            color: Theme.modifiedIndicatorColor
            font: Theme.bodyFont
            visible: profileEditorPage.profileModified && !ProfileManager.isCurrentProfileReadOnly
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: bottomBar.contentColor; opacity: 0.3; visible: profileEditorPage.profile }
        Text {
            text: profileEditorPage.profile ? profileEditorPage.profile.steps.length + " " + TranslationManager.translate("profileEditor.frames", "frames") : ""
            color: bottomBar.contentColor
            font: Theme.bodyFont
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: bottomBar.contentColor; opacity: 0.3; visible: profileEditorPage.profile }
        Text {
            text: {
                if (!profileEditorPage.profile) return ""
                var parts = []
                if (profileEditorPage.profile.target_weight > 0) parts.push(profileEditorPage.profile.target_weight.toFixed(0) + "g")
                if (profileEditorPage.profile.target_volume > 0) parts.push(profileEditorPage.profile.target_volume.toFixed(0) + "ml")
                return parts.length > 0 ? parts.join(" / ") : TranslationManager.translate("profileEditor.off", "off")
            }
            color: {
                if (!profileEditorPage.profile) return bottomBar.contentColor
                var hasWeight = profileEditorPage.profile.target_weight > 0
                var hasVolume = profileEditorPage.profile.target_volume > 0
                if (hasWeight && !hasVolume) return Theme.weightColor
                if (hasVolume && !hasWeight) return Theme.flowColor
                return bottomBar.contentColor  // both set — neutral color
            }
            font: Theme.bodyFont
        }
        AccessibleButton {
            id: doneButton2
            text: TranslationManager.translate("profileEditor.doneButton", "Done")
            accessibleName: TranslationManager.translate("profileEditor.finishEditing", "Finish editing profile")
            onClicked: {
                profileEditorPage.flushPendingEdits()
                if (profileEditorPage.profileModified) {
                    exitDialog.open()
                } else {
                    AppShell.backRequested()
                }
            }
            // White button with primary text for bottom bar
            background: Rectangle {
                implicitWidth: Math.max(Theme.scaled(80), doneText.implicitWidth + Theme.scaled(32))
                implicitHeight: Theme.scaled(36)
                radius: Theme.scaled(6)
                color: doneButton2.down || doneButton2.isPressed ? Qt.darker(Theme.primaryContrastColor, 1.1) : Theme.primaryContrastColor
            }
            contentItem: Text {
                id: doneText
                text: doneButton2.text
                font.pixelSize: Theme.scaled(14)
                font.family: Theme.bodyFont.family
                color: Theme.primaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    } // KeyboardAwareContainer

    // Save As dialog - just title input, filename derived automatically
    DecenzaDialog {
        id: saveAsDialog
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Theme.scaled(400)
        modal: true
        padding: 0

        property string pendingFilename: ""

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            Text {
                text: TranslationManager.translate("profileEditor.saveProfileAs", "Save Profile As")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            Text {
                text: TranslationManager.translate("profileeditor.label.profiletitle", "Profile Title")
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(10)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            StyledTextField {
                id: saveAsTitleField
                Accessible.name: TranslationManager.translate("profileEditor.profileName", "Profile name")
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(6)
                text: profileEditorPage.profile ? profileEditorPage.profile.title : ""
                font: Theme.bodyFont
                color: Theme.textColor
                leftPadding: Theme.scaled(12)
                rightPadding: Theme.scaled(12)
                topPadding: Theme.scaled(12)
                bottomPadding: Theme.scaled(12)
                background: Rectangle {
                    color: Theme.backgroundColor
                    radius: Theme.scaled(4)
                    border.color: saveAsTitleField.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                    border.width: 1
                }
                onAccepted: saveAsDialog.doSave()
            }

            RowLayout {
                spacing: Theme.scaled(10)
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)

                AccessibleButton {
                    text: TranslationManager.translate("profileEditor.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("profileEditor.cancelSave", "Cancel save")
                    Layout.fillWidth: true
                    onClicked: saveAsDialog.close()
                }

                AccessibleButton {
                    text: TranslationManager.translate("profileEditor.save", "Save")
                    accessibleName: TranslationManager.translate("profileEditor.saveProfile", "Save profile")
                    primary: true
                    Layout.fillWidth: true
                    onClicked: saveAsDialog.doSave()
                }
            }
        }

        function doSave() {
            Keyboard.commit()
            if (saveAsTitleField.text.length > 0) {
                var filename = ProfileManager.titleToFilename(saveAsTitleField.text)
                if (ProfileManager.isBuiltInFilename(filename)) {
                    saveAsDialog.close()
                    builtInNameDialog.open()
                    return
                }
                if (ProfileManager.profileExists(filename) && filename !== profileEditorPage.originalProfileName) {
                    saveAsDialog.pendingFilename = filename
                    saveAsDialog.close()
                    overwriteDialog.open()
                    return
                }
                if (profileEditorPage.saveProfileAs(filename, saveAsTitleField.text)) {
                    AppShell.backRequested()
                } else {
                    saveErrorDialog.open()
                }
            }
            saveAsDialog.close()
        }

        onOpened: {
            saveAsTitleField.text = profileEditorPage.profile ? profileEditorPage.profile.title : ""
            saveAsTitleField.forceActiveFocus()
        }
    }

    // Overwrite confirmation dialog
    DecenzaDialog {
        id: overwriteDialog
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
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

            Text {
                text: TranslationManager.translate("profileEditor.profileExists", "Profile Exists")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            Text {
                text: TranslationManager.translate("profileeditor.dialog.overwriteconfirm", "A profile with this name already exists.\nDo you want to overwrite it?")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(10)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
            }

            RowLayout {
                spacing: Theme.scaled(10)
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)

                AccessibleButton {
                    text: TranslationManager.translate("profileEditor.no", "No")
                    accessibleName: TranslationManager.translate("profileEditor.cancelOverwrite", "Cancel overwrite")
                    Layout.fillWidth: true
                    onClicked: overwriteDialog.close()
                }

                AccessibleButton {
                    text: TranslationManager.translate("profileEditor.yes", "Yes")
                    accessibleName: TranslationManager.translate("profileEditor.confirmOverwrite", "Confirm overwrite")
                    destructive: true
                    Layout.fillWidth: true
                    onClicked: {
                        overwriteDialog.close()
                        if (profileEditorPage.saveProfileAs(saveAsDialog.pendingFilename, saveAsTitleField.text)) {
                            AppShell.backRequested()
                        } else {
                            saveErrorDialog.open()
                        }
                    }
                }
            }
        }
    }

    // Built-in profile name collision dialog
    DecenzaDialog {
        id: builtInNameDialog
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))
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
                text: TranslationManager.translate("profileEditor.reservedName", "Reserved Name")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            Text {
                text: TranslationManager.translate("profileEditor.reservedNameMessage", "This name is reserved for a built-in profile. Please choose a different name.")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(10)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
            }

            AccessibleButton {
                text: TranslationManager.translate("common.button.ok", "OK")
                accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                onClicked: {
                    builtInNameDialog.close()
                    saveAsDialog.open()
                }
            }
        }
    }

    // Save error dialog
    DecenzaDialog {
        id: saveErrorDialog
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Theme.scaled(350)
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
                text: TranslationManager.translate("profileEditor.saveError", "Save Failed")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            Text {
                text: TranslationManager.translate("profileeditor.dialog.saveerror", "Could not save the profile. Please try again or use Save As with a different name.")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(10)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
            }

            AccessibleButton {
                text: TranslationManager.translate("profileEditor.ok", "OK")
                accessibleName: TranslationManager.translate("profileEditor.dismissError", "Dismiss error")
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                onClicked: saveErrorDialog.close()
            }
        }
    }

    // Exit dialog for unsaved changes
    UnsavedChangesDialog {
        id: exitDialog
        itemType: "profile"
        canSave: profileEditorPage.originalProfileName !== "" && !ProfileManager.isCurrentProfileReadOnly
        showTry: true
        onDiscardClicked: {
            if (profileEditorPage.originalProfileName) {
                ProfileManager.loadProfile(profileEditorPage.originalProfileName)
            }
            AppShell.backRequested()
        }
        onTryClicked: {
            ProfileManager.uploadCurrentProfile()
            AppShell.backRequested()
        }
        onSaveAsClicked: saveAsDialog.open()
        onSaveClicked: {
            if (profileEditorPage.saveProfile()) {
                AppShell.backRequested()
            } else {
                saveErrorDialog.open()
            }
        }
    }

    // No selection placeholder
    Component {
        id: noSelectionComponent

        Item {
            Column {
                anchors.centerIn: parent
                spacing: Theme.scaled(15)

                Tr {
                    key: "profileeditor.noselection.title"
                    fallback: "Select a frame"
                    font: Theme.titleFont
                    color: Theme.textSecondaryColor
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Tr {
                    key: "profileeditor.noselection.hint"
                    fallback: "Click on the graph to select\na frame for editing"
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }

    // Frame editor component
    Component {
        id: stepEditorComponent

        ScrollView {
            id: stepEditorScroll
            clip: true
            contentWidth: availableWidth

            property var step: (profileEditorPage.stepVersion >= 0) && profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0 && profileEditorPage.selectedStepIndex < profileEditorPage.profile.steps.length ?
                   profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex] : null

            ColumnLayout {
                width: stepEditorScroll.width - Theme.scaled(24)
                spacing: Theme.scaled(4)

                // Frame name
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    Text { text: TranslationManager.translate("profileEditor.frameName", "Frame name"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    StyledTextField {
                        Accessible.name: TranslationManager.translate("profileEditor.frameName", "Frame name")
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        text: { var v = profileEditorPage.stepVersion; return stepEditorScroll.step ? stepEditorScroll.step.name : "" }
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                        color: Theme.textColor
                        leftPadding: Theme.scaled(12); rightPadding: Theme.scaled(12); topPadding: Theme.scaled(10); bottomPadding: Theme.scaled(10)
                        background: Rectangle { color: Theme.backgroundColor; radius: Theme.scaled(4); border.color: parent.activeFocus ? Theme.primaryColor : Theme.borderColor; border.width: 1 }
                        onEditingFinished: { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0 && profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].name !== text) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].name = text; profileEditorPage.uploadProfile() } }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderColor }

                // ── Section 1: Temperature ──
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    Text { text: "1:"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.primaryColor }
                    Text { text: TranslationManager.translate("profileEditor.temperature", "Temperature"); font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.textColor }
                }

                // Temperature
                ValueInput { Layout.fillWidth: true; valueColor: Theme.temperatureColor; accessibleName: TranslationManager.translate("profileEditor.stepTemperature", "Step temperature"); from: Theme.cToDisplay(70); to: Theme.cToDisplay(100); stepSize: 0.1; suffix: Theme.tempUnitSuffix(); value: Theme.cToDisplay(profileEditorPage.stepVersion >= 0 && stepEditorScroll.step ? stepEditorScroll.step.temperature : 93); onValueModified: function(newValue) { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].temperature = Math.round(Theme.displayToC(newValue) * 10) / 10 } }; onValueCommitted: profileEditorPage.uploadProfile() }

                // Sensor toggle
                Text { text: TranslationManager.translate("profileEditor.sensor", "Sensor"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Theme.scaled(28)
                        radius: Theme.scaled(6)
                        color: stepEditorScroll.step && stepEditorScroll.step.sensor !== "water" ? Theme.temperatureColor : Theme.insetBackgroundColor
                        border.width: stepEditorScroll.step && stepEditorScroll.step.sensor !== "water" ? 0 : 1; border.color: Theme.borderColor
                        Accessible.role: Accessible.Button; Accessible.name: "Sensor: Coffee"; Accessible.focusable: true
                        Accessible.onPressAction: sensorCoffeeArea.clicked(null)
                        Text { anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.coffee", "Coffee"); font: Theme.captionFont; color: stepEditorScroll.step && stepEditorScroll.step.sensor !== "water" ? Theme.primaryContrastColor : Theme.textSecondaryColor; Accessible.ignored: true }
                        MouseArea { id: sensorCoffeeArea; anchors.fill: parent; onClicked: { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].sensor = "coffee"; profileEditorPage.uploadProfile() } } }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Theme.scaled(28)
                        radius: Theme.scaled(6)
                        color: stepEditorScroll.step && stepEditorScroll.step.sensor === "water" ? Theme.flowColor : Theme.insetBackgroundColor
                        border.width: stepEditorScroll.step && stepEditorScroll.step.sensor === "water" ? 0 : 1; border.color: Theme.borderColor
                        Accessible.role: Accessible.Button; Accessible.name: "Sensor: Water"; Accessible.focusable: true
                        Accessible.onPressAction: sensorWaterArea.clicked(null)
                        Text { anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.water", "Water"); font: Theme.captionFont; color: stepEditorScroll.step && stepEditorScroll.step.sensor === "water" ? Theme.primaryContrastColor : Theme.textSecondaryColor; Accessible.ignored: true }
                        MouseArea { id: sensorWaterArea; anchors.fill: parent; onClicked: { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].sensor = "water"; profileEditorPage.uploadProfile() } } }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderColor }

                // ── Section 2: Goal ──
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    Text { text: "2:"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.primaryColor }
                    Text { text: TranslationManager.translate("profileEditor.goal", "Goal"); font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.textColor }
                }

                // Step goal toggle (pressure or flow)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Theme.scaled(28)
                        radius: Theme.scaled(6)
                        color: stepEditorScroll.step && stepEditorScroll.step.pump === "pressure" ? Theme.pressureColor : Theme.insetBackgroundColor
                        border.width: stepEditorScroll.step && stepEditorScroll.step.pump === "pressure" ? 0 : 1; border.color: Theme.borderColor
                        Accessible.role: Accessible.Button; Accessible.name: "Goal: Pressure"; Accessible.focusable: true
                        Accessible.onPressAction: goalPressureArea.clicked(null)
                        Text { anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.pressure", "Pressure"); font: Theme.captionFont; color: stepEditorScroll.step && stepEditorScroll.step.pump === "pressure" ? Theme.primaryContrastColor : Theme.textSecondaryColor; Accessible.ignored: true }
                        MouseArea { id: goalPressureArea; anchors.fill: parent; onClicked: { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].pump = "pressure"; if (!(profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].max_flow_or_pressure > 0)) profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].max_flow_or_pressure = ProfileManager.defaultPressureFlowLimit; profileEditorPage.uploadProfile() } } }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Theme.scaled(28)
                        radius: Theme.scaled(6)
                        color: stepEditorScroll.step && stepEditorScroll.step.pump === "flow" ? Theme.flowColor : Theme.insetBackgroundColor
                        border.width: stepEditorScroll.step && stepEditorScroll.step.pump === "flow" ? 0 : 1; border.color: Theme.borderColor
                        Accessible.role: Accessible.Button; Accessible.name: "Goal: Flow"; Accessible.focusable: true
                        Accessible.onPressAction: goalFlowArea.clicked(null)
                        Text { anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.flow", "Flow"); font: Theme.captionFont; color: stepEditorScroll.step && stepEditorScroll.step.pump === "flow" ? Theme.primaryContrastColor : Theme.textSecondaryColor; Accessible.ignored: true }
                        MouseArea { id: goalFlowArea; anchors.fill: parent; onClicked: { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].pump = "flow"; profileEditorPage.uploadProfile() } } }
                    }
                }

                // Pressure/Flow goal (switches based on pump mode)
                ValueInput {
                    Layout.fillWidth: true
                    valueColor: stepEditorScroll.step && stepEditorScroll.step.pump === "flow" ? Theme.flowColor : Theme.pressureColor
                    accessibleName: stepEditorScroll.step && stepEditorScroll.step.pump === "flow" ? "Flow goal" : "Pressure goal"
                    from: 0; to: stepEditorScroll.step && stepEditorScroll.step.pump === "flow" ? ProfileManager.maxSettableFlow : 12; stepSize: 0.01
                    suffix: stepEditorScroll.step && stepEditorScroll.step.pump === "flow" ? " mL/s" : " bar"
                    value: { var v = profileEditorPage.stepVersion; return stepEditorScroll.step ? (stepEditorScroll.step.pump === "flow" ? stepEditorScroll.step.flow : stepEditorScroll.step.pressure) : 0 }
                    onValueModified: function(newValue) {
                        if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) {
                            var val = Math.round(newValue * 100) / 100
                            if (profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].pump === "flow") {
                                profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].flow = val
                            } else {
                                profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].pressure = val
                            }
                        }
                    }
                    onValueCommitted: profileEditorPage.uploadProfile()
                }

                // Transition toggle
                Text { text: TranslationManager.translate("profileEditor.transition", "Transition"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Theme.scaled(28)
                        radius: Theme.scaled(6)
                        color: stepEditorScroll.step && stepEditorScroll.step.transition !== "smooth" ? Theme.primaryColor : Theme.insetBackgroundColor
                        border.width: stepEditorScroll.step && stepEditorScroll.step.transition !== "smooth" ? 0 : 1; border.color: Theme.borderColor
                        Accessible.role: Accessible.Button; Accessible.name: "Transition: Fast"; Accessible.focusable: true
                        Accessible.onPressAction: transitionFastArea.clicked(null)
                        Text { anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.fast", "Fast"); font: Theme.captionFont; color: stepEditorScroll.step && stepEditorScroll.step.transition !== "smooth" ? Theme.primaryContrastColor : Theme.textSecondaryColor; Accessible.ignored: true }
                        MouseArea { id: transitionFastArea; anchors.fill: parent; onClicked: { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].transition = "fast"; profileEditorPage.uploadProfile() } } }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Theme.scaled(28)
                        radius: Theme.scaled(6)
                        color: stepEditorScroll.step && stepEditorScroll.step.transition === "smooth" ? Theme.primaryColor : Theme.insetBackgroundColor
                        border.width: stepEditorScroll.step && stepEditorScroll.step.transition === "smooth" ? 0 : 1; border.color: Theme.borderColor
                        Accessible.role: Accessible.Button; Accessible.name: "Transition: Smooth"; Accessible.focusable: true
                        Accessible.onPressAction: transitionSmoothArea.clicked(null)
                        Text { anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.smooth", "Smooth"); font: Theme.captionFont; color: stepEditorScroll.step && stepEditorScroll.step.transition === "smooth" ? Theme.primaryContrastColor : Theme.textSecondaryColor; Accessible.ignored: true }
                        MouseArea { id: transitionSmoothArea; anchors.fill: parent; onClicked: { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].transition = "smooth"; profileEditorPage.uploadProfile() } } }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderColor }

                // ── Section 3: Maximum ──
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    Text { text: "3:"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.primaryColor }
                    Text { text: TranslationManager.translate("profileEditor.maximum", "Maximum"); font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.textColor }
                }

                // Max duration
                Text { text: TranslationManager.translate("profileEditor.maxDuration", "Max duration"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                ValueInput { Layout.fillWidth: true; accessibleName: TranslationManager.translate("profileEditor.maxDuration", "Max duration"); from: 0; to: 120; stepSize: 1; suffix: " s"; displayText: profileEditorPage.stepVersion >= 0 && stepEditorScroll.step && stepEditorScroll.step.seconds === 0 ? TranslationManager.translate("profileEditor.off", "off") : ""; value: profileEditorPage.stepVersion >= 0 && stepEditorScroll.step ? stepEditorScroll.step.seconds : 30; onValueModified: function(newValue) { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].seconds = Math.round(newValue) } }; onValueCommitted: profileEditorPage.uploadProfile() }

                // Max volume
                Text { text: TranslationManager.translate("profileEditor.maxVolume", "Volume"); font: Theme.captionFont; color: Theme.flowColor }
                ValueInput { Layout.fillWidth: true; valueColor: Theme.flowColor; accessibleName: TranslationManager.translate("profileEditor.maxVolume.accessible", "Max volume"); from: 0; to: 500; stepSize: 1; suffix: " mL"; displayText: profileEditorPage.stepVersion >= 0 && stepEditorScroll.step && (stepEditorScroll.step.volume || 0) === 0 ? TranslationManager.translate("profileEditor.off", "off") : ""; value: profileEditorPage.stepVersion >= 0 && stepEditorScroll.step ? (stepEditorScroll.step.volume || 0) : 0; onValueModified: function(newValue) { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].volume = Math.round(newValue) } }; onValueCommitted: profileEditorPage.uploadProfile() }

                // Max weight (independent, app-side exit)
                Text { text: TranslationManager.translate("profileEditor.maxWeight", "Weight"); font: Theme.captionFont; color: Theme.weightColor }
                ValueInput { Layout.fillWidth: true; valueColor: Theme.weightColor; accessibleName: TranslationManager.translate("profileEditor.maxWeight.accessible", "Max weight"); from: 0; to: 500; stepSize: 0.1; suffix: " g"; displayText: profileEditorPage.stepVersion >= 0 && stepEditorScroll.step && (stepEditorScroll.step.exit_weight || 0) === 0 ? TranslationManager.translate("profileEditor.off", "off") : ""; value: profileEditorPage.stepVersion >= 0 && stepEditorScroll.step ? (stepEditorScroll.step.exit_weight || 0) : 0; onValueModified: function(newValue) { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].exit_weight = Math.round(newValue * 10) / 10 } }; onValueCommitted: profileEditorPage.uploadProfile() }

                // Flow/Pressure limit (opposite of goal in section 2)
                Text { text: stepEditorScroll.step && stepEditorScroll.step.pump === "pressure" ? TranslationManager.translate("profileEditor.maxFlow", "Flow limit") : TranslationManager.translate("profileEditor.maxPressure", "Pressure limit"); font: Theme.captionFont; color: stepEditorScroll.step && stepEditorScroll.step.pump === "pressure" ? Theme.flowColor : Theme.pressureColor }
                ValueInput {
                    Layout.fillWidth: true
                    valueColor: stepEditorScroll.step && stepEditorScroll.step.pump === "pressure" ? Theme.flowColor : Theme.pressureColor
                    accessibleName: stepEditorScroll.step && stepEditorScroll.step.pump === "pressure" ? TranslationManager.translate("profileEditor.maxFlow", "Flow limit") : TranslationManager.translate("profileEditor.maxPressure", "Pressure limit")
                    // A pressure step's flow limit can no longer be off: it floors at
                    // 0.1 and a typed 0 snaps to the default. A flow step's limiter is a
                    // PRESSURE limit, where off is still legal, so it keeps 0 and "off".
                    from: stepEditorScroll.step && stepEditorScroll.step.pump === "pressure" ? 0.1 : 0
                    to: stepEditorScroll.step && stepEditorScroll.step.pump === "pressure" ? ProfileManager.maxSettableFlow : 12; stepSize: 0.01
                    snapZeroTo: stepEditorScroll.step && stepEditorScroll.step.pump === "pressure" ? ProfileManager.defaultPressureFlowLimit : 0
                    suffix: stepEditorScroll.step && stepEditorScroll.step.pump === "pressure" ? " mL/s" : " bar"
                    displayText: { var v = profileEditorPage.stepVersion; var val = stepEditorScroll.step ? (stepEditorScroll.step.max_flow_or_pressure || 0) : 0; return (val === 0 && !(stepEditorScroll.step && stepEditorScroll.step.pump === "pressure")) ? TranslationManager.translate("profileEditor.off", "off") : "" }
                    value: { var v = profileEditorPage.stepVersion; return stepEditorScroll.step ? (stepEditorScroll.step.max_flow_or_pressure || 0) : 0 }
                    onValueModified: function(newValue) { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].max_flow_or_pressure = Math.round(newValue * 100) / 100 } }
                    onValueCommitted: profileEditorPage.uploadProfile()
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderColor }

                // ── Section 4: Move on if... ──
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    Text { text: "4:"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.primaryColor }
                    Text { text: TranslationManager.translate("profileEditor.moveOnIf", "Move on if..."); font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.textColor }
                    Item { Layout.fillWidth: true }
                    StyledSwitch {
                        id: exitIfSwitch
                        checked: { var v = profileEditorPage.stepVersion; return stepEditorScroll.step ? stepEditorScroll.step.exit_if : false }
                        onToggled: { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].exit_if = checked; profileEditorPage.uploadProfile() } }
                        accessibleName: TranslationManager.translate("profileEditor.moveOnIfConditionMet", "Move on if condition met")
                    }
                }

                // Exit type selector + value slider
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: exitIfSwitch.checked
                    spacing: Theme.scaled(4)

                    StyledComboBox {
                        id: exitTypeCombo
                        Layout.fillWidth: true
                        model: ["Pressure Over", "Pressure Under", "Flow Over", "Flow Under"]
                        accessibleLabel: TranslationManager.translate("profileEditor.exitType", "Exit type")
                        contentItem: Text { text: exitTypeCombo.displayText; font: Theme.bodyFont; color: Theme.textColor; leftPadding: Theme.scaled(10); verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { implicitHeight: Theme.scaled(36); color: Theme.backgroundColor; radius: Theme.scaled(6); border.width: 1; border.color: Theme.borderColor }
                        currentIndex: { var v = profileEditorPage.stepVersion; if (!stepEditorScroll.step) return 0; switch (stepEditorScroll.step.exit_type) { case "pressure_over": return 0; case "pressure_under": return 1; case "flow_over": return 2; case "flow_under": return 3; default: return 0 } }
                        onActivated: function(index) { if (!profileEditorPage.profile || profileEditorPage.selectedStepIndex < 0) return; var types = ["pressure_over", "pressure_under", "flow_over", "flow_under"]; profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].exit_type = types[index]; profileEditorPage.uploadProfile() }
                    }

                    // Exit value slider
                    Text { text: TranslationManager.translate("profileEditor.exitValue", "Value"); font: Theme.captionFont; color: stepEditorScroll.step && (stepEditorScroll.step.exit_type === "flow_over" || stepEditorScroll.step.exit_type === "flow_under") ? Theme.flowColor : Theme.pressureColor }
                    ValueInput {
                        Layout.fillWidth: true
                        valueColor: stepEditorScroll.step && (stepEditorScroll.step.exit_type === "flow_over" || stepEditorScroll.step.exit_type === "flow_under") ? Theme.flowColor : Theme.pressureColor
                        accessibleName: TranslationManager.translate("profileEditor.exitValue.accessible", "Exit value")
                        from: 0; to: { if (!stepEditorScroll.step) return 12; switch (stepEditorScroll.step.exit_type) { case "flow_over": case "flow_under": return 8; default: return 12 } }
                        stepSize: 0.01
                        suffix: stepEditorScroll.step && (stepEditorScroll.step.exit_type === "flow_over" || stepEditorScroll.step.exit_type === "flow_under") ? " mL/s" : " bar"
                        value: { var v = profileEditorPage.stepVersion; if (!stepEditorScroll.step) return 0; switch (stepEditorScroll.step.exit_type) { case "pressure_over": return stepEditorScroll.step.exit_pressure_over || 0; case "pressure_under": return stepEditorScroll.step.exit_pressure_under || 0; case "flow_over": return stepEditorScroll.step.exit_flow_over || 0; case "flow_under": return stepEditorScroll.step.exit_flow_under || 0; default: return 0 } }
                        onValueModified: function(newValue) {
                            if (!profileEditorPage.profile || profileEditorPage.selectedStepIndex < 0) return
                            var val = Math.round(newValue * 100) / 100
                            switch (profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].exit_type) {
                                case "pressure_over": profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].exit_pressure_over = val; break
                                case "pressure_under": profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].exit_pressure_under = val; break
                                case "flow_over": profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].exit_flow_over = val; break
                                case "flow_under": profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].exit_flow_under = val; break
                            }
                        }
                        onValueCommitted: profileEditorPage.uploadProfile()
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderColor }

                // Popup message
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    Text { text: TranslationManager.translate("profileEditor.popupMessage", "Popup message"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    StyledTextField {
                        Accessible.name: TranslationManager.translate("profileEditor.popupMessage", "Popup message")
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        text: { var v = profileEditorPage.stepVersion; return stepEditorScroll.step ? (stepEditorScroll.step.popup || "") : "" }
                        font: Theme.bodyFont; color: Theme.textColor
                        placeholder: TranslationManager.translate("profileEditor.popupMessagePlaceholder", "e.g., Swirl now, $weight")
                        leftPadding: Theme.scaled(12); rightPadding: Theme.scaled(12); topPadding: Theme.scaled(10); bottomPadding: Theme.scaled(10)
                        background: Rectangle { color: Theme.backgroundColor; radius: Theme.scaled(4); border.color: parent.activeFocus ? Theme.primaryColor : Theme.borderColor; border.width: 1 }
                        onEditingFinished: { if (profileEditorPage.profile && profileEditorPage.selectedStepIndex >= 0) { profileEditorPage.profile.steps[profileEditorPage.selectedStepIndex].popup = text; profileEditorPage.uploadProfile() } }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // Profile name edit dialog
    DecenzaDialog {
        id: profileNameDialog
        parent: Overlay.overlay
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

            Text {
                text: TranslationManager.translate("profileEditor.editProfileName", "Edit Profile Name")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            StyledTextField {
                id: nameField
                Accessible.name: TranslationManager.translate("profileEditor.profileName", "Profile name")
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(10)
                text: profileEditorPage.profile ? profileEditorPage.profile.title : ""
                font: Theme.bodyFont
                color: Theme.textColor
                leftPadding: Theme.scaled(12)
                rightPadding: Theme.scaled(12)
                topPadding: Theme.scaled(12)
                bottomPadding: Theme.scaled(12)
                background: Rectangle {
                    color: Theme.backgroundColor
                    radius: Theme.scaled(4)
                    border.color: nameField.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                    border.width: 1
                }
                onAccepted: profileNameDialog.doSave()
            }

            RowLayout {
                spacing: Theme.scaled(10)
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)

                AccessibleButton {
                    text: TranslationManager.translate("profileEditor.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("profileEditor.cancelRename", "Cancel rename")
                    Layout.fillWidth: true
                    onClicked: profileNameDialog.close()
                }

                AccessibleButton {
                    text: TranslationManager.translate("profileEditor.ok", "OK")
                    accessibleName: TranslationManager.translate("profileEditor.confirmRename", "Confirm rename")
                    Layout.fillWidth: true
                    onClicked: profileNameDialog.doSave()
                }
            }
        }

        function doSave() {
            Keyboard.commit()
            if (profileEditorPage.profile && nameField.text.length > 0) {
                profileEditorPage.profile.title = nameField.text
                profileEditorPage.updatePageTitle()
                profileEditorPage.uploadProfile()
            }
            profileNameDialog.close()
        }

        onOpened: {
            nameField.text = profileEditorPage.profile ? profileEditorPage.profile.title : ""
            nameField.selectAll()
            nameField.forceActiveFocus()
        }
    }

    // Apply limit ranges to all steps (de1app apply_range_to_all_steps behavior)
    // Pressure-pump steps get the flow range limit; flow-pump steps get the pressure range limit
    function applyRangeToAllSteps() {
        if (!profile || !profile.steps) return
        var flowRange = profile.maximum_flow_range_advanced ?? 0.6
        var pressureRange = profile.maximum_pressure_range_advanced ?? 0.6
        for (var i = 0; i < profile.steps.length; i++) {
            if (profile.steps[i].pump === "pressure") {
                profile.steps[i].max_flow_or_pressure_range = flowRange
            } else if (profile.steps[i].pump === "flow") {
                profile.steps[i].max_flow_or_pressure_range = pressureRange
            }
        }
    }

    // Helper functions
    function addStep() {
        if (!profile) return
        if (profile.steps.length >= 20) return  // DE1 max frames limit

        var newStep = {
            name: "Frame " + (profile.steps.length + 1),
            temperature: 93.0,
            sensor: "coffee",
            pump: "pressure",
            transition: "fast",
            pressure: 9.0,
            flow: 2.0,
            seconds: 30.0,
            volume: 0,
            weight: 0,
            exit_if: false,
            exit_type: "pressure_over",
            exit_pressure_over: 0,
            exit_pressure_under: 0,
            exit_flow_over: 0,
            exit_flow_under: 0,
            exit_weight: 0,
            max_flow_or_pressure: 0,
            max_flow_or_pressure_range: 0.6,
            popup: ""
        }

        // Insert after selected frame, or at end
        var insertIndex = selectedStepIndex >= 0 ? selectedStepIndex + 1 : profile.steps.length
        profile.steps.splice(insertIndex, 0, newStep)

        // Force step editor bindings to re-evaluate BEFORE changing selection
        // This ensures the new step's data is properly bound
        stepVersion++

        Qt.callLater(function() { selectedStepIndex = insertIndex })
        // Force graph update by reassigning frames array
        profileGraph.frames = []
        profileGraph.frames = profile.steps
        uploadProfile()
    }

    function duplicateStep(index) {
        if (!profile || index < 0 || index >= profile.steps.length) return
        if (profile.steps.length >= 20) return  // DE1 max frames limit

        var original = profile.steps[index]
        var copy = JSON.parse(JSON.stringify(original))  // Deep copy
        copy.name = original.name + " (copy)"

        profile.steps.splice(index + 1, 0, copy)

        // Force step editor bindings to re-evaluate BEFORE changing selection
        stepVersion++

        Qt.callLater(function() { selectedStepIndex = index + 1 })
        // Force graph update by reassigning frames array
        profileGraph.frames = []
        profileGraph.frames = profile.steps
        uploadProfile()
    }

    function deleteStep(index) {
        if (!profile || index < 0 || index >= profile.steps.length) return

        profile.steps.splice(index, 1)

        if (selectedStepIndex >= profile.steps.length) {
            selectedStepIndex = profile.steps.length - 1
        }

        // Force graph update by reassigning frames array
        profileGraph.frames = []
        profileGraph.frames = profile.steps
        uploadProfile()
    }

    function moveStep(fromIndex, toIndex) {
        if (!profile || fromIndex < 0 || fromIndex >= profile.steps.length) return
        if (toIndex < 0 || toIndex >= profile.steps.length) return

        var step = profile.steps.splice(fromIndex, 1)[0]
        profile.steps.splice(toIndex, 0, step)
        // Force graph update by reassigning frames array
        profileGraph.frames = []
        profileGraph.frames = profile.steps
        // Update selection after frames are reassigned
        selectedStepIndex = toIndex
        profileGraph.selectedFrameIndex = toIndex
        uploadProfile()

        // Announce the move for screen readers
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled) {
            var name = step.name || TranslationManager.translate("profileEditor.unnamed", "unnamed")
            var direction = toIndex < fromIndex ? TranslationManager.translate("profileEditor.left", "left") : TranslationManager.translate("profileEditor.right", "right")
            AccessibilityManager.announce(TranslationManager.translate("profileEditor.movedFrame", "Moved %1 %2 to position %3 of %4").arg(name).arg(direction).arg(toIndex + 1).arg(profile.steps.length))
        }
    }

    function loadCurrentProfile() {
        // Load profile data from ProfileManager
        var loadedProfile = ProfileManager.getCurrentProfile()
        if (loadedProfile && loadedProfile.steps && loadedProfile.steps.length > 0) {
            profile = loadedProfile
        } else {
            // Fallback to empty profile
            profile = {
                title: ProfileManager.currentProfileName || "New Profile",
                steps: [],
                target_weight: ProfileManager.targetWeight || 36,
                target_volume: 0,
                espresso_temperature: 93,
                mode: "frame_based",
                tank_desired_water_temperature: 0,
                maximum_flow_range_advanced: 0.6,
                maximum_pressure_range_advanced: 0.6,
                preinfuse_frame_count: 0
            }
        }
        // Track the original profile filename for saving (not the title!)
        originalProfileName = ProfileManager.baseProfileName || ""
        selectedStepIndex = -1
        updatePageTitle()
        // Force graph to update with new profile data
        if (profile && profile.steps) {
            profileGraph.frames = profile.steps.slice()
        }
    }

    // Reload profile when page becomes active (StackView reactivation)
    StackView.onActivating: {
        loadCurrentProfile()
    }

    Component.onCompleted: {
        loadCurrentProfile()
    }

    StackView.onActivated: {
        updatePageTitle()
        lastAnnouncedFrame = null  // Reset for fresh announcements
        announceProfileInfo()
    }

    function announceProfileInfo() {
        if (typeof AccessibilityManager === "undefined" || AccessibilityManager === null || !AccessibilityManager.enabled) return
        if (!profile) return

        var frameCount = profile.steps ? profile.steps.length : 0
        var title = AccessibilityManager.cleanForSpeech(profile.title || "Untitled")
        var announcement = title + " profile. " + frameCount + " frame" + (frameCount !== 1 ? "s" : "")
        AccessibilityManager.announce(announcement)
    }
}
