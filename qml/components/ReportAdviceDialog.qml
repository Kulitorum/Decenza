import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

Dialog {
    id: root
    anchors.centerIn: parent
    width: Theme.scaled(450)
    modal: true
    padding: 0
    closePolicy: Dialog.NoAutoClose
    Accessible.name: TranslationManager.translate("aiReport.title", "Report Bad Advice")

    property string conversationTranscript: ""
    property string shotDebugLog: ""
    property string providerName: ""
    property string modelName: ""
    property string systemPrompt: ""
    property string contextLabel: ""

    // State management
    property string dialogState: "prompt"  // prompt, submitting, success, error
    property string issueUrl: ""
    property string errorMessage: ""

    // Format the AI report as crash_log for the existing crash-report endpoint
    function buildCrashLog() {
        var body = "[AI Report] Bad advice from " + root.providerName + " / " + root.modelName
        if (root.contextLabel.length > 0)
            body += "\nContext: " + root.contextLabel
        body += "\n\n--- System Prompt ---\n" + root.systemPrompt
        body += "\n\n--- Conversation Transcript ---\n" + root.conversationTranscript
        if (root.shotDebugLog.length > 0)
            body += "\n\n--- Shot Debug Log ---\n" + root.shotDebugLog
        return body
    }

    function submitReport() {
        if (CrashReporter.submitting) {
            root.errorMessage = TranslationManager.translate("aiReport.alreadySubmitting",
                "Another report is already being submitted. Please wait and try again.")
            root.dialogState = "error"
            return
        }
        root.dialogState = "submitting"
        CrashReporter.submitReport(buildCrashLog(), userNotesInput.text)
    }

    onOpened: {
        dialogState = "prompt"
        issueUrl = ""
        errorMessage = ""
        userNotesInput.text = ""
    }

    Connections {
        target: CrashReporter
        function onSubmitted(url) {
            if (root.dialogState === "submitting") {
                root.issueUrl = url
                root.dialogState = "success"
            }
        }
        function onFailed(error) {
            if (root.dialogState === "submitting") {
                root.errorMessage = error
                root.dialogState = "error"
            }
        }
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: "white"
    }

    contentItem: ColumnLayout {
        spacing: 0

        // === PROMPT STATE ===
        ColumnLayout {
            visible: root.dialogState === "prompt"
            spacing: 0
            Layout.fillWidth: true

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.topMargin: Theme.scaled(10)

                RowLayout {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scaled(12)

                    Rectangle {
                        width: Theme.scaled(32)
                        height: Theme.scaled(32)
                        radius: Theme.scaled(16)
                        color: Theme.warningColor
                        Accessible.ignored: true

                        Image {
                            anchors.centerIn: parent
                            source: "qrc:/emoji/26a0.svg"  // Warning sign
                            sourceSize.width: Theme.scaled(18)
                            sourceSize.height: Theme.scaled(18)
                            Accessible.ignored: true
                        }
                    }

                    Text {
                        text: TranslationManager.translate("aiReport.title", "Report Bad Advice")
                        font: Theme.titleFont
                        color: Theme.textColor
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Message
            Text {
                text: TranslationManager.translate("aiReport.message",
                    "This will send the following to help improve the AI advisor:")
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(12)
            }

            // Bullet list of what's included
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(32)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(6)
                spacing: Theme.scaled(2)

                Text {
                    text: "\u2022 " + TranslationManager.translate("aiReport.bullet.transcript", "AI conversation transcript")
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                }
                Text {
                    text: "\u2022 " + TranslationManager.translate("aiReport.bullet.systemprompt", "System prompt (AI instructions)")
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                }
                Text {
                    text: "\u2022 " + TranslationManager.translate("aiReport.bullet.debuglog", "Shot debug log (machine data from the shot)")
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                }
                Text {
                    text: "\u2022 " + TranslationManager.translate("aiReport.bullet.device", "App version and device info")
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                }
            }

            // Transcript preview (collapsible)
            Rectangle {
                id: transcriptPreview
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(12)
                Layout.preferredHeight: previewExpanded ? Theme.scaled(150) : Theme.scaled(36)
                color: Theme.backgroundColor
                radius: Theme.scaled(4)
                clip: true

                property bool previewExpanded: false

                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: 200 }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(8)
                    spacing: Theme.scaled(4)

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: TranslationManager.translate("aiReport.previewData", "Preview data")
                            font: Theme.labelFont
                            color: Theme.textSecondaryColor
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: transcriptPreview.previewExpanded
                                  ? TranslationManager.translate("aiReport.hide", "Hide")
                                  : TranslationManager.translate("aiReport.show", "Show")
                            font: Theme.labelFont
                            color: Theme.primaryColor

                            Accessible.role: Accessible.Button
                            Accessible.name: transcriptPreview.previewExpanded
                                ? TranslationManager.translate("aiReport.hide", "Hide")
                                : TranslationManager.translate("aiReport.show", "Show")
                            Accessible.focusable: true
                            Accessible.onPressAction: transcriptPreview.previewExpanded = !transcriptPreview.previewExpanded

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: transcriptPreview.previewExpanded = !transcriptPreview.previewExpanded
                            }
                        }
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: transcriptPreview.previewExpanded
                        clip: true

                        TextArea {
                            readOnly: true
                            text: root.conversationTranscript
                            font.family: "monospace"
                            font.pixelSize: Theme.scaled(10)
                            color: Theme.textColor
                            wrapMode: TextArea.Wrap
                            background: null
                            Accessible.role: Accessible.StaticText
                            Accessible.name: TranslationManager.translate("aiReport.previewData", "Preview data")
                            Accessible.description: text.substring(0, 200)
                        }
                    }
                }
            }

            // User notes input (required)
            Text {
                text: TranslationManager.translate("aiReport.whatWasWrong", "What was wrong with the advice?")
                font: Theme.labelFont
                color: Theme.textSecondaryColor
                Layout.leftMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(12)
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(70)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(8)
                color: Theme.backgroundColor
                radius: Theme.scaled(4)
                border.color: userNotesInput.activeFocus ? Theme.primaryColor : Theme.borderColor
                border.width: 1

                TextArea {
                    id: userNotesInput
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(8)
                    font: Theme.bodyFont
                    color: Theme.textColor
                    placeholderText: TranslationManager.translate("aiReport.placeholder",
                        "e.g., 'It suggested a finer grind but my shot was already choking'")
                    placeholderTextColor: Qt.rgba(Theme.textSecondaryColor.r, Theme.textSecondaryColor.g, Theme.textSecondaryColor.b, 0.5)
                    wrapMode: TextArea.Wrap
                    background: null
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("aiReport.whatWasWrong", "What was wrong with the advice?")
                    Accessible.description: text
                    Accessible.focusable: true
                }
            }

            // Validation hint
            Text {
                visible: userNotesInput.text.trim().length === 0
                text: TranslationManager.translate("aiReport.notesRequired", "Please describe the issue to submit")
                font: Theme.labelFont
                color: Theme.textSecondaryColor
                Layout.leftMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(4)
            }

            // Buttons
            Grid {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                columns: 2
                spacing: Theme.scaled(10)

                property real buttonWidth: (width - spacing) / 2
                property real buttonHeight: Theme.scaled(50)

                AccessibleButton {
                    width: parent.buttonWidth
                    height: parent.buttonHeight
                    text: TranslationManager.translate("aiReport.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("aiReport.cancelAccessible", "Cancel report")
                    onClicked: root.close()
                    background: Rectangle {
                        implicitHeight: Theme.scaled(60)
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
                    width: parent.buttonWidth
                    height: parent.buttonHeight
                    enabled: userNotesInput.text.trim().length > 0
                    text: TranslationManager.translate("aiReport.submit", "Submit")
                    accessibleName: TranslationManager.translate("aiReport.submitAccessible", "Submit AI advice report")
                    opacity: enabled ? 1.0 : 0.5
                    onClicked: root.submitReport()
                    background: Rectangle {
                        implicitHeight: Theme.scaled(60)
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

        // === SUBMITTING STATE ===
        ColumnLayout {
            visible: root.dialogState === "submitting"
            spacing: Theme.scaled(20)
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(40)

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: parent.visible
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: TranslationManager.translate("aiReport.submitting", "Submitting report...")
                font: Theme.bodyFont
                color: Theme.textColor
            }
        }

        // === SUCCESS STATE ===
        ColumnLayout {
            visible: root.dialogState === "success"
            spacing: 0
            Layout.fillWidth: true

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.topMargin: Theme.scaled(10)

                RowLayout {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scaled(12)

                    Rectangle {
                        width: Theme.scaled(32)
                        height: Theme.scaled(32)
                        radius: Theme.scaled(16)
                        color: Theme.primaryColor
                        Accessible.ignored: true

                        Text {
                            anchors.centerIn: parent
                            text: "\u2713"
                            font.pixelSize: Theme.scaled(20)
                            color: "white"
                            Accessible.ignored: true
                        }
                    }

                    Text {
                        text: TranslationManager.translate("aiReport.reportSubmitted", "Report Submitted")
                        font: Theme.titleFont
                        color: Theme.textColor
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            Text {
                text: TranslationManager.translate("aiReport.thankYou",
                    "Thank you! Your report will help us improve the AI advisor.")
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
            }

            Text {
                visible: root.issueUrl !== ""
                text: TranslationManager.translate("aiReport.viewOnGithub", "View issue on GitHub")
                font: Theme.bodyFont
                color: Theme.primaryColor
                Layout.leftMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(10)

                Accessible.role: Accessible.Link
                Accessible.name: TranslationManager.translate("aiReport.viewOnGithub", "View issue on GitHub")
                Accessible.focusable: true
                Accessible.onPressAction: Qt.openUrlExternally(root.issueUrl)

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Qt.openUrlExternally(root.issueUrl)
                }
            }

            // Button
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.margins: Theme.scaled(20)

                AccessibleButton {
                    anchors.right: parent.right
                    width: Theme.scaled(120)
                    height: parent.height
                    text: TranslationManager.translate("aiReport.ok", "OK")
                    accessibleName: TranslationManager.translate("aiReport.closeDialog", "Close dialog")
                    onClicked: root.close()
                    background: Rectangle {
                        implicitHeight: Theme.scaled(60)
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

        // === ERROR STATE ===
        ColumnLayout {
            visible: root.dialogState === "error"
            spacing: 0
            Layout.fillWidth: true

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.topMargin: Theme.scaled(10)

                RowLayout {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scaled(12)

                    Rectangle {
                        width: Theme.scaled(32)
                        height: Theme.scaled(32)
                        radius: Theme.scaled(16)
                        color: Theme.errorColor
                        Accessible.ignored: true

                        Text {
                            anchors.centerIn: parent
                            text: "X"
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                            color: "white"
                            Accessible.ignored: true
                        }
                    }

                    Text {
                        text: TranslationManager.translate("aiReport.submissionFailed", "Submission Failed")
                        font: Theme.titleFont
                        color: Theme.textColor
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            Text {
                text: TranslationManager.translate("aiReport.failedToSubmit",
                    "Failed to submit report:\n%1").arg(root.errorMessage)
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
            }

            // Buttons
            Grid {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                columns: 2
                spacing: Theme.scaled(10)

                property real buttonWidth: (width - spacing) / 2
                property real buttonHeight: Theme.scaled(50)

                AccessibleButton {
                    width: parent.buttonWidth
                    height: parent.buttonHeight
                    text: TranslationManager.translate("aiReport.dismiss", "Dismiss")
                    accessibleName: TranslationManager.translate("aiReport.dismissAccessible", "Dismiss report")
                    onClicked: root.close()
                    background: Rectangle {
                        implicitHeight: Theme.scaled(60)
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
                    width: parent.buttonWidth
                    height: parent.buttonHeight
                    enabled: !CrashReporter.submitting
                    text: TranslationManager.translate("aiReport.retry", "Retry")
                    accessibleName: TranslationManager.translate("aiReport.retryAccessible", "Retry submitting report")
                    onClicked: root.submitReport()
                    background: Rectangle {
                        implicitHeight: Theme.scaled(60)
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
}
