import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Shown when ProfileManager has exhausted its retry budget for BLE profile
// uploads. Means a transient BLE issue (frame-ACK mismatch, write-ACK
// timeout) didn't resolve on its own after 5 attempts spanning ~15 seconds,
// and the DE1 almost certainly needs to be power-cycled.
//
// Bound to ProfileManager.de1CommunicationFailure. The single OK button
// calls acknowledgeDe1CommunicationFailure() to clear the flag.
Dialog {
    id: root
    anchors.centerIn: parent
    width: Theme.dialogWidth + 2 * padding
    modal: true
    dim: true
    padding: Theme.dialogPadding
    closePolicy: Dialog.NoAutoClose

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 2
        border.color: Theme.errorColor
    }

    onOpened: {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(
                TranslationManager.translate("de1CommError.announce",
                    "DE1 communication issue. The current profile could not be loaded. Please power-cycle the DE1 and reconnect."))
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Header (icon + title)
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
                        font.pixelSize: Theme.scaled(18)
                        font.bold: true
                        color: Theme.primaryContrastColor
                        Accessible.ignored: true
                    }
                }

                Text {
                    text: TranslationManager.translate("de1CommError.title",
                        "DE1 Communication Issue")
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

        // Message body
        Text {
            text: TranslationManager.translate("de1CommError.body",
                "The app couldn't load the current profile onto the DE1 after several attempts. " +
                "This usually means the DE1 needs a quick power cycle.\n\n" +
                "1. Turn the DE1 off at its back switch.\n" +
                "2. Wait a few seconds.\n" +
                "3. Turn it back on — Decenza will reconnect automatically.")
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(20)
            Accessible.ignored: true
        }

        // Single OK button
        Item {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.bottomMargin: Theme.scaled(20)
            Layout.preferredHeight: Theme.scaled(50)

            AccessibleButton {
                anchors.right: parent.right
                width: Theme.scaled(140)
                height: Theme.scaled(50)
                text: TranslationManager.translate("common.button.ok", "OK")
                accessibleName: TranslationManager.translate("de1CommError.acknowledgeAccessible",
                    "Acknowledge DE1 communication issue")
                onClicked: {
                    ProfileManager.acknowledgeDe1CommunicationFailure()
                    root.close()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(50)
                    radius: Theme.buttonRadius
                    color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.bodyFont
                    color: Theme.primaryContrastColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Accessible.ignored: true
                }
            }
        }
    }
}
