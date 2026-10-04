import QtQuick
import QtQuick.Layouts
import Decenza

// One upload destination on the Shot Upload tab: name and on/off switch, a
// one-line description, the account status, then the destination's own content.
Rectangle {
    id: root

    property string title
    property string description
    property bool switchedOn
    property string statusText
    property color statusColor: Theme.textSecondaryColor
    default property alias content: contentColumn.data

    signal switchToggled(bool on)

    color: Theme.cardBackgroundColor
    radius: Theme.cardRadius
    implicitHeight: column.implicitHeight + 2 * Theme.spacingMedium

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingMedium

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Text {
                Layout.fillWidth: true
                text: root.title
                color: Theme.textColor
                font: Theme.subtitleFont
                elide: Text.ElideRight
                Accessible.ignored: true
            }

            StyledSwitch {
                checked: root.switchedOn
                accessibleName: TranslationManager.translate("settings.upload.switchAccessible", "Upload shots to %1")
                                    .arg(root.title)
                onToggled: root.switchToggled(checked)
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.description
            color: Theme.textSecondaryColor
            font: Theme.captionFont
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: root.statusText.length > 0
            text: root.statusText
            color: root.statusColor
            font: Theme.captionFont
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        ColumnLayout {
            id: contentColumn
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
        }
    }
}
