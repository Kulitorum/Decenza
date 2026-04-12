import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    visible: Settings.discussShotApp !== Settings.discussAppNone

    // Claude Desktop mode needs a session URL pasted from `claude remote-control`.
    // Keep the button visible but disabled until the URL is set, so the user sees
    // where to tap after completing setup.
    readonly property bool isClaudeDesktopReady:
        Settings.discussShotApp !== Settings.discussAppClaudeDesktop
        || Settings.claudeRcSessionUrl.length > 0

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    function openDiscuss() {
        if (!root.isClaudeDesktopReady) return
        var url = Settings.discussShotUrl()
        if (url.length > 0) Settings.openDiscussUrl(url)
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        opacity: root.isClaudeDesktopReady ? 1.0 : 0.5
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        RowLayout {
            id: compactRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Image {
                source: "qrc:/icons/sparkle.svg"
                sourceSize.height: Theme.scaled(20)
                fillMode: Image.PreserveAspectFit
                Accessible.ignored: true

                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: Theme.textColor
                }
            }
            Tr {
                key: "idle.button.discuss"
                fallback: "Discuss"
                font: Theme.bodyFont
                color: Theme.textColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            accessibleName: root.isClaudeDesktopReady
                ? TranslationManager.translate("idle.accessible.discuss.description", "Open AI app to discuss your last shot")
                : TranslationManager.translate("idle.accessible.discuss.disabled", "Discuss — requires session URL. Open AI Settings to configure.")
            onAccessibleClicked: root.openDiscuss()
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: Theme.scaled(150)
        implicitHeight: Theme.scaled(120)

        ActionButton {
            anchors.fill: parent
            translationKey: "idle.button.discuss"
            translationFallback: "Discuss"
            iconSource: "qrc:/icons/discuss.svg"
            iconSize: Theme.scaled(43)
            backgroundColor: Theme.primaryColor
            enabled: root.isClaudeDesktopReady
            onClicked: root.openDiscuss()
        }
    }
}
