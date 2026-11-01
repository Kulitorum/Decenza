// The language-list delegate reads this file's `languageList` id; Bound makes it
// statically resolvable. It declares its one injected role, `modelData`, required in
// the same edit -- without that, Bound stops role injection and every language row
// renders blank at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Decenza

Item {
    id: languageTab
    property string pendingSubmitRequest: ""
    readonly property var scalarPreferences: SettingsPreferencesService.preferences

    function scalarPreference(key: string, fallback: var): var {
        var value = scalarPreferences[key]
        return value === undefined ? fallback : value
    }

    PreferenceWrite {
        id: accessibilityPreferenceWrite
        domain: "accessibility"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: appPreferenceWrite
        domain: "app"
        onFailed: function(message) { preferenceError.show(message) }
    }
    StatusToast { id: preferenceError; error: true }
    StatusToast { id: languageFileResult }

    LanguageFileActions {
        id: languageFileActions
        executeOperation: function(operation, args) { return LanguageService.execute(operation, args) }
        cancelTransfer: function(transferId) { ImportService.execute("cancelTransfer", {transferId: transferId}) }
        onDownloadRequested: function(bundle) {
            if (!languageDownload.start(bundle))
                languageFileActions.refuseDownload(bundle)
        }
        onFinished: function(success, result, reason) {
            if (reason === "cancelled" || reason === "saveCancelled") return
            languageFileResult.error = !success
            const message = success
                ? result.translationsSaved === true
                    ? TranslationManager.translate("language.file.imported", "Translations imported.")
                    : TranslationManager.translate("language.file.success", "Translation file saved.")
                : reason === "languageMismatch"
                    ? TranslationManager.translate("language.file.languageMismatch", "Select the file's language before importing it.")
                    : reason === "metadataWriteFailed" && result.translationsSaved === true
                        ? TranslationManager.translate("language.file.metadataWriteFailed", "Translations were saved, but language details could not be updated.")
                        : reason === "staleContext"
                            ? TranslationManager.translate("language.file.staleContext", "The host language or translations changed. Select the language again and retry.")
                            : reason === "machineBusy"
                                ? TranslationManager.translate("language.file.machineBusy", "Stop the machine operation before importing or exporting translations.")
                            : TranslationManager.translate("language.file.failed", "The translation file could not be imported or saved. Check the file and try again.")
            languageFileResult.show(message)
            AccessibilityManager.announce(message, !success)
        }
    }

    FileDialog {
        id: languageImportDialog
        title: TranslationManager.translate("language.file.select", "Select translation JSON file")
        fileMode: FileDialog.OpenFile
        nameFilters: [TranslationManager.translate("language.file.jsonFilter", "JSON files (*.json)")]
        onRejected: languageFileActions.cancelPicker()
        onAccepted: {
            if (languageFileActions.acceptSelection()
                    && !languageTransfer.start([selectedFile], "languageJson", ({})))
                languageFileActions.transferCompleted(false, ({}), "serviceUnavailable")
        }
    }
    LocalFileTransfer {
        id: languageTransfer
        onFinished: function(success, result, reason) {
            languageFileActions.transferCompleted(success, result, reason)
        }
    }
    LocalFileDownload {
        id: languageDownload
        executeOperation: function(operation, args) { return LanguageService.execute(operation, args) }
        onFinished: function(success, reason) { languageFileActions.downloadCompleted(success, reason) }
    }

    // Scan all strings and fetch available languages when page loads
    Component.onCompleted: {
        LanguageService.execute("scanStrings")
        LanguageService.execute("refreshLanguageList")
    }

    // Handle translation submission result and language downloads
    Connections {
        target: LanguageService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            languageFileActions.completeOperation(requestId, operation, success, result, reason)
            languageDownload.completeOperation(requestId, operation, success, result, reason)
            if (requestId === languageTab.pendingSubmitRequest && operation === "submitTranslation") {
                languageTab.pendingSubmitRequest = ""
                submitResultPopup.isSuccess = success
                submitResultPopup.resultMessage = success
                    ? TranslationManager.translate("language.submit.success", "Translation submitted to the community.")
                    : TranslationManager.translate("language.submit.failure", "Translation submission failed. Please try again.")
                submitResultPopup.open()
                return
            }

            if (operation !== "selectLanguage" && operation !== "downloadLanguage")
                return
            const code = result.code || ""
            if (success) {
                // The service completion is correlated to this client's request and
                // arrives after a remote-language download has been applied. The catalog
                // adapter intentionally exposes no manager-only download signal.
                if (code.length > 0 && code === LanguageService.snapshot.currentLanguage)
                    languageTab.maybeOfferAiTranslation(code)
            } else if (reason === "downloadFailed" && code.length > 0) {
                WebDebugLogger.warn("App", "SettingsLanguageTab",
                    ["Language download failed for", code].map(String).join(" "))
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingMedium

        // ========== LEFT COLUMN: Language + Translation ==========
        SettingsCard {
            searchId: "language"
            title: TranslationManager.translate("language.languages", "Languages")
            description: TranslationManager.translate("settings.search.languageDesc", "Select app language")
            keywords: ["language", "translate", "locale", "i18n"]
            // The header scrolls with the content.
            showHeader: false
            fillContent: true
            Layout.fillWidth: false
            Layout.preferredWidth: Theme.scaled(300)
            Layout.maximumWidth: Theme.scaled(350)
            // Scrollable: this column is taller than the card whenever the window is short, and
            // without this everything below the fold is simply unreachable — there is no other
            // route to it. The "Submit to Community" button sits at the very bottom and was
            // invisible on a 2560x1080 display with the window part-height.

            ScrollView {
                id: languageColumnScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth   // vertical only; never scroll sideways

                ColumnLayout {
                    width: languageColumnScroll.availableWidth
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
                    SettingsSearch.title: TranslationManager.translate("language.languages", "Languages")
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(140)
                    clip: true
                    model: LanguageService.availableLanguages

                    delegate: Item {
                        id: langDelegate
                        required property var modelData

                        width: languageList.width
                        height: Math.max(Theme.scaled(36), langContentRow.implicitHeight + Theme.scaled(6) * 2)

                        property bool highlighted: langDelegate.modelData === TranslationManager.currentLanguage
                        property string langCode: langDelegate.modelData
                        property string displayName: LanguageService.getLanguageDisplayName(langDelegate.modelData)
                        property string nativeName: LanguageService.getLanguageNativeName(langDelegate.modelData)

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
                                    if (LanguageService.isRemoteLanguage(langDelegate.langCode)) return Theme.sourceBadgeBlueColor
                                    return Theme.successColor
                                }
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }

                            Text {
                                visible: {
                                    var version = TranslationManager.translationVersion  // Force re-evaluation
                                    return langDelegate.langCode !== "en" && !LanguageService.isRemoteLanguage(langDelegate.langCode)
                                }
                                text: {
                                    var snapshot = LanguageService.snapshot
                                    return LanguageService.getTranslationPercent(langDelegate.langCode) + "%"
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
                                var code = langDelegate.modelData  // Force binding to modelData
                                var display = LanguageService.getLanguageDisplayName(code)
                                var native_ = LanguageService.getLanguageNativeName(code)
                                var name = native_ !== display ? display + ", " + native_ : display
                                if (code === TranslationManager.currentLanguage) {
                                    name += ", " + TranslationManager.translate("language.accessible.selected", "selected")
                                }
                                return name
                            }
                            accessibleItem: langDelegate

                            onAccessibleClicked: {
                                var code = langDelegate.modelData
                                LanguageService.selectLanguage(code)  // Host owns the language switch and optional download
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
                        onClicked: AppShell.addLanguageRequested()
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
                        enabled: TranslationManager.currentLanguage !== "en" && TranslationManager.currentLanguage !== "en-GB"
                        onClicked: deleteConfirmPopup.open()
                    }

                    AccessibleButton {
                        readonly property bool useAiTranslation: {
                            const rows = LanguageService.snapshot.languages || []
                            for (let i = 0; i < rows.length; ++i) {
                                if (rows[i].code === TranslationManager.currentLanguage)
                                    return TranslationManager.currentLanguage !== "en" && rows[i].communityAvailable === false
                            }
                            return false
                        }
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        text: LanguageService.downloading
                            ? "..."
                            : useAiTranslation
                                ? TranslationManager.translate("language.button.aiTranslate", "AI Translate...")
                                : TranslationManager.translate("language.button.update", "Update")
                        SettingsSearch.title: TranslationManager.translate("settings.search.updateTranslations", "Update translations")
                        accessibleName: LanguageService.downloading ? TranslationManager.translate("language.accessible.downloading", "Downloading") : useAiTranslation ? TranslationManager.translate("language.accessible.aiTranslate", "Translate missing strings with AI") : TranslationManager.translate("language.accessible.update", "Update community translations")
                        accessibleDescription: useAiTranslation ? TranslationManager.translate("language.accessible.aiTranslate.description", "Generate translations using your configured AI provider") : TranslationManager.translate("language.accessible.update.description", "Download latest translations from the community")
                        primary: true
                        enabled: !LanguageService.downloading && TranslationManager.currentLanguage !== "en" && !LanguageService.isRemoteLanguage(TranslationManager.currentLanguage)
                                 && (!useAiTranslation || (!LanguageService.autoTranslating && LanguageService.uniqueUntranslatedCount > 0))
                        onClicked: {
                            if (useAiTranslation) languageTab.showAiTranslationOffer()
                            else LanguageService.downloadLanguage(TranslationManager.currentLanguage)
                        }
                    }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
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
                    color: Theme.insetBackgroundColor
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
                                // The language's NATIVE name — this card sits inside that
                                // language's own UI, so "العربية" belongs here and "Arabic" does
                                // not. The list above still shows "English (Native)" for
                                // languages you are choosing between; this is the one you are in.
                                text: {
                                    var code = TranslationManager.currentLanguage
                                    var nativeName = LanguageService.getLanguageNativeName(code)
                                    return nativeName.length > 0
                                           ? nativeName
                                           : LanguageService.getLanguageDisplayName(code)
                                }
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(13)
                                font.bold: true
                                color: Theme.textColor
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: {
                                    var total = LanguageService.totalStringCount
                                    var translated = total - LanguageService.untranslatedCount
                                    return translated + " / " + total
                                }
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                            }
                        }

                        ProgressBar {
                            id: progressBar
                            Layout.fillWidth: true
                            from: 0
                            to: Math.max(1, LanguageService.totalStringCount)
                            value: LanguageService.totalStringCount - LanguageService.untranslatedCount

                            Accessible.role: Accessible.ProgressBar
                            Accessible.name: {
                                var total = LanguageService.totalStringCount
                                var translated = total - LanguageService.untranslatedCount
                                var percent = Math.round((translated / Math.max(1, total)) * 100)
                                return TranslationManager.translate(
                                    "language.accessible.progress",
                                    "Translation progress: %1 percent, %2 of %3 strings")
                                    .arg(percent).arg(translated).arg(total)
                            }

                            background: Rectangle {
                                implicitHeight: Theme.scaled(6)
                                color: Theme.borderColor
                                radius: Theme.scaled(3)
                            }

                            contentItem: Item {
                                Rectangle {
                                    width: progressBar.visualPosition * parent.width
                                    height: parent.height
                                    radius: Theme.scaled(3)
                                    color: Theme.successColor
                                }
                            }
                        }

                        Text {
                            text: {
                                var total = LanguageService.totalStringCount
                                var translated = total - LanguageService.untranslatedCount
                                // Compare COUNTS, not a rounded percentage. Math.round() turned
                                // 2974/2977 (99.9%) into 100 and printed "Translation complete!"
                                // directly beneath the "2974 / 2977" that disproved it, while the
                                // language list showed 99% because getTranslationPercent() uses
                                // integer division. Same number, two roundings, one of them lying.
                                if (translated >= total && total > 0)
                                    return TranslationManager.translate(
                                        "language.progress.complete", "Translation complete!")
                                return TranslationManager.translate(
                                    "language.progress.needed", "%1 strings need translation")
                                    .arg(LanguageService.untranslatedCount)
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
                    color: Theme.insetBackgroundColor
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
                    SettingsSearch.title: TranslationManager.translate("settings.search.browseStrings", "Browse & customize strings")
                    accessibleName: TranslationManager.currentLanguage === "en" ? TranslationManager.translate("language.accessible.browse.en", "Browse and customize strings") : TranslationManager.translate("language.accessible.browse", "Browse and translate strings")
                    accessibleDescription: TranslationManager.currentLanguage === "en" ? TranslationManager.translate("language.accessible.browse.en.description", "Open the string browser to customize English text") : TranslationManager.translate("language.accessible.browse.description", "Open the translation browser to translate individual strings")
                    primary: true
                    onClicked: AppShell.stringBrowserRequested()
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("language.file.import", "Import JSON")
                        SettingsSearch.title: TranslationManager.translate("language.file.import", "Import JSON")
                        accessibleName: TranslationManager.translate("language.file.importAccessible", "Import a translation file for the selected language")
                        enabled: LanguageService.ready && !LanguageService.busy && !languageFileActions.busy
                        onClicked: {
                            if (languageFileActions.beginImport(LanguageService.currentContext()))
                                languageImportDialog.open()
                        }
                    }
                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("language.file.export", "Export JSON")
                        SettingsSearch.title: TranslationManager.translate("language.file.export", "Export JSON")
                        accessibleName: TranslationManager.translate("language.file.exportAccessible", "Save the selected language as a translation file on this device")
                        enabled: LanguageService.ready && !LanguageService.busy && !languageFileActions.busy
                        onClicked: languageFileActions.beginExport(LanguageService.currentContext())
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: languageFileActions.busy
                    text: TranslationManager.translate("language.file.working", "Working with the translation file...")
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                AccessibleButton {
                    Layout.fillWidth: true
                    visible: languageFileActions.phase === "transfer"
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("language.file.cancelImport", "Cancel translation file transfer")
                    onClicked: languageTransfer.abort("cancelled")
                }

                // Submit to community button (not for English, developer mode only)
                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(40)
                    activeFocusOnTab: false
                    text: LanguageService.uploading
                        ? TranslationManager.translate("language.button.uploading", "Uploading...")
                        : TranslationManager.translate("language.button.submit", "Submit to Community")
                    SettingsSearch.title: TranslationManager.translate("language.button.submit", "Submit to Community")
                    accessibleName: LanguageService.uploading ? TranslationManager.translate("language.accessible.uploading", "Uploading translation") : TranslationManager.translate("language.accessible.submit", "Submit to community")
                    accessibleDescription: TranslationManager.translate("language.accessible.submit.description", "Share your translations with the community")
                    primary: true
                    visible: TranslationManager.currentLanguage !== "en"
                        && appPreferenceWrite.displayedValue("developerTranslationUpload",
                            languageTab.scalarPreference("app.developerTranslationUpload", false))
                    enabled: !LanguageService.uploading
                    onClicked: {
                        if (languageTab.pendingSubmitRequest === "")
                            languageTab.pendingSubmitRequest = LanguageService.execute("submitTranslation")
                    }
                }

                }
            }
        }

        // ========== RIGHT COLUMN: Accessibility ==========
        SettingsCard {
            searchId: "accessibility"
            title: TranslationManager.translate("settings.accessibility.title", "Accessibility")
            description: TranslationManager.translate("settings.accessibility.desc", "Screen reader support and audio feedback for blind and visually impaired users")
            keywords: ["accessibility", "talkback", "voiceover", "screen reader", "tts", "blind", "voice", "speech", "announce", "talk", "tick", "sound", "audio", "frame", "beep"]
            // The header scrolls with the content.
            showHeader: false
            fillContent: true
            contentMargins: Theme.scaled(12)
            Layout.fillWidth: true
            // Same treatment as the left column: this list grows with the accessibility
            // options and would clip the last few on a short window.
            ScrollView {
                id: accessibilityColumnScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

            ColumnLayout {
                id: accessibilityColumn
                width: accessibilityColumnScroll.availableWidth
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
                            checked: accessibilityPreferenceWrite.displayedValue(
                                "enabled", languageTab.scalarPreference("accessibility.enabled", false))
                            accessibleName: TranslationManager.translate("settings.accessibility.enable", "Enable Accessibility")
                            onToggled: accessibilityPreferenceWrite.submit("enabled", checked)
                        }
                    }

                    // TTS toggle
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        opacity: languageTab.scalarPreference("accessibility.enabled", false) ? 1.0 : 0.5

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
                            checked: accessibilityPreferenceWrite.displayedValue(
                                "ttsEnabled", languageTab.scalarPreference("accessibility.ttsEnabled", true))
                            enabled: languageTab.scalarPreference("accessibility.enabled", false)
                            accessibleName: TranslationManager.translate("settings.accessibility.voiceAnnouncements", "Voice Announcements")
                            onToggled: {
                                var value = checked
                                if (languageTab.scalarPreference("accessibility.enabled", false)) {
                                    if (value) {
                                        accessibilityPreferenceWrite.submit("ttsEnabled", true)
                                        AccessibilityManager.announce(TranslationManager.translate("accessibility.voiceAnnouncementsEnabled", "Voice announcements enabled"), true)
                                    } else {
                                        AccessibilityManager.announce(TranslationManager.translate("accessibility.voiceAnnouncementsDisabled", "Voice announcements disabled"), true)
                                        accessibilityPreferenceWrite.submit("ttsEnabled", false)
                                    }
                                } else {
                                    accessibilityPreferenceWrite.submit("ttsEnabled", value)
                                }
                            }
                        }
                    }

                    // Tick sound toggle
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        opacity: languageTab.scalarPreference("accessibility.enabled", false) ? 1.0 : 0.5

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
                            checked: accessibilityPreferenceWrite.displayedValue(
                                "tickEnabled", languageTab.scalarPreference("accessibility.tickEnabled", true))
                            enabled: languageTab.scalarPreference("accessibility.enabled", false)
                            accessibleName: TranslationManager.translate("settings.accessibility.frameTick", "Frame Tick Sound")
                            onToggled: {
                                var value = checked
                                accessibilityPreferenceWrite.submit("tickEnabled", value)
                                if (languageTab.scalarPreference("accessibility.enabled", false)) {
                                    AccessibilityManager.announce(value ? TranslationManager.translate("accessibility.frameTickEnabled", "Frame tick sound enabled") : TranslationManager.translate("accessibility.frameTickDisabled", "Frame tick sound disabled"), true)
                                }
                            }
                        }
                    }

                    // Tick sound picker and volume
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        opacity: (languageTab.scalarPreference("accessibility.enabled", false)
                            && languageTab.scalarPreference("accessibility.tickEnabled", true)) ? 1.0 : 0.5

                        Tr {
                            key: "settings.accessibility.tickSound"
                            fallback: "Tick Sound"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            Layout.maximumWidth: Theme.scaled(150)
                            value: accessibilityPreferenceWrite.displayedValue(
                                "tickSoundIndex", languageTab.scalarPreference("accessibility.tickSoundIndex", 1))
                            from: 1
                            to: 4
                            stepSize: 1
                            suffix: ""
                            displayText: TranslationManager.translate("accessibility.soundValue", "Sound %1").arg(value)
                            SettingsSearch.title: TranslationManager.translate("settings.search.tickSound", "Tick sound")
                            accessibleName: TranslationManager.translate("accessibility.selectTickSound", "Select tick sound, 1 to 4. Current: %1").arg(value)
                            enabled: languageTab.scalarPreference("accessibility.enabled", false)
                                && languageTab.scalarPreference("accessibility.tickEnabled", true)
                            onValueModified: function(newValue) {
                                accessibilityPreferenceWrite.submit("tickSoundIndex", newValue)
                            }
                        }

                        ValueInput {
                            Layout.maximumWidth: Theme.scaled(150)
                            value: accessibilityPreferenceWrite.displayedValue(
                                "tickVolume", languageTab.scalarPreference("accessibility.tickVolume", 100))
                            from: 10
                            to: 100
                            stepSize: 10
                            suffix: "%"
                            SettingsSearch.title: TranslationManager.translate("settings.search.tickVolume", "Tick volume")
                            accessibleName: TranslationManager.translate("accessibility.tickVolume", "Tick volume. Current: %1 percent").arg(value)
                            enabled: languageTab.scalarPreference("accessibility.enabled", false)
                                && languageTab.scalarPreference("accessibility.tickEnabled", true)
                            onValueModified: function(newValue) {
                                accessibilityPreferenceWrite.submit("tickVolume", newValue)
                            }
                        }
                    }

                    // Divider
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
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
                            checked: accessibilityPreferenceWrite.displayedValue(
                                "extractionAnnouncementsEnabled",
                                languageTab.scalarPreference("accessibility.extractionAnnouncementsEnabled", true))
                            enabled: languageTab.scalarPreference("accessibility.enabled", false)
                            accessibleName: TranslationManager.translate("settings.accessibility.extractionEnable", "Enable During Extraction")
                            onToggled: accessibilityPreferenceWrite.submit("extractionAnnouncementsEnabled", checked)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        opacity: languageTab.scalarPreference("accessibility.extractionAnnouncementsEnabled", true) ? 1.0 : 0.5

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
                            accessibleLabel: TranslationManager.translate("settings.accessibility.announcementMode.accessible", "Announcement mode")
                            enabled: languageTab.scalarPreference("accessibility.enabled", false)
                                && languageTab.scalarPreference("accessibility.extractionAnnouncementsEnabled", true)
                            model: [
                                TranslationManager.translate("settings.accessibility.modeBoth", "Time + Milestones"),
                                TranslationManager.translate("settings.accessibility.modeTimed", "Timed Updates"),
                                TranslationManager.translate("settings.accessibility.modeMilestones", "Weight Milestones")
                            ]
                            currentIndex: {
                                var mode = languageTab.scalarPreference("accessibility.extractionAnnouncementMode", "both")
                                if (mode === "both") return 0
                                if (mode === "timed") return 1
                                if (mode === "milestones_only") return 2
                                return 0
                            }
                            onActivated: function(index) {
                                var modes = ["both", "timed", "milestones_only"]
                                accessibilityPreferenceWrite.submit("extractionAnnouncementMode", modes[index])
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        visible: languageTab.scalarPreference("accessibility.extractionAnnouncementMode", "both") !== "milestones_only"
                        opacity: languageTab.scalarPreference("accessibility.extractionAnnouncementsEnabled", true) ? 1.0 : 0.5

                        Tr {
                            key: "settings.accessibility.updateInterval"
                            fallback: "Announce Every"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            value: accessibilityPreferenceWrite.displayedValue(
                                "extractionAnnouncementInterval",
                                languageTab.scalarPreference("accessibility.extractionAnnouncementInterval", 5))
                            from: 5
                            to: 30
                            stepSize: 5
                            suffix: "s"
                            accessibleName: TranslationManager.translate("settings.accessibility.updateInterval.accessible", "Update Interval")
                            enabled: languageTab.scalarPreference("accessibility.enabled", false)
                                && languageTab.scalarPreference("accessibility.extractionAnnouncementsEnabled", true)
                            onValueModified: function(newValue) {
                                accessibilityPreferenceWrite.submit("extractionAnnouncementInterval", newValue)
                            }
                        }
                    }
                }
            }
        }
        }

    // Delete confirmation popup
    DecenzaDialog {
        id: deleteConfirmPopup
        property string targetLanguage: ""
        onAboutToShow: targetLanguage = TranslationManager.currentLanguage
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
                text: TranslationManager.translate("language.deleteConfirmMessage", "Delete %1 and all its translations?\n\nThis cannot be undone.").arg(LanguageService.getLanguageDisplayName(deleteConfirmPopup.targetLanguage))
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
                        LanguageService.execute("deleteLanguage", { code: deleteConfirmPopup.targetLanguage })
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
        visible: LanguageService.scanning
        z: 100

        ProgressBar {
            id: progressBar2
            anchors.centerIn: parent
            width: parent.width * 0.6
            from: 0
            to: Math.max(1, LanguageService.scanTotal)
            value: LanguageService.scanProgress

            background: Rectangle {
                implicitHeight: Theme.scaled(8)
                color: Qt.rgba(1, 1, 1, 0.2)
                radius: Theme.scaled(4)
            }

            contentItem: Item {
                Rectangle {
                    width: progressBar2.visualPosition * parent.width
                    height: parent.height
                    radius: Theme.scaled(4)
                    color: Theme.primaryColor
                }
            }
        }
    }

    // Submission result popup
    DecenzaDialog {
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
                text: submitResultPopup.isSuccess
                      ? TranslationManager.translate("common.success", "Success!")
                      : TranslationManager.translate("common.error", "Error")
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
        visible: LanguageService.retryStatus !== ""

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
                text: LanguageService.retryStatus
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

    // --- Offer AI translation when a language has gaps -------------------------
    //
    // Missing strings are NOT translated on the fly: autoTranslate() is user-initiated
    // and needs a configured AI provider. Someone switching to a partially-translated
    // language would otherwise just see English scattered through the UI with no hint
    // that they can do anything about it. This surfaces the option at the one moment
    // it is obviously relevant.
    //
    // Offered once per language per VISIT to this tab — this property's lifetime is the
    // tab item's, not the app's, so navigating away and back re-arms it. An event-based
    // set, not a timer. Known gap: a user who dismisses and THEN configures an AI
    // provider is not re-offered until they leave and return.
    property var _aiOfferedFor: ({})

    function maybeOfferAiTranslation(lang: string): void {
        if (!lang || lang === "en") return                  // base language is never incomplete
        if (lang !== LanguageService.snapshot.currentLanguage) return
        if (LanguageService.autoTranslating) return
        if (languageTab._aiOfferedFor[lang]) return         // already asked on this tab visit
        // A language selection can change host state before its remote catalog fetch
        // completes. Only call this from the correlated service completion, after the
        // host has applied the downloaded file and published current coverage.
        if (LanguageService.downloading) return
        if (LanguageService.uniqueUntranslatedCount <= 0) return
        languageTab._aiOfferedFor[lang] = true
        languageTab.showAiTranslationOffer()
    }

    function showAiTranslationOffer(): void {
        // Read current property values immediately before opening the dialog. Bindings
        // would also update, but this popup snapshots its offer for this visit.
        aiTranslateOfferPopup.targetContext = LanguageService.currentContext()
        aiTranslateOfferPopup.resultText = ""
        aiTranslateOfferPopup.resultIsError = false
        aiTranslateOfferPopup.userCancelled = false
        aiTranslateOfferPopup.aiAvailable = LanguageService.canAutoTranslate
        aiTranslateOfferPopup.missingCount = LanguageService.uniqueUntranslatedCount
        aiTranslateOfferPopup.percentDone = LanguageService.totalStringCount > 0
            ? Math.round(100 * (LanguageService.totalStringCount - LanguageService.untranslatedCount)
                             / LanguageService.totalStringCount)
            : 0
        aiTranslateOfferPopup.open()
    }

    DecenzaDialog {
        id: aiTranslateOfferPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(380))
        modal: true
        dim: true
        padding: Theme.spacingMedium
        closePolicy: LanguageService.autoTranslating ? Dialog.NoAutoClose
                                                        : (Dialog.CloseOnEscape | Dialog.CloseOnPressOutside)

        // Set from maybeOfferAiTranslation() immediately before open() — see the note
        // there. Deliberately NOT bindings on invokables.
        property bool aiAvailable: false
        property int missingCount: 0
        property var targetContext: ({})
        readonly property string langName: LanguageService.getLanguageDisplayName(targetContext.language || "")
        // Snapshotted alongside missingCount, not a live binding. Mixing the two produced
        // self-contradicting text ("94% translated — 812 phrases still in English") when
        // a download landed while the dialog was open.
        property int percentDone: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryColor
        }

        contentItem: Column {
            spacing: Theme.spacingMedium

            Text {
                width: parent.width
                text: TranslationManager.translate("language.aiOffer.title", "Translation Incomplete")
                font: Theme.subtitleFont
                color: Theme.textColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            Text {
                width: parent.width
                text: TranslationManager.translate("language.aiOffer.message",
                          "%1 is %2% translated — %3 phrases are still in English.")
                      .arg(aiTranslateOfferPopup.langName)
                      .arg(aiTranslateOfferPopup.percentDone)
                      .arg(aiTranslateOfferPopup.missingCount)
                font: Theme.bodyFont
                color: Theme.textColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: !LanguageService.autoTranslating
            }

            // No provider configured — say what to do rather than offering a dead button.
            Text {
                width: parent.width
                text: TranslationManager.translate("language.aiOffer.noProvider",
                          "Set up an AI provider in Settings → AI to translate the rest automatically.")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: !LanguageService.autoTranslating && !aiTranslateOfferPopup.aiAvailable
            }

            Text {
                width: parent.width
                text: TranslationManager.translate("language.aiOffer.question",
                          "Translate them now with your configured AI?")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: !LanguageService.autoTranslating && aiTranslateOfferPopup.aiAvailable
            }

            // Progress, shown in place of the prompt once translation is running.
            Text {
                width: parent.width
                text: aiTranslateOfferPopup.resultText
                font: Theme.bodyFont
                color: Theme.errorColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: aiTranslateOfferPopup.resultIsError
            }

            Text {
                width: parent.width
                text: TranslationManager.translate("language.aiOffer.progress", "Translating… %1 / %2")
                      .arg(LanguageService.autoTranslateProgress)
                      .arg(LanguageService.autoTranslateTotal)
                font: Theme.bodyFont
                color: Theme.textColor
                horizontalAlignment: Text.AlignHCenter
                visible: LanguageService.autoTranslating
            }

            Rectangle {
                width: parent.width
                height: Theme.scaled(6)
                radius: height / 2
                color: Theme.borderColor
                visible: LanguageService.autoTranslating

                Rectangle {
                    height: parent.height
                    radius: parent.radius
                    color: Theme.primaryColor
                    width: parent.width * (LanguageService.autoTranslateProgress
                                           / Math.max(1, LanguageService.autoTranslateTotal))
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingSmall

                AccessibleButton {
                    width: aiTranslateOfferPopup.aiAvailable && !LanguageService.autoTranslating
                           ? (parent.width - Theme.spacingSmall) / 2 : parent.width
                    text: LanguageService.autoTranslating
                          ? TranslationManager.translate("language.aiOffer.cancel", "Stop")
                          : TranslationManager.translate("language.aiOffer.dismiss", "Not Now")
                    accessibleName: LanguageService.autoTranslating
                          ? TranslationManager.translate("language.aiOffer.accessible.cancel", "Stop translating")
                          : TranslationManager.translate("language.aiOffer.accessible.dismiss", "Continue without translating")
                    onClicked: {
                        if (LanguageService.autoTranslating) {
                            // cancelAutoTranslate() reports completion as a FAILURE
                            // ("Translation cancelled"), so without this flag the user's
                            // own Stop press would paint a red error and leave the dialog
                            // open, needing a second press to dismiss.
                            aiTranslateOfferPopup.userCancelled = true
                            LanguageService.execute("cancelAutoTranslate")
                        } else {
                            aiTranslateOfferPopup.close()
                        }
                    }
                }

                AccessibleButton {
                    width: (parent.width - Theme.spacingSmall) / 2
                    visible: aiTranslateOfferPopup.aiAvailable && !LanguageService.autoTranslating
                    text: TranslationManager.translate("language.aiOffer.translate", "Translate")
                    accessibleName: TranslationManager.translate("language.aiOffer.accessible.translate",
                                        "Translate missing phrases using the configured AI provider")
                    onClicked: aiTranslateOfferPopup.pendingAutoRequest = LanguageService.execute("autoTranslate", aiTranslateOfferPopup.targetContext)
                }
            }
        }

        // Surfaced rather than swallowed: a failed run (bad key, quota, network) used to
        // close the dialog exactly like a successful one, leaving the user to notice the
        // strings were still English and guess why.
        property string resultText: ""
        property bool resultIsError: false
        property bool userCancelled: false
        property string pendingAutoRequest: ""

        Connections {
            target: LanguageService
            function onOperationCompleted(requestId, operation, success, result, reason) {
                if (requestId !== aiTranslateOfferPopup.pendingAutoRequest || operation !== "autoTranslate")
                    return
                aiTranslateOfferPopup.pendingAutoRequest = ""
                if (!aiTranslateOfferPopup.opened) return
                if (success || aiTranslateOfferPopup.userCancelled) {
                    aiTranslateOfferPopup.userCancelled = false
                    aiTranslateOfferPopup.close()
                    return
                }
                aiTranslateOfferPopup.resultIsError = true
                // Deliberately the translated generic string, not `message`: the backend
                // messages are raw English literals, and this dialog is only ever shown to
                // users running a non-English UI. The provider's text goes to the log.
                aiTranslateOfferPopup.resultText = reason === "staleContext"
                    ? TranslationManager.translate("language.aiOffer.staleContext", "The host language or translations changed. Close this offer and review the current language before translating.")
                    : TranslationManager.translate("language.aiOffer.failedGeneric",
                        "Translation failed. Check your AI provider settings and try again.")
            }
        }
    }

    // Refusals from TranslationManager reach the user here instead of only the log. Without this
    // a guarded save looks exactly like a broken button — which is how it was found: an edit was
    // correctly refused, nothing said so, and the tester had to ask whether the feature worked.
    TranslationErrorToast {
        // File actions report their own correlated outcome, including partial saves.
        enabled: !languageFileActions.busy
    }
}
