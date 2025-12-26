import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App
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

    function updatePageTitle() {
        root.currentPageTitle = profile ? profile.title : "Profile Editor"
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

    // Main content area
    Item {
        anchors.top: parent.top
        anchors.topMargin: 80
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
                    anchors.margins: Theme.scaled(10)
                    spacing: Theme.scaled(10)

                    // Frame toolbar
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(10)

                        Text {
                            text: "Frames"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "+ Add"
                            onClicked: addStep()
                            background: Rectangle {
                                implicitWidth: Theme.scaled(80)
                                implicitHeight: Theme.scaled(32)
                                radius: Theme.scaled(6)
                                color: parent.down ? Qt.darker(Theme.accentColor, 1.2) : Theme.accentColor
                            }
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: "white"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Button {
                            text: "Delete"
                            enabled: selectedStepIndex >= 0
                            onClicked: deleteStep(selectedStepIndex)
                            background: Rectangle {
                                implicitWidth: Theme.scaled(80)
                                implicitHeight: Theme.scaled(32)
                                radius: Theme.scaled(6)
                                color: parent.down ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor
                                opacity: parent.enabled ? 1.0 : 0.4
                            }
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: "white"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                opacity: parent.enabled ? 1.0 : 0.5
                            }
                        }

                        Button {
                            text: "\u2190"
                            enabled: selectedStepIndex > 0
                            onClicked: moveStep(selectedStepIndex, selectedStepIndex - 1)
                            background: Rectangle {
                                implicitWidth: Theme.scaled(36)
                                implicitHeight: Theme.scaled(32)
                                radius: Theme.scaled(6)
                                color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                                opacity: parent.enabled ? 1.0 : 0.4
                            }
                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: Theme.scaled(18)
                                color: "white"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                opacity: parent.enabled ? 1.0 : 0.5
                            }
                        }

                        Button {
                            text: "\u2192"
                            enabled: selectedStepIndex >= 0 && selectedStepIndex < (profile ? profile.steps.length - 1 : 0)
                            onClicked: moveStep(selectedStepIndex, selectedStepIndex + 1)
                            background: Rectangle {
                                implicitWidth: Theme.scaled(36)
                                implicitHeight: Theme.scaled(32)
                                radius: Theme.scaled(6)
                                color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                                opacity: parent.enabled ? 1.0 : 0.4
                            }
                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: Theme.scaled(18)
                                color: "white"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                opacity: parent.enabled ? 1.0 : 0.5
                            }
                        }
                    }

                    // Profile graph
                    ProfileGraph {
                        id: profileGraph
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.bottomMargin: Theme.scaled(30)  // Space for legend
                        frames: profile ? profile.steps : []
                        selectedFrameIndex: selectedStepIndex

                        onFrameSelected: function(index) {
                            selectedStepIndex = index
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

                Loader {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    sourceComponent: selectedStepIndex >= 0 ? stepEditorComponent : noSelectionComponent
                }
            }
        }
    }

    // Bottom bar
    Rectangle {
        id: bottomBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.scaled(70)
        color: Theme.primaryColor

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.scaled(10)
            anchors.rightMargin: Theme.scaled(20)
            spacing: Theme.scaled(15)

            // Back button - handle unsaved changes
            RoundButton {
                Layout.preferredWidth: 100
                Layout.preferredHeight: 70
                icon.source: "qrc:/icons/back.svg"
                icon.width: 28
                icon.height: 28
                flat: true
                icon.color: "white"
                onClicked: {
                    if (profileModified) {
                        exitDialog.open()
                    } else {
                        root.goBack()
                    }
                }
            }

            // Profile name
            Text {
                text: profile ? profile.title : "Profile"
                color: "white"
                font.pixelSize: 20
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            // Modified indicator
            Text {
                text: profileModified ? "\u2022 Modified" : ""
                color: "#FFCC00"
                font: Theme.bodyFont
                visible: profileModified
            }

            Rectangle { width: 1; height: 30; color: "white"; opacity: 0.3; visible: profile }

            // Frame count
            Text {
                text: profile ? profile.steps.length + " frames" : ""
                color: "white"
                font: Theme.bodyFont
            }

            Rectangle { width: 1; height: 30; color: "white"; opacity: 0.3; visible: profile }

            // Target weight
            Text {
                text: profile ? profile.target_weight.toFixed(0) + "g" : ""
                color: "white"
                font: Theme.bodyFont
            }

            // Done button
            Button {
                text: "Done"
                onClicked: {
                    if (profileModified) {
                        exitDialog.open()
                    } else {
                        root.goBack()
                    }
                }
                background: Rectangle {
                    implicitWidth: Theme.scaled(100)
                    implicitHeight: Theme.scaled(40)
                    radius: Theme.scaled(8)
                    color: parent.down ? Qt.darker("white", 1.2) : "white"
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.bodyFont
                    color: Theme.primaryColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // Save As dialog
    Dialog {
        id: saveAsDialog
        title: "Save Profile As"
        anchors.centerIn: parent
        width: Theme.scaled(400)
        modal: true
        standardButtons: Dialog.Save | Dialog.Cancel

        ColumnLayout {
            width: parent.width
            spacing: Theme.scaled(15)

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(5)

                Text {
                    text: "Profile Title"
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                }

                TextField {
                    id: saveAsTitleField
                    Layout.fillWidth: true
                    text: profile ? profile.title : ""
                    font: Theme.bodyFont
                    placeholderText: "Enter profile title"
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(5)

                Text {
                    text: "Filename"
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                }

                TextField {
                    id: saveAsFilenameField
                    Layout.fillWidth: true
                    text: originalProfileName || "my_profile"
                    font: Theme.bodyFont
                    placeholderText: "Enter filename (without .json)"
                    validator: RegularExpressionValidator { regularExpression: /[a-zA-Z0-9_-]+/ }
                }
            }
        }

        onAccepted: {
            if (saveAsFilenameField.text.length > 0 && saveAsTitleField.text.length > 0) {
                saveProfileAs(saveAsFilenameField.text, saveAsTitleField.text)
                root.goBack()
            }
        }

        onOpened: {
            saveAsTitleField.text = profile ? profile.title : ""
            saveAsFilenameField.text = originalProfileName || "my_profile"
            saveAsTitleField.forceActiveFocus()
        }
    }

    // Exit dialog for unsaved changes
    Dialog {
        id: exitDialog
        title: "Unsaved Changes"
        anchors.centerIn: parent
        width: Theme.scaled(480)
        padding: Theme.scaled(20)
        modal: true

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.scaled(20)

            Text {
                text: "You have unsaved changes to this profile.\nWhat would you like to do?"
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(10)

                Button {
                    text: "Discard"
                    onClicked: {
                        // Reload original profile to discard changes
                        if (originalProfileName) {
                            MainController.loadProfile(originalProfileName)
                        }
                        exitDialog.close()
                        root.goBack()
                    }
                    background: Rectangle {
                        implicitWidth: Theme.scaled(90)
                        implicitHeight: Theme.scaled(40)
                        radius: Theme.scaled(8)
                        color: parent.down ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Save As..."
                    onClicked: {
                        exitDialog.close()
                        saveAsDialog.open()
                    }
                    background: Rectangle {
                        implicitWidth: Theme.scaled(100)
                        implicitHeight: Theme.scaled(40)
                        radius: Theme.scaled(8)
                        color: parent.down ? Qt.darker(Theme.accentColor, 1.2) : Theme.accentColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    text: "Save"
                    enabled: originalProfileName !== ""
                    onClicked: {
                        saveProfile()
                        exitDialog.close()
                        root.goBack()
                    }
                    background: Rectangle {
                        implicitWidth: Theme.scaled(80)
                        implicitHeight: Theme.scaled(40)
                        radius: Theme.scaled(8)
                        color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                        opacity: parent.enabled ? 1.0 : 0.4
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

    // No selection placeholder
    Component {
        id: noSelectionComponent

        Item {
            Column {
                anchors.centerIn: parent
                spacing: Theme.scaled(15)

                Text {
                    text: "Select a frame"
                    font: Theme.titleFont
                    color: Theme.textSecondaryColor
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Text {
                    text: "Click on the graph to select\na frame for editing"
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

                    Text {
                        text: "Frame Name"
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
                        background: Rectangle {
                            color: Qt.rgba(255, 255, 255, 0.1)
                            radius: Theme.scaled(4)
                        }
                        onTextChanged: {
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
                    title: "Pump Mode"
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
                            text: "Pressure"
                            checked: step && step.pump === "pressure"
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            onCheckedChanged: {
                                if (checked && profile && selectedStepIndex >= 0 && profile.steps[selectedStepIndex].pump !== "pressure") {
                                    profile.steps[selectedStepIndex].pump = "pressure"
                                    uploadProfile()
                                }
                            }
                        }

                        RadioButton {
                            text: "Flow"
                            checked: step && step.pump === "flow"
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            onCheckedChanged: {
                                if (checked && profile && selectedStepIndex >= 0 && profile.steps[selectedStepIndex].pump !== "flow") {
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
                        text: step && step.pump === "flow" ? "Flow" : "Pressure"
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

                    Text {
                        text: "Temp"
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

                    Text {
                        text: "Duration"
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
                    title: "Transition"
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
                            text: "Fast"
                            checked: step && step.transition === "fast"
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            onCheckedChanged: {
                                if (checked && profile && selectedStepIndex >= 0 && profile.steps[selectedStepIndex].transition !== "fast") {
                                    profile.steps[selectedStepIndex].transition = "fast"
                                    uploadProfile()
                                }
                            }
                        }

                        RadioButton {
                            text: "Smooth"
                            checked: step && step.transition === "smooth"
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            onCheckedChanged: {
                                if (checked && profile && selectedStepIndex >= 0 && profile.steps[selectedStepIndex].transition !== "smooth") {
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
                    title: "Exit Condition"
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
                            text: "Enable early exit"
                            checked: step ? step.exit_if : false
                            contentItem: Text {
                                text: parent.text
                                font: Theme.bodyFont
                                color: Theme.textColor
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            onCheckedChanged: {
                                if (profile && selectedStepIndex >= 0 && profile.steps[selectedStepIndex].exit_if !== checked) {
                                    profile.steps[selectedStepIndex].exit_if = checked
                                    uploadProfile()
                                }
                            }
                        }

                        ComboBox {
                            id: exitTypeCombo
                            Layout.fillWidth: true
                            enabled: exitIfCheck.checked
                            model: ["Pressure Over", "Pressure Under", "Flow Over", "Flow Under"]
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
                                    default: return 0
                                }
                            }
                            onCurrentIndexChanged: {
                                if (!profile || selectedStepIndex < 0) return
                                var types = ["pressure_over", "pressure_under", "flow_over", "flow_under"]
                                if (profile.steps[selectedStepIndex].exit_type !== types[currentIndex]) {
                                    profile.steps[selectedStepIndex].exit_type = types[currentIndex]
                                    uploadProfile()
                                }
                            }
                        }

                        ValueInput {
                            id: exitValueInput
                            Layout.fillWidth: true
                            enabled: exitIfCheck.checked
                            from: 0
                            to: step && (step.exit_type === "flow_over" || step.exit_type === "flow_under") ? 8 : 12
                            value: {
                                if (!step) return 0
                                switch (step.exit_type) {
                                    case "pressure_over": return step.exit_pressure_over || 0
                                    case "pressure_under": return step.exit_pressure_under || 0
                                    case "flow_over": return step.exit_flow_over || 0
                                    case "flow_under": return step.exit_flow_under || 0
                                    default: return 0
                                }
                            }
                            stepSize: 0.1
                            suffix: step && (step.exit_type === "flow_over" || step.exit_type === "flow_under") ? " mL/s" : " bar"
                            onValueModified: function(newValue) {
                                if (!profile || selectedStepIndex < 0) return
                                var s = profile.steps[selectedStepIndex]
                                switch (s.exit_type) {
                                    case "pressure_over": profile.steps[selectedStepIndex].exit_pressure_over = newValue; break
                                    case "pressure_under": profile.steps[selectedStepIndex].exit_pressure_under = newValue; break
                                    case "flow_over": profile.steps[selectedStepIndex].exit_flow_over = newValue; break
                                    case "flow_under": profile.steps[selectedStepIndex].exit_flow_under = newValue; break
                                }
                                uploadProfile()
                            }
                        }
                    }
                }

                // Limiter (max pressure when flow-controlled, or max flow when pressure-controlled)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    Text {
                        text: step && step.pump === "flow" ? "Max P" : "Max F"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Layout.preferredWidth: Theme.scaled(80)
                    }

                    ValueInput {
                        id: limiterInput
                        Layout.fillWidth: true
                        from: 0
                        to: step && step.pump === "flow" ? 12 : 8
                        value: step ? step.max_flow_or_pressure : 0
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

                // Spacer
                Item { Layout.fillHeight: true }
            }
        }
    }

    // Profile name edit dialog
    Dialog {
        id: profileNameDialog
        title: "Edit Profile Name"
        anchors.centerIn: parent
        width: Theme.scaled(400)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        TextField {
            id: nameField
            width: parent.width
            text: profile ? profile.title : ""
            font: Theme.bodyFont
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

    // Helper functions
    function addStep() {
        if (!profile) return

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
            exit_if: false,
            exit_type: "pressure_over",
            exit_pressure_over: 0,
            exit_pressure_under: 0,
            exit_flow_over: 0,
            exit_flow_under: 0,
            max_flow_or_pressure: 0,
            max_flow_or_pressure_range: 0.6
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
        profile = {
            title: MainController.currentProfileName || "New Profile",
            steps: [],
            target_weight: MainController.targetWeight || 36,
            espresso_temperature: 93,
            mode: "frame_based"
        }
        // Try to load actual profile data
        var loadedProfile = MainController.getCurrentProfile()
        if (loadedProfile) {
            profile = loadedProfile
        }
        // Track the original profile name for saving
        originalProfileName = MainController.currentProfileName || ""
        profileModified = false
        selectedStepIndex = -1
        updatePageTitle()
    }

    // Reload profile when page becomes active (StackView reactivation)
    StackView.onActivating: {
        loadCurrentProfile()
    }

    Component.onCompleted: {
        loadCurrentProfile()
    }
}
