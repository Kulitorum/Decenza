import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: profileEditorPage
    objectName: "profileEditorPage"
    background: Rectangle { color: Theme.backgroundColor }

    property var profile: null
    property int selectedStepIndex: -1
    property bool profileModified: MainController.profileModified
    property string originalProfileName: ""
    property int stepVersion: 0  // Increment to force step editor refresh

    // For accessibility: track previously announced frame to only speak differences
    property var lastAnnouncedFrame: null

    // Announce frame when selected
    onSelectedStepIndexChanged: announceFrame()

    function announceFrame() {
        if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
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
                parts.push(step.flow.toFixed(1) + " mL per second")
            } else {
                parts.push(step.pressure.toFixed(1) + " bar")
            }
            parts.push(step.temperature.toFixed(0) + " degrees")
            parts.push(step.seconds.toFixed(0) + " seconds")
            parts.push(step.transition === "smooth" ? "smooth transition" : "fast transition")
        } else {
            // Announce only differences
            if (step.pump !== prev.pump) {
                parts.push(step.pump === "flow" ? "flow mode" : "pressure mode")
            }
            if (step.pump === "flow") {
                if (step.flow !== prev.flow || step.pump !== prev.pump) {
                    parts.push(step.flow.toFixed(1) + " mL per second")
                }
            } else {
                if (step.pressure !== prev.pressure || step.pump !== prev.pump) {
                    parts.push(step.pressure.toFixed(1) + " bar")
                }
            }
            if (step.temperature !== prev.temperature) {
                parts.push(step.temperature.toFixed(0) + " degrees")
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
        root.currentPageTitle = profile ? profile.title : TranslationManager.translate("profileEditor.title", "Profile Editor")
    }

    // Auto-upload profile to machine on any change
    function uploadProfile() {
        if (profile) {
            MainController.uploadProfile(profile)
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
        return MainController.saveProfile(originalProfileName)
    }

    // Save profile with new name. Returns true on success.
    function saveProfileAs(filename, title) {
        if (!profile) return false
        if (MainController.saveProfileAs(filename, title)) {
            originalProfileName = filename
            return true
        }
        return false
    }

    KeyboardAwareContainer {
        id: keyboardContainer
        anchors.fill: parent
        textFields: [profileNotesFieldInline]

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
                color: "white"
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
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(0)
                    spacing: Theme.scaled(0)

                    // Frame toolbar wrapper
                    Rectangle {
                        color: Theme.surfaceColor
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
                                onClicked: addStep()
                            }

                            AccessibleButton {
                                text: TranslationManager.translate("profileEditor.deleteFrame", "Delete")
                                accessibleName: TranslationManager.translate("profileEditor.deleteFrameAccessible", "Delete selected frame")
                                destructive: true
                                enabled: selectedStepIndex >= 0 && profile && profile.steps.length > 1
                                onClicked: deleteStep(selectedStepIndex)
                            }

                            AccessibleButton {
                                primary: true
                                text: TranslationManager.translate("profileEditor.copyFrame", "Copy")
                                accessibleName: TranslationManager.translate("profileEditor.duplicateFrameAccessible", "Duplicate selected frame")
                                enabled: selectedStepIndex >= 0 && profile && profile.steps.length < 20
                                onClicked: duplicateStep(selectedStepIndex)
                            }

                            StyledIconButton {
                                text: "\u2190"
                                accessibleName: TranslationManager.translate("profileEditor.moveFrameLeft", "Move frame left")
                                accessibleDescription: {
                                    if (selectedStepIndex < 0) return TranslationManager.translate("profileEditor.selectFrameFirst", "Select a frame first")
                                    if (selectedStepIndex === 0) return TranslationManager.translate("profileEditor.frameAlreadyFirst", "Frame is already first")
                                    return ""
                                }
                                enabled: selectedStepIndex > 0
                                onClicked: moveStep(selectedStepIndex, selectedStepIndex - 1)
                            }

                            StyledIconButton {
                                text: "\u2192"
                                accessibleName: TranslationManager.translate("profileEditor.moveFrameRight", "Move frame right")
                                accessibleDescription: {
                                    if (selectedStepIndex < 0) return TranslationManager.translate("profileEditor.selectFrameFirst", "Select a frame first")
                                    if (profile && selectedStepIndex >= profile.steps.length - 1) return TranslationManager.translate("profileEditor.frameAlreadyLast", "Frame is already last")
                                    return ""
                                }
                                enabled: selectedStepIndex >= 0 && selectedStepIndex < (profile ? profile.steps.length - 1 : 0)
                                onClicked: moveStep(selectedStepIndex, selectedStepIndex + 1)
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
                            frames: profile ? profile.steps : []
                            selectedFrameIndex: selectedStepIndex
                            targetWeight: profile ? (profile.target_weight || 0) : 0
                            targetVolume: profile ? (profile.target_volume || 0) : 0

                            onFrameSelected: function(index) {
                                selectedStepIndex = index
                            }
                        }
                    }

                    // Profile description
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(90)

                        Text {
                            id: profileDescLabel
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.scaled(4)
                            text: TranslationManager.translate("profileEditor.descriptionLabel", "Description")
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                        }

                        Rectangle {
                            anchors.top: profileDescLabel.bottom
                            anchors.topMargin: Theme.scaled(2)
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            color: Theme.surfaceColor
                            radius: Theme.cardRadius

                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(6)
                                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                                TextArea {
                                    id: profileNotesFieldInline
                                    text: profile ? (profile.profile_notes || "") : ""
                                    font.pixelSize: Theme.scaled(10)
                                    color: Theme.textColor
                                    wrapMode: TextArea.Wrap
                                    leftPadding: Theme.scaled(8)
                                    rightPadding: Theme.scaled(8)
                                    topPadding: Theme.scaled(4)
                                    bottomPadding: Theme.scaled(4)
                                    background: Rectangle {
                                        color: Theme.backgroundColor
                                        radius: Theme.scaled(4)
                                        border.color: profileNotesFieldInline.activeFocus ? Theme.primaryColor : Theme.borderColor
                                        border.width: 1
                                    }
                                    onEditingFinished: {
                                        if (profile && text !== (profile.profile_notes || "")) {
                                            profile.profile_notes = text
                                            uploadProfile()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Right side: Frame editor panel
            Rectangle {
                Layout.preferredWidth: Theme.scaled(320)
                Layout.fillHeight: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(12)

                    // Profile settings + limits buttons
                    RowLayout {
                        Layout.fillWidth: true
                        visible: profile !== null
                        spacing: Theme.scaled(6)

                        AccessibleButton {
                            Layout.fillWidth: true
                            text: {
                                stepVersion
                                if (!profile) return TranslationManager.translate("profileEditor.settings", "Settings")
                                var temp = profile.steps.length > 0 ? profile.steps[0].temperature.toFixed(0) : "93"
                                return TranslationManager.translate("profileEditor.settings", "Settings") + " (" + temp + "\u00B0C)"
                            }
                            accessibleName: TranslationManager.translate("profileEditor.openProfileSettings", "Open profile settings")
                            onClicked: profileSettingsPopup.open()
                            background: Rectangle {
                                color: parent.down ? Qt.darker(Theme.surfaceColor, 1.2) : Qt.rgba(255, 255, 255, 0.05)
                                radius: Theme.scaled(8)
                                border.width: 1
                                border.color: Theme.textSecondaryColor
                            }
                            contentItem: Text {
                                text: parent.text
                                font: Theme.captionFont
                                color: Theme.textColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        AccessibleButton {
                            Layout.fillWidth: true
                            text: {
                                stepVersion
                                if (!profile) return TranslationManager.translate("profileEditor.limits", "Limits")
                                var stopAtValue = profile.stop_at_type === "volume"
                                    ? (profile.target_volume || 36).toFixed(0) + "ml"
                                    : (profile.target_weight || 36).toFixed(0) + "g"
                                return TranslationManager.translate("profileEditor.limits", "Limits") + " (" + stopAtValue + ")"
                            }
                            accessibleName: TranslationManager.translate("profileEditor.openLimits", "Open limits settings")
                            onClicked: limitsPopup.open()
                            background: Rectangle {
                                color: parent.down ? Qt.darker(Theme.surfaceColor, 1.2) : Qt.rgba(255, 255, 255, 0.05)
                                radius: Theme.scaled(8)
                                border.width: 1
                                border.color: Theme.textSecondaryColor
                            }
                            contentItem: Text {
                                text: parent.text
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
                        sourceComponent: selectedStepIndex >= 0 ? stepEditorComponent : noSelectionComponent
                    }
                }
            }
        }
    }

    // Profile Settings Popup
    Popup {
        id: profileSettingsPopup
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))
        padding: Theme.scaled(15)
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        onOpened: {
            profileNameField.text = profile ? profile.title : ""
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
                TextField {
                    id: profileNameField
                    Layout.fillWidth: true
                    text: profile ? profile.title : ""
                    font: Theme.bodyFont
                    color: Theme.textColor
                    placeholderText: TranslationManager.translate("profileEditor.profileNamePlaceholder", "Profile name")
                    placeholderTextColor: Theme.textSecondaryColor
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
                        if (profile && text.length > 0 && text !== profile.title) {
                            profile.title = text
                            updatePageTitle()
                            uploadProfile()
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

            // Global temperature (applies to all frames)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)
                RowLayout { Layout.fillWidth: true
                    Text { text: TranslationManager.translate("profileEditor.allTemps", "All temps"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    Item { Layout.fillWidth: true }
                    Text { text: stepVersion >= 0 && profile && profile.steps.length > 0 ? profile.steps[0].temperature.toFixed(1) + "\u00B0C" : "93.0\u00B0C"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.temperatureColor }
                }
                StepSlider {
                    Layout.fillWidth: true; accessibleName: "Global temperature"; from: 70; to: 100; stepSize: 0.5
                    value: { stepVersion; return profile && profile.steps.length > 0 ? profile.steps[0].temperature : 93 }
                    onMoved: {
                        if (profile && profile.steps.length > 0) {
                            var newValue = Math.round(value * 2) / 2
                            var delta = newValue - profile.steps[0].temperature
                            for (var i = 0; i < profile.steps.length; i++) {
                                profile.steps[i].temperature += delta
                            }
                            profile.espresso_temperature = newValue
                            uploadProfile()
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

            // Recommended dose
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)
                RowLayout { Layout.fillWidth: true
                    Text { text: TranslationManager.translate("profileEditor.dose", "Recommended dose"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    Item { Layout.fillWidth: true }
                    Switch {
                        id: recommendedDoseSwitch
                        checked: profile ? !!profile.has_recommended_dose : false
                        onToggled: { if (profile) { profile.has_recommended_dose = checked; uploadProfile() } }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: recommendedDoseSwitch.checked
                    spacing: Theme.scaled(2)
                    RowLayout { Layout.fillWidth: true
                        Text { text: TranslationManager.translate("profileEditor.doseValue", "Dose"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                        Item { Layout.fillWidth: true }
                        Text { text: stepVersion >= 0 ? (profile ? (profile.recommended_dose || 18) : 18).toFixed(1) + "g" : "18.0g"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.weightColor }
                    }
                    StepSlider {
                        Layout.fillWidth: true; accessibleName: "Recommended dose"; from: 5; to: 100; stepSize: 0.1
                        value: { stepVersion; return profile ? (profile.recommended_dose || 18) : 18 }
                        onMoved: { if (profile) { profile.recommended_dose = Math.round(value * 10) / 10; uploadProfile() } }
                    }
                }
            }

            // Close button
            AccessibleButton {
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(6)
                text: TranslationManager.translate("profileEditor.done", "Done")
                accessibleName: TranslationManager.translate("profileEditor.closeProfileSettings", "Close profile settings")
                onClicked: profileSettingsPopup.close()
                background: Rectangle {
                    implicitHeight: Theme.scaled(44)
                    radius: Theme.buttonRadius
                    color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.bodyFont
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // Limits Popup
    Popup {
        id: limitsPopup
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(450))
        padding: Theme.scaled(15)
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: TranslationManager.translate("profileEditor.preheatTank", "Preheat water tank")
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: {
                            stepVersion
                            var val = profile ? (profile.tank_desired_water_temperature || 0) : 0
                            return val === 0 ? TranslationManager.translate("profileEditor.off", "Off") : val.toFixed(0) + "\u00B0C"
                        }
                        font.family: Theme.captionFont.family
                        font.pixelSize: Theme.captionFont.pixelSize
                        font.bold: true
                        color: Theme.temperatureColor
                    }
                }
                StepSlider {
                    Layout.fillWidth: true
                    accessibleName: TranslationManager.translate("profileEditor.preheatTankAccessible", "Preheat water tank temperature")
                    from: 0; to: 45; stepSize: 1
                    value: profile ? (profile.tank_desired_water_temperature || 0) : 0
                    onMoved: {
                        if (profile) {
                            profile.tank_desired_water_temperature = Math.round(value)
                            uploadProfile()
                        }
                    }
                }
            }

            // Preinfusion ends after
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: TranslationManager.translate("profileEditor.preinfusionEnds", "Preinfusion ends after")
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: {
                            stepVersion
                            if (!profile) return "0"
                            var count = profile.preinfuse_frame_count || 0
                            var stepName = ""
                            if (count > 0 && profile.steps && count <= profile.steps.length) {
                                stepName = profile.steps[count - 1].name || ""
                            }
                            return count + (stepName ? " (" + stepName + ")" : "")
                        }
                        font.family: Theme.captionFont.family
                        font.pixelSize: Theme.captionFont.pixelSize
                        font.bold: true
                        color: Theme.textColor
                    }
                }
                StepSlider {
                    Layout.fillWidth: true
                    accessibleName: TranslationManager.translate("profileEditor.preinfusionEndsAccessible", "Preinfusion ends after step")
                    from: 0; to: profile ? profile.steps.length : 0; stepSize: 1
                    value: profile ? (profile.preinfuse_frame_count || 0) : 0
                    onMoved: {
                        if (profile) {
                            profile.preinfuse_frame_count = Math.round(value)
                            uploadProfile()
                        }
                    }
                }
            }

            // Stop at weight
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: TranslationManager.translate("profileEditor.stopAtWeight", "Stop at weight")
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: {
                            stepVersion
                            var val = profile ? (profile.target_weight || 0) : 0
                            return val === 0 ? TranslationManager.translate("profileEditor.disabled", "Disabled") : val.toFixed(1) + "g"
                        }
                        font.family: Theme.captionFont.family
                        font.pixelSize: Theme.captionFont.pixelSize
                        font.bold: true
                        color: Theme.weightColor
                    }
                }
                StepSlider {
                    Layout.fillWidth: true
                    accessibleName: TranslationManager.translate("profileEditor.stopAtWeightAccessible", "Stop at weight")
                    from: 0; to: 100; stepSize: 0.5
                    value: profile ? (profile.target_weight || 0) : 0
                    onMoved: {
                        if (profile) {
                            profile.target_weight = Math.round(value * 2) / 2
                            if (value > 0) {
                                profile.stop_at_type = "weight"
                            }
                            stepVersion++
                            uploadProfile()
                        }
                    }
                }
            }

            // Stop at volume
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: TranslationManager.translate("profileEditor.stopAtVolume", "Stop at volume")
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: {
                            stepVersion
                            var val = profile ? (profile.target_volume || 0) : 0
                            return val === 0 ? TranslationManager.translate("profileEditor.disabled", "Disabled") : val.toFixed(0) + "mL"
                        }
                        font.family: Theme.captionFont.family
                        font.pixelSize: Theme.captionFont.pixelSize
                        font.bold: true
                        color: Theme.flowColor
                    }
                }
                StepSlider {
                    Layout.fillWidth: true
                    accessibleName: TranslationManager.translate("profileEditor.stopAtVolumeAccessible", "Stop at volume")
                    from: 0; to: 500; stepSize: 1
                    value: profile ? (profile.target_volume || 0) : 0
                    onMoved: {
                        if (profile) {
                            profile.target_volume = Math.round(value)
                            if (value > 0) {
                                profile.stop_at_type = "volume"
                            }
                            stepVersion++
                            uploadProfile()
                        }
                    }
                }
            }

            // Limit flow range (applied to pressure-pump steps)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: TranslationManager.translate("profileEditor.limitFlowRange", "Limit flow range")
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: {
                            stepVersion
                            var val = profile ? (profile.maximum_flow_range_advanced || 0.6) : 0.6
                            return val.toFixed(1) + " mL/s"
                        }
                        font.family: Theme.captionFont.family
                        font.pixelSize: Theme.captionFont.pixelSize
                        font.bold: true
                        color: Theme.flowColor
                    }
                }
                StepSlider {
                    Layout.fillWidth: true
                    accessibleName: TranslationManager.translate("profileEditor.limitFlowRangeAccessible", "Limit flow range for pressure steps")
                    from: 0; to: 8; stepSize: 0.1
                    value: profile ? (profile.maximum_flow_range_advanced || 0.6) : 0.6
                    onMoved: {
                        if (profile) {
                            var newRange = Math.round(value * 10) / 10
                            profile.maximum_flow_range_advanced = newRange
                            applyRangeToAllSteps()
                            uploadProfile()
                        }
                    }
                }
            }

            // Limit pressure range (applied to flow-pump steps)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: TranslationManager.translate("profileEditor.limitPressureRange", "Limit pressure range")
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: {
                            stepVersion
                            var val = profile ? (profile.maximum_pressure_range_advanced || 0.6) : 0.6
                            return val.toFixed(1) + " bar"
                        }
                        font.family: Theme.captionFont.family
                        font.pixelSize: Theme.captionFont.pixelSize
                        font.bold: true
                        color: Theme.pressureColor
                    }
                }
                StepSlider {
                    Layout.fillWidth: true
                    accessibleName: TranslationManager.translate("profileEditor.limitPressureRangeAccessible", "Limit pressure range for flow steps")
                    from: 0; to: 8; stepSize: 0.1
                    value: profile ? (profile.maximum_pressure_range_advanced || 0.6) : 0.6
                    onMoved: {
                        if (profile) {
                            var newRange = Math.round(value * 10) / 10
                            profile.maximum_pressure_range_advanced = newRange
                            applyRangeToAllSteps()
                            uploadProfile()
                        }
                    }
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

    // Bottom bar
    BottomBar {
        id: bottomBar
        title: profile ? profile.title : TranslationManager.translate("profileEditor.profile", "Profile")
        onBackClicked: {
            if (profileModified) {
                exitDialog.open()
            } else {
                root.goBack()
            }
        }

        // Modified indicator
        Tr {
            key: "profileeditor.status.modified"
            fallback: "\u2022 Modified"
            color: "#FFCC00"
            font: Theme.bodyFont
            visible: profileModified
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: "white"; opacity: 0.3; visible: profile }
        Text {
            text: profile ? profile.steps.length + " " + TranslationManager.translate("profileEditor.frames", "frames") : ""
            color: "white"
            font: Theme.bodyFont
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: "white"; opacity: 0.3; visible: profile }
        Text {
            text: {
                if (!profile) return ""
                if (profile.stop_at_type === "volume") {
                    return (profile.target_volume || 36).toFixed(0) + "ml"
                } else {
                    return (profile.target_weight || 36).toFixed(0) + "g"
                }
            }
            color: profile && profile.stop_at_type === "volume" ? Theme.flowColor : Theme.weightColor
            font: Theme.bodyFont
        }
        AccessibleButton {
            text: TranslationManager.translate("profileEditor.doneButton", "Done")
            accessibleName: TranslationManager.translate("profileEditor.finishEditing", "Finish editing profile")
            onClicked: {
                if (profileModified) {
                    exitDialog.open()
                } else {
                    root.goBack()
                }
            }
            // White button with primary text for bottom bar
            background: Rectangle {
                implicitWidth: Math.max(Theme.scaled(80), doneText.implicitWidth + Theme.scaled(32))
                implicitHeight: Theme.scaled(36)
                radius: Theme.scaled(6)
                color: parent.down ? Qt.darker("white", 1.1) : "white"
            }
            contentItem: Text {
                id: doneText
                text: parent.text
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
    Dialog {
        id: saveAsDialog
        title: TranslationManager.translate("profileEditor.saveProfileAs", "Save Profile As")
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Theme.scaled(400)
        modal: true
        standardButtons: Dialog.Save | Dialog.Cancel

        property string pendingFilename: ""

        ColumnLayout {
            width: parent.width
            spacing: Theme.scaled(10)

            Tr {
                key: "profileeditor.label.profiletitle"
                fallback: "Profile Title"
                font: Theme.captionFont
                color: Theme.textSecondaryColor
            }

            TextField {
                id: saveAsTitleField
                Layout.fillWidth: true
                text: profile ? profile.title : ""
                font: Theme.bodyFont
                color: Theme.textColor
                placeholderTextColor: Theme.textSecondaryColor
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
                onAccepted: saveAsDialog.accept()
            }
        }

        onAccepted: {
            if (saveAsTitleField.text.length > 0) {
                var filename = MainController.titleToFilename(saveAsTitleField.text)
                if (MainController.profileExists(filename) && filename !== originalProfileName) {
                    // File exists and it's not the current file - ask to overwrite
                    saveAsDialog.pendingFilename = filename
                    overwriteDialog.open()
                } else {
                    if (saveProfileAs(filename, saveAsTitleField.text)) {
                        root.goBack()
                    } else {
                        saveErrorDialog.open()
                    }
                }
            }
        }

        onOpened: {
            saveAsTitleField.text = profile ? profile.title : ""
            saveAsTitleField.forceActiveFocus()
        }
    }

    // Overwrite confirmation dialog
    Dialog {
        id: overwriteDialog
        title: TranslationManager.translate("profileEditor.profileExists", "Profile Exists")
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Theme.scaled(400)
        modal: true
        standardButtons: Dialog.Yes | Dialog.No

        contentItem: Tr {
            width: overwriteDialog.availableWidth
            key: "profileeditor.dialog.overwriteconfirm"
            fallback: "A profile with this name already exists.\nDo you want to overwrite it?"
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
        }

        onAccepted: {
            if (saveProfileAs(saveAsDialog.pendingFilename, saveAsTitleField.text)) {
                root.goBack()
            } else {
                saveErrorDialog.open()
            }
        }
    }

    // Save error dialog
    Dialog {
        id: saveErrorDialog
        title: TranslationManager.translate("profileEditor.saveError", "Save Failed")
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Theme.scaled(350)
        modal: true
        standardButtons: Dialog.Ok

        contentItem: Tr {
            width: saveErrorDialog.availableWidth
            key: "profileeditor.dialog.saveerror"
            fallback: "Could not save the profile. Please try again or use Save As with a different name."
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
        }
    }

    // Exit dialog for unsaved changes
    UnsavedChangesDialog {
        id: exitDialog
        itemType: "profile"
        canSave: originalProfileName !== ""
        onDiscardClicked: {
            if (originalProfileName) {
                MainController.loadProfile(originalProfileName)
            }
            root.goBack()
        }
        onSaveAsClicked: saveAsDialog.open()
        onSaveClicked: {
            if (saveProfile()) {
                root.goBack()
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

            property var step: (stepVersion >= 0) && profile && selectedStepIndex >= 0 && selectedStepIndex < profile.steps.length ?
                   profile.steps[selectedStepIndex] : null

            ColumnLayout {
                width: stepEditorScroll.width - Theme.scaled(10)
                spacing: Theme.scaled(4)

                // Frame name
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    Text { text: TranslationManager.translate("profileEditor.frameName", "Frame Name"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    TextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        text: { var v = stepVersion; return step ? step.name : "" }
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                        color: Theme.textColor
                        placeholderTextColor: Theme.textSecondaryColor
                        leftPadding: Theme.scaled(12); rightPadding: Theme.scaled(12); topPadding: Theme.scaled(10); bottomPadding: Theme.scaled(10)
                        background: Rectangle { color: Theme.backgroundColor; radius: Theme.scaled(4); border.color: parent.activeFocus ? Theme.primaryColor : Theme.borderColor; border.width: 1 }
                        onEditingFinished: { if (profile && selectedStepIndex >= 0 && profile.steps[selectedStepIndex].name !== text) { profile.steps[selectedStepIndex].name = text; uploadProfile() } }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // Temperature
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    RowLayout { Layout.fillWidth: true
                        Text { text: TranslationManager.translate("profileEditor.temp", "Temperature"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                        Item { Layout.fillWidth: true }
                        Text { text: stepVersion >= 0 && step ? step.temperature.toFixed(1) + "\u00B0C" : "93.0\u00B0C"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.temperatureColor }
                    }
                    StepSlider { Layout.fillWidth: true; accessibleName: "Step temperature"; from: 70; to: 100; stepSize: 0.1; value: stepVersion >= 0 && step ? step.temperature : 93; onMoved: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].temperature = Math.round(value * 10) / 10; uploadProfile() } } }
                }

                // Sensor toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    Text { text: TranslationManager.translate("profileEditor.sensor", "Sensor"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    Item { Layout.fillWidth: true }
                    RowLayout {
                        spacing: Theme.scaled(4)
                        Rectangle {
                            Layout.preferredWidth: coffeePillText.implicitWidth + Theme.scaled(16); Layout.preferredHeight: Theme.scaled(28)
                            radius: Theme.scaled(14); color: step && step.sensor === "coffee" ? Theme.temperatureColor : "transparent"; border.width: 1; border.color: Theme.temperatureColor
                            Text { id: coffeePillText; anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.coffee", "Coffee"); font: Theme.captionFont; color: step && step.sensor === "coffee" ? "white" : Theme.temperatureColor }
                            MouseArea { anchors.fill: parent; Accessible.role: Accessible.Button; Accessible.name: "Coffee sensor"; Accessible.focusable: true; onClicked: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].sensor = "coffee"; uploadProfile() } } }
                        }
                        Rectangle {
                            Layout.preferredWidth: waterPillText.implicitWidth + Theme.scaled(16); Layout.preferredHeight: Theme.scaled(28)
                            radius: Theme.scaled(14); color: step && step.sensor === "water" ? Theme.temperatureColor : "transparent"; border.width: 1; border.color: Theme.temperatureColor
                            Text { id: waterPillText; anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.water", "Water"); font: Theme.captionFont; color: step && step.sensor === "water" ? "white" : Theme.temperatureColor }
                            MouseArea { anchors.fill: parent; Accessible.role: Accessible.Button; Accessible.name: "Water sensor"; Accessible.focusable: true; onClicked: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].sensor = "water"; uploadProfile() } } }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // Pump mode toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    Text { text: TranslationManager.translate("profileEditor.pumpMode", "Pump"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    Item { Layout.fillWidth: true }
                    RowLayout {
                        spacing: Theme.scaled(4)
                        Rectangle {
                            Layout.preferredWidth: pressurePillText.implicitWidth + Theme.scaled(16); Layout.preferredHeight: Theme.scaled(28)
                            radius: Theme.scaled(14); color: step && step.pump === "pressure" ? Theme.pressureColor : "transparent"; border.width: 1; border.color: Theme.pressureColor
                            Text { id: pressurePillText; anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.pressure", "Pressure"); font: Theme.captionFont; color: step && step.pump === "pressure" ? "white" : Theme.pressureColor }
                            MouseArea { anchors.fill: parent; Accessible.role: Accessible.Button; Accessible.name: "Pressure pump mode"; Accessible.focusable: true; onClicked: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].pump = "pressure"; uploadProfile() } } }
                        }
                        Rectangle {
                            Layout.preferredWidth: flowPillText.implicitWidth + Theme.scaled(16); Layout.preferredHeight: Theme.scaled(28)
                            radius: Theme.scaled(14); color: step && step.pump === "flow" ? Theme.flowColor : "transparent"; border.width: 1; border.color: Theme.flowColor
                            Text { id: flowPillText; anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.flow", "Flow"); font: Theme.captionFont; color: step && step.pump === "flow" ? "white" : Theme.flowColor }
                            MouseArea { anchors.fill: parent; Accessible.role: Accessible.Button; Accessible.name: "Flow pump mode"; Accessible.focusable: true; onClicked: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].pump = "flow"; uploadProfile() } } }
                        }
                    }
                }

                // Setpoint (pressure or flow)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    RowLayout { Layout.fillWidth: true
                        Text { text: step && step.pump === "flow" ? TranslationManager.translate("profileEditor.flowValue", "Flow") : TranslationManager.translate("profileEditor.pressureValue", "Pressure"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                        Item { Layout.fillWidth: true }
                        Text { text: stepVersion >= 0 && step ? (step.pump === "flow" ? step.flow.toFixed(1) + " mL/s" : step.pressure.toFixed(1) + " bar") : "0.0"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: step && step.pump === "flow" ? Theme.flowColor : Theme.pressureColor }
                    }
                    StepSlider {
                        Layout.fillWidth: true; accessibleName: step && step.pump === "flow" ? "Flow setpoint" : "Pressure setpoint"
                        from: 0; to: step && step.pump === "flow" ? 8 : 12; stepSize: 0.1
                        value: { var v = stepVersion; return step ? (step.pump === "flow" ? step.flow : step.pressure) : 0 }
                        onMoved: { if (profile && selectedStepIndex >= 0) { var val = Math.round(value * 10) / 10; if (profile.steps[selectedStepIndex].pump === "flow") { profile.steps[selectedStepIndex].flow = val } else { profile.steps[selectedStepIndex].pressure = val }; uploadProfile() } }
                    }
                }

                // Limiter (flow limit for pressure steps, pressure limit for flow steps)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    RowLayout { Layout.fillWidth: true
                        Text { text: step && step.pump === "flow" ? TranslationManager.translate("profileEditor.pressureLimit", "Pressure limit") : TranslationManager.translate("profileEditor.flowLimit", "Flow limit"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                        Item { Layout.fillWidth: true }
                        Text { text: stepVersion >= 0 && step && (step.max_flow_or_pressure || 0) > 0 ? (step.max_flow_or_pressure || 0).toFixed(1) + (step.pump === "flow" ? " bar" : " mL/s") : TranslationManager.translate("profileEditor.off", "Off"); font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: step && (step.max_flow_or_pressure || 0) > 0 ? Theme.warningColor : Theme.textSecondaryColor }
                    }
                    StepSlider { Layout.fillWidth: true; accessibleName: step && step.pump === "flow" ? "Pressure limit" : "Flow limit"; from: 0; to: step && step.pump === "flow" ? 12 : 8; stepSize: 0.1; value: stepVersion >= 0 && step ? (step.max_flow_or_pressure || 0) : 0; onMoved: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].max_flow_or_pressure = Math.round(value * 10) / 10; uploadProfile() } } }
                }

                // Limiter range
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    RowLayout { Layout.fillWidth: true
                        Text { text: TranslationManager.translate("profileEditor.limiterRange", "Limiter range"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                        Item { Layout.fillWidth: true }
                        Text { text: stepVersion >= 0 ? (step ? (step.max_flow_or_pressure_range || 0.6) : 0.6).toFixed(1) + (step && step.pump === "flow" ? " bar" : " mL/s") : "0.6"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.textSecondaryColor }
                    }
                    StepSlider { Layout.fillWidth: true; accessibleName: "Limiter range"; from: 0.1; to: 2.0; stepSize: 0.1; value: stepVersion >= 0 && step ? (step.max_flow_or_pressure_range || 0.6) : 0.6; onMoved: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].max_flow_or_pressure_range = Math.round(value * 10) / 10; uploadProfile() } } }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // Transition toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    Text { text: TranslationManager.translate("profileEditor.transition", "Transition"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    Item { Layout.fillWidth: true }
                    RowLayout {
                        spacing: Theme.scaled(4)
                        Rectangle {
                            Layout.preferredWidth: fastPillText.implicitWidth + Theme.scaled(16); Layout.preferredHeight: Theme.scaled(28)
                            radius: Theme.scaled(14); color: step && step.transition === "fast" ? Theme.primaryColor : "transparent"; border.width: 1; border.color: Theme.primaryColor
                            Text { id: fastPillText; anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.fast", "Fast"); font: Theme.captionFont; color: step && step.transition === "fast" ? "white" : Theme.primaryColor }
                            MouseArea { anchors.fill: parent; Accessible.role: Accessible.Button; Accessible.name: "Fast transition"; Accessible.focusable: true; onClicked: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].transition = "fast"; uploadProfile() } } }
                        }
                        Rectangle {
                            Layout.preferredWidth: smoothPillText.implicitWidth + Theme.scaled(16); Layout.preferredHeight: Theme.scaled(28)
                            radius: Theme.scaled(14); color: step && step.transition === "smooth" ? Theme.primaryColor : "transparent"; border.width: 1; border.color: Theme.primaryColor
                            Text { id: smoothPillText; anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.smooth", "Smooth"); font: Theme.captionFont; color: step && step.transition === "smooth" ? "white" : Theme.primaryColor }
                            MouseArea { anchors.fill: parent; Accessible.role: Accessible.Button; Accessible.name: "Smooth transition"; Accessible.focusable: true; onClicked: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].transition = "smooth"; uploadProfile() } } }
                        }
                    }
                }

                // Duration
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    RowLayout { Layout.fillWidth: true
                        Text { text: TranslationManager.translate("profileEditor.duration", "Max seconds"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                        Item { Layout.fillWidth: true }
                        Text { text: stepVersion >= 0 && step ? Math.round(step.seconds) + "s" : "30s"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.textColor }
                    }
                    StepSlider { Layout.fillWidth: true; accessibleName: "Step duration"; from: 0; to: 120; stepSize: 1; value: stepVersion >= 0 && step ? step.seconds : 30; onMoved: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].seconds = Math.round(value); uploadProfile() } } }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // Exit condition
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    Text { text: TranslationManager.translate("profileEditor.exitCondition", "Exit condition"); font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.textColor }
                    Item { Layout.fillWidth: true }
                    Switch {
                        id: exitIfSwitch
                        checked: { var v = stepVersion; return step ? step.exit_if : false }
                        onToggled: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].exit_if = checked; uploadProfile() } }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: exitIfSwitch.checked
                    spacing: Theme.scaled(4)

                    StyledComboBox {
                        id: exitTypeCombo
                        Layout.fillWidth: true
                        model: ["Pressure Over", "Pressure Under", "Flow Over", "Flow Under", "Weight Over"]
                        contentItem: Text { text: exitTypeCombo.displayText; font: Theme.bodyFont; color: Theme.textColor; leftPadding: Theme.scaled(10); verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { implicitHeight: Theme.scaled(36); color: Theme.backgroundColor; radius: Theme.scaled(6); border.width: 1; border.color: Theme.borderColor }
                        popup: Popup {
                            y: exitTypeCombo.height
                            width: exitTypeCombo.width
                            padding: 1
                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: exitTypeCombo.popup.visible ? exitTypeCombo.delegateModel : null
                                ScrollIndicator.vertical: ScrollIndicator {}
                            }
                            background: Rectangle { color: Theme.surfaceColor; radius: Theme.scaled(6) }
                        }
                        delegate: ItemDelegate {
                            width: exitTypeCombo.width
                            contentItem: Text { text: modelData; font: Theme.bodyFont; color: Theme.textColor }
                            background: Rectangle { color: highlighted ? Theme.primaryColor : "transparent" }
                            highlighted: exitTypeCombo.highlightedIndex === index
                        }
                        currentIndex: { var v = stepVersion; if (!step) return 0; switch (step.exit_type) { case "pressure_over": return 0; case "pressure_under": return 1; case "flow_over": return 2; case "flow_under": return 3; case "weight": return 4; default: return 0 } }
                        onActivated: function(index) { if (!profile || selectedStepIndex < 0) return; var types = ["pressure_over", "pressure_under", "flow_over", "flow_under", "weight"]; profile.steps[selectedStepIndex].exit_type = types[index]; uploadProfile() }
                    }

                    // Exit value slider
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)
                        RowLayout { Layout.fillWidth: true
                            Text { text: TranslationManager.translate("profileEditor.exitValue", "Value"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: {
                                    var v = stepVersion; if (!step) return "0"
                                    var val = 0; var unit = " bar"
                                    switch (step.exit_type) {
                                        case "pressure_over": val = step.exit_pressure_over || 0; unit = " bar"; break
                                        case "pressure_under": val = step.exit_pressure_under || 0; unit = " bar"; break
                                        case "flow_over": val = step.exit_flow_over || 0; unit = " mL/s"; break
                                        case "flow_under": val = step.exit_flow_under || 0; unit = " mL/s"; break
                                        case "weight": val = step.exit_weight || 0; unit = "g"; break
                                    }
                                    return val.toFixed(1) + unit
                                }
                                font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true
                                color: { if (!step) return Theme.textSecondaryColor; switch (step.exit_type) { case "flow_over": case "flow_under": return Theme.flowColor; case "weight": return Theme.weightColor; default: return Theme.pressureColor } }
                            }
                        }
                        StepSlider {
                            Layout.fillWidth: true; accessibleName: "Exit value"
                            from: 0; to: { if (!step) return 12; switch (step.exit_type) { case "flow_over": case "flow_under": return 8; case "weight": return 100; default: return 12 } }
                            stepSize: step && step.exit_type === "weight" ? 0.5 : 0.1
                            value: { var v = stepVersion; if (!step) return 0; switch (step.exit_type) { case "pressure_over": return step.exit_pressure_over || 0; case "pressure_under": return step.exit_pressure_under || 0; case "flow_over": return step.exit_flow_over || 0; case "flow_under": return step.exit_flow_under || 0; case "weight": return step.exit_weight || 0; default: return 0 } }
                            onMoved: {
                                if (!profile || selectedStepIndex < 0) return
                                var val = step && step.exit_type === "weight" ? Math.round(value * 2) / 2 : Math.round(value * 10) / 10
                                switch (profile.steps[selectedStepIndex].exit_type) {
                                    case "pressure_over": profile.steps[selectedStepIndex].exit_pressure_over = val; break
                                    case "pressure_under": profile.steps[selectedStepIndex].exit_pressure_under = val; break
                                    case "flow_over": profile.steps[selectedStepIndex].exit_flow_over = val; break
                                    case "flow_under": profile.steps[selectedStepIndex].exit_flow_under = val; break
                                    case "weight": profile.steps[selectedStepIndex].exit_weight = val; break
                                }
                                uploadProfile()
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // Volume limit
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    RowLayout { Layout.fillWidth: true
                        Text { text: TranslationManager.translate("profileEditor.limitVolume", "Volume limit"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                        Item { Layout.fillWidth: true }
                        Text { text: stepVersion >= 0 && step && (step.volume || 0) > 0 ? Math.round(step.volume || 0) + " mL" : TranslationManager.translate("profileEditor.off", "Off"); font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: step && (step.volume || 0) > 0 ? Theme.flowColor : Theme.textSecondaryColor }
                    }
                    StepSlider { Layout.fillWidth: true; accessibleName: "Step volume limit"; from: 0; to: 500; stepSize: 1; value: stepVersion >= 0 && step ? (step.volume || 0) : 0; onMoved: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].volume = Math.round(value); uploadProfile() } } }
                }

                // Weight limit
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    RowLayout { Layout.fillWidth: true
                        Text { text: TranslationManager.translate("profileEditor.limitWeight", "Weight limit"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                        Item { Layout.fillWidth: true }
                        Text { text: stepVersion >= 0 && step && (step.exit_weight || 0) > 0 ? (step.exit_weight || 0).toFixed(1) + "g" : TranslationManager.translate("profileEditor.off", "Off"); font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: step && (step.exit_weight || 0) > 0 ? Theme.weightColor : Theme.textSecondaryColor }
                    }
                    StepSlider { Layout.fillWidth: true; accessibleName: "Step weight limit"; from: 0; to: 100; stepSize: 0.5; value: stepVersion >= 0 && step ? (step.exit_weight || 0) : 0; onMoved: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].exit_weight = Math.round(value * 2) / 2; uploadProfile() } } }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // Popup message
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    Text { text: TranslationManager.translate("profileEditor.popupMessage", "Popup Message"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    TextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        text: { var v = stepVersion; return step ? (step.popup || "") : "" }
                        font: Theme.bodyFont; color: Theme.textColor
                        placeholderText: TranslationManager.translate("profileEditor.popupMessagePlaceholder", "e.g., Swirl now, $weight")
                        placeholderTextColor: Theme.textSecondaryColor
                        leftPadding: Theme.scaled(12); rightPadding: Theme.scaled(12); topPadding: Theme.scaled(10); bottomPadding: Theme.scaled(10)
                        background: Rectangle { color: Theme.backgroundColor; radius: Theme.scaled(4); border.color: parent.activeFocus ? Theme.primaryColor : Theme.borderColor; border.width: 1 }
                        onEditingFinished: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].popup = text; uploadProfile() } }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // Profile name edit dialog
    Dialog {
        id: profileNameDialog
        title: TranslationManager.translate("profileEditor.editProfileName", "Edit Profile Name")
        anchors.centerIn: parent
        width: Theme.scaled(400)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        TextField {
            id: nameField
            width: parent.width
            text: profile ? profile.title : ""
            font: Theme.bodyFont
            color: Theme.textColor
            placeholderTextColor: Theme.textSecondaryColor
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
        }

        onAccepted: {
            if (profile && nameField.text.length > 0) {
                profile.title = nameField.text
                updatePageTitle()
                uploadProfile()
            }
        }

        onOpened: {
            nameField.text = profile ? profile.title : ""
            nameField.selectAll()
            nameField.forceActiveFocus()
        }
    }

    // Apply limit ranges to all steps (de1app apply_range_to_all_steps behavior)
    // Pressure-pump steps get the flow range limit; flow-pump steps get the pressure range limit
    function applyRangeToAllSteps() {
        if (!profile || !profile.steps) return
        var flowRange = profile.maximum_flow_range_advanced || 0.6
        var pressureRange = profile.maximum_pressure_range_advanced || 0.6
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

        selectedStepIndex = insertIndex
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

        selectedStepIndex = index + 1
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
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            var name = step.name || TranslationManager.translate("profileEditor.unnamed", "unnamed")
            var direction = toIndex < fromIndex ? TranslationManager.translate("profileEditor.left", "left") : TranslationManager.translate("profileEditor.right", "right")
            AccessibilityManager.announce(TranslationManager.translate("profileEditor.movedFrame", "Moved %1 %2 to position %3 of %4").arg(name).arg(direction).arg(toIndex + 1).arg(profile.steps.length))
        }
    }

    function loadCurrentProfile() {
        // Load profile data from MainController
        var loadedProfile = MainController.getCurrentProfile()
        if (loadedProfile && loadedProfile.steps && loadedProfile.steps.length > 0) {
            profile = loadedProfile
        } else {
            // Fallback to empty profile
            profile = {
                title: MainController.currentProfileName || "New Profile",
                steps: [],
                target_weight: MainController.targetWeight || 36,
                target_volume: 36,
                stop_at_type: "weight",
                espresso_temperature: 93,
                mode: "frame_based",
                tank_desired_water_temperature: 0,
                maximum_flow_range_advanced: 0.6,
                maximum_pressure_range_advanced: 0.6,
                preinfuse_frame_count: 0
            }
        }
        // Track the original profile filename for saving (not the title!)
        originalProfileName = MainController.baseProfileName || ""
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
        if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
        if (!profile) return

        var frameCount = profile.steps ? profile.steps.length : 0
        var title = root.cleanForSpeech(profile.title || "Untitled")
        var announcement = title + " profile. " + frameCount + " frame" + (frameCount !== 1 ? "s" : "")
        AccessibilityManager.announce(announcement)
    }
}
