import QtQuick
import QtQuick.Layouts
import Decenza

// One upload destination on the Shot Upload tab: name and on/off switch, a
// one-line description, the account status, then the destination's own content.
// A SettingsCard, so each instance on the tab is a settings-search result.
SettingsCard {
    id: root

    property bool switchedOn
    property string statusText
    property color statusColor: Theme.textSecondaryColor
    // An instance's children go below the status. `content` is assigned explicitly below so
    // this card's own rows do not land here too.
    default property alias destinationContent: contentColumn.data

    signal switchToggled(bool on)

    showHeader: false
    contentMargins: Theme.spacingMedium
    spacing: Theme.spacingMedium

    content: [
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
        },

        Text {
            Layout.fillWidth: true
            text: root.description
            color: Theme.textSecondaryColor
            font: Theme.captionFont
            wrapMode: Text.WordWrap
        },

        Text {
            Layout.fillWidth: true
            visible: root.statusText.length > 0
            text: root.statusText
            color: root.statusColor
            font: Theme.captionFont
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        },

        ColumnLayout {
            id: contentColumn
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
        }
    ]
}
