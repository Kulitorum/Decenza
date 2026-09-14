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
    // Sized to the WINDOW, not the picker it is declared in: a drag list needs
    // rows on screen, and the picker's card area left it one and a half.
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width * 0.9 : Theme.scaled(560), Theme.scaled(640))
    height: parent ? parent.height * 0.9 : Theme.scaled(700)
    padding: 0
    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

    // Staged working copy so drag reorders / deletes are local until Confirm,
    // which commits them to Settings.app in one go alongside the mode switch.
    property var _order: []
    // custom | alpha | usage, committed on Done. Under alpha/usage the list is
    // a read-only preview of the order the app will keep.
    property string _mode: "custom"

    onAboutToShow: {
        root._mode = Settings.app.favoriteProfileOrder
        root._order = Settings.app.favoriteProfiles
    }

    readonly property var _preview: {
        var list = root._order.slice()
        if (root._mode === "alpha") {
            list.sort(function(a, b) { return String(a.name).localeCompare(String(b.name)) })
        } else if (root._mode === "usage") {
            var usage = ProfileManager.profileUsage
            list.sort(function(a, b) {
                var ta = usage[a.name] ? usage[a.name].lastTimestamp : 0
                var tb = usage[b.name] ? usage[b.name].lastTimestamp : 0
                if (!!ta !== !!tb) return ta ? -1 : 1
                if (ta !== tb) return tb - ta
                return String(a.name).localeCompare(String(b.name))
            })
        }
        return list
    }

    component ModeButton: AccessibleButton {
        property string mode: ""
        primary: root._mode === mode
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.scaled(40)
        onClicked: root._mode = mode
    }

    header: Item {
        implicitHeight: Theme.scaled(50)
        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.scaled(20)
            anchors.verticalCenter: parent.verticalCenter
            text: TranslationManager.translate("profilepicker.reorder.title", "Favorites Order")
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
        Accessible.name: TranslationManager.translate("profilepicker.reorder.title", "Favorites Order")

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(16)
            Layout.rightMargin: Theme.scaled(16)
            text: TranslationManager.translate("profilepicker.reorder.hint", "Order of the profile pills on the idle screen.")
            font: Theme.captionFont
            color: Theme.textSecondaryColor
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(16)
            Layout.rightMargin: Theme.scaled(16)
            spacing: Theme.scaled(8)
            ModeButton {
                mode: "alpha"
                text: TranslationManager.translate("profilepicker.sort.alpha", "A–Z")
                accessibleName: TranslationManager.translate("profilepicker.reorder.accessible.alpha", "Order favorites A to Z")
            }
            ModeButton {
                mode: "usage"
                text: TranslationManager.translate("profilepicker.sort.recent", "Recently used")
                accessibleName: TranslationManager.translate("profilepicker.reorder.accessible.usage", "Order favorites by most recent shot")
            }
            ModeButton {
                mode: "custom"
                text: TranslationManager.translate("profilepicker.sort.custom", "Custom")
                accessibleName: TranslationManager.translate("profilepicker.reorder.accessible.custom", "Order favorites by hand")
            }
        }

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
                // Drag only means something under Custom; the other modes show
                // the order the app will keep.
                enabled: root._mode === "custom"
                model: root._preview
                selectedIndex: -1
                showDeleteButton: true
                displayTextFn: function(row, index) { return row ? row.name : "" }
                accessibleNameFn: function(row, index) { return row ? AccessibilityManager.cleanForSpeech(row.name) : "" }
                deleteAccessibleNameFn: function(row, index) {
                    return TranslationManager.translate("profileselector.accessible.remove", "Remove") + " " +
                           (row ? AccessibilityManager.cleanForSpeech(row.name) : "") + " " +
                           TranslationManager.translate("profileselector.accessible.from_favorites", "from favorites")
                }
                rowAccessibleDescription: root._mode === "custom"
                    ? TranslationManager.translate("profilepicker.reorder.row_hint", "Drag the handle to reorder.")
                    : ""

                // Row indices are positions in _preview, the list on screen;
                // resolve them to _order by filename so a preview sort can
                // never make a delete or a drag land on the wrong favorite.
                function orderIndexOf(previewIndex) {
                    var fn = root._preview[previewIndex] ? root._preview[previewIndex].filename : ""
                    for (var i = 0; i < root._order.length; ++i)
                        if (root._order[i].filename === fn) return i
                    return -1
                }
                onRowMoved: function(from, to) {
                    var arr = root._order.slice()
                    var src = orderIndexOf(from), dst = orderIndexOf(to)
                    if (src < 0 || dst < 0) return
                    var item = arr[src]
                    arr.splice(src, 1)
                    arr.splice(dst, 0, item)
                    root._order = arr
                }
                onRowDeleted: function(index) {
                    var arr = root._order.slice()
                    var src = orderIndexOf(index)
                    if (src < 0) return
                    arr.splice(src, 1)
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
                    // Mode first: switching to alpha/usage re-sorts through
                    // ProfileManager; custom keeps whatever is written next.
                    Settings.app.favoriteProfileOrder = root._mode
                    if (root._mode === "custom") {
                        var filenames = []
                        for (i = 0; i < root._order.length; ++i) filenames.push(root._order[i].filename)
                        Settings.app.setFavoritesOrder(filenames)
                    }
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
