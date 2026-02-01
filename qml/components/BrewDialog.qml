import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    property real dialogScale: 0.75
    width: Theme.scaled(420) * dialogScale
    modal: true
    padding: 0

    // Accessibility: Dialog is announced via onAboutToShow below
    // Note: Don't set 'title' property - it creates a built-in header frame
    // Note: Don't set Accessible properties on Dialog - it doesn't derive from Item

    // Temperature override
    property double temperatureValue: MainController.profileTargetTemperature
    property double profileTemperature: MainController.profileTargetTemperature

    // Dose value (editable, default 18g)
    property double doseValue: 18.0
    property double ratio: Settings.lastUsedRatio

    // Target (yield) value and tracking
    property double targetValue: doseValue * ratio
    property double profileTargetWeight: MainController.targetWeight
    property bool targetManuallySet: false

    // Grinder and grind setting
    property string grinderModel: ""
    property string grindSetting: ""

    // Low dose warning - shown when dose is low OR when scale read failed
    property bool showScaleWarning: false
    property bool lowDoseWarning: doseValue < 3 || showScaleWarning

    // Recalculate target when dose or ratio changes (unless manually overridden)
    onDoseValueChanged: {
        if (!targetManuallySet) {
            targetValue = doseValue * ratio
        }
    }

    onRatioChanged: {
        if (!targetManuallySet) {
            targetValue = doseValue * ratio
        }
    }

    onAboutToShow: {
        // Announce dialog for accessibility
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            var announcement = qsTr("Brew Settings dialog. Profile: ") + MainController.currentProfileName
            if (Settings.dyeBeanBrand.length > 0 || Settings.dyeBeanType.length > 0) {
                announcement += ". " + qsTr("Beans: ")
                if (Settings.dyeBeanBrand.length > 0) announcement += Settings.dyeBeanBrand
                if (Settings.dyeBeanType.length > 0) announcement += " " + Settings.dyeBeanType
            }
            AccessibilityManager.announce(announcement)
        }

        // Update profile temperature, use override if active
        profileTemperature = MainController.profileTargetTemperature
        profileTargetWeight = MainController.targetWeight
        temperatureValue = Settings.hasTemperatureOverride ? Settings.temperatureOverride : profileTemperature

        // Use DYE fields for dose and grind (source of truth)
        doseValue = Settings.dyeBeanWeight > 0 ? Settings.dyeBeanWeight : 18.0
        grinderModel = Settings.dyeGrinderModel
        grindSetting = Settings.dyeGrinderSetting
        showScaleWarning = false

        // Yield: use override if set, otherwise use profile target weight
        if (Settings.hasBrewYieldOverride) {
            targetValue = Settings.brewYieldOverride
            ratio = doseValue > 0 ? targetValue / doseValue : Settings.lastUsedRatio
            targetManuallySet = true
        } else if (profileTargetWeight > 0) {
            targetValue = profileTargetWeight
            ratio = doseValue > 0 ? targetValue / doseValue : Settings.lastUsedRatio
            targetManuallySet = false
        } else {
            ratio = Settings.lastUsedRatio
            targetManuallySet = false
            targetValue = doseValue * ratio
        }
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: "white"
    }

    contentItem: Item {
        implicitHeight: mainColumn.implicitHeight * root.dialogScale
        implicitWidth: Theme.scaled(420) * root.dialogScale
        clip: true

        ColumnLayout {
            id: mainColumn
            width: Theme.scaled(420)
            scale: root.dialogScale
            transformOrigin: Item.TopLeft
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
                text: qsTr("Brew Settings")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true  // Dialog title announced on open
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderColor
            }
        }

        // Base Recipe Info
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(12)
            color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.1)
            radius: Theme.scaled(8)
            implicitHeight: recipeColumn.implicitHeight + Theme.scaled(16)

            // Accessibility: group the recipe info as one element
            Accessible.role: Accessible.StaticText
            Accessible.name: {
                var name = qsTr("Base Recipe: ") + MainController.currentProfileName
                if (Settings.dyeBeanBrand.length > 0 || Settings.dyeBeanType.length > 0) {
                    name += ". " + qsTr("Beans: ")
                    if (Settings.dyeBeanBrand.length > 0) name += Settings.dyeBeanBrand
                    if (Settings.dyeBeanType.length > 0) name += " " + Settings.dyeBeanType
                }
                return name
            }
            Accessible.focusable: true

            ColumnLayout {
                id: recipeColumn
                anchors.fill: parent
                anchors.margins: Theme.scaled(8)
                spacing: Theme.scaled(4)

                Text {
                    text: qsTr("Base Recipe")
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(11)
                    font.bold: true
                    color: Theme.textSecondaryColor
                    Accessible.ignored: true  // Parent handles accessibility
                }

                Text {
                    text: MainController.currentProfileName
                    font: Theme.bodyFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Accessible.ignored: true  // Parent handles accessibility
                }

                Text {
                    visible: Settings.dyeBeanBrand.length > 0 || Settings.dyeBeanType.length > 0
                    text: {
                        var parts = []
                        if (Settings.dyeBeanBrand.length > 0) parts.push(Settings.dyeBeanBrand)
                        if (Settings.dyeBeanType.length > 0) parts.push(Settings.dyeBeanType)
                        return parts.join(" - ")
                    }
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(12)
                    color: Theme.textSecondaryColor
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Accessible.ignored: true  // Parent handles accessibility
                }
            }
        }

        // Content
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(12)
            spacing: Theme.scaled(12)

            // Low dose warning
            Rectangle {
                Layout.fillWidth: true
                visible: root.lowDoseWarning
                color: Theme.surfaceColor
                border.width: 1
                border.color: Theme.warningColor
                radius: Theme.scaled(8)
                implicitHeight: warningText.implicitHeight + Theme.scaled(24)

                // Accessibility: announce warning and make it focusable
                Accessible.role: Accessible.AlertMessage
                Accessible.name: warningText.text
                Accessible.focusable: true

                // Announce warning when it becomes visible
                onVisibleChanged: {
                    if (visible && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                        AccessibilityManager.announce(qsTr("Warning: ") + warningText.text)
                    }
                }

                Text {
                    id: warningText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.scaled(12)
                    text: qsTr("Please put the portafilter with coffee on the scale")
                    font: Theme.bodyFont
                    color: Theme.warningColor
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.ignored: true  // Parent handles accessibility
                }
            }

            // Temperature input
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: qsTr("Temp:")
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: Theme.scaled(55)
                        Accessible.ignored: true  // Label for sighted users; input has accessibleName
                    }

                    ValueInput {
                        id: tempInput
                        Layout.fillWidth: true
                        value: root.temperatureValue
                        from: 70
                        to: 100
                        stepSize: 1
                        decimals: 0
                        suffix: "°C"
                        valueColor: Math.abs(root.temperatureValue - root.profileTemperature) > 0.1 ? Theme.temperatureColor : Theme.textSecondaryColor
                        accentColor: Theme.temperatureColor
                        accessibleName: qsTr("Brew temperature")
                        onValueModified: function(newValue) {
                            root.temperatureValue = newValue
                        }
                    }

                    // Save to profile button
                    AccessibleButton {
                        Layout.preferredHeight: Theme.scaled(56)
                        text: qsTr("Save")
                        accessibleName: qsTr("Save temperature to profile")
                        primary: true
                        enabled: Math.abs(root.temperatureValue - root.profileTemperature) > 0.1
                        onClicked: {
                            var profile = MainController.getCurrentProfile()
                            if (profile && profile.steps.length > 0) {
                                var delta = root.temperatureValue - profile.steps[0].temperature
                                for (var i = 0; i < profile.steps.length; i++) {
                                    profile.steps[i].temperature += delta
                                }
                                profile.espresso_temperature = root.temperatureValue
                                MainController.uploadProfile(profile)
                            }
                            root.profileTemperature = root.temperatureValue
                            if (MainController.baseProfileName.length > 0) {
                                MainController.saveProfile(MainController.baseProfileName)
                            }
                        }
                    }
                }

                // Visual indicator showing profile default
                Text {
                    visible: Math.abs(root.temperatureValue - root.profileTemperature) > 0.1
                    text: qsTr("Profile: %1°C").arg(root.profileTemperature.toFixed(1))
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(11)
                    font.italic: true
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                    Layout.leftMargin: Theme.scaled(55) + Theme.scaled(8)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("Profile default temperature: %1 degrees").arg(root.profileTemperature.toFixed(1))
                }
            }

            // Dose input
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Text {
                    text: qsTr("Dose:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: Theme.scaled(55)
                    Accessible.ignored: true  // Label for sighted users; input has accessibleName
                }

                ValueInput {
                    id: doseInput
                    Layout.fillWidth: true
                    value: root.doseValue
                    from: 1
                    to: 50
                    stepSize: 0.1
                    decimals: 1
                    suffix: "g"
                    valueColor: Theme.weightColor
                    accentColor: Theme.weightColor
                    accessibleName: qsTr("Dose weight")
                    onValueModified: function(newValue) {
                        root.targetManuallySet = false  // Reset manual flag when dose changes
                        root.doseValue = newValue
                        if (newValue >= 3) {
                            root.showScaleWarning = false
                        }
                    }
                }

                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(56)
                    text: qsTr("Get from scale")
                    accessibleName: qsTr("Get dose from scale")
                    primary: true
                    onClicked: {
                        var scaleWeight = MachineState.scaleWeight
                        if (scaleWeight >= 3) {
                            root.showScaleWarning = false
                            root.targetManuallySet = false  // Reset manual flag
                            root.doseValue = scaleWeight
                        } else {
                            // Show warning but don't change dose
                            root.showScaleWarning = true
                        }
                    }
                }
            }

            // Profile recommended dose indicator
            Text {
                visible: MainController.profileHasRecommendedDose && Math.abs(root.doseValue - MainController.profileRecommendedDose) > 0.05
                text: qsTr("Profile: %1g").arg(MainController.profileRecommendedDose.toFixed(1))
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.scaled(11)
                font.italic: true
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignHCenter
                Layout.leftMargin: Theme.scaled(55) + Theme.scaled(8)
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Profile recommended dose: %1 grams").arg(MainController.profileRecommendedDose.toFixed(1))
            }

            // Ratio input
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Text {
                    text: qsTr("Ratio: 1:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.preferredWidth: Theme.scaled(55)
                    Accessible.ignored: true  // Label for sighted users; input has accessibleName
                }

                ValueInput {
                    id: ratioInput
                    Layout.fillWidth: true
                    value: root.ratio
                    from: 0.5
                    to: 20.0
                    stepSize: 0.1
                    decimals: 1
                    valueColor: Theme.primaryColor
                    accentColor: Theme.primaryColor
                    accessibleName: qsTr("Brew ratio")
                    onValueModified: function(newValue) {
                        root.targetManuallySet = false  // Reset manual flag when ratio changes
                        root.ratio = newValue
                    }
                }
            }

            // Yield input
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: qsTr("Yield:")
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: Theme.scaled(55)
                        Accessible.ignored: true  // Label for sighted users; input has accessibleName
                    }

                    ValueInput {
                        id: targetInput
                        Layout.fillWidth: true
                        value: root.targetValue
                        from: 1
                        to: 400
                        stepSize: 1
                        decimals: 0
                        suffix: "g"
                        // Color changes based on whether value is auto-calculated or manually set
                        valueColor: root.targetManuallySet ? Theme.primaryColor : Theme.weightColor
                        accentColor: root.targetManuallySet ? Theme.primaryColor : Theme.weightColor
                        accessibleName: qsTr("Yield weight") + (root.targetManuallySet ? qsTr(" (manual)") : qsTr(" (calculated)"))
                        onValueModified: function(newValue) {
                            root.targetManuallySet = true  // Mark as manually set
                            root.targetValue = newValue
                            // Update ratio to match (yield / dose)
                            if (root.doseValue > 0) {
                                root.ratio = newValue / root.doseValue
                            }
                        }
                    }

                    // Save to profile button
                    AccessibleButton {
                        Layout.preferredHeight: Theme.scaled(56)
                        text: qsTr("Save")
                        accessibleName: qsTr("Save yield to profile")
                        primary: true
                        enabled: root.targetValue !== root.profileTargetWeight
                        onClicked: {
                            var profile = MainController.getCurrentProfile()
                            if (profile) {
                                profile.target_weight = root.targetValue
                                MainController.uploadProfile(profile)
                            }
                            root.profileTargetWeight = root.targetValue
                            if (MainController.baseProfileName.length > 0) {
                                MainController.saveProfile(MainController.baseProfileName)
                            }
                        }
                    }
                }

            }

            // Grinder and setting input (only shown when Beans feature is enabled)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(4)
                visible: Settings.visualizerExtendedMetadata

                Text {
                    text: qsTr("Grind:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: Theme.scaled(55)
                    Accessible.ignored: true  // Label for sighted users; inputs have accessibleName
                }

                StyledComboBox {
                    id: grinderInput
                    Layout.fillWidth: true
                    Layout.preferredWidth: Theme.scaled(100)
                    model: MainController.shotHistory ? MainController.shotHistory.getDistinctGrinders() : []
                    currentIndex: model.indexOf(root.grinderModel)
                    onCurrentTextChanged: if (currentIndex >= 0) root.grinderModel = currentText
                    Accessible.name: qsTr("Grinder model")
                }

                Text {
                    text: "@"
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Accessible.ignored: true
                }

                StyledTextField {
                    id: grindInput
                    Layout.fillWidth: true
                    Layout.preferredWidth: Theme.scaled(80)
                    text: root.grindSetting
                    placeholder: qsTr("Setting")
                    onTextChanged: root.grindSetting = text
                    Accessible.name: qsTr("Grind setting")
                }
            }
        }

        // Buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.bottomMargin: Theme.scaled(20)
            spacing: Theme.scaled(10)

            // Clear All button
            AccessibleButton {
                Layout.preferredHeight: Theme.scaled(50)
                text: qsTr("Clear")
                accessibleName: qsTr("Clear all overrides")
                onClicked: {
                    // Reset to current profile and bean preset values (not cached values from dialog open)
                    root.profileTemperature = MainController.profileTargetTemperature
                    root.temperatureValue = root.profileTemperature
                    root.profileTargetWeight = MainController.targetWeight

                    // Use bean preset dose if available, otherwise default 18g
                    root.doseValue = Settings.dyeBeanWeight > 0 ? Settings.dyeBeanWeight : 18.0
                    root.grinderModel = Settings.dyeGrinderModel   // Bean's grinder
                    root.grindSetting = Settings.dyeGrinderSetting  // Bean's grind setting

                    // Calculate ratio from profile target weight / dose
                    var profileTarget = MainController.targetWeight
                    root.ratio = (profileTarget > 0 && root.doseValue > 0) ? profileTarget / root.doseValue : Settings.lastUsedRatio
                    root.targetManuallySet = false
                    root.targetValue = root.doseValue * root.ratio
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(50)
                    radius: Theme.buttonRadius
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.warningColor
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.bodyFont
                    color: Theme.warningColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            AccessibleButton {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                text: qsTr("Cancel")
                accessibleName: qsTr("Cancel brew settings")
                onClicked: root.reject()
                background: Rectangle {
                    implicitHeight: Theme.scaled(50)
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
                text: qsTr("OK")
                accessibleName: qsTr("Confirm brew settings")
                onClicked: {
                    Settings.lastUsedRatio = root.ratio
                    Settings.dyeGrinderModel = root.grinderModel
                    Settings.dyeGrinderSetting = root.grindSetting
                    // Use the new activateBrewWithOverrides method
                    MainController.activateBrewWithOverrides(
                        root.doseValue,
                        root.targetValue,
                        root.temperatureValue,
                        root.grindSetting
                    )
                    root.accept()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(50)
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
        } // ColumnLayout (mainColumn)
    } // Item (contentItem)
}
