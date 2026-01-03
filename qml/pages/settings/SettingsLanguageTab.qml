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
                        height: 44

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
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 8

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

                    Button {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        text: "Add..."

                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("language.accessible.add", "Add language")
                        Accessible.description: TranslationManager.translate("language.accessible.add.description", "Add a new language for translation")

                        onClicked: pageStack.push("AddLanguagePage.qml")

                        background: Rectangle {
                            implicitHeight: 48
                            color: parent.down ? Qt.darker(Theme.surfaceColor, 1.2) : Qt.lighter(Theme.surfaceColor, 1.3)
                            radius: Theme.buttonRadius
                        }

                        contentItem: Text {
                            text: parent.text
                            font: Theme.bodyFont
                            color: Theme.textColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    // Delete button - temporarily visible
                    Button {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        visible: true
                        text: "Delete"
                        enabled: TranslationManager.currentLanguage !== "en"

                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("language.accessible.delete", "Delete language")
                        Accessible.description: TranslationManager.translate("language.accessible.delete.description", "Delete the selected language and its translations")

                        onClicked: deleteConfirmPopup.open()

                        background: Rectangle {
                            implicitHeight: 48
                            color: parent.down ? Qt.darker(Theme.warningColor, 1.2) : Theme.warningColor
                            radius: Theme.buttonRadius
                            opacity: parent.enabled ? 1.0 : 0.3
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
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        text: TranslationManager.downloading ? "..." : "Update"
                        enabled: !TranslationManager.downloading && TranslationManager.currentLanguage !== "en" && !TranslationManager.isRemoteLanguage(TranslationManager.currentLanguage)

                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.downloading ? TranslationManager.translate("language.accessible.downloading", "Downloading") : TranslationManager.translate("language.accessible.update", "Update community translations")
                        Accessible.description: TranslationManager.translate("language.accessible.update.description", "Download latest translations from the community")

                        background: Rectangle {
                            implicitHeight: 48
                            color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                            radius: Theme.buttonRadius
                            opacity: parent.enabled ? 1.0 : 0.5
                        }

                        contentItem: Text {
                            text: parent.text
                            font: Theme.bodyFont
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

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
                        anchors.margins: 12
                        spacing: 8

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
                                implicitHeight: 8
                                color: Theme.borderColor
                                radius: 4
                            }

                            contentItem: Item {
                                Rectangle {
                                    width: parent.parent.visualPosition * parent.width
                                    height: parent.height
                                    radius: 4
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
                    implicitHeight: 50
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
                Button {
                    Layout.fillWidth: true
                    text: TranslationManager.currentLanguage === "en" ? "Browse & Customize Strings..." : "Browse & Translate Strings..."

                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.currentLanguage === "en" ? TranslationManager.translate("language.accessible.browse.en", "Browse and customize strings") : TranslationManager.translate("language.accessible.browse", "Browse and translate strings")
                    Accessible.description: TranslationManager.currentLanguage === "en" ? TranslationManager.translate("language.accessible.browse.en.description", "Open the string browser to customize English text") : TranslationManager.translate("language.accessible.browse.description", "Open the translation browser to translate individual strings")

                    background: Rectangle {
                        implicitHeight: 48
                        color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                        radius: Theme.buttonRadius
                    }

                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: pageStack.push("StringBrowserPage.qml")
                }

                Item { Layout.fillHeight: true }

                // Submit to community button (not for English, developer mode only)
                Button {
                    Layout.fillWidth: true
                    text: TranslationManager.uploading ? "Uploading..." : "Submit to Community"
                    visible: TranslationManager.currentLanguage !== "en" && Settings.developerTranslationUpload
                    enabled: !TranslationManager.uploading

                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.uploading ? TranslationManager.translate("language.accessible.uploading", "Uploading translation") : TranslationManager.translate("language.accessible.submit", "Submit to community")
                    Accessible.description: TranslationManager.translate("language.accessible.submit.description", "Share your translations with the community")

                    background: Rectangle {
                        implicitHeight: 48
                        color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                        radius: Theme.buttonRadius
                        opacity: parent.enabled ? 1.0 : 0.5
                    }

                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: TranslationManager.submitTranslation()
                }
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
            width: 280

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

                Button {
                    width: (parent.width - Theme.spacingSmall) / 2
                    text: "Cancel"

                    background: Rectangle {
                        implicitHeight: 40
                        color: parent.down ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
                        radius: Theme.buttonRadius
                        border.width: 1
                        border.color: Theme.borderColor
                    }

                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: deleteConfirmPopup.close()
                }

                Button {
                    width: (parent.width - Theme.spacingSmall) / 2
                    text: "Delete"

                    background: Rectangle {
                        implicitHeight: 40
                        color: parent.down ? Qt.darker(Theme.warningColor, 1.2) : Theme.warningColor
                        radius: Theme.buttonRadius
                    }

                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

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
                implicitHeight: 8
                color: Qt.rgba(1, 1, 1, 0.2)
                radius: 4
            }

            contentItem: Item {
                Rectangle {
                    width: parent.parent.visualPosition * parent.width
                    height: parent.height
                    radius: 4
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
            width: 300

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

            Button {
                width: parent.width
                text: "OK"

                background: Rectangle {
                    implicitHeight: 40
                    color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    radius: Theme.buttonRadius
                }

                contentItem: Text {
                    text: parent.text
                    font: Theme.bodyFont
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: submitResultPopup.close()
            }
        }
    }
}
