import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    property real dialogScale: 0.75
    width: Theme.scaled(520) * dialogScale
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
    property double profileTargetWeight: MainController.profileTargetWeight
    property bool targetManuallySet: false

    // Grinder and grind setting
    property string grinderModel: ""
    property string grindSetting: ""

    // Bean and profile
    property string beanBrand: ""
    property string beanType: ""
    property string selectedProfileTitle: ""

    function getBeanBrandSuggestions() {
        var suggestions = MainController.shotHistory ? MainController.shotHistory.getDistinctBeanBrands() : []
        if (root.beanBrand.length > 0 && suggestions.indexOf(root.beanBrand) === -1)
            suggestions.unshift(root.beanBrand)
        return suggestions
    }

    function getBeanTypeSuggestions() {
        var suggestions = MainController.shotHistory ? MainController.shotHistory.getDistinctBeanTypesForBrand(root.beanBrand) : []
        if (root.beanType.length > 0 && suggestions.indexOf(root.beanType) === -1)
            suggestions.unshift(root.beanType)
        return suggestions
    }

    function getProfileSuggestions() {
        var profiles = MainController.availableProfiles
        var titles = []
        for (var i = 0; i < profiles.length; i++)
            titles.push(profiles[i].title)
        return titles
    }

    function loadProfileByTitle(title) {
        var filename = MainController.findProfileByTitle(title)
        if (filename.length > 0) {
            MainController.loadProfile(filename)
            root.profileTemperature = MainController.profileTargetTemperature
            root.temperatureValue = root.profileTemperature
            root.profileTargetWeight = MainController.profileTargetWeight
            if (!root.targetManuallySet)
                root.targetValue = root.profileTargetWeight
        }
    }

    // Combine grinder suggestions from history + current bean preset
    function getGrinderSuggestions() {
        var suggestions = MainController.shotHistory ? MainController.shotHistory.getDistinctGrinders() : []
        // Add current bean grinder if not in list
        if (Settings.dyeGrinderModel.length > 0 && suggestions.indexOf(Settings.dyeGrinderModel) === -1) {
            suggestions.unshift(Settings.dyeGrinderModel)  // Add at beginning
        }
        return suggestions
    }

    // Combine grind setting suggestions from history (filtered by grinder) + current bean preset
    function getGrinderSettingSuggestions() {
        var suggestions = MainController.shotHistory ? MainController.shotHistory.getDistinctGrinderSettingsForGrinder(root.grinderModel) : []
        // Add current bean grind setting if not in list
        if (Settings.dyeGrinderSetting.length > 0 && suggestions.indexOf(Settings.dyeGrinderSetting) === -1) {
            suggestions.unshift(Settings.dyeGrinderSetting)  // Add at beginning
        }
        return suggestions
    }

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
            var announcement = TranslationManager.translate("brewDialog.dialogAnnouncement", "Brew Settings dialog. Profile: ") + MainController.currentProfileName
            if (Settings.dyeBeanBrand.length > 0 || Settings.dyeBeanType.length > 0) {
                announcement += ". " + TranslationManager.translate("brewDialog.beansLabel", "Beans: ")
                if (Settings.dyeBeanBrand.length > 0) announcement += Settings.dyeBeanBrand
                if (Settings.dyeBeanType.length > 0) announcement += " " + Settings.dyeBeanType
            }
            AccessibilityManager.announce(announcement)
        }

        // Update profile temperature, use override if active
        profileTemperature = MainController.profileTargetTemperature
        profileTargetWeight = MainController.profileTargetWeight
        temperatureValue = Settings.hasTemperatureOverride ? Settings.temperatureOverride : profileTemperature

        // Use DYE fields for dose and grind (source of truth)
        doseValue = Settings.dyeBeanWeight > 0 ? Settings.dyeBeanWeight : 18.0
        grinderModel = Settings.dyeGrinderModel
        grindSetting = Settings.dyeGrinderSetting
        beanBrand = Settings.dyeBeanBrand
        beanType = Settings.dyeBeanType
        selectedProfileTitle = MainController.currentProfileName
        showScaleWarning = false

        // Yield: use override if active, otherwise use profile default
        targetValue = Settings.hasBrewYieldOverride ? Settings.brewYieldOverride : profileTargetWeight
        ratio = doseValue > 0 ? targetValue / doseValue : Settings.lastUsedRatio
        targetManuallySet = Settings.hasBrewYieldOverride
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: "white"
    }

    contentItem: Item {
        implicitHeight: mainColumn.implicitHeight * root.dialogScale
        implicitWidth: Theme.scaled(520) * root.dialogScale
        clip: true

        ColumnLayout {
            id: mainColumn
            width: Theme.scaled(520)
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
                text: TranslationManager.translate("brewDialog.title", "Brew Settings")
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

        // Profile selector
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(12)
            spacing: Theme.scaled(4)

            Text {
                text: TranslationManager.translate("brewDialog.profileLabel", "Profile:")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: Theme.scaled(55)
                Accessible.ignored: true
            }

            SuggestionField {
                id: profileInput
                Layout.fillWidth: true
                label: ""
                accessibleName: TranslationManager.translate("brewDialog.profile", "Profile")
                text: root.selectedProfileTitle
                suggestions: root.getProfileSuggestions()
                onTextEdited: function(t) {
                    root.selectedProfileTitle = t
                    // Only load profile on exact match (e.g. suggestion selection),
                    // not partial typing that might accidentally match a short title
                    if (root.getProfileSuggestions().indexOf(t) >= 0)
                        root.loadProfileByTitle(t)
                }
            }
        }

        // Bean info
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            spacing: Theme.scaled(4)

            Text {
                text: TranslationManager.translate("brewDialog.beansLabel2", "Beans:")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: Theme.scaled(55)
                Accessible.ignored: true
            }

            SuggestionField {
                id: beanBrandInput
                Layout.fillWidth: true
                Layout.preferredWidth: Theme.scaled(130)
                label: ""
                accessibleName: TranslationManager.translate("brewDialog.beanBrand", "Bean brand")
                text: root.beanBrand
                suggestions: root.getBeanBrandSuggestions()
                onTextEdited: function(t) { root.beanBrand = t }
            }

            Text {
                text: "-"
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignVCenter
                Accessible.ignored: true
            }

            SuggestionField {
                id: beanTypeInput
                Layout.fillWidth: true
                Layout.preferredWidth: Theme.scaled(130)
                label: ""
                accessibleName: TranslationManager.translate("brewDialog.beanType", "Bean type")
                text: root.beanType
                suggestions: root.getBeanTypeSuggestions()
                onTextEdited: function(t) { root.beanType = t }
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
                        AccessibilityManager.announce(TranslationManager.translate("brewDialog.warningPrefix", "Warning: ") + warningText.text)
                    }
                }

                Text {
                    id: warningText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.scaled(12)
                    text: TranslationManager.translate("brewDialog.scaleWarning", "Please put the portafilter with coffee on the scale")
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
                        text: TranslationManager.translate("brewDialog.tempLabel", "Temp:")
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
                        accessibleName: TranslationManager.translate("brewDialog.brewTemperature", "Brew temperature")
                        onValueModified: function(newValue) {
                            root.temperatureValue = newValue
                        }
                    }

                    // Save to profile button
                    AccessibleButton {
                        Layout.preferredHeight: Theme.scaled(56)
                        text: TranslationManager.translate("brewDialog.updateProfile", "Update Profile")
                        accessibleName: TranslationManager.translate("brewDialog.saveTemperatureToProfile", "Save temperature to profile")
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
                    text: TranslationManager.translate("brewDialog.profileTempIndicator", "Profile: %1°C").arg(root.profileTemperature.toFixed(1))
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(11)
                    font.italic: true
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                    Layout.leftMargin: Theme.scaled(55) + Theme.scaled(8)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("brewDialog.profileDefaultTemp", "Profile default temperature: %1 degrees").arg(root.profileTemperature.toFixed(1))
                }
            }

            // Dose input
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Text {
                    text: TranslationManager.translate("brewDialog.doseLabel", "Dose:")
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
                    accessibleName: TranslationManager.translate("brewDialog.doseWeight", "Dose weight")
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
                    text: TranslationManager.translate("brewDialog.getFromScale", "Get from scale")
                    accessibleName: TranslationManager.translate("brewDialog.getDoseFromScale", "Get dose from scale")
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
                text: TranslationManager.translate("brewDialog.profileDoseIndicator", "Profile: %1g").arg(MainController.profileRecommendedDose.toFixed(1))
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.scaled(11)
                font.italic: true
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignHCenter
                Layout.leftMargin: Theme.scaled(55) + Theme.scaled(8)
                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("brewDialog.profileRecommendedDose", "Profile recommended dose: %1 grams").arg(MainController.profileRecommendedDose.toFixed(1))
            }

            // Ratio input
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Text {
                    text: TranslationManager.translate("brewDialog.ratioLabel", "Ratio: 1:")
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
                    accessibleName: TranslationManager.translate("brewDialog.brewRatio", "Brew ratio")
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
                        text: TranslationManager.translate("brewDialog.yieldLabel", "Yield:")
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
                        accessibleName: TranslationManager.translate("brewDialog.yieldWeight", "Yield weight") + (root.targetManuallySet ? TranslationManager.translate("brewDialog.manual", " (manual)") : TranslationManager.translate("brewDialog.calculated", " (calculated)"))
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
                        text: TranslationManager.translate("brewDialog.updateProfile", "Update Profile")
                        accessibleName: TranslationManager.translate("brewDialog.saveYieldToProfile", "Save yield to profile")
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

                // Visual indicator showing profile default
                Text {
                    visible: Math.abs(root.targetValue - root.profileTargetWeight) > 0.1
                    text: qsTr("Profile: %1g").arg(root.profileTargetWeight.toFixed(0))
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(11)
                    font.italic: true
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                    Layout.leftMargin: Theme.scaled(55) + Theme.scaled(8)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("Profile default yield: %1 grams").arg(root.profileTargetWeight.toFixed(0))
                }
            }

            // Grinder and setting input (only shown when Beans feature is enabled)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(4)
                visible: Settings.visualizerExtendedMetadata

                Text {
                    text: TranslationManager.translate("brewDialog.grinderLabel", "Grinder:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: Theme.scaled(65)
                    Accessible.ignored: true  // Label for sighted users; inputs have accessibleName
                }

                SuggestionField {
                    id: grinderInput
                    Layout.fillWidth: true
                    Layout.preferredWidth: Theme.scaled(120)
                    label: ""  // Empty label - the "Grinder:" label already provides context
                    accessibleName: TranslationManager.translate("brewDialog.grinderModel", "Grinder model")
                    text: root.grinderModel
                    suggestions: root.getGrinderSuggestions()
                    onTextEdited: function(t) { root.grinderModel = t }
                    // Note: No inputFocused signal needed here since BrewDialog doesn't use keyboard offset
                }

                Text {
                    text: "@"
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Accessible.ignored: true
                }

                SuggestionField {
                    id: grindInput
                    Layout.fillWidth: true
                    Layout.preferredWidth: Theme.scaled(110)
                    label: ""  // Empty label - the "Grinder:" label and grinder field already provide context
                    accessibleName: TranslationManager.translate("brewDialog.grinderSetting", "Grinder setting")
                    text: root.grindSetting
                    suggestions: root.getGrinderSettingSuggestions()
                    onTextEdited: function(t) { root.grindSetting = t }
                    // Note: No inputFocused signal needed here since BrewDialog doesn't use keyboard offset
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
                text: TranslationManager.translate("brewDialog.clear", "Clear")
                accessibleName: TranslationManager.translate("brewDialog.clearAllOverrides", "Clear all overrides")
                onClicked: {
                    // Reset to current profile and bean preset values (not cached values from dialog open)
                    root.profileTemperature = MainController.profileTargetTemperature
                    root.temperatureValue = root.profileTemperature
                    root.profileTargetWeight = MainController.profileTargetWeight

                    // Use bean preset dose if available, otherwise default 18g
                    root.doseValue = Settings.dyeBeanWeight > 0 ? Settings.dyeBeanWeight : 18.0
                    root.beanBrand = Settings.dyeBeanBrand
                    root.beanType = Settings.dyeBeanType
                    root.selectedProfileTitle = MainController.currentProfileName
                    root.grinderModel = Settings.dyeGrinderModel   // Bean's grinder
                    root.grindSetting = Settings.dyeGrinderSetting  // Bean's grind setting

                    // Calculate ratio from profile target weight / dose
                    var profileTarget = MainController.profileTargetWeight
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
                text: TranslationManager.translate("brewDialog.cancel", "Cancel")
                accessibleName: TranslationManager.translate("brewDialog.cancelBrewSettings", "Cancel brew settings")
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
                text: TranslationManager.translate("brewDialog.ok", "OK")
                accessibleName: TranslationManager.translate("brewDialog.confirmBrewSettings", "Confirm brew settings")
                onClicked: {
                    Settings.lastUsedRatio = root.ratio
                    Settings.dyeBeanBrand = root.beanBrand
                    Settings.dyeBeanType = root.beanType
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
