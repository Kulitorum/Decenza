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

    property string crashLog: ""
    property string debugLogTail: ""

    signal dismissed()
    signal reported()

    // State management
    property string dialogState: "prompt"  // prompt, submitting, success, error
    property string issueUrl: ""
    property string errorMessage: ""

    Connections {
        target: CrashReporter
        function onSubmitted(url) {
            root.issueUrl = url
            root.dialogState = "success"
        }
        function onFailed(error) {
            root.errorMessage = error
            root.dialogState = "error"
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
                        color: Theme.errorColor

                        Text {
                            anchors.centerIn: parent
                            text: "!"
                            font.pixelSize: Theme.scaled(20)
                            font.bold: true
                            color: "white"
                        }
                    }

                    Text {
                        text: TranslationManager.translate("crashReport.appCrashed", "App Crashed")
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
                text: TranslationManager.translate("crashReport.message", "The app crashed during the last session.\nWould you like to send a crash report to help us fix this issue?")
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
            }

            // Crash log preview (collapsible)
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.preferredHeight: detailsExpanded ? Theme.scaled(150) : Theme.scaled(36)
                color: Theme.backgroundColor
                radius: Theme.scaled(4)
                clip: true

                property bool detailsExpanded: false

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
                            text: TranslationManager.translate("crashReport.crashDetails", "Crash Details")
                            font: Theme.labelFont
                            color: Theme.textSecondaryColor
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: parent.parent.parent.detailsExpanded ? TranslationManager.translate("crashReport.hide", "Hide") : TranslationManager.translate("crashReport.show", "Show")
                            font: Theme.labelFont
                            color: Theme.primaryColor

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: parent.parent.parent.parent.detailsExpanded = !parent.parent.parent.parent.detailsExpanded
                            }
                        }
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: parent.parent.detailsExpanded
                        clip: true

                        TextArea {
                            readOnly: true
                            text: crashLog
                            font.family: "monospace"
                            font.pixelSize: Theme.scaled(10)
                            color: Theme.textColor
                            wrapMode: TextArea.Wrap
                            background: null
                        }
                    }
                }
            }

            // User notes input
            Text {
                text: TranslationManager.translate("crashReport.whatWereYouDoing", "What were you doing? (optional)")
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
                    placeholderText: TranslationManager.translate("crashReport.placeholder", "e.g., 'Steaming milk after a shot'")
                    placeholderTextColor: Qt.rgba(Theme.textSecondaryColor.r, Theme.textSecondaryColor.g, Theme.textSecondaryColor.b, 0.5)
                    wrapMode: TextArea.Wrap
                    background: null
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("crashReport.userNotes", "What were you doing?")
                    Accessible.value: text
                }
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
                    text: TranslationManager.translate("crashReport.dismiss", "Dismiss")
                    accessibleName: TranslationManager.translate("crashReport.dismissAccessible", "Dismiss crash report")
                    onClicked: {
                        root.close()
                        root.dismissed()
                    }
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
                    text: TranslationManager.translate("crashReport.sendReport", "Send Report")
                    accessibleName: TranslationManager.translate("crashReport.sendReportAccessible", "Send crash report")
                    onClicked: {
                        root.dialogState = "submitting"
                        CrashReporter.submitReport(crashLog, userNotesInput.text, debugLogTail)
                    }
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
                text: TranslationManager.translate("crashReport.submitting", "Submitting crash report...")
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

                        Text {
                            anchors.centerIn: parent
                            text: "\u2713"
                            font.pixelSize: Theme.scaled(20)
                            color: "white"
                        }
                    }

                    Text {
                        text: TranslationManager.translate("crashReport.reportSubmitted", "Report Submitted")
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
                text: TranslationManager.translate("crashReport.thankYou", "Thank you! Your crash report has been submitted and will help us improve the app.")
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
            }

            Text {
                visible: issueUrl !== ""
                text: TranslationManager.translate("crashReport.viewOnGithub", "View issue on GitHub")
                font: Theme.bodyFont
                color: Theme.primaryColor
                Layout.leftMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(10)

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Qt.openUrlExternally(issueUrl)
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
                    text: TranslationManager.translate("crashReport.ok", "OK")
                    accessibleName: TranslationManager.translate("crashReport.closeDialog", "Close dialog")
                    onClicked: {
                        root.close()
                        root.reported()
                    }
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

                        Text {
                            anchors.centerIn: parent
                            text: "X"
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                            color: "white"
                        }
                    }

                    Text {
                        text: TranslationManager.translate("crashReport.submissionFailed", "Submission Failed")
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
                text: TranslationManager.translate("crashReport.failedToSubmit", "Failed to submit crash report:\n%1").arg(errorMessage)
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
                    text: TranslationManager.translate("crashReport.dismiss", "Dismiss")
                    accessibleName: TranslationManager.translate("crashReport.dismissAccessible", "Dismiss crash report")
                    onClicked: {
                        root.close()
                        root.dismissed()
                    }
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
                    text: TranslationManager.translate("crashReport.retry", "Retry")
                    accessibleName: TranslationManager.translate("crashReport.retryAccessible", "Retry sending crash report")
                    onClicked: {
                        root.dialogState = "submitting"
                        CrashReporter.submitReport(crashLog, userNotesInput.text, debugLogTail)
                    }
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
