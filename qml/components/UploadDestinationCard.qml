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
    implicitHeight: column.implicitHeight + Theme.scaled(30)

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.scaled(15)
        spacing: Theme.scaled(12)

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(15)

            Text {
                Layout.fillWidth: true
                text: root.title
                color: Theme.textColor
                font.pixelSize: Theme.scaled(16)
                font.bold: true
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
            font.pixelSize: Theme.scaled(12)
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: root.statusText.length > 0
            text: root.statusText
            color: root.statusColor
            font.pixelSize: Theme.scaled(12)
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        ColumnLayout {
            id: contentColumn
            Layout.fillWidth: true
            spacing: Theme.scaled(12)
        }
    }
}
