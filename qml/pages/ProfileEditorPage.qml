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

    // Commit any text fields that use onEditingFinished (which won't fire on navigation)
    function flushPendingEdits() {
        if (profile) {
            if (profileNotesFieldInline.text !== (profile.profile_notes || "")) {
                profile.profile_notes = profileNotesFieldInline.text
                uploadProfile()
            }
        }
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
        textFields: [profileNotesFieldInline, profileNameField]

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
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(80)
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius
                        clip: true

                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(6)
                            contentWidth: availableWidth
                            ScrollBar.vertical.policy: ScrollBar.AsNeeded

                            TextArea {
                                id: profileNotesFieldInline
                                Accessible.role: Accessible.EditableText
                                Accessible.name: TranslationManager.translate("profileEditor.accessible.profileDescription", "Profile description")
                                Accessible.description: text
                                Accessible.focusable: true
                                text: profile ? (profile.profile_notes || "") : ""
                                font: Theme.labelFont
                                color: Theme.textColor
                                wrapMode: TextArea.Wrap
                                leftPadding: Theme.scaled(8)
                                rightPadding: Theme.scaled(8)
                                topPadding: Theme.scaled(4)
                                bottomPadding: Theme.scaled(4)
                                background: Rectangle { color: "transparent" }
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
                                color: parent.down ? Qt.darker(Theme.surfaceColor, 1.2) : Qt.rgba(1, 1, 1, 0.05)
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
                                color: parent.down ? Qt.darker(Theme.surfaceColor, 1.2) : Qt.rgba(1, 1, 1, 0.05)
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
                StyledTextField {
                    id: profileNameField
                    Accessible.name: "Profile name"
                    Layout.fillWidth: true
                    text: profile ? profile.title : ""
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
                ValueInput {
                    Layout.fillWidth: true; valueColor: Theme.temperatureColor
                    accessibleName: "Global temperature"; from: 70; to: 100; stepSize: 0.5
                    value: { stepVersion; return profile && profile.steps.length > 0 ? profile.steps[0].temperature : 93 }
                    onValueModified: function(newValue) {
                        if (profile && profile.steps.length > 0) {
                            var rounded = Math.round(newValue * 2) / 2
                            var delta = rounded - profile.steps[0].temperature
                            for (var i = 0; i < profile.steps.length; i++) {
                                profile.steps[i].temperature += delta
                            }
                            profile.espresso_temperature = rounded
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
                        Accessible.name: "Toggle recommended dose"
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: recommendedDoseSwitch.checked
                    spacing: Theme.scaled(2)
                    ValueInput {
                        Layout.fillWidth: true; valueColor: Theme.weightColor
                        accessibleName: "Recommended dose"; from: 5; to: 100; stepSize: 0.1
                        value: { stepVersion; return profile ? (profile.recommended_dose || 18) : 18 }
                        onValueModified: function(newValue) { if (profile) { profile.recommended_dose = Math.round(newValue * 10) / 10; uploadProfile() } }
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
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Text { Layout.fillWidth: true; text: TranslationManager.translate("profileEditor.preheatTank", "Preheat tank"); font: Theme.captionFont; color: Theme.temperatureColor; verticalAlignment: Text.AlignVCenter }
                ValueInput {
                    Layout.preferredWidth: Theme.scaled(160); valueColor: Theme.temperatureColor
                    accessibleName: TranslationManager.translate("profileEditor.preheatTankAccessible", "Preheat water tank temperature")
                    from: 0; to: 45; stepSize: 1
                    value: { stepVersion; return profile ? (profile.tank_desired_water_temperature || 0) : 0 }
                    onValueModified: function(newValue) {
                        if (profile) {
                            profile.tank_desired_water_temperature = Math.round(newValue)
                            uploadProfile()
                        }
                    }
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
                    from: 0; to: profile ? profile.steps.length : 0; stepSize: 1
                    value: { stepVersion; return profile ? (profile.preinfuse_frame_count || 0) : 0 }
                    onValueModified: function(newValue) {
                        if (profile) {
                            profile.preinfuse_frame_count = Math.round(newValue)
                            uploadProfile()
                        }
                    }
                }
            }

            // Stop at volume
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Text { Layout.fillWidth: true; text: TranslationManager.translate("profileEditor.stopAtVolume", "Stop at volume (mL)"); font: Theme.captionFont; color: Theme.flowColor; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap }
                ValueInput {
                    Layout.preferredWidth: Theme.scaled(160); valueColor: Theme.flowColor
                    accessibleName: TranslationManager.translate("profileEditor.afterPreinfusionStopAccessible", "After preinfusion, stop the shot at volume")
                    from: 0; to: 500; stepSize: 1
                    value: { stepVersion; return profile ? (profile.target_volume || 0) : 0 }
                    onValueModified: function(newValue) {
                        if (profile) {
                            profile.target_volume = Math.round(newValue)
                            if (newValue > 0) {
                                profile.stop_at_type = "volume"
                            }
                            stepVersion++
                            uploadProfile()
                        }
                    }
                }
            }

            // Stop at weight
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Text { Layout.fillWidth: true; text: TranslationManager.translate("profileEditor.stopAtWeight", "Stop at weight (g)"); font: Theme.captionFont; color: Theme.weightColor; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap }
                ValueInput {
                    Layout.preferredWidth: Theme.scaled(160); valueColor: Theme.weightColor
                    accessibleName: TranslationManager.translate("profileEditor.stopAtWeightAccessible", "Stop at weight")
                    from: 0; to: 100; stepSize: 0.5
                    value: { stepVersion; return profile ? (profile.target_weight || 0) : 0 }
                    onValueModified: function(newValue) {
                        if (profile) {
                            profile.target_weight = Math.round(newValue * 2) / 2
                            if (newValue > 0) {
                                profile.stop_at_type = "weight"
                            }
                            stepVersion++
                            uploadProfile()
                        }
                    }
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
                    from: 0; to: 8; stepSize: 0.1
                    value: { stepVersion; return profile ? (profile.maximum_flow_range_advanced || 0.6) : 0.6 }
                    onValueModified: function(newValue) {
                        if (profile) {
                            var newRange = Math.round(newValue * 10) / 10
                            profile.maximum_flow_range_advanced = newRange
                            applyRangeToAllSteps()
                            uploadProfile()
                        }
                    }
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
                    from: 0; to: 8; stepSize: 0.1
                    value: { stepVersion; return profile ? (profile.maximum_pressure_range_advanced || 0.6) : 0.6 }
                    onValueModified: function(newValue) {
                        if (profile) {
                            var newRange = Math.round(newValue * 10) / 10
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

    // Bottom bar — counteract keyboard shift so it stays at screen bottom (behind keyboard)
    BottomBar {
        id: bottomBar
        transform: Translate { y: keyboardContainer.keyboardOffset }
        title: profile ? profile.title : TranslationManager.translate("profileEditor.profile", "Profile")
        onBackClicked: {
            flushPendingEdits()
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
                flushPendingEdits()
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
            anchors.left: parent.left; anchors.right: parent.right
            spacing: Theme.scaled(10)

            Tr {
                key: "profileeditor.label.profiletitle"
                fallback: "Profile Title"
                font: Theme.captionFont
                color: Theme.textSecondaryColor
            }

            StyledTextField {
                id: saveAsTitleField
                Accessible.name: "Profile name"
                Layout.fillWidth: true
                text: profile ? profile.title : ""
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

        Tr {
            anchors.left: parent.left; anchors.right: parent.right
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

        Tr {
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
                width: stepEditorScroll.width - Theme.scaled(24)
                spacing: Theme.scaled(4)

                // Frame name
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    Text { text: TranslationManager.translate("profileEditor.frameName", "Frame Name"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    StyledTextField {
                        Accessible.name: "Frame name"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        text: { var v = stepVersion; return step ? step.name : "" }
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                        color: Theme.textColor
                        leftPadding: Theme.scaled(12); rightPadding: Theme.scaled(12); topPadding: Theme.scaled(10); bottomPadding: Theme.scaled(10)
                        background: Rectangle { color: Theme.backgroundColor; radius: Theme.scaled(4); border.color: parent.activeFocus ? Theme.primaryColor : Theme.borderColor; border.width: 1 }
                        onEditingFinished: { if (profile && selectedStepIndex >= 0 && profile.steps[selectedStepIndex].name !== text) { profile.steps[selectedStepIndex].name = text; uploadProfile() } }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // ── Section 1: Temperature ──
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    Text { text: "1:"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.primaryColor }
                    Text { text: TranslationManager.translate("profileEditor.temperature", "Temperature"); font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.textColor }
                }

                // Temperature
                ValueInput { Layout.fillWidth: true; valueColor: Theme.temperatureColor; accessibleName: "Step temperature"; from: 70; to: 100; stepSize: 0.1; value: stepVersion >= 0 && step ? step.temperature : 93; onValueModified: function(newValue) { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].temperature = Math.round(newValue * 10) / 10; uploadProfile() } } }

                // Sensor toggle
                Text { text: TranslationManager.translate("profileEditor.sensor", "Sensor"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Theme.scaled(28)
                        radius: Theme.scaled(6)
                        color: step && step.sensor !== "water" ? Theme.temperatureColor : Theme.backgroundColor
                        border.width: step && step.sensor !== "water" ? 0 : 1; border.color: Theme.borderColor
                        Accessible.role: Accessible.Button; Accessible.name: "Sensor: Coffee"; Accessible.focusable: true
                        Accessible.onPressAction: sensorCoffeeArea.clicked(null)
                        Text { anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.coffee", "Coffee"); font: Theme.captionFont; color: step && step.sensor !== "water" ? "white" : Theme.textSecondaryColor; Accessible.ignored: true }
                        MouseArea { id: sensorCoffeeArea; anchors.fill: parent; onClicked: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].sensor = "coffee"; uploadProfile() } } }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Theme.scaled(28)
                        radius: Theme.scaled(6)
                        color: step && step.sensor === "water" ? Theme.flowColor : Theme.backgroundColor
                        border.width: step && step.sensor === "water" ? 0 : 1; border.color: Theme.borderColor
                        Accessible.role: Accessible.Button; Accessible.name: "Sensor: Water"; Accessible.focusable: true
                        Accessible.onPressAction: sensorWaterArea.clicked(null)
                        Text { anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.water", "Water"); font: Theme.captionFont; color: step && step.sensor === "water" ? "white" : Theme.textSecondaryColor; Accessible.ignored: true }
                        MouseArea { id: sensorWaterArea; anchors.fill: parent; onClicked: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].sensor = "water"; uploadProfile() } } }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

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
                        color: step && step.pump === "pressure" ? Theme.pressureColor : Theme.backgroundColor
                        border.width: step && step.pump === "pressure" ? 0 : 1; border.color: Theme.borderColor
                        Accessible.role: Accessible.Button; Accessible.name: "Goal: Pressure"; Accessible.focusable: true
                        Accessible.onPressAction: goalPressureArea.clicked(null)
                        Text { anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.pressure", "Pressure"); font: Theme.captionFont; color: step && step.pump === "pressure" ? "white" : Theme.textSecondaryColor; Accessible.ignored: true }
                        MouseArea { id: goalPressureArea; anchors.fill: parent; onClicked: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].pump = "pressure"; uploadProfile() } } }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Theme.scaled(28)
                        radius: Theme.scaled(6)
                        color: step && step.pump === "flow" ? Theme.flowColor : Theme.backgroundColor
                        border.width: step && step.pump === "flow" ? 0 : 1; border.color: Theme.borderColor
                        Accessible.role: Accessible.Button; Accessible.name: "Goal: Flow"; Accessible.focusable: true
                        Accessible.onPressAction: goalFlowArea.clicked(null)
                        Text { anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.flow", "Flow"); font: Theme.captionFont; color: step && step.pump === "flow" ? "white" : Theme.textSecondaryColor; Accessible.ignored: true }
                        MouseArea { id: goalFlowArea; anchors.fill: parent; onClicked: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].pump = "flow"; uploadProfile() } } }
                    }
                }

                // Pressure/Flow goal (switches based on pump mode)
                ValueInput {
                    Layout.fillWidth: true
                    valueColor: step && step.pump === "flow" ? Theme.flowColor : Theme.pressureColor
                    accessibleName: step && step.pump === "flow" ? "Flow goal" : "Pressure goal"
                    from: 0; to: step && step.pump === "flow" ? 8 : 12; stepSize: 0.1
                    value: { var v = stepVersion; return step ? (step.pump === "flow" ? step.flow : step.pressure) : 0 }
                    onValueModified: function(newValue) {
                        if (profile && selectedStepIndex >= 0) {
                            var val = Math.round(newValue * 10) / 10
                            if (profile.steps[selectedStepIndex].pump === "flow") {
                                profile.steps[selectedStepIndex].flow = val
                            } else {
                                profile.steps[selectedStepIndex].pressure = val
                            }
                            uploadProfile()
                        }
                    }
                }

                // Transition toggle
                Text { text: TranslationManager.translate("profileEditor.transition", "Transition"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Theme.scaled(28)
                        radius: Theme.scaled(6)
                        color: step && step.transition !== "smooth" ? Theme.primaryColor : Theme.backgroundColor
                        border.width: step && step.transition !== "smooth" ? 0 : 1; border.color: Theme.borderColor
                        Accessible.role: Accessible.Button; Accessible.name: "Transition: Fast"; Accessible.focusable: true
                        Accessible.onPressAction: transitionFastArea.clicked(null)
                        Text { anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.fast", "Fast"); font: Theme.captionFont; color: step && step.transition !== "smooth" ? "white" : Theme.textSecondaryColor; Accessible.ignored: true }
                        MouseArea { id: transitionFastArea; anchors.fill: parent; onClicked: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].transition = "fast"; uploadProfile() } } }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Theme.scaled(28)
                        radius: Theme.scaled(6)
                        color: step && step.transition === "smooth" ? Theme.primaryColor : Theme.backgroundColor
                        border.width: step && step.transition === "smooth" ? 0 : 1; border.color: Theme.borderColor
                        Accessible.role: Accessible.Button; Accessible.name: "Transition: Smooth"; Accessible.focusable: true
                        Accessible.onPressAction: transitionSmoothArea.clicked(null)
                        Text { anchors.centerIn: parent; text: TranslationManager.translate("profileEditor.smooth", "Smooth"); font: Theme.captionFont; color: step && step.transition === "smooth" ? "white" : Theme.textSecondaryColor; Accessible.ignored: true }
                        MouseArea { id: transitionSmoothArea; anchors.fill: parent; onClicked: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].transition = "smooth"; uploadProfile() } } }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // ── Section 3: Maximum ──
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    Text { text: "3:"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.primaryColor }
                    Text { text: TranslationManager.translate("profileEditor.maximum", "Maximum"); font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.textColor }
                }

                // Max duration
                Text { text: TranslationManager.translate("profileEditor.maxDuration", "Duration (seconds)"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                ValueInput { Layout.fillWidth: true; accessibleName: "Max duration"; from: 0; to: 120; stepSize: 1; value: stepVersion >= 0 && step ? step.seconds : 30; onValueModified: function(newValue) { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].seconds = Math.round(newValue); uploadProfile() } } }

                // Max volume
                Text { text: TranslationManager.translate("profileEditor.maxVolume", "Volume (mL)"); font: Theme.captionFont; color: Theme.flowColor }
                ValueInput { Layout.fillWidth: true; valueColor: Theme.flowColor; accessibleName: "Max volume"; from: 0; to: 500; stepSize: 1; value: stepVersion >= 0 && step ? (step.volume || 0) : 0; onValueModified: function(newValue) { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].volume = Math.round(newValue); uploadProfile() } } }

                // Max weight (independent, app-side exit)
                Text { text: TranslationManager.translate("profileEditor.maxWeight", "Weight (g)"); font: Theme.captionFont; color: Theme.weightColor }
                ValueInput { Layout.fillWidth: true; valueColor: Theme.weightColor; accessibleName: "Max weight"; from: 0; to: 100; stepSize: 0.5; value: stepVersion >= 0 && step ? (step.exit_weight || 0) : 0; onValueModified: function(newValue) { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].exit_weight = Math.round(newValue * 2) / 2; uploadProfile() } } }

                // Flow/Pressure limit (opposite of goal in section 2)
                Text { text: step && step.pump === "pressure" ? TranslationManager.translate("profileEditor.maxFlow", "Flow limit (mL/s)") : TranslationManager.translate("profileEditor.maxPressure", "Pressure limit (bar)"); font: Theme.captionFont; color: step && step.pump === "pressure" ? Theme.flowColor : Theme.pressureColor }
                ValueInput {
                    Layout.fillWidth: true
                    valueColor: step && step.pump === "pressure" ? Theme.flowColor : Theme.pressureColor
                    accessibleName: step && step.pump === "pressure" ? "Flow limit" : "Pressure limit"
                    from: 0; to: step && step.pump === "pressure" ? 8 : 12; stepSize: 0.1
                    value: { var v = stepVersion; return step ? (step.max_flow_or_pressure || 0) : 0 }
                    onValueModified: function(newValue) { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].max_flow_or_pressure = Math.round(newValue * 10) / 10; uploadProfile() } }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // ── Section 4: Move on if... ──
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    Text { text: "4:"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.primaryColor }
                    Text { text: TranslationManager.translate("profileEditor.moveOnIf", "Move on if..."); font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.textColor }
                    Item { Layout.fillWidth: true }
                    Switch {
                        id: exitIfSwitch
                        checked: { var v = stepVersion; return step ? step.exit_if : false }
                        onToggled: { if (profile && selectedStepIndex >= 0) { profile.steps[selectedStepIndex].exit_if = checked; uploadProfile() } }
                        Accessible.name: "Move on if condition met"
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
                        currentIndex: { var v = stepVersion; if (!step) return 0; switch (step.exit_type) { case "pressure_over": return 0; case "pressure_under": return 1; case "flow_over": return 2; case "flow_under": return 3; default: return 0 } }
                        onActivated: function(index) { if (!profile || selectedStepIndex < 0) return; var types = ["pressure_over", "pressure_under", "flow_over", "flow_under"]; profile.steps[selectedStepIndex].exit_type = types[index]; uploadProfile() }
                    }

                    // Exit value slider
                    Text { text: TranslationManager.translate("profileEditor.exitValue", "Value"); font: Theme.captionFont; color: step && (step.exit_type === "flow_over" || step.exit_type === "flow_under") ? Theme.flowColor : Theme.pressureColor }
                    ValueInput {
                        Layout.fillWidth: true
                        valueColor: step && (step.exit_type === "flow_over" || step.exit_type === "flow_under") ? Theme.flowColor : Theme.pressureColor
                        accessibleName: "Exit value"
                        from: 0; to: { if (!step) return 12; switch (step.exit_type) { case "flow_over": case "flow_under": return 8; default: return 12 } }
                        stepSize: 0.1
                        value: { var v = stepVersion; if (!step) return 0; switch (step.exit_type) { case "pressure_over": return step.exit_pressure_over || 0; case "pressure_under": return step.exit_pressure_under || 0; case "flow_over": return step.exit_flow_over || 0; case "flow_under": return step.exit_flow_under || 0; default: return 0 } }
                        onValueModified: function(newValue) {
                            if (!profile || selectedStepIndex < 0) return
                            var val = Math.round(newValue * 10) / 10
                            switch (profile.steps[selectedStepIndex].exit_type) {
                                case "pressure_over": profile.steps[selectedStepIndex].exit_pressure_over = val; break
                                case "pressure_under": profile.steps[selectedStepIndex].exit_pressure_under = val; break
                                case "flow_over": profile.steps[selectedStepIndex].exit_flow_over = val; break
                                case "flow_under": profile.steps[selectedStepIndex].exit_flow_under = val; break
                            }
                            uploadProfile()
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // Popup message
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    Text { text: TranslationManager.translate("profileEditor.popupMessage", "Popup Message"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    StyledTextField {
                        Accessible.name: "Popup message"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        text: { var v = stepVersion; return step ? (step.popup || "") : "" }
                        font: Theme.bodyFont; color: Theme.textColor
                        placeholder: TranslationManager.translate("profileEditor.popupMessagePlaceholder", "e.g., Swirl now, $weight")
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

        StyledTextField {
            id: nameField
            Accessible.name: "Profile name"
            anchors.left: parent.left; anchors.right: parent.right
            text: profile ? profile.title : ""
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
