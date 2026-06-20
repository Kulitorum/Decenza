import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Equipment package card (add-equipment-packages). Mirrors BagCard. Shows the
// grinder identity (brand/model, burrs subtitle), the last-used dial, and the
// basket line (add-basket-equipment). Equipment is switched per-bag from Brew Settings, so the
// card itself is informational + edit/remove (no global "selected" state). One
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
    readonly property string grinderTitle: {
        var brand = (pkg && pkg.grinderBrand) || ""
        var model = (pkg && pkg.grinderModel) || ""
        var combined = [brand, model].filter(function(s) { return s.length > 0 }).join(" ")
        return (pkg && pkg.name && String(pkg.name).length > 0) ? String(pkg.name) : combined
    }
    readonly property string burrs: (pkg && pkg.grinderBurrs) || ""
    readonly property bool rpmCapable: !!(pkg && pkg.rpmCapable)
    // Basket identity line (add-basket-equipment); empty when the package has none.
    readonly property string basketLine: {
        var _ = TranslationManager.translationVersion
        var b = [(pkg && pkg.basketBrand) || "", (pkg && pkg.basketModel) || ""]
                .filter(function(s) { return s.length > 0 }).join(" ")
        return b.length > 0 ? TranslationManager.translate("equipment.card.basket", "Basket: %1").arg(b) : ""
    }
    // Puck-prep summary line (add-puckprep-equipment); empty when the package has
    // none. Shows the short labels of the set flags, e.g. "Prep: WDT · Shaker".
    readonly property string puckPrepLine: {
        var _ = TranslationManager.translationVersion
        if (!pkg) return ""
        var labels = []
        if (pkg.puckPrep_wdt) labels.push(TranslationManager.translate("equipment.dialog.puckWdt", "WDT"))
        if (pkg.puckPrep_shaker) labels.push(TranslationManager.translate("equipment.dialog.puckShaker", "Shaker"))
        if (pkg.puckPrep_puckScreen) labels.push(TranslationManager.translate("equipment.dialog.puckScreen", "Puck screen"))
        if (pkg.puckPrep_paperFilter) labels.push(TranslationManager.translate("equipment.dialog.puckPaper", "Bottom paper filter"))
        if (pkg.puckPrep_rdt) labels.push(TranslationManager.translate("equipment.dialog.puckRdt", "RDT (spritz)"))
        return labels.length > 0
            ? TranslationManager.translate("equipment.card.puckPrep", "Prep: %1").arg(labels.join(" · ")) : ""
    }

    readonly property string lastDialLine: {
        var _ = TranslationManager.translationVersion
        var parts = []
        var g = (pkg && pkg.lastGrindSetting) || ""
        if (String(g).length > 0)
            parts.push(TranslationManager.translate("equipment.card.lastGrind", "Grind %1").arg(g))
        var rpm = pkg && pkg.lastRpm ? Number(pkg.lastRpm) : 0
        if (rpmCapable && rpm > 0)
            parts.push(TranslationManager.translate("equipment.card.lastRpm", "%1 rpm").arg(rpm))
        return parts.join(" · ")
    }

    readonly property string accessibleSummary: {
        var bits = [grinderTitle, burrs].filter(function(s) { return s.length > 0 })
        if (lastDialLine.length > 0) bits.push(lastDialLine)
        if (basketLine.length > 0) bits.push(basketLine)
        if (puckPrepLine.length > 0) bits.push(puckPrepLine)
        if (selected) bits.push(TranslationManager.translate("accessibility.selected", "selected"))
        return bits.join(", ")
    }

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
            implicitHeight: infoColumn.implicitHeight

            ColumnLayout {
                id: infoColumn
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.scaled(2)

                Text {
                    Layout.fillWidth: true
                    text: card.grinderTitle
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.subtitleFont.pixelSize
                    font.bold: true
                    color: Theme.textColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }

                Text {
                    Layout.fillWidth: true
                    visible: card.burrs.length > 0
                    text: card.burrs
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }

                Text {
                    Layout.fillWidth: true
                    visible: card.lastDialLine.length > 0
                    text: card.lastDialLine
                    font: Theme.captionFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }

                // Basket + puck prep last: separate equipment, so keep the grinder
                // identity (title + burrs) and its dial (grind) contiguous above them.
                Text {
                    Layout.fillWidth: true
                    visible: card.basketLine.length > 0
                    text: card.basketLine
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }

                Text {
                    Layout.fillWidth: true
                    visible: card.puckPrepLine.length > 0
                    text: card.puckPrepLine
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
            }

            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: card.accessibleSummary
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
