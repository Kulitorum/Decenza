pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

DecenzaDialog {
    id: archive
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(1100), parent ? parent.width - Theme.spacingLarge : Theme.scaled(1100))
    height: Math.min(Theme.scaled(740), parent ? parent.height - Theme.spacingLarge : Theme.scaled(740))
    title: TranslationManager.translate("settings.debug.aiArchive", "AI conversation archive")
    modal: true
    closePolicy: archive.busy || download.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }
    header: Text {
        text: archive.title
        color: Theme.textColor
        font: Theme.subtitleFont
        padding: Theme.spacingMedium
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.Heading
        Accessible.name: archive.title
    }

    property var entries: []
    property string selectedKey: ""
    property string selectedLabel: ""
    property string transcript: ""
    property string errorMessage: ""
    property string pendingId: ""
    property string pendingOperation: ""
    property bool viewingConversation: false
    property bool truncated: false
    readonly property bool busy: pendingId !== ""
    readonly property bool narrow: archive.width < Theme.scaled(760)
    readonly property var filteredEntries: {
        const query = search.text.trim().toLocaleLowerCase()
        return archive.entries.filter(function(entry) { return !query || entry.label.toLocaleLowerCase().indexOf(query) >= 0 })
    }

    function request(operation: string, fields: var): void {
        if (archive.busy || download.busy) return
        archive.errorMessage = ""
        archive.pendingOperation = operation
        archive.pendingId = ConversationSession.request(operation, fields)
    }

    function selectEntry(entry: var): void {
        archive.selectedKey = entry.key
        archive.selectedLabel = entry.label
        archive.transcript = ""
        archive.truncated = false
        archive.request("readArchive", {key: entry.key})
    }

    onOpened: {
        archive.viewingConversation = false
        archive.request("listArchive", ({}))
        search.forceActiveFocus()
    }
    onClosed: {
        download.cancelSilently()
        archive.pendingId = ""
        archive.transcript = ""
        archive.selectedKey = ""
        archive.selectedLabel = ""
    }

    LocalFileDownload {
        id: download
        downloadDirectly: true
        executeOperation: function(operation, fields) { return ConversationSession.request(operation, fields) }
        onFinished: function(success, reason) {
            archive.errorMessage = success ? "" : TranslationManager.translate(
                "settings.debug.aiArchiveDownloadFailed", "The conversation could not be downloaded (%1).").arg(reason)
            if (success) savedToast.show(TranslationManager.translate("common.fileSaved", "File saved"))
        }
    }
    StatusToast { id: savedToast; parent: Overlay.overlay }
    Connections {
        target: ConversationSession
        function onCompleted(id, operation, success, result, reason) {
            download.completeOperation(id, operation, success, result, reason)
            if (id !== archive.pendingId || operation !== archive.pendingOperation) return
            archive.pendingId = ""
            if (!success) {
                archive.errorMessage = TranslationManager.translate("settings.debug.aiArchiveFailed",
                    "The archive could not be read (%1). Refresh and try again.").arg(reason)
                return
            }
            if (operation === "listArchive") archive.entries = result.entries || []
            else if (operation === "readArchive") {
                archive.transcript = result.text || ""
                archive.truncated = result.truncated === true
                archive.viewingConversation = true
            } else if (operation === "exportArchive" && !download.start(result)) {
                archive.errorMessage = TranslationManager.translate("settings.debug.aiArchiveDownloadFailed",
                    "The conversation could not be downloaded (%1).").arg("saveUnavailable")
            }
        }
    }

    contentItem: ColumnLayout {
        Accessible.role: Accessible.Dialog
        Accessible.name: archive.title
        spacing: Theme.spacingSmall
        Text {
            Layout.fillWidth: true
            visible: archive.height >= Theme.scaled(450)
            text: TranslationManager.translate("settings.debug.aiArchiveHint",
                "Browse saved conversations for troubleshooting. Downloads include the full conversation and system prompt.")
            wrapMode: Text.WordWrap
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
        }
        RowLayout {
            Layout.fillWidth: true
            StyledTextField {
                id: search
                Layout.fillWidth: true
                visible: !archive.narrow || !archive.viewingConversation
                placeholder: TranslationManager.translate("settings.debug.aiArchiveSearch", "Search coffee, profile or equipment")
                accessibleName: placeholder
            }
            AccessibleButton {
                visible: archive.narrow && archive.viewingConversation
                accessibleName: TranslationManager.translate("settings.debug.aiArchiveBack", "Back to conversations")
                text: accessibleName
                onClicked: archive.viewingConversation = false
            }
            AccessibleButton {
                accessibleName: TranslationManager.translate("common.refresh", "Refresh")
                text: accessibleName
                enabled: !archive.busy && !download.busy && ConversationSession.ready
                onClicked: archive.request("listArchive", ({}))
            }
        }
        Text {
            Layout.fillWidth: true
            visible: archive.errorMessage !== ""
            text: archive.errorMessage
            color: Theme.errorColor
            font: Theme.bodyFont
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.AlertMessage
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMedium
            ListView {
                id: archiveList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: archive.narrow ? archive.width : archive.width * 0.32
                visible: !archive.narrow || !archive.viewingConversation
                clip: true
                model: archive.filteredEntries
                spacing: Theme.spacingSmall
                ScrollBar.vertical: ScrollBar {}
                delegate: AccessibleButton {
                    id: archiveRow
                    required property var modelData
                    width: archiveList.width
                    height: rowText.implicitHeight + archiveRow.topPadding + archiveRow.bottomPadding
                    enabled: !archive.busy && !download.busy
                    primary: archive.selectedKey === archiveRow.modelData.key
                    accessibleName: archiveRow.modelData.label
                    onClicked: archive.selectEntry(archiveRow.modelData)
                    contentItem: Column {
                        id: rowText
                        spacing: Theme.spacingSmall
                        Text {
                            width: rowText.width
                            text: archiveRow.modelData.label || TranslationManager.translate("conversation.unknown", "AI conversation")
                            color: archiveRow.primary ? Theme.primaryContrastColor : Theme.textColor
                            font: Theme.bodyFont
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            text: archiveRow.modelData.timestamp > 0
                                ? Qt.formatDateTime(new Date(archiveRow.modelData.timestamp * 1000))
                                : TranslationManager.translate("settings.debug.aiArchiveUnknownDate", "Date unavailable")
                            color: archiveRow.primary ? Theme.primaryContrastColor : Theme.textSecondaryColor
                            font: Theme.captionFont
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    width: archiveList.width
                    visible: !archive.busy && archive.errorMessage === "" && archive.filteredEntries.length === 0
                    text: TranslationManager.translate("settings.debug.aiArchiveEmpty", "No saved conversations match.")
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: archive.width * 0.68
                visible: !archive.narrow || archive.viewingConversation
                Text {
                    Layout.fillWidth: true
                    text: archive.selectedLabel || TranslationManager.translate("settings.debug.aiArchiveChoose", "Select a conversation")
                    font: Theme.subtitleFont
                    color: Theme.textColor
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    visible: archive.truncated
                    text: TranslationManager.translate("settings.debug.aiArchiveShortened", "Preview shortened. Download the complete conversation below.")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    wrapMode: Text.WordWrap
                }
                ScrollView {
                    id: transcriptScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    TextArea {
                        width: transcriptScroll.availableWidth
                        text: archive.transcript
                        placeholderText: archive.selectedKey !== "" && !archive.busy && !archive.truncated
                            ? TranslationManager.translate("settings.debug.aiArchiveNoMessages", "This saved conversation contains no messages.") : ""
                        placeholderTextColor: Theme.textSecondaryColor
                        readOnly: true
                        selectByMouse: true
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.WordWrap
                        font: Theme.bodyFont
                        color: Theme.textColor
                        background: null
                        Accessible.name: TranslationManager.translate("settings.debug.aiArchiveTranscript", "Archived conversation transcript")
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: archive.selectedKey !== "" && (!archive.narrow || archive.viewingConversation)
            AccessibleButton {
                Layout.fillWidth: true
                accessibleName: TranslationManager.translate("settings.debug.aiArchiveText", "Download Text")
                text: accessibleName
                enabled: !archive.busy && !download.busy && ConversationSession.ready
                onClicked: archive.request("exportArchive", {key: archive.selectedKey, format: "text"})
            }
            AccessibleButton {
                Layout.fillWidth: true
                accessibleName: TranslationManager.translate("settings.debug.aiArchiveJson", "Download JSON")
                text: accessibleName
                enabled: !archive.busy && !download.busy && ConversationSession.ready
                onClicked: archive.request("exportArchive", {key: archive.selectedKey, format: "json"})
            }
        }
        RowLayout {
            Layout.fillWidth: true
            BusyIndicator {
                running: archive.busy || download.busy
                visible: running
                Layout.preferredHeight: Theme.scaled(28)
                Layout.preferredWidth: Theme.scaled(28)
                Accessible.name: TranslationManager.translate("settings.debug.aiArchiveLoading", "Loading conversation")
            }
            Item { Layout.fillWidth: true }
            AccessibleButton {
                accessibleName: TranslationManager.translate("common.close", "Close")
                text: accessibleName
                enabled: !archive.busy && !download.busy
                onClicked: archive.close()
            }
        }
    }
}
