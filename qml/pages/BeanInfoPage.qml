// The bag-card Repeater delegate below reads this file's ids (`flickable`,
// `changeBeansDialog`); Bound makes them statically resolvable. The delegate declares
// its one injected model role required in the same edit -- without that, Bound stops
// role injection and `modelData` goes undefined at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

// Bean bag inventory (bean-bag-inventory change): replaces the old editable
// DYE-fields + presets page. Shows all bags with inInventory = true as cards;
// tapping a card selects it (sets activeBagId), and the Change Beans dialog
// handles search/creation. There are no editable bean text fields here —
// bag edits go through the dialog's Edit Bag form.
T.Page {
    id: bagInventoryPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("beaninfo.title", "Beans")

    objectName: "bagInventoryPage"
    background: ThemedPageBackground {}


    property var inventoryBags: []
    // "No bags yet" is a fact about the DATABASE, and until the async read
    // answers we do not have it. Rendering the empty state from the initial []
    // told every user with bags that they had none, for as long as the read
    // took — the page opened on its own failure message and then corrected
    // itself.
    property bool inventoryLoaded: false
    // A failed read is not an empty one. Without this the page renders nothing
    // at all when the database will not open — worse than the wrong "No bags
    // yet" it replaced.
    property bool inventoryFailed: false

    Component.onCompleted: {
        MainController.bagStorage.requestInventory()
        addBagButton.forceActiveFocus()
    }

    Connections {
        target: MainController.bagStorage
        function onInventoryReady(bags) {
            bagInventoryPage.inventoryBags = bags
            bagInventoryPage.inventoryLoaded = true
            bagInventoryPage.inventoryFailed = false
        }
        function onInventoryFailed() {
            bagInventoryPage.inventoryLoaded = true
            bagInventoryPage.inventoryFailed = true
        }
        function onBagsChanged() {
            MainController.bagStorage.requestInventory()
        }
    }

    ChangeBeansDialog {
        id: changeBeansDialog
        context: "inventory"
    }

    Flickable {
        id: flickable
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        contentHeight: contentColumn.implicitHeight + Theme.scaled(20)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: contentColumn
            width: flickable.width
            spacing: Theme.spacingMedium

            // Header row: title + Add New Bag
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Tr {
                    key: "beaninfo.inventory.title"
                    fallback: "Bag Inventory"
                    font: Theme.titleFont
                    color: Theme.textColor
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                Item { Layout.fillWidth: true }

                // Two creation entry points (add-recipe-wizard-tea): the kind
                // is stamped at creation, so coffee and tea get their own
                // buttons and flows — coffee keeps the Bean Base search-first
                // dialog; tea searches past tea bags only (straight to the
                // form when there are none).
                AccessibleButton {
                    id: addTeaButton
                    Layout.preferredHeight: Theme.scaled(44)
                    icon.source: "qrc:/icons/tea.svg"
                    text: TranslationManager.translate("beaninfo.inventory.addTea", "Add Tea")
                    accessibleName: TranslationManager.translate("beaninfo.inventory.accessible.addTea", "Add a new bag of tea")
                    onClicked: {
                        var hasTea = false
                        for (var i = 0; i < bagInventoryPage.inventoryBags.length; ++i) {
                            if (String(bagInventoryPage.inventoryBags[i].kind || "") === "tea") {
                                hasTea = true
                                break
                            }
                        }
                        changeBeansDialog.openTeaEntry(hasTea)
                    }
                }

                AccessibleButton {
                    id: addBagButton
                    primary: true
                    Layout.preferredHeight: Theme.scaled(44)
                    icon.source: "qrc:/icons/plus.svg"
                    text: TranslationManager.translate("beaninfo.inventory.addCoffee", "Add Coffee")
                    accessibleName: TranslationManager.translate("beaninfo.inventory.accessible.addBag", "Add a new bag of beans")
                    onClicked: {
                        changeBeansDialog.bagKind = "coffee"
                        changeBeansDialog.open()
                    }
                }
            }

            // The read failed — say so rather than claiming there are no bags.
            Tr {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.scaled(40)
                visible: bagInventoryPage.inventoryFailed
                         && bagInventoryPage.inventoryBags.length === 0
                key: "beaninfo.inventory.unavailable"
                fallback: "Couldn't read your bags — the bean database didn't open."
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            // Empty state
            ColumnLayout {
                visible: bagInventoryPage.inventoryLoaded
                         && !bagInventoryPage.inventoryFailed
                         && bagInventoryPage.inventoryBags.length === 0
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(40)
                spacing: Theme.spacingSmall

                Tr {
                    Layout.alignment: Qt.AlignHCenter
                    key: "beaninfo.inventory.empty.title"
                    fallback: "No bags yet"
                    font: Theme.subtitleFont
                    color: Theme.textColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Tr {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: flickable.width * 0.8
                    key: "beaninfo.inventory.empty.hint"
                    fallback: "Add your first bag to track beans, freshness and grinder settings per bag."
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }

            // Bag cards
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Repeater {
                    model: bagInventoryPage.inventoryBags

                    BagCard {
                        required property var modelData

                        bag: modelData
                        width: {
                            var avail = flickable.width
                            var cardW = Theme.scaled(380)
                            var columns = Math.max(1, Math.floor(avail / cardW))
                            return (avail - (columns - 1) * Theme.spacingMedium) / columns
                        }
                        onEditRequested: function(b) { changeBeansDialog.openForEdit(b) }
                        onLinkRequested: function(b) { changeBeansDialog.openForEditAndLink(b) }
                    }
                }
            }
        }
    }

    BottomBar {
        barColor: "transparent"
        onBackClicked: AppShell.backRequested()
    }
}
