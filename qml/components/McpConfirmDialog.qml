import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

Dialog {
    id: root
    anchors.centerIn: parent
    width: Theme.dialogWidth + 2 * padding
    modal: true
    dim: true
    padding: Theme.dialogPadding
    closePolicy: Dialog.NoAutoClose

    property string toolDescription: ""
    property string sessionId: ""
    property bool userResponded: false
    property int countdown: 15

    signal confirmed(string sessionId)
    signal denied(string sessionId)

    // 15-second auto-dismiss timer (legitimate UI auto-dismiss per CLAUDE.md)
    Timer {
        id: autoDismissTimer
        interval: 15000
        running: root.visible
        onTriggered: root.close()
    }

    // Countdown for display
    Timer {
        interval: 1000
        running: root.visible
        repeat: true
        onTriggered: if (root.countdown > 0) root.countdown--
    }

    // Dialog closure drives the callback — not the raw timer
    onClosed: {
        if (!root.userResponded)
            root.denied(root.sessionId)
        root.userResponded = false
        root.countdown = 15
    }

    onOpened: {
        root.userResponded = false
        root.countdown = 15
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(
                TranslationManager.translate("mcp.confirm.announce",
                    "AI assistant wants to %1. Allow or Deny. Auto-denies in 15 seconds.")
                    .arg(root.toolDescription))
        }
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 2
        border.color: Theme.warningButtonColor
    }

    contentItem: ColumnLayout {
        spacing: 0

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
                    color: Theme.warningButtonColor

                    Text {
                        anchors.centerIn: parent
                        text: "!"
                        font.pixelSize: Theme.scaled(18)
                        font.bold: true
                        color: "white"
                        Accessible.ignored: true
                    }
                }

                Text {
                    text: TranslationManager.translate("mcp.confirm.title", "AI Confirmation")
                    font: Theme.titleFont
                    color: Theme.textColor
                    Accessible.ignored: true
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
            text: TranslationManager.translate("mcp.confirm.body",
                "An AI assistant wants to:\n\n%1\n\nAllow this action?").arg(root.toolDescription)
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(20)
            Accessible.ignored: true
        }

        // Countdown
        Text {
            text: TranslationManager.translate("mcp.confirm.countdown",
                "Auto-denying in %1 seconds").arg(root.countdown)
            font.family: Theme.labelFont.family
            font.pixelSize: Theme.labelFont.pixelSize
            color: Theme.textSecondaryColor
            Layout.leftMargin: Theme.scaled(20)
            Layout.bottomMargin: Theme.scaled(10)
            Accessible.ignored: true
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
                text: TranslationManager.translate("mcp.confirm.deny", "Deny")
                accessibleName: TranslationManager.translate("mcp.confirm.denyAccessible", "Deny AI action")
                onClicked: {
                    root.userResponded = true
                    root.denied(root.sessionId)
                    root.close()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(60)
                    radius: Theme.buttonRadius
                    color: parent.down ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.bodyFont
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Accessible.ignored: true
                }
            }

            AccessibleButton {
                width: parent.buttonWidth
                height: parent.buttonHeight
                text: TranslationManager.translate("mcp.confirm.allow", "Allow")
                accessibleName: TranslationManager.translate("mcp.confirm.allowAccessible",
                    "Allow AI action: %1").arg(root.toolDescription)
                onClicked: {
                    root.userResponded = true
                    root.confirmed(root.sessionId)
                    root.close()
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
                    Accessible.ignored: true
                }
            }
        }
    }
}
