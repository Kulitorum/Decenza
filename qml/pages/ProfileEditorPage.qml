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
    property bool profileModified: false
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
        root.currentPageTitle = profile ? profile.title : qsTr("Profile Editor")
    }

    // Auto-upload profile to machine on any change
    function uploadProfile() {
        if (profile) {
            MainController.uploadProfile(profile)
            profileModified = true
            // Force step editor bindings to re-evaluate
            stepVersion++
            // Force graph to update by creating a new array reference
            // (assigning same reference doesn't trigger onFramesChanged)
            profileGraph.frames = profile.steps.slice()
        }
    }

    // Save profile to file
    function saveProfile() {
        if (profile && originalProfileName) {
            MainController.saveProfile(originalProfileName)
            profileModified = false
        }
    }

    // Save profile with new name
    function saveProfileAs(filename, title) {
        if (profile) {
            MainController.saveProfileAs(filename, title)
            originalProfileName = filename
            profileModified = false
        }
    }

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
                text: qsTr("Advanced Editor")
                font.family: Theme.titleFont.family
                font.pixelSize: Theme.titleFont.pixelSize
                font.bold: true
                color: "white"
            }

            Text {
                text: qsTr("Full frame-by-frame control • Click frames to edit")
                font: Theme.captionFont
                color: Qt.rgba(1, 1, 1, 0.8)
                Layout.fillWidth: true
            }

            StyledButton {
                text: qsTr("Switch to D-Flow Editor")
                subtle: true
                onClicked: switchToDFlowDialog.open()
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

                            StyledButton {
                                primary: true
                                text: qsTr("+ Add")
                                onClicked: addStep()
                            }

                            StyledButton {
                                id: deleteFrameBtn
                                text: qsTr("Delete")
                                enabled: selectedStepIndex >= 0 && profile && profile.steps.length > 1
                                onClicked: deleteStep(selectedStepIndex)
                                background: Rectangle {
                                    implicitWidth: deleteFrameBtn.implicitWidth
                                    implicitHeight: Theme.scaled(36)
                                    radius: Theme.scaled(6)
                                    color: deleteFrameBtn.down ? Qt.darker(Theme.errorColor, 1.1) : Theme.errorColor
                                    opacity: deleteFrameBtn.enabled ? 1.0 : 0.4
                                }
                                contentItem: Text {
                                    text: deleteFrameBtn.text
                                    font.pixelSize: Theme.scaled(14)
                                    font.family: Theme.bodyFont.family
                                    color: "white"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    opacity: deleteFrameBtn.enabled ? 1.0 : 0.5
                                }
                            }

                            StyledButton {
                                primary: true
                                text: qsTr("Copy")
                                enabled: selectedStepIndex >= 0 && profile && profile.steps.length < 20
                                onClicked: duplicateStep(selectedStepIndex)
                            }

                            StyledIconButton {
                                text: "\u2190"
                                accessibleName: qsTr("Move frame left")
                                enabled: selectedStepIndex > 0
                                onClicked: moveStep(selectedStepIndex, selectedStepIndex - 1)
                            }

                            StyledIconButton {
                                text: "\u2192"
                                accessibleName: qsTr("Move frame right")
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

                            onFrameSelected: function(index) {
                                selectedStepIndex = index
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

                    // Profile settings button
                    AccessibleButton {
                        Layout.fillWidth: true
                        visible: profile !== null
                        text: qsTr("Profile Settings") + (profile ? " (" + profile.target_weight.toFixed(0) + "g, " + (profile.steps.length > 0 ? profile.steps[0].temperature.toFixed(0) : "93") + "°C)" : "")
                        accessibleName: qsTr("Open profile settings")
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
        width: Theme.scaled(320)
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.width: 1
            border.color: Theme.textSecondaryColor
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.scaled(15)
            spacing: Theme.scaled(15)

            Text {
                text: qsTr("Profile Settings")
                font: Theme.titleFont
                color: Theme.textColor
            }

            // Stop at weight
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                Text {
                    text: qsTr("Stop at")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.preferredWidth: Theme.scaled(80)
                }

                ValueInput {
                    id: targetWeightInput
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: {
                        stepVersion  // Force re-evaluation on profile changes
                        return profile ? profile.target_weight : 36
                    }
                    stepSize: 1
                    suffix: "g"
                    valueColor: Theme.weightColor
                    accentColor: Theme.weightColor
                    onValueModified: function(newValue) {
                        if (profile) {
                            profile.target_weight = newValue
                            uploadProfile()
                        }
                    }
                }
            }

            // Global temperature (applies to all frames)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                Text {
                    text: qsTr("All temps")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.preferredWidth: Theme.scaled(80)
                }

                ValueInput {
                    id: globalTempInput
                    Layout.fillWidth: true
                    from: 70
                    to: 100
                    value: {
                        stepVersion  // Force re-evaluation on profile changes
                        return profile && profile.steps.length > 0 ? profile.steps[0].temperature : 93
                    }
                    stepSize: 0.5
                    suffix: "\u00B0C"
                    valueColor: Theme.temperatureColor
                    accentColor: Theme.temperatureGoalColor
                    onValueModified: function(newValue) {
                        if (profile && profile.steps.length > 0) {
                            for (var i = 0; i < profile.steps.length; i++) {
                                profile.steps[i].temperature = newValue
                            }
                            // Also update the profile-level espresso_temperature
                            profile.espresso_temperature = newValue
                            uploadProfile()
                        }
                    }
                }
            }

            // Close button
            AccessibleButton {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Done")
                accessibleName: qsTr("Close profile settings")
                onClicked: profileSettingsPopup.close()
                background: Rectangle {
                    implicitWidth: Theme.scaled(80)
                    implicitHeight: Theme.scaled(36)
                    color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    radius: Theme.scaled(8)
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

    // Bottom bar
    BottomBar {
        id: bottomBar
        title: profile ? profile.title : qsTr("Profile")
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
            text: profile ? profile.steps.length + " " + qsTr("frames") : ""
            color: "white"
            font: Theme.bodyFont
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: "white"; opacity: 0.3; visible: profile }
        Text {
            text: profile ? profile.target_weight.toFixed(0) + "g" : ""
            color: "white"
            font: Theme.bodyFont
        }
        StyledButton {
            text: qsTr("Done")
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

    // Save As dialog - just title input, filename derived automatically
    Dialog {
        id: saveAsDialog
        title: qsTr("Save Profile As")
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
                    saveProfileAs(filename, saveAsTitleField.text)
                    root.goBack()
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
        title: qsTr("Profile Exists")
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
            saveProfileAs(saveAsDialog.pendingFilename, saveAsTitleField.text)
            root.goBack()
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
            saveProfile()
            root.goBack()
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
            contentWidth: availableWidth  // Disable horizontal scroll

            // Reference stepVersion to force re-evaluation when it changes
            property var step: {
                stepVersion  // Dependency trigger
                return profile && selectedStepIndex >= 0 && selectedStepIndex < profile.steps.length ?
                       profile.steps[selectedStepIndex] : null
            }

            ColumnLayout {
                width: stepEditorScroll.width - Theme.scaled(10)
                spacing: Theme.scaled(15)

                // Frame name
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    Tr {
                        key: "profileeditor.label.framename"
                        fallback: "Frame Name"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }

                    TextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(45)
                        text: step ? step.name : ""
                        font: Theme.titleFont
                        color: Theme.textColor
                        placeholderTextColor: Theme.textSecondaryColor
                        leftPadding: Theme.scaled(12)
                        rightPadding: Theme.scaled(12)
                        topPadding: Theme.scaled(12)
                        bottomPadding: Theme.scaled(12)
                        background: Rectangle {
                            color: Qt.rgba(255, 255, 255, 0.1)
                            radius: Theme.scaled(4)
                        }
                        onEditingFinished: {
                            if (profile && selectedStepIndex >= 0 && profile.steps[selectedStepIndex].name !== text) {
                                profile.steps[selectedStepIndex].name = text
                                uploadProfile()
                            }
                        }
                    }
                }

                // Pump mode
                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Pump Mode")
                    background: Rectangle {
                        color: Qt.rgba(255, 255, 255, 0.05)
                        radius: Theme.scaled(8)
                        y: parent.topPadding - parent.padding
                        width: parent.width
                        height: parent.height - parent.topPadding + parent.padding
                    }
                    label: Text {
                        text: parent.title
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }

                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.scaled(20)

                        RadioButton {
                            text: qsTr("Pressure")
                            checked: step && step.pump === "pressure"
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            onToggled: {
                                if (checked && profile && selectedStepIndex >= 0) {
                                    profile.steps[selectedStepIndex].pump = "pressure"
                                    uploadProfile()
                                }
                            }
                        }

                        RadioButton {
                            text: qsTr("Flow")
                            checked: step && step.pump === "flow"
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            onToggled: {
                                if (checked && profile && selectedStepIndex >= 0) {
                                    profile.steps[selectedStepIndex].pump = "flow"
                                    uploadProfile()
                                }
                            }
                        }
                    }
                }

                // Setpoint value (pressure or flow)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    Text {
                        text: step && step.pump === "flow" ? qsTr("Flow") : qsTr("Pressure")
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Layout.preferredWidth: Theme.scaled(80)
                    }

                    ValueInput {
                        id: setpointInput
                        Layout.fillWidth: true
                        from: 0
                        to: step && step.pump === "flow" ? 8 : 12
                        value: step ? (step.pump === "flow" ? step.flow : step.pressure) : 0
                        stepSize: 0.1
                        suffix: step && step.pump === "flow" ? " mL/s" : " bar"
                        valueColor: step && step.pump === "flow" ? Theme.flowColor : Theme.pressureColor
                        accentColor: step && step.pump === "flow" ? Theme.flowGoalColor : Theme.pressureGoalColor
                        onValueModified: function(newValue) {
                            if (profile && selectedStepIndex >= 0) {
                                if (profile.steps[selectedStepIndex].pump === "flow") {
                                    profile.steps[selectedStepIndex].flow = newValue
                                } else {
                                    profile.steps[selectedStepIndex].pressure = newValue
                                }
                                uploadProfile()
                            }
                        }
                    }
                }

                // Temperature
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    Tr {
                        key: "profileeditor.label.temp"
                        fallback: "Temp"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Layout.preferredWidth: Theme.scaled(80)
                    }

                    ValueInput {
                        id: tempInput
                        Layout.fillWidth: true
                        from: 70
                        to: 100
                        value: step ? step.temperature : 93
                        stepSize: 0.1
                        suffix: "\u00B0C"
                        valueColor: Theme.temperatureColor
                        accentColor: Theme.temperatureGoalColor
                        onValueModified: function(newValue) {
                            if (profile && selectedStepIndex >= 0) {
                                profile.steps[selectedStepIndex].temperature = newValue
                                uploadProfile()
                            }
                        }
                    }
                }

                // Duration
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    Tr {
                        key: "profileeditor.label.duration"
                        fallback: "Duration"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Layout.preferredWidth: Theme.scaled(80)
                    }

                    ValueInput {
                        id: durationInput
                        Layout.fillWidth: true
                        from: 0
                        to: 120
                        value: step ? step.seconds : 30
                        stepSize: 1
                        decimals: 0
                        suffix: "s"
                        accentColor: Theme.accentColor
                        onValueModified: function(newValue) {
                            if (profile && selectedStepIndex >= 0) {
                                profile.steps[selectedStepIndex].seconds = newValue
                                uploadProfile()
                            }
                        }
                    }
                }

                // Transition
                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Transition")
                    background: Rectangle {
                        color: Qt.rgba(255, 255, 255, 0.05)
                        radius: Theme.scaled(8)
                        y: parent.topPadding - parent.padding
                        width: parent.width
                        height: parent.height - parent.topPadding + parent.padding
                    }
                    label: Text {
                        text: parent.title
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }

                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.scaled(20)

                        RadioButton {
                            text: qsTr("Fast")
                            checked: step && step.transition === "fast"
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            onToggled: {
                                if (checked && profile && selectedStepIndex >= 0) {
                                    profile.steps[selectedStepIndex].transition = "fast"
                                    uploadProfile()
                                }
                            }
                        }

                        RadioButton {
                            text: qsTr("Smooth")
                            checked: step && step.transition === "smooth"
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            onToggled: {
                                if (checked && profile && selectedStepIndex >= 0) {
                                    profile.steps[selectedStepIndex].transition = "smooth"
                                    uploadProfile()
                                }
                            }
                        }
                    }
                }

                // Exit conditions (collapsible)
                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Exit Condition")
                    background: Rectangle {
                        color: Qt.rgba(255, 255, 255, 0.05)
                        radius: Theme.scaled(8)
                        y: parent.topPadding - parent.padding
                        width: parent.width
                        height: parent.height - parent.topPadding + parent.padding
                    }
                    label: Text {
                        text: parent.title
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Theme.scaled(10)

                        CheckBox {
                            id: exitIfCheck
                            text: qsTr("Enable early exit")
                            checked: step ? step.exit_if : false
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            onToggled: {
                                if (profile && selectedStepIndex >= 0) {
                                    profile.steps[selectedStepIndex].exit_if = checked
                                    uploadProfile()
                                }
                            }
                        }

                        StyledComboBox {
                            id: exitTypeCombo
                            Layout.fillWidth: true
                            enabled: exitIfCheck.checked
                            model: [qsTr("Pressure Over"), qsTr("Pressure Under"), qsTr("Flow Over"), qsTr("Flow Under"), qsTr("Weight Over")]
                            contentItem: Text {
                                text: exitTypeCombo.displayText
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: Theme.scaled(10)
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                implicitHeight: Theme.scaled(36)
                                color: Qt.rgba(255, 255, 255, 0.1)
                                radius: Theme.scaled(6)
                            }
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
                                background: Rectangle {
                                    color: Theme.surfaceColor
                                    radius: Theme.scaled(6)
                                }
                            }
                            delegate: ItemDelegate {
                                width: exitTypeCombo.width
                                contentItem: Text {
                                    text: modelData
                                    font: Theme.bodyFont
                                    color: Theme.textColor
                                }
                                background: Rectangle {
                                    color: highlighted ? Theme.primaryColor : "transparent"
                                }
                                highlighted: exitTypeCombo.highlightedIndex === index
                            }
                            currentIndex: {
                                if (!step) return 0
                                switch (step.exit_type) {
                                    case "pressure_over": return 0
                                    case "pressure_under": return 1
                                    case "flow_over": return 2
                                    case "flow_under": return 3
                                    case "weight": return 4
                                    default: return 0
                                }
                            }
                            onActivated: function(index) {
                                if (!profile || selectedStepIndex < 0) return
                                var types = ["pressure_over", "pressure_under", "flow_over", "flow_under", "weight"]
                                profile.steps[selectedStepIndex].exit_type = types[index]
                                uploadProfile()
                            }
                        }

                        ValueInput {
                            id: exitValueInput
                            Layout.fillWidth: true
                            enabled: exitIfCheck.checked
                            from: 0
                            to: {
                                if (!step) return 12
                                switch (step.exit_type) {
                                    case "flow_over":
                                    case "flow_under":
                                        return 8
                                    case "weight":
                                        return 100
                                    default:
                                        return 12
                                }
                            }
                            value: {
                                if (!step) return 0
                                switch (step.exit_type) {
                                    case "pressure_over": return step.exit_pressure_over || 0
                                    case "pressure_under": return step.exit_pressure_under || 0
                                    case "flow_over": return step.exit_flow_over || 0
                                    case "flow_under": return step.exit_flow_under || 0
                                    case "weight": return step.exit_weight || 0
                                    default: return 0
                                }
                            }
                            stepSize: step && step.exit_type === "weight" ? 0.5 : 0.1
                            suffix: {
                                if (!step) return " bar"
                                switch (step.exit_type) {
                                    case "flow_over":
                                    case "flow_under":
                                        return " mL/s"
                                    case "weight":
                                        return " g"
                                    default:
                                        return " bar"
                                }
                            }
                            valueColor: {
                                if (!step) return Theme.textSecondaryColor
                                switch (step.exit_type) {
                                    case "flow_over":
                                    case "flow_under":
                                        return Theme.flowColor
                                    case "weight":
                                        return Theme.weightColor
                                    case "pressure_over":
                                    case "pressure_under":
                                        return Theme.pressureColor
                                    default:
                                        return Theme.textSecondaryColor
                                }
                            }
                            accentColor: {
                                if (!step) return Theme.textSecondaryColor
                                switch (step.exit_type) {
                                    case "flow_over":
                                    case "flow_under":
                                        return Theme.flowGoalColor
                                    case "weight":
                                        return Theme.weightColor
                                    case "pressure_over":
                                    case "pressure_under":
                                        return Theme.pressureGoalColor
                                    default:
                                        return Theme.textSecondaryColor
                                }
                            }
                            onValueModified: function(newValue) {
                                if (!profile || selectedStepIndex < 0) return
                                var s = profile.steps[selectedStepIndex]
                                switch (s.exit_type) {
                                    case "pressure_over": profile.steps[selectedStepIndex].exit_pressure_over = newValue; break
                                    case "pressure_under": profile.steps[selectedStepIndex].exit_pressure_under = newValue; break
                                    case "flow_over": profile.steps[selectedStepIndex].exit_flow_over = newValue; break
                                    case "flow_under": profile.steps[selectedStepIndex].exit_flow_under = newValue; break
                                    case "weight": profile.steps[selectedStepIndex].exit_weight = newValue; break
                                }
                                uploadProfile()
                            }
                        }
                    }
                }

                // Sensor selection (coffee/water)
                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Sensor")
                    background: Rectangle {
                        color: Qt.rgba(255, 255, 255, 0.05)
                        radius: Theme.scaled(8)
                        y: parent.topPadding - parent.padding
                        width: parent.width
                        height: parent.height - parent.topPadding + parent.padding
                    }
                    label: Text {
                        text: parent.title
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }

                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.scaled(20)

                        RadioButton {
                            text: qsTr("Coffee")
                            checked: step && step.sensor === "coffee"
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            onToggled: {
                                if (checked && profile && selectedStepIndex >= 0) {
                                    profile.steps[selectedStepIndex].sensor = "coffee"
                                    uploadProfile()
                                }
                            }
                        }

                        RadioButton {
                            text: qsTr("Water")
                            checked: step && step.sensor === "water"
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            onToggled: {
                                if (checked && profile && selectedStepIndex >= 0) {
                                    profile.steps[selectedStepIndex].sensor = "water"
                                    uploadProfile()
                                }
                            }
                        }
                    }
                }

                // Maximum limits section
                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Maximum Limits")
                    background: Rectangle {
                        color: Qt.rgba(255, 255, 255, 0.05)
                        radius: Theme.scaled(8)
                        y: parent.topPadding - parent.padding
                        width: parent.width
                        height: parent.height - parent.topPadding + parent.padding
                    }
                    label: Text {
                        text: parent.title
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Theme.scaled(8)

                        // Volume limit
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            Text {
                                text: qsTr("Volume")
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                Layout.preferredWidth: Theme.scaled(60)
                            }

                            ValueInput {
                                Layout.fillWidth: true
                                from: 0
                                to: 500
                                value: step ? (step.volume || 0) : 0
                                stepSize: 1
                                decimals: 0
                                suffix: " mL"
                                valueColor: value > 0 ? Theme.flowColor : Theme.textSecondaryColor
                                accentColor: Theme.flowColor
                                onValueModified: function(newValue) {
                                    if (profile && selectedStepIndex >= 0) {
                                        profile.steps[selectedStepIndex].volume = newValue
                                        uploadProfile()
                                    }
                                }
                            }
                        }

                        // Weight limit (for exit)
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            Text {
                                text: qsTr("Weight")
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                Layout.preferredWidth: Theme.scaled(60)
                            }

                            ValueInput {
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                value: step ? (step.exit_weight || step.weight || 0) : 0
                                stepSize: 0.5
                                suffix: " g"
                                valueColor: value > 0 ? Theme.weightColor : Theme.textSecondaryColor
                                accentColor: Theme.weightColor
                                onValueModified: function(newValue) {
                                    if (profile && selectedStepIndex >= 0) {
                                        profile.steps[selectedStepIndex].exit_weight = newValue
                                        profile.steps[selectedStepIndex].weight = newValue
                                        uploadProfile()
                                    }
                                }
                            }
                        }
                    }
                }

                // Limiter section
                GroupBox {
                    Layout.fillWidth: true
                    title: step && step.pump === "flow" ? qsTr("Pressure Limit") : qsTr("Flow Limit")
                    background: Rectangle {
                        color: Qt.rgba(255, 255, 255, 0.05)
                        radius: Theme.scaled(8)
                        y: parent.topPadding - parent.padding
                        width: parent.width
                        height: parent.height - parent.topPadding + parent.padding
                    }
                    label: Text {
                        text: parent.title
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Theme.scaled(8)

                        // Limiter value
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            Text {
                                text: qsTr("Limit")
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                Layout.preferredWidth: Theme.scaled(60)
                            }

                            ValueInput {
                                Layout.fillWidth: true
                                from: 0
                                to: step && step.pump === "flow" ? 12 : 8
                                value: step ? (step.max_flow_or_pressure || 0) : 0
                                stepSize: 0.1
                                suffix: step && step.pump === "flow" ? " bar" : " mL/s"
                                valueColor: value > 0 ? Theme.warningColor : Theme.textSecondaryColor
                                accentColor: Theme.warningColor
                                onValueModified: function(newValue) {
                                    if (profile && selectedStepIndex >= 0) {
                                        profile.steps[selectedStepIndex].max_flow_or_pressure = newValue
                                        uploadProfile()
                                    }
                                }
                            }
                        }

                        // Limiter range (P/I control)
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            Text {
                                text: qsTr("Range")
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                Layout.preferredWidth: Theme.scaled(60)
                            }

                            ValueInput {
                                Layout.fillWidth: true
                                from: 0.1
                                to: 2.0
                                value: step ? (step.max_flow_or_pressure_range || 0.6) : 0.6
                                stepSize: 0.1
                                suffix: step && step.pump === "flow" ? " bar" : " mL/s"
                                valueColor: Theme.textSecondaryColor
                                accentColor: Theme.warningColor
                                onValueModified: function(newValue) {
                                    if (profile && selectedStepIndex >= 0) {
                                        profile.steps[selectedStepIndex].max_flow_or_pressure_range = newValue
                                        uploadProfile()
                                    }
                                }
                            }
                        }
                    }
                }

                // Popup message
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    Text {
                        text: qsTr("Popup Message")
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }

                    TextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        text: step ? (step.popup || "") : ""
                        font: Theme.bodyFont
                        color: Theme.textColor
                        placeholderText: qsTr("e.g., Swirl now, $weight")
                        placeholderTextColor: Theme.textSecondaryColor
                        leftPadding: Theme.scaled(12)
                        rightPadding: Theme.scaled(12)
                        topPadding: Theme.scaled(10)
                        bottomPadding: Theme.scaled(10)
                        background: Rectangle {
                            color: Qt.rgba(255, 255, 255, 0.1)
                            radius: Theme.scaled(4)
                        }
                        onEditingFinished: {
                            if (profile && selectedStepIndex >= 0) {
                                profile.steps[selectedStepIndex].popup = text
                                uploadProfile()
                            }
                        }
                    }
                }

                // Spacer
                Item { Layout.fillHeight: true }
            }
        }
    }

    // Profile name edit dialog
    Dialog {
        id: profileNameDialog
        title: qsTr("Edit Profile Name")
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

    // Switch to D-Flow confirmation dialog
    Dialog {
        id: switchToDFlowDialog
        anchors.centerIn: parent
        width: Theme.scaled(450)
        modal: true
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: "white"
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
                    text: qsTr("Switch to D-Flow Editor")
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

            // Content
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(12)

                Text {
                    Layout.fillWidth: true
                    text: qsTr("This will simplify the profile to fit the D-Flow format.")
                    font: Theme.bodyFont
                    color: Theme.textColor
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("The converter will attempt to retain the main idea of your profile, but advanced settings like custom exit conditions, per-frame weight exits, and popup messages may be lost.")
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("D-Flow profiles use a fixed structure: Fill → Bloom → Infuse → Ramp → Pour → Decline")
                    font: Theme.captionFont
                    color: Theme.warningColor
                    wrapMode: Text.WordWrap
                }
            }

            // Buttons
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                spacing: Theme.scaled(10)

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(50)
                    text: qsTr("Cancel")
                    accessibleName: qsTr("Cancel and stay in Advanced Editor")
                    onClicked: switchToDFlowDialog.close()
                    background: Rectangle {
                        radius: Theme.buttonRadius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.textSecondaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(50)
                    text: qsTr("Convert")
                    accessibleName: qsTr("Convert to D-Flow format")
                    onClicked: {
                        switchToDFlowDialog.close()
                        MainController.convertCurrentProfileToRecipe()
                        root.switchToRecipeEditor()
                    }
                    background: Rectangle {
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
                espresso_temperature: 93,
                mode: "frame_based"
            }
        }
        // Track the original profile filename for saving (not the title!)
        originalProfileName = MainController.baseProfileName || ""
        profileModified = false
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
