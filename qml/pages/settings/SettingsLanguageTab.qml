import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
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
            submitResultPopup.message = message
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

        // Left column: Language selection
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: 300
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingMedium

                Text {
                    text: "Languages"
                    font: Theme.subtitleFont
                    color: Theme.textColor
                }

                ListView {
                    id: languageList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: TranslationManager.availableLanguages

                    delegate: Item {
                        id: langDelegate
                        width: languageList.width
                        height: Theme.scaled(44)

                        property bool highlighted: modelData === TranslationManager.currentLanguage
                        property string langCode: modelData
                        property string displayName: TranslationManager.getLanguageDisplayName(modelData)
                        property string nativeName: TranslationManager.getLanguageNativeName(modelData)

                        Accessible.role: Accessible.Button
                        Accessible.name: {
                            var code = modelData  // Force binding to modelData
                            var display = TranslationManager.getLanguageDisplayName(code)
                            var native_ = TranslationManager.getLanguageNativeName(code)
                            var name = native_ !== display ? display + ", " + native_ : display
                            if (code === TranslationManager.currentLanguage) {
                                name += ", " + TranslationManager.translate("language.accessible.selected", "selected")
                            }
                            return name
                        }
                        Accessible.focusable: true

                        Rectangle {
                            anchors.fill: parent
                            color: langDelegate.highlighted ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : "transparent"
                            radius: Theme.buttonRadius
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(8)
                            anchors.rightMargin: Theme.scaled(8)
                            spacing: Theme.scaled(8)

                            Text {
                                Layout.fillWidth: true
                                text: langDelegate.nativeName !== langDelegate.displayName ? langDelegate.displayName + " (" + langDelegate.nativeName + ")" : langDelegate.displayName
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.bodyFont.pixelSize
                                font.bold: langDelegate.highlighted
                                color: {
                                    var version = TranslationManager.translationVersion  // Force re-evaluation
                                    if (TranslationManager.isRemoteLanguage(langDelegate.langCode)) return "#2196F3"
                                    return Theme.successColor
                                }
                                elide: Text.ElideRight
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
                        Layout.preferredHeight: Theme.scaled(48)
                        text: "Add..."
                        accessibleName: TranslationManager.translate("language.accessible.add", "Add language")
                        accessibleDescription: TranslationManager.translate("language.accessible.add.description", "Add a new language for translation")
                        onClicked: pageStack.push("AddLanguagePage.qml")
                    }

                    // Delete button - temporarily visible
                    AccessibleButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(48)
                        visible: true
                        text: "Delete"
                        accessibleName: TranslationManager.translate("language.accessible.delete", "Delete language")
                        accessibleDescription: TranslationManager.translate("language.accessible.delete.description", "Delete the selected language and its translations")
                        warning: true
                        enabled: TranslationManager.currentLanguage !== "en"
                        onClicked: deleteConfirmPopup.open()
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(48)
                        text: TranslationManager.downloading ? "..." : "Update"
                        accessibleName: TranslationManager.downloading ? TranslationManager.translate("language.accessible.downloading", "Downloading") : TranslationManager.translate("language.accessible.update", "Update community translations")
                        accessibleDescription: TranslationManager.translate("language.accessible.update.description", "Download latest translations from the community")
                        primary: true
                        enabled: !TranslationManager.downloading && TranslationManager.currentLanguage !== "en" && !TranslationManager.isRemoteLanguage(TranslationManager.currentLanguage)
                        onClicked: TranslationManager.downloadLanguage(TranslationManager.currentLanguage)
                    }
                }
            }
        }

        // Right column: Translation progress & actions
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingMedium

                Text {
                    text: "Translation"
                    font: Theme.subtitleFont
                    color: Theme.textColor
                }

                // Progress card (non-English only)
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: progressColumn.height + 24
                    color: Theme.backgroundColor
                    radius: Theme.buttonRadius
                    visible: TranslationManager.currentLanguage !== "en"

                    ColumnLayout {
                        id: progressColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(8)

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.getLanguageDisplayName(TranslationManager.currentLanguage)
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.bodyFont.pixelSize
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
                                implicitHeight: Theme.scaled(8)
                                color: Theme.borderColor
                                radius: Theme.scaled(4)
                            }

                            contentItem: Item {
                                Rectangle {
                                    width: parent.parent.visualPosition * parent.width
                                    height: parent.height
                                    radius: Theme.scaled(4)
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
                    implicitHeight: Theme.scaled(50)
                    color: Theme.backgroundColor
                    radius: Theme.buttonRadius
                    visible: TranslationManager.currentLanguage === "en"

                    Text {
                        anchors.centerIn: parent
                        text: "English is the base language.\nYou can customize the default text below."
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                // Browse strings button
                AccessibleButton {
                    Layout.fillWidth: true
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
                    activeFocusOnTab: false
                    text: TranslationManager.uploading ? "Uploading..." : "Submit to Community"
                    accessibleName: TranslationManager.uploading ? TranslationManager.translate("language.accessible.uploading", "Uploading translation") : TranslationManager.translate("language.accessible.submit", "Submit to community")
                    accessibleDescription: TranslationManager.translate("language.accessible.submit.description", "Share your translations with the community")
                    primary: true
                    visible: TranslationManager.currentLanguage !== "en" && Settings.developerTranslationUpload
                    enabled: !TranslationManager.uploading
                    onClicked: TranslationManager.submitTranslation()
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // Delete confirmation popup
    Popup {
        id: deleteConfirmPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        dim: true
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.warningColor
        }

        contentItem: Column {
            spacing: Theme.spacingMedium
            width: Theme.scaled(280)

            Text {
                width: parent.width
                text: "Delete Language?"
                font: Theme.subtitleFont
                color: Theme.warningColor
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                width: parent.width
                text: "Delete " + TranslationManager.getLanguageDisplayName(TranslationManager.currentLanguage) + " and all its translations?\n\nThis cannot be undone."
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
                    text: "Cancel"
                    accessibleName: TranslationManager.translate("settings.language.cancelKeep", "Cancel and keep language")
                    onClicked: deleteConfirmPopup.close()
                }

                AccessibleButton {
                    width: (parent.width - Theme.spacingSmall) / 2
                    text: "Delete"
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
    Popup {
        id: submitResultPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        dim: true
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property bool isSuccess: false
        property string message: ""

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: submitResultPopup.isSuccess ? Theme.successColor : Theme.warningColor
        }

        contentItem: Column {
            spacing: Theme.spacingMedium
            width: Theme.scaled(300)

            Text {
                width: parent.width
                text: submitResultPopup.isSuccess ? "Success!" : "Error"
                font: Theme.subtitleFont
                color: submitResultPopup.isSuccess ? Theme.successColor : Theme.warningColor
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                width: parent.width
                text: submitResultPopup.message
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            AccessibleButton {
                width: parent.width
                text: "OK"
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
            width: Theme.scaled(280)

            Text {
                width: parent.width
                text: TranslationManager.retryStatus
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                width: parent.width
                text: "Please wait..."
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
