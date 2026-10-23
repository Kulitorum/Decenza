// The equipment-card Repeater delegate below reads this file's ids (`flickable`,
// `switchEquipmentDialog`); Bound makes them statically resolvable. The delegate declares
// its one injected model role required in the same edit -- without that, Bound stops
// role injection and `modelData` goes undefined at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

// Equipment inventory (add-equipment-packages): mirrors BeanInfoPage. Shows all
// packages with inInventory = true as cards, archived ones behind "Show archived
// (N)" as finished bags are; "Add [Equipment]" opens the create dialog. Equipment is switched per-bag from Brew Settings, so there is no
// global selection here — cards are informational + edit/remove.
T.Page {
    id: equipmentPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("equipment.title", "Equipment")

    objectName: "equipmentPage"
    background: ThemedPageBackground {}


    property var inventoryPackages: []
    // Archived packages (removed by the user, not forked) behind "Show archived (N)".
    property var archivedPackages: []
    property bool showArchived: false

    Component.onCompleted: {
        MainController.equipmentStorage.requestInventory()
        MainController.equipmentStorage.requestArchived()
        addEquipmentButton.forceActiveFocus()
    }

    Connections {
        target: MainController.equipmentStorage
        function onInventoryReady(packages) {
            equipmentPage.inventoryPackages = packages
        }
        function onArchivedReady(packages) {
            equipmentPage.archivedPackages = packages
        }
        function onPackagesChanged() {
            MainController.equipmentStorage.requestInventory()
            MainController.equipmentStorage.requestArchived()
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

                AddLabel {}

                AccessibleButton {
                    id: addEquipmentButton
                    Layout.preferredHeight: Theme.scaled(44)
                    icon.source: "qrc:/icons/grind.svg"
                    text: TranslationManager.translate("equipment.inventory.addButton", "Equipment")
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
                        required property var modelData

                        pkg: modelData
                        width: Theme.cardGridWidth(flickable.width)
                        onEditRequested: function(p) { switchEquipmentDialog.openForEdit(p) }
                    }
                }
            }

            AccessibleButton {
                visible: equipmentPage.archivedPackages.length > 0
                Layout.preferredHeight: Theme.scaled(36)   // Layout child: raw height is ignored
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: (equipmentPage.showArchived
                       ? TranslationManager.translate("equipment.archived.hide", "Hide archived")
                       : TranslationManager.translate("equipment.archived.show", "Show archived"))
                      + " (" + equipmentPage.archivedPackages.length + ")"
                accessibleName: text
                onClicked: equipmentPage.showArchived = !equipmentPage.showArchived
            }

            Flow {
                visible: equipmentPage.showArchived
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Repeater {
                    model: equipmentPage.showArchived ? equipmentPage.archivedPackages : []

                    EquipmentCard {
                        required property var modelData

                        pkg: modelData
                        archivedCard: true
                        width: Theme.cardGridWidth(flickable.width)
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
