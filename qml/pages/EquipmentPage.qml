// The equipment-page delegate uses the inventory view's Repeater model role.
// Bound keeps both references statically resolvable.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Decenza

// Equipment inventory (add-equipment-packages): shows every package in inventory.
// The host supplies storage, registry suggestions, and persistence through one
// typed backend; this page owns the shared inventory and editor presentation.
T.Page {
    id: equipmentPage

    required property EquipmentUiBackend backend
    // Native enters this page from Idle. A browser may initially show Equipment
    // as its root until the shared home route is available.
    property bool canGoBack: true

    readonly property string pageTitle: TranslationManager.translate("equipment.title", "Equipment")

    objectName: "equipmentPage"
    background: ThemedPageBackground {}

    property var archivedPackages: []
    property bool archivedReadFailed: false
    property bool showArchived: false
    readonly property bool serviceReady: EquipmentService.ready
    onServiceReadyChanged: {
        if (serviceReady) {
            EquipmentService.requestArchived()
        } else {
            archivedPackages = []
            archivedReadFailed = true
        }
    }

    Component.onCompleted: {
        backend.refresh()
        EquipmentService.requestArchived()
        Qt.callLater(function() {
            if (inventoryView.headerFocusItem)
                inventoryView.headerFocusItem.forceActiveFocus()
        })
    }

    Connections {
        target: equipmentPage.backend
        function onPackagesInvalidated() {
            // The open dialog owns refresh while preserving its draft.
            if (!equipmentDialog.visible)
                equipmentPage.backend.refresh()
        }
    }

    Connections {
        target: EquipmentService
        function onArchivedReady(packages) {
            equipmentPage.archivedPackages = packages
            equipmentPage.archivedReadFailed = false
        }
        function onArchivedFailed(reason) {
            equipmentPage.archivedPackages = []
            equipmentPage.archivedReadFailed = true
        }
        function onPackagesChanged() { EquipmentService.requestArchived() }
    }

    NativeEquipmentInventoryStyle { id: nativeInventoryStyle }
    NativeEquipmentDialogStyle { id: nativeEquipmentStyle }
    NativeEquipmentDialogText { id: nativeEquipmentStrings }

    SharedEquipmentDialog {
        id: equipmentDialog
        backend: equipmentPage.backend
        equipmentStyle: nativeEquipmentStyle
        strings: nativeEquipmentStrings
        logName: "SwitchEquipmentDialog"
        onLogShown: function(name) {
            WebDebugLogger.info("App", "Popup", name + " dialog shown")
        }
    }

    EquipmentInventoryView {
        id: inventoryView
        anchors.fill: parent
        inventoryStyle: nativeInventoryStyle
        title: nativeEquipmentStrings.inventoryTitle
        emptyTitle: nativeEquipmentStrings.inventoryEmptyTitle
        emptyHint: nativeEquipmentStrings.inventoryEmptyHint
        emptyStateVisible: equipmentPage.backend.emptyStateVisible
        packages: equipmentPage.backend.packages
        buttonStyle: nativeEquipmentStyle.buttonStyle
        interaction: nativeEquipmentStyle.buttonInteraction
        addLabel: nativeEquipmentStrings.addLabel
        addButtonText: nativeEquipmentStrings.inventoryAddButton
        addAccessibleName: nativeEquipmentStrings.inventoryAccessibleAdd
        actionsEnabled: equipmentPage.backend.actionsEnabled
        statusVisible: equipmentPage.backend.statusVisible
        statusComponent: Component {
            Text {
                text: equipmentPage.backend.statusText
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
        footerComponent: Component {
            ColumnLayout {
                spacing: Theme.spacingMedium
                Text {
                    Layout.fillWidth: true
                    visible: equipmentPage.archivedReadFailed
                    text: TranslationManager.translate("equipment.archived.loadFailed",
                        "Could not load archived equipment. Please try again.")
                    font: Theme.captionFont
                    color: Theme.warningColor
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                AccessibleButton {
                    visible: equipmentPage.archivedPackages.length > 0
                    Layout.preferredHeight: Theme.scaled(36)
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
                            width: inventoryView.cardWidth
                            actionsEnabled: equipmentPage.backend.actionsEnabled
                        }
                    }
                }
            }
        }
        cardDelegate: Component {
            EquipmentCard {
                required property var modelData
                pkg: modelData
                width: inventoryView.cardWidth
                actionsEnabled: equipmentPage.backend.actionsEnabled
                onEditRequested: function(p) { equipmentDialog.openForEdit(p) }
            }
        }
        onAddRequested: equipmentDialog.openForCreate()
    }

    BottomBar {
        barColor: "transparent"
        showBackButton: equipmentPage.canGoBack
        onBackClicked: {
            if (equipmentPage.canGoBack)
                AppShell.backRequested()
        }
    }
}
