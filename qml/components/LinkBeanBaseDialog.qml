import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Lightweight "Link to Bean Base" for a SINGLE shot: search the canonical
// database, pick the matching coffee, and hand the entry back to the caller —
// which writes only that shot's bean-base snapshot. Unlike ChangeBeansDialog
// this creates no inventory bag and never touches the active bag; it is the
// metadata-enrichment path for a (historical) shot whose bean simply isn't
// linked to Bean Base yet. The heavyweight Change Beans dialog remains the
// path for actually creating or re-pointing a bag.
Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(520), parent ? parent.width * 0.95 : Theme.scaled(520))
    modal: true
    closePolicy: Dialog.CloseOnEscape
    padding: 0

    // Emitted with the picked canonical entry (Bean Base blob-shaped keys).
    signal entryPicked(var entry)

    // Seed query (roaster + coffee) prefilled into the search line on open.
    property string seedQuery: ""

    function openWith(query) {
        root.seedQuery = query
        root.open()
    }

    onOpened: {
        // Prefill the search line with the shot's coffee and run it, so the
        // matching results are one tap away — the user shouldn't have to type
        // a name the shot already knows.
        linkBar.prefillAndSearch(root.seedQuery)
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    contentItem: KeyboardAwareContainer {
        id: keyboardContainer
        inOverlay: true
        implicitWidth: root.width
        implicitHeight: mainColumn.implicitHeight
        textFields: [linkBar.textField]

        ColumnLayout {
            id: mainColumn
            width: keyboardContainer.width
            spacing: Theme.scaled(14)

            Accessible.role: Accessible.Dialog
            Accessible.name: trLinkTitle.text

            // Header: title + Cancel
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(16)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(12)
                spacing: Theme.scaled(8)

                Tr {
                    id: trLinkTitle
                    Layout.fillWidth: true
                    key: "beanbase.link.title"
                    fallback: "Link to Bean Base"
                    font: Theme.titleFont
                    color: Theme.textColor
                    Accessible.ignored: true  // announced via the dialog name
                }

                HideKeyboardButton {
                    Layout.alignment: Qt.AlignVCenter
                }

                AccessibleButton {
                    Layout.alignment: Qt.AlignVCenter
                    height: Theme.scaled(38)
                    leftPadding: Theme.scaled(14)
                    rightPadding: Theme.scaled(14)
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: root.close()
                }
            }

            // Subtitle: make the "no bag created" contract explicit so this
            // never surprises the way the full Change Beans flow did.
            Tr {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                key: "beanbase.link.subtitle"
                fallback: "Attach the coffee's Bean Base record to this shot. Only this shot is updated — no bag is created."
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
            }

            BeanBaseSearchBar {
                id: linkBar
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                onEntrySelected: function(entry) {
                    root.entryPicked(entry)
                    root.close()
                }
            }
        }
    }
}
