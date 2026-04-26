import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

Item {
    id: languageTab

    // Scan all strings and fetch available languages when page loads
    Component.onCompleted: {
        TranslationManager.scanAllStrings()
        TranslationManager.downloadLanguageList()
    }

    // Handle translation submission result and language downloads
    Connections {
        target: TranslationManager
        function onTranslationSubmitted(success, message) {
            submitResultPopup.isSuccess = success
            submitResultPopup.resultMessage = message
            submitResultPopup.open()
        }
        function onLanguageDownloaded(langCode, success, error) {
            if (success) {
                // Auto-select the language after successful download
                TranslationManager.currentLanguage = langCode
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingMedium

        // ========== LEFT COLUMN: Language + Translation ==========
        Rectangle {
            objectName: "language"
            Layout.preferredWidth: Theme.scaled(300)
            Layout.maximumWidth: Theme.scaled(350)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(12)

                Tr {
                    key: "language.languages"
                    fallback: "Languages"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                ListView {
                    id: languageList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(140)
                    clip: true
                    model: TranslationManager.availableLanguages

                    delegate: Item {
                        id: langDelegate
                        width: languageList.width
                        height: Math.max(Theme.scaled(36), langContentRow.implicitHeight + Theme.scaled(6) * 2)

                        property bool highlighted: modelData === TranslationManager.currentLanguage
                        property string langCode: modelData
                        property string displayName: TranslationManager.getLanguageDisplayName(modelData)
                        property string nativeName: TranslationManager.getLanguageNativeName(modelData)

                        Accessible.ignored: true

                        Rectangle {
                            anchors.fill: parent
                            color: langDelegate.highlighted ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : "transparent"
                            radius: Theme.buttonRadius
                        }

                        RowLayout {
                            id: langContentRow
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(8)
                            anchors.rightMargin: Theme.scaled(8)
                            spacing: Theme.scaled(8)

                            Text {
                                Layout.fillWidth: true
                                text: langDelegate.nativeName !== langDelegate.displayName ? langDelegate.displayName + " (" + langDelegate.nativeName + ")" : langDelegate.displayName
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(13)
                                font.bold: langDelegate.highlighted
                                color: {
                                    var version = TranslationManager.translationVersion  // Force re-evaluation
                                    if (TranslationManager.isRemoteLanguage(langDelegate.langCode)) return Theme.sourceBadgeBlueColor
                                    return Theme.successColor
                                }
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }

                            Text {
                                visible: {
                                    var version = TranslationManager.translationVersion  // Force re-evaluation
                                    return langDelegate.langCode !== "en" && !TranslationManager.isRemoteLanguage(langDelegate.langCode)
                                }
                                text: {
                                    var version = TranslationManager.translationVersion
                                    return TranslationManager.getTranslationPercent(langDelegate.langCode) + "%"
                                }
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent
                            // Compute directly from modelData to avoid recycling issues
                            accessibleName: {
                                var code = modelData  // Force binding to modelData
                                var display = TranslationManager.getLanguageDisplayName(code)
                                var native_ = TranslationManager.getLanguageNativeName(code)
                                var name = native_ !== display ? display + ", " + native_ : display
                                if (code === TranslationManager.currentLanguage) {
                                    name += ", " + TranslationManager.translate("language.accessible.selected", "selected")
                                }
                                return name
                            }
                            accessibleItem: langDelegate

                            onAccessibleClicked: {
                                var code = modelData
                                var isRemote = TranslationManager.isRemoteLanguage(code)
                                TranslationManager.currentLanguage = code  // Immediate visual feedback
                                if (isRemote) {
                                    TranslationManager.downloadLanguage(code)
                                }
                            }
                        }
                    }
                }

                // Add / Delete / Download buttons
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    AccessibleButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        text: TranslationManager.translate("language.button.add", "Add...")
                        accessibleName: TranslationManager.translate("language.accessible.add", "Add language")
                        accessibleDescription: TranslationManager.translate("language.accessible.add.description", "Add a new language for translation")
                        onClicked: pageStack.push("AddLanguagePage.qml")
                    }

                    // Delete button
                    AccessibleButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        visible: true
                        text: TranslationManager.translate("language.delete", "Delete")
                        accessibleName: TranslationManager.translate("language.accessible.delete", "Delete language")
                        accessibleDescription: TranslationManager.translate("language.accessible.delete.description", "Delete the selected language and its translations")
                        warning: true
                        enabled: TranslationManager.currentLanguage !== "en"
                        onClicked: deleteConfirmPopup.open()
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        text: TranslationManager.downloading ? "..." : "Update"
                        accessibleName: TranslationManager.downloading ? TranslationManager.translate("language.accessible.downloading", "Downloading") : TranslationManager.translate("language.accessible.update", "Update community translations")
                        accessibleDescription: TranslationManager.translate("language.accessible.update.description", "Download latest translations from the community")
                        primary: true
                        enabled: !TranslationManager.downloading && TranslationManager.currentLanguage !== "en" && !TranslationManager.isRemoteLanguage(TranslationManager.currentLanguage)
                        onClicked: TranslationManager.downloadLanguage(TranslationManager.currentLanguage)
                    }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.borderColor
                    Layout.topMargin: Theme.scaled(4)
                    Layout.bottomMargin: Theme.scaled(4)
                }

                // Translation section
                Tr {
                    key: "language.translation"
                    fallback: "Translation"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                // Progress card (non-English only)
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: progressColumn.height + Theme.scaled(16)
                    color: Theme.backgroundColor
                    radius: Theme.buttonRadius
                    visible: TranslationManager.currentLanguage !== "en"

                    ColumnLayout {
                        id: progressColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(6)

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.getLanguageDisplayName(TranslationManager.currentLanguage)
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(13)
                                font.bold: true
                                color: Theme.textColor
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: {
                                    var total = TranslationManager.totalStringCount
                                    var translated = total - TranslationManager.untranslatedCount
                                    return translated + " / " + total
                                }
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                            }
                        }

                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: Math.max(1, TranslationManager.totalStringCount)
                            value: TranslationManager.totalStringCount - TranslationManager.untranslatedCount

                            Accessible.role: Accessible.ProgressBar
                            Accessible.name: {
                                var total = TranslationManager.totalStringCount
                                var translated = total - TranslationManager.untranslatedCount
                                var percent = Math.round((translated / Math.max(1, total)) * 100)
                                return "Translation progress: " + percent + " percent, " + translated + " of " + total + " strings"
                            }

                            background: Rectangle {
                                implicitHeight: Theme.scaled(6)
                                color: Theme.borderColor
                                radius: Theme.scaled(3)
                            }

                            contentItem: Item {
                                Rectangle {
                                    width: parent.parent.visualPosition * parent.width
                                    height: parent.height
                                    radius: Theme.scaled(3)
                                    color: Theme.successColor
                                }
                            }
                        }

                        Text {
                            text: {
                                var total = TranslationManager.totalStringCount
                                var translated = total - TranslationManager.untranslatedCount
                                var percent = Math.round((translated / Math.max(1, total)) * 100)
                                if (percent === 100) return "Translation complete!"
                                return TranslationManager.untranslatedCount + " strings need translation"
                            }
                            font: Theme.labelFont
                            color: Theme.textSecondaryColor
                        }
                    }
                }

                // English info
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: englishInfoText.implicitHeight + Theme.scaled(16)
                    color: Theme.backgroundColor
                    radius: Theme.buttonRadius
                    visible: TranslationManager.currentLanguage === "en"

                    Text {
                        id: englishInfoText
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: Theme.scaled(8)
                        text: TranslationManager.translate("language.englishBaseNote", "English is the base language.\nYou can customize the default text below.")
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(12)
                        color: Theme.textSecondaryColor
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }

                // Browse strings button
                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(40)
                    activeFocusOnTab: false
                    text: TranslationManager.currentLanguage === "en" ? TranslationManager.translate("language.browseCustomize", "Browse & Customize Strings...") : TranslationManager.translate("language.browseTranslate", "Browse & Translate Strings...")
                    accessibleName: TranslationManager.currentLanguage === "en" ? TranslationManager.translate("language.accessible.browse.en", "Browse and customize strings") : TranslationManager.translate("language.accessible.browse", "Browse and translate strings")
                    accessibleDescription: TranslationManager.currentLanguage === "en" ? TranslationManager.translate("language.accessible.browse.en.description", "Open the string browser to customize English text") : TranslationManager.translate("language.accessible.browse.description", "Open the translation browser to translate individual strings")
                    primary: true
                    onClicked: pageStack.push("StringBrowserPage.qml")
                }

                // Submit to community button (not for English, developer mode only)
                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(40)
                    activeFocusOnTab: false
                    text: TranslationManager.uploading ? "Uploading..." : "Submit to Community"
                    accessibleName: TranslationManager.uploading ? TranslationManager.translate("language.accessible.uploading", "Uploading translation") : TranslationManager.translate("language.accessible.submit", "Submit to community")
                    accessibleDescription: TranslationManager.translate("language.accessible.submit.description", "Share your translations with the community")
                    primary: true
                    visible: TranslationManager.currentLanguage !== "en" && Settings.app.developerTranslationUpload
                    enabled: !TranslationManager.uploading
                    onClicked: TranslationManager.submitTranslation()
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ========== RIGHT COLUMN: Accessibility ==========
        Rectangle {
            objectName: "accessibility"
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                id: accessibilityColumn
                anchors.fill: parent
                anchors.margins: Theme.scaled(12)
                spacing: Theme.scaled(6)

                    Tr {
                        key: "settings.accessibility.title"
                        fallback: "Accessibility"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.accessibility.desc"
                        fallback: "Screen reader support and audio feedback for blind and visually impaired users"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                    }

                    // Enable toggle
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "settings.accessibility.enable"
                            fallback: "Enable Accessibility"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            checked: AccessibilityManager.enabled
                            accessibleName: TranslationManager.translate("settings.accessibility.enable", "Enable Accessibility")
                            onCheckedChanged: AccessibilityManager.enabled = checked
                        }
                    }

                    // TTS toggle
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        opacity: AccessibilityManager.enabled ? 1.0 : 0.5

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "settings.accessibility.voiceAnnouncements"
                                fallback: "Voice Announcements"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }
                            Tr {
                                Layout.fillWidth: true
                                key: "settings.accessibility.voiceAnnouncementsDesc"
                                fallback: "Speak shot progress aloud"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(11)
                                wrapMode: Text.WordWrap
                            }
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            checked: AccessibilityManager.ttsEnabled
                            enabled: AccessibilityManager.enabled
                            accessibleName: TranslationManager.translate("settings.accessibility.voiceAnnouncements", "Voice Announcements")
                            onCheckedChanged: {
                                if (AccessibilityManager.enabled) {
                                    if (checked) {
                                        AccessibilityManager.ttsEnabled = true
                                        AccessibilityManager.announce(TranslationManager.translate("accessibility.voiceAnnouncementsEnabled", "Voice announcements enabled"), true)
                                    } else {
                                        AccessibilityManager.announce(TranslationManager.translate("accessibility.voiceAnnouncementsDisabled", "Voice announcements disabled"), true)
                                        AccessibilityManager.ttsEnabled = false
                                    }
                                } else {
                                    AccessibilityManager.ttsEnabled = checked
                                }
                            }
                        }
                    }

                    // Tick sound toggle
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        opacity: AccessibilityManager.enabled ? 1.0 : 0.5

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "settings.accessibility.frameTick"
                                fallback: "Frame Tick Sound"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }
                            Tr {
                                Layout.fillWidth: true
                                key: "settings.accessibility.frameTickDesc"
                                fallback: "Play a tick when the machine moves to the next extraction step"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(11)
                                wrapMode: Text.WordWrap
                            }
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            checked: AccessibilityManager.tickEnabled
                            enabled: AccessibilityManager.enabled
                            accessibleName: TranslationManager.translate("settings.accessibility.frameTick", "Frame Tick Sound")
                            onCheckedChanged: {
                                AccessibilityManager.tickEnabled = checked
                                if (AccessibilityManager.enabled) {
                                    AccessibilityManager.announce(checked ? TranslationManager.translate("accessibility.frameTickEnabled", "Frame tick sound enabled") : TranslationManager.translate("accessibility.frameTickDisabled", "Frame tick sound disabled"), true)
                                }
                            }
                        }
                    }

                    // Tick sound picker and volume
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        opacity: (AccessibilityManager.enabled && AccessibilityManager.tickEnabled) ? 1.0 : 0.5

                        Tr {
                            key: "settings.accessibility.tickSound"
                            fallback: "Tick Sound"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            Layout.maximumWidth: Theme.scaled(150)
                            value: AccessibilityManager.tickSoundIndex
                            from: 1
                            to: 4
                            stepSize: 1
                            suffix: ""
                            displayText: TranslationManager.translate("accessibility.soundValue", "Sound %1").arg(value)
                            accessibleName: TranslationManager.translate("accessibility.selectTickSound", "Select tick sound, 1 to 4. Current: %1").arg(value)
                            enabled: AccessibilityManager.enabled && AccessibilityManager.tickEnabled
                            onValueModified: function(newValue) {
                                AccessibilityManager.tickSoundIndex = newValue
                            }
                        }

                        ValueInput {
                            Layout.maximumWidth: Theme.scaled(150)
                            value: AccessibilityManager.tickVolume
                            from: 10
                            to: 100
                            stepSize: 10
                            suffix: "%"
                            accessibleName: TranslationManager.translate("accessibility.tickVolume", "Tick volume. Current: %1 percent").arg(value)
                            enabled: AccessibilityManager.enabled && AccessibilityManager.tickEnabled
                            onValueModified: function(newValue) {
                                AccessibilityManager.tickVolume = newValue
                            }
                        }
                    }

                    // Divider
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.borderColor
                        Layout.topMargin: Theme.scaled(2)
                        Layout.bottomMargin: Theme.scaled(2)
                    }

                    // Extraction announcements
                    Tr {
                        key: "settings.accessibility.extractionTitle"
                        fallback: "Extraction Announcements"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.accessibility.extractionDesc"
                        fallback: "Spoken updates during espresso extraction"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "settings.accessibility.extractionEnable"
                            fallback: "Enable During Extraction"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            checked: AccessibilityManager.extractionAnnouncementsEnabled
                            enabled: AccessibilityManager.enabled
                            accessibleName: TranslationManager.translate("settings.accessibility.extractionEnable", "Enable During Extraction")
                            onCheckedChanged: AccessibilityManager.extractionAnnouncementsEnabled = checked
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        opacity: AccessibilityManager.extractionAnnouncementsEnabled ? 1.0 : 0.5

                        Tr {
                            key: "settings.accessibility.announcementMode"
                            fallback: "Mode"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        StyledComboBox {
                            id: modeComboBox
                            Layout.preferredWidth: Theme.scaled(160)
                            Layout.maximumWidth: Theme.scaled(180)
                            accessibleLabel: TranslationManager.translate("settings.accessibility.announcementMode", "Announcement mode")
                            enabled: AccessibilityManager.enabled && AccessibilityManager.extractionAnnouncementsEnabled
                            model: [
                                TranslationManager.translate("settings.accessibility.modeBoth", "Time + Milestones"),
                                TranslationManager.translate("settings.accessibility.modeTimed", "Timed Updates"),
                                TranslationManager.translate("settings.accessibility.modeMilestones", "Weight Milestones")
                            ]
                            currentIndex: {
                                var mode = AccessibilityManager.extractionAnnouncementMode
                                if (mode === "both") return 0
                                if (mode === "timed") return 1
                                if (mode === "milestones_only") return 2
                                return 0
                            }
                            onCurrentIndexChanged: {
                                var modes = ["both", "timed", "milestones_only"]
                                AccessibilityManager.extractionAnnouncementMode = modes[currentIndex]
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        visible: AccessibilityManager.extractionAnnouncementMode !== "milestones_only"
                        opacity: AccessibilityManager.extractionAnnouncementsEnabled ? 1.0 : 0.5

                        Tr {
                            key: "settings.accessibility.updateInterval"
                            fallback: "Announce Every"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            value: AccessibilityManager.extractionAnnouncementInterval
                            from: 5
                            to: 30
                            stepSize: 5
                            suffix: "s"
                            accessibleName: TranslationManager.translate("settings.accessibility.updateInterval", "Update Interval")
                            enabled: AccessibilityManager.enabled && AccessibilityManager.extractionAnnouncementsEnabled
                            onValueModified: function(newValue) {
                                AccessibilityManager.extractionAnnouncementInterval = newValue
                            }
                        }
                    }
                }
            }
        }

    // Delete confirmation popup
    Dialog {
        id: deleteConfirmPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(320))
        modal: true
        dim: true
        padding: Theme.spacingMedium
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.warningColor
        }

        contentItem: Column {
            spacing: Theme.spacingMedium

            Text {
                width: parent.width
                text: TranslationManager.translate("language.deleteConfirm.title", "Delete Language?")
                font: Theme.subtitleFont
                color: Theme.warningColor
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                width: parent.width
                text: TranslationManager.translate("language.deleteConfirmMessage", "Delete %1 and all its translations?\n\nThis cannot be undone.").arg(TranslationManager.getLanguageDisplayName(TranslationManager.currentLanguage))
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            Row {
                width: parent.width
                spacing: Theme.spacingSmall

                AccessibleButton {
                    width: (parent.width - Theme.spacingSmall) / 2
                    text: TranslationManager.translate("language.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("settings.language.cancelKeep", "Cancel and keep language")
                    onClicked: deleteConfirmPopup.close()
                }

                AccessibleButton {
                    width: (parent.width - Theme.spacingSmall) / 2
                    text: TranslationManager.translate("language.deleteConfirm", "Delete")
                    accessibleName: TranslationManager.translate("settings.language.permanentlyDelete", "Permanently delete this language and its translations")
                    warning: true
                    onClicked: {
                        TranslationManager.deleteLanguage(TranslationManager.currentLanguage)
                        deleteConfirmPopup.close()
                    }
                }
            }
        }
    }

    // Scanning overlay - simple progress bar
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.5)
        visible: TranslationManager.scanning
        z: 100

        ProgressBar {
            anchors.centerIn: parent
            width: parent.width * 0.6
            from: 0
            to: Math.max(1, TranslationManager.scanTotal)
            value: TranslationManager.scanProgress

            background: Rectangle {
                implicitHeight: Theme.scaled(8)
                color: Qt.rgba(1, 1, 1, 0.2)
                radius: Theme.scaled(4)
            }

            contentItem: Item {
                Rectangle {
                    width: parent.parent.visualPosition * parent.width
                    height: parent.height
                    radius: Theme.scaled(4)
                    color: Theme.primaryColor
                }
            }
        }
    }

    // Submission result popup
    Dialog {
        id: submitResultPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(340))
        modal: true
        dim: true
        padding: Theme.spacingMedium
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        property bool isSuccess: false
        property string resultMessage: ""

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: submitResultPopup.isSuccess ? Theme.successColor : Theme.warningColor
        }

        contentItem: Column {
            spacing: Theme.spacingMedium

            Text {
                width: parent.width
                text: submitResultPopup.isSuccess ? "Success!" : "Error"
                font: Theme.subtitleFont
                color: submitResultPopup.isSuccess ? Theme.successColor : Theme.warningColor
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                width: parent.width
                text: submitResultPopup.resultMessage
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            AccessibleButton {
                width: parent.width
                text: TranslationManager.translate("common.button.ok", "OK")
                accessibleName: TranslationManager.translate("settings.language.closeSubmitResult", "Close submission result dialog")
                primary: true
                onClicked: submitResultPopup.close()
            }
        }
    }

    // Retry status popup - shows when server is busy and retrying
    Popup {
        id: retryStatusPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(320))
        modal: false
        dim: false
        padding: Theme.spacingMedium
        closePolicy: Popup.NoAutoClose

        // Auto-show/hide based on retryStatus
        visible: TranslationManager.retryStatus !== ""

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.warningColor
        }

        contentItem: Column {
            spacing: Theme.spacingSmall

            Text {
                width: parent.width
                text: TranslationManager.retryStatus
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                width: parent.width
                text: TranslationManager.translate("common.pleaseWait", "Please wait...")
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
