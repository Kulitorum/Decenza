import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Shown when ProfileManager refused to activate a profile because this build
// cannot promise it brews what the file describes — a step setting or a value
// the parser does not understand.
//
// The refusal is deliberate, not a failure to try: ignoring an unknown frame
// setting would pour a different shot with nothing to show for it. So the dialog
// has one job beyond saying no — hand the user the exact key, in text worth
// pasting into a bug report, because the key is the whole diagnosis and a user
// who only sees "invalid profile" has nothing to report and no reason to trust
// the refusal.
//
// Driven by ProfileManager.profileRefusedUnreadable. Either key list may be
// empty (a profile can also be refused for having no steps, or too many), so the
// detail block hides rather than showing an empty parenthesis.
Dialog {
    id: root

    property string profileTitle: ""
    property string profileFilename: ""
    property var unsupportedKeys: []
    property var malformedValues: []

    readonly property bool hasKeyDetail: unsupportedKeys.length > 0 || malformedValues.length > 0

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

    // Screen readers get the whole message in one announcement: the name, why it
    // was refused, and the keys. toAccessibleText strips markup so the reader
    // speaks words rather than glyph names.
    onOpened: {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled) {
            AccessibilityManager.announce(Theme.toAccessibleText(
                titleText.text + ". " + bodyText.text
                + (root.hasKeyDetail ? " " + detailText.text : "")))
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
                anchors.right: parent.right
                anchors.rightMargin: Theme.scaled(20)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.scaled(12)

                // implicitWidth/Height, not width/height: this Rectangle is a
                // RowLayout child, and setting the geometry a layout owns is
                // undefined behaviour (Quick.layout-positioning).
                Rectangle {
                    implicitWidth: Theme.scaled(32)
                    implicitHeight: Theme.scaled(32)
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
                    id: titleText
                    text: TranslationManager.translate("profileRefused.title",
                        "Profile Not Loaded")
                    font: Theme.titleFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                    Layout.fillWidth: true
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

        // Why it was refused. Names the profile, and says plainly that the
        // previous profile is still active — otherwise the user is left guessing
        // what the machine is about to brew.
        Text {
            id: bodyText
            text: TranslationManager.translate("profileRefused.body",
                      "“%1” uses settings this version of Decenza does not understand, so it was not loaded.")
                      .arg(root.profileTitle.length > 0 ? root.profileTitle : root.profileFilename)
                  + "\n\n"
                  + TranslationManager.translate("profileRefused.whyRefused",
                      "Loading it anyway would brew a different shot than the profile describes, without saying so. Your previously selected profile is still active.")
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(20)
            Accessible.ignored: true
        }

        // The detail worth pasting into a bug report. Selectable on purpose —
        // asking someone to retype a key list from a screenshot is how bug
        // reports arrive without the one fact that would resolve them.
        TextEdit {
            id: detailText
            visible: root.hasKeyDetail
            text: {
                var parts = []
                if (root.unsupportedKeys.length > 0)
                    parts.push(TranslationManager.translate("profileRefused.unsupportedKeys",
                        "Unrecognised settings: %1").arg(root.unsupportedKeys.join(", ")))
                if (root.malformedValues.length > 0)
                    parts.push(TranslationManager.translate("profileRefused.malformedValues",
                        "Values that could not be read: %1").arg(root.malformedValues.join(", ")))
                return parts.join("\n")
            }
            readOnly: true
            selectByMouse: true
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: TextEdit.Wrap
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(14)
            Accessible.role: Accessible.StaticText
            Accessible.name: Theme.toAccessibleText(text)
        }

        // The ask. A refused profile is a gap in Decenza, not user error, and the
        // key above is only useful to us if it actually reaches us.
        Text {
            text: TranslationManager.translate("profileRefused.pleaseReport",
                "Please report this so support for it can be added — include the details above.")
            font: Theme.subtitleFont
            color: Theme.textSecondaryColor
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(14)
            Accessible.ignored: true
        }

        // Buttons: report (opens the issue tracker) + dismiss. Styling comes from
        // AccessibleButton's own variants — overriding background/contentItem here
        // would also lose its press handling, since Button.down stays false by
        // design (its internal touchArea accepts the press).
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(20)
            Layout.bottomMargin: Theme.scaled(20)
            spacing: Theme.scaled(12)

            Item { Layout.fillWidth: true }

            AccessibleButton {
                primary: true
                text: TranslationManager.translate("profileRefused.reportButton", "Report Issue")
                accessibleName: TranslationManager.translate("profileRefused.reportAccessible",
                    "Report this profile problem on GitHub")
                onClicked: Qt.openUrlExternally("https://github.com/Kulitorum/Decenza/issues/new")
            }

            AccessibleButton {
                text: TranslationManager.translate("common.button.close", "Close")
                accessibleName: TranslationManager.translate("common.accessibility.dismissDialog",
                    "Dismiss dialog")
                onClicked: root.close()
            }
        }
    }
}
