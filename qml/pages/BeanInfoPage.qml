import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"

// Bean bag inventory (bean-bag-inventory change): replaces the old editable
// DYE-fields + presets page. Shows all bags with inInventory = true as cards;
// tapping a card selects it (sets activeBagId), and the Change Beans dialog
// handles search/creation. There are no editable bean text fields here —
// bag edits go through the dialog's Edit Bag form.
Page {
    id: bagInventoryPage
    objectName: "bagInventoryPage"
    background: Rectangle { color: Theme.backgroundColor }

    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("beaninfo.title", "Beans")

    property var inventoryBags: []

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("beaninfo.title", "Beans")
        MainController.bagStorage.requestInventory()
        addBagButton.forceActiveFocus()
    }

    Connections {
        target: MainController.bagStorage
        function onInventoryReady(bags) {
            bagInventoryPage.inventoryBags = bags
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

                AccessibleButton {
                    id: addBagButton
                    primary: true
                    Layout.preferredHeight: Theme.scaled(44)
                    icon.source: "qrc:/icons/plus.svg"
                    text: TranslationManager.translate("beaninfo.inventory.addBag", "Add New Bag")
                    accessibleName: TranslationManager.translate("beaninfo.inventory.accessible.addBag", "Add a new bag of beans")
                    onClicked: changeBeansDialog.open()
                }
            }

            // Empty state
            ColumnLayout {
                visible: bagInventoryPage.inventoryBags.length === 0
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
                        bag: modelData
                        width: {
                            var avail = flickable.width
                            var cardW = Theme.scaled(380)
                            var columns = Math.max(1, Math.floor(avail / cardW))
                            return (avail - (columns - 1) * Theme.spacingMedium) / columns
                        }
                        onEditRequested: function(b) { changeBeansDialog.openForEdit(b) }
                        onSaveAsRequested: function(b) { changeBeansDialog.openForSaveAs(b) }
                    }
                }
            }
        }
    }

    BottomBar {
        barColor: "transparent"
        onBackClicked: root.goBack()
    }
}
