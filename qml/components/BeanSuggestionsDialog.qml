import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Decenza

// Bean Search for one bag: your history and the community's, opened from a bag
// card's Suggestions button.
DecenzaDialog {
    id: dialog
    logName: "beanSuggestions"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - Theme.scaled(24), Theme.scaled(560))
    padding: Theme.scaled(16)
    header: Item {}
    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    // A bag map (roasterName, coffeeName, roastLevel, kind, beanBaseId, id).
    property var bag: ({})

    function openFor(b) {
        bag = b || ({})
        open()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingSmall

        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(suggestionsCard.implicitHeight,
                                             dialog.parent.height * 0.8 - Theme.scaled(80))
            contentHeight: suggestionsCard.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            CommunitySuggestionsCard {
                id: suggestionsCard
                width: parent.width
                beanBrand: String(dialog.bag.roasterName || "")
                beanType: String(dialog.bag.coffeeName || "")
                roastLevel: String(dialog.bag.roastLevel || "")
                isCoffee: String(dialog.bag.kind || "") !== "tea"
                beanBaseId: String(dialog.bag.beanBaseId || "")
                bagId: Number(dialog.bag.id || 0)
                productUrl: {
                    try {
                        return String(JSON.parse(dialog.bag.beanBaseData || "{}").link || "")
                    } catch (e) {
                        return ""
                    }
                }
            }
        }

        AccessibleButton {
            Layout.alignment: Qt.AlignRight
            text: TranslationManager.translate("common.button.close", "Close")
            accessibleName: text
            onClicked: dialog.close()
        }
    }
}
