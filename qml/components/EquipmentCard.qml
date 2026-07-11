import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Equipment package card (add-equipment-packages). Mirrors BagCard. Shows the
// package identity via the shared EquipmentSummary renderer (grinder, dial,
// basket, puck prep), then adds the inventory chrome. Equipment is switched
// per-bag from Brew Settings, so the card itself is informational + edit/remove
// (no global "selected" state). One
// removal action follows the package's life: a trash icon while no SHOT
// references it (a mistaken creation — hard delete), then "Remove" once shots
// exist (soft-delete, history kept). Storage is the authoritative backstop — it
// also refuses a hard delete when a bag references the package, surfacing the
// deleteRefused toast in that (rarer) case.
Rectangle {
    id: card

    property var pkg: ({})

    signal editRequested(var pkg)

    readonly property bool selected: pkg && pkg.id !== undefined && pkg.id === Settings.dye.activeEquipmentId
    readonly property bool hasReferences: pkg && (pkg.shotCount ?? 0) > 0

    color: Theme.surfaceColor
    radius: Theme.cardRadius
    border.width: selected ? 2 : 1
    border.color: selected ? Theme.primaryColor : Theme.borderColor

    implicitWidth: Theme.scaled(360)
    implicitHeight: cardColumn.implicitHeight + Theme.scaled(24)

    Accessible.ignored: true

    Timer {
        id: deleteRefusedTimer
        interval: 4000  // UI auto-dismiss (allowed timer use)
        onTriggered: deleteRefusedText.visible = false
    }

    Connections {
        target: MainController.equipmentStorage
        function onPackageDeleted(packageId, success) {
            if (!card.pkg || packageId !== card.pkg.id || success) return
            deleteRefusedText.visible = true
            deleteRefusedTimer.restart()
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                AccessibilityManager.announce(deleteRefusedText.text)
        }
    }

    ColumnLayout {
        id: cardColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.scaled(12)
        spacing: Theme.scaled(6)

        Item {
            id: infoArea
            Layout.fillWidth: true
            implicitHeight: summary.implicitHeight

            // Shared identity rendering (grinder/basket/puck prep). The package map
            // exposes the canonical puck-prep string as `puckPrepCanonical`.
            EquipmentSummary {
                id: summary
                anchors.left: parent.left
                anchors.right: parent.right
                grinderName: (card.pkg && card.pkg.name) ? String(card.pkg.name) : ""
                grinderBrand: (card.pkg && card.pkg.grinderBrand) || ""
                grinderModel: (card.pkg && card.pkg.grinderModel) || ""
                grinderBurrs: (card.pkg && card.pkg.grinderBurrs) || ""
                // Grind/rpm are a per-shot dial-in, not equipment — the inventory
                // card lists only what the package IS, so the last-dial line is
                // left unfed here (Shot Detail / Post-Shot Review still show it).
                basketBrand: (card.pkg && card.pkg.basketBrand) || ""
                basketModel: (card.pkg && card.pkg.basketModel) || ""
                puckPrepCanonical: (card.pkg && card.pkg.puckPrepCanonical) || ""
            }

            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: card.selected
                    ? summary.accessibleSummary + ", " + TranslationManager.translate("accessibility.selected", "selected")
                    : summary.accessibleSummary
                accessibleItem: infoArea
                onAccessibleClicked: {
                    if (card.pkg && card.pkg.id !== undefined)
                        Settings.dye.switchToEquipment(card.pkg)
                }
            }
        }

        Tr {
            id: deleteRefusedText
            visible: false
            Layout.fillWidth: true
            key: "equipment.card.deleteRefused"
            fallback: "This package is used by bags or shots — use Remove instead"
            font: Theme.captionFont
            color: Theme.warningColor
            wrapMode: Text.Wrap
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.scaled(6)

            AccessibleButton {
                visible: card.hasReferences
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("equipment.card.remove", "Remove")
                accessibleName: TranslationManager.translate("equipment.card.accessible.remove", "Remove this equipment from inventory; history is kept")
                onClicked: MainController.equipmentStorage.requestMarkRemoved(card.pkg.id)
            }

            StyledIconButton {
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                icon.source: "qrc:/icons/edit.svg"
                accessibleName: TranslationManager.translate("equipment.card.accessible.edit", "Edit equipment details")
                onClicked: card.editRequested(card.pkg)
            }

            StyledIconButton {
                visible: !card.hasReferences
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                icon.source: "qrc:/icons/trash.svg"
                accessibleName: TranslationManager.translate("equipment.card.accessible.delete", "Delete equipment")
                accessibleDescription: TranslationManager.translate("equipment.card.accessible.deleteHint", "Deletes this unused equipment package entirely")
                onClicked: MainController.equipmentStorage.requestDeletePackage(card.pkg.id)
            }
        }
    }
}
