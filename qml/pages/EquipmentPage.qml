import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"

// Equipment inventory (add-equipment-packages): mirrors BeanInfoPage. Shows all
// packages with inInventory = true as cards; "Add Equipment" opens the create
// dialog. Equipment is switched per-bag from Brew Settings, so there is no
// global selection here — cards are informational + edit/remove.
Page {
    id: equipmentPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("equipment.title", "Equipment")

    objectName: "equipmentPage"
    background: ThemedPageBackground {}


    property var inventoryPackages: []

    Component.onCompleted: {
        MainController.equipmentStorage.requestInventory()
        addEquipmentButton.forceActiveFocus()
    }

    Connections {
        target: MainController.equipmentStorage
        function onInventoryReady(packages) {
            equipmentPage.inventoryPackages = packages
        }
        function onPackagesChanged() {
            MainController.equipmentStorage.requestInventory()
        }
    }

    SwitchEquipmentDialog {
        id: switchEquipmentDialog
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

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Tr {
                    key: "equipment.inventory.title"
                    fallback: "Equipment"
                    font: Theme.titleFont
                    color: Theme.textColor
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    id: addEquipmentButton
                    primary: true
                    Layout.preferredHeight: Theme.scaled(44)
                    icon.source: "qrc:/icons/plus.svg"
                    text: TranslationManager.translate("equipment.inventory.add", "Add Equipment")
                    accessibleName: TranslationManager.translate("equipment.inventory.accessible.add", "Add a new equipment package")
                    onClicked: switchEquipmentDialog.openForCreate()
                }
            }

            // Empty state
            ColumnLayout {
                visible: equipmentPage.inventoryPackages.length === 0
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(40)
                spacing: Theme.spacingSmall

                Tr {
                    Layout.alignment: Qt.AlignHCenter
                    key: "equipment.inventory.empty.title"
                    fallback: "No equipment yet"
                    font: Theme.subtitleFont
                    color: Theme.textColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Tr {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: flickable.width * 0.8
                    key: "equipment.inventory.empty.hint"
                    fallback: "Add your grinder to track its model, burrs and dial-in per bag."
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }

            // Package cards
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Repeater {
                    model: equipmentPage.inventoryPackages

                    EquipmentCard {
                        pkg: modelData
                        width: {
                            var avail = flickable.width
                            var cardW = Theme.scaled(380)
                            var columns = Math.max(1, Math.floor(avail / cardW))
                            return (avail - (columns - 1) * Theme.spacingMedium) / columns
                        }
                        onEditRequested: function(p) { switchEquipmentDialog.openForEdit(p) }
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
