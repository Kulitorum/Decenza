import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Decenza

// The AI knowledge-base popup for a profile: sparkle-headed dialog rendering
// the profile's KB entry (dial-in expectations, flavor notes, cross-profile
// grind guidance). ONE implementation shared by the profile selector, the
// shot detail / post-shot review pages, and the recipe wizard's profile
// tiles — call openFor(title), or set profileTitle/content and open().
Dialog {
    id: knowledgeDialog
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(500), parent.width - Theme.scaled(40))
    height: Math.min(knowledgeContent.implicitHeight + Theme.scaled(120), parent.height - Theme.scaled(80))
    padding: 0
    modal: true

    property string profileTitle: ""
    property string content: ""

    function openFor(title) {
        profileTitle = title
        content = ProfileManager.profileKnowledgeContent(title)
        open()
    }

    // Format raw KB markdown into HTML for display:
    // - strips internal metadata lines (Also matches, AnalysisFlags)
    // - bolds field labels ("Category:", "How it works:", etc.)
    // - italicizes DO NOT lines
    function formatContent(raw) {
        var lines = raw.split('\n')
        var parts = []
        for (var i = 0; i < lines.length; i++) {
            var line = lines[i]
            if (!line.trim()) continue
            if (line.startsWith('Also matches:') || line.startsWith('AnalysisFlags:')) continue

            var colonIdx = line.indexOf(': ')
            if (colonIdx > 0 && colonIdx <= 35 && !line.startsWith('DO NOT') && !line.startsWith('-')) {
                var label = line.substring(0, colonIdx).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
                var value = line.substring(colonIdx + 2).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
                parts.push('<b>' + label + ':</b> ' + value)
            } else if (line.startsWith('DO NOT')) {
                parts.push('<i>' + line.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;') + '</i>')
            } else {
                parts.push(line.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'))
            }
        }
        return parts.join('<br>')
    }

    header: Item {
        implicitHeight: Theme.scaled(50)

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.scaled(20)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.scaled(8)

            Image {
                source: "qrc:/icons/sparkle.svg"
                sourceSize.width: Theme.scaled(18)
                sourceSize.height: Theme.scaled(18)
                anchors.verticalCenter: parent.verticalCenter

                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: Theme.primaryColor
                }
            }

            Text {
                text: knowledgeDialog.profileTitle
                font: Theme.titleFont
                color: Theme.textColor
                anchors.verticalCenter: parent.verticalCenter
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

    contentItem: Flickable {
        clip: true
        contentHeight: knowledgeContent.implicitHeight + Theme.scaled(30)
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        Text {
            id: knowledgeContent
            width: parent.width - Theme.scaled(40)
            x: Theme.scaled(20)
            y: Theme.scaled(15)
            text: knowledgeDialog.formatContent(knowledgeDialog.content)
            textFormat: Text.RichText
            color: Theme.textColor
            font: Theme.bodyFont
            wrapMode: Text.WordWrap
            lineHeight: 1.5

            Accessible.role: Accessible.StaticText
            Accessible.name: TranslationManager.translate("profileselector.accessible.knowledgeContent", "Profile knowledge base")
            Accessible.description: Theme.stripMarkdown(knowledgeDialog.content)
            Accessible.focusable: true
            activeFocusOnTab: true
        }
    }

    footer: Item {
        implicitHeight: Theme.scaled(55)

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.borderColor
        }

        AccessibleButton {
            anchors.centerIn: parent
            width: Theme.scaled(100)
            text: TranslationManager.translate("common.button.ok", "OK")
            accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
            onClicked: knowledgeDialog.close()
        }
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
    }
}
