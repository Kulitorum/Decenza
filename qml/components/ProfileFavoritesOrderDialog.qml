// The FavoritesListView delegate below reads this file's `root`; Bound makes
// it statically resolvable. No other delegate/injected model role here.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// profile-favorites-order: the Custom… reorder dialog, replacing the old
// right-hand favorites panel. Drag handles + remove come from the shared
// FavoritesListView (already used by bean presets) — this file only wires it
// to Settings.app's favorites store and sets the mode to `custom` on confirm.
DecenzaDialog {
    id: root
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width * 0.9 : Theme.scaled(400), Theme.scaled(420))
    height: Math.min(parent ? parent.height * 0.8 : Theme.scaled(500), Theme.scaled(560))
    padding: 0
    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

    // Staged working copy so drag reorders / deletes are local until Confirm,
    // which commits them to Settings.app in one go alongside the mode switch.
    property var _order: []

    onAboutToShow: root._order = Settings.app.favoriteProfiles

    header: Item {
        implicitHeight: Theme.scaled(50)
        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.scaled(20)
            anchors.verticalCenter: parent.verticalCenter
            text: TranslationManager.translate("profilepicker.reorder.title", "Reorder Favorites")
            font: Theme.titleFont
            color: Theme.textColor
        }
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.borderColor
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.scaled(10)

        // Accessible.* must attach to an Item-derived object; Dialog (a Popup)
        // is not one, so the dialog semantics live on its contentItem instead.
        Accessible.role: Accessible.Dialog
        Accessible.name: TranslationManager.translate("profilepicker.reorder.title", "Reorder Favorites")

        Text {
            visible: root._order.length === 0
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(20)
            text: TranslationManager.translate(
                "profileselector.favorites.empty",
                "No favorites yet.\nTap the star icon on any profile\nto add it to favorites.")
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        ScrollView {
            visible: root._order.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.scaled(16)
            Layout.rightMargin: Theme.scaled(16)
            clip: true
            contentWidth: availableWidth

            FavoritesListView {
                width: parent ? parent.width : 0
                model: root._order
                selectedIndex: -1
                showDeleteButton: true
                displayTextFn: function(row, index) { return row ? row.name : "" }
                accessibleNameFn: function(row, index) { return row ? AccessibilityManager.cleanForSpeech(row.name) : "" }
                deleteAccessibleNameFn: function(row, index) {
                    return TranslationManager.translate("profileselector.accessible.remove", "Remove") + " " +
                           (row ? AccessibilityManager.cleanForSpeech(row.name) : "") + " " +
                           TranslationManager.translate("profileselector.accessible.from_favorites", "from favorites")
                }
                rowAccessibleDescription: TranslationManager.translate(
                    "profilepicker.reorder.row_hint", "Drag the handle to reorder.")

                onRowMoved: function(from, to) {
                    var arr = root._order.slice()
                    var item = arr[from]
                    arr.splice(from, 1)
                    arr.splice(to, 0, item)
                    root._order = arr
                }
                onRowDeleted: function(index) {
                    var arr = root._order.slice()
                    arr.splice(index, 1)
                    root._order = arr
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(16)
            spacing: Theme.scaled(12)

            Item { Layout.fillWidth: true }

            AccessibleButton {
                text: TranslationManager.translate("common.button.cancel", "Cancel")
                accessibleName: TranslationManager.translate("common.accessibility.cancel", "Cancel")
                onClicked: root.close()
            }

            AccessibleButton {
                text: TranslationManager.translate("common.button.done", "Done")
                accessibleName: TranslationManager.translate("profilepicker.reorder.accessible.confirm", "Confirm favorites order")
                primary: true
                onClicked: {
                    // Reconcile removals against the LIVE store first —
                    // setFavoritesOrder() is a pure reorder and cannot itself
                    // drop an entry the dialog's X button removed.
                    var liveNames = Settings.app.favoriteProfiles.map(function(f) { return f.filename })
                    var keep = {}
                    for (var i = 0; i < root._order.length; ++i) keep[root._order[i].filename] = true
                    for (i = liveNames.length - 1; i >= 0; --i) {
                        if (!keep[liveNames[i]])
                            Settings.app.removeFavoriteProfile(i)
                    }
                    var filenames = []
                    for (i = 0; i < root._order.length; ++i) filenames.push(root._order[i].filename)
                    Settings.app.setFavoritesOrder(filenames)
                    Settings.app.setFavoriteProfileOrder("custom")
                    root.close()
                }
            }
        }
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }
}
