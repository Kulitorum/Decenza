import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Inventory bag card (bean-bag-inventory). Adaptive content: canonical-linked
// bags show a dense attribute line + verified badge; partial bags show only
// what is available plus a subtle "Find in Bean Base" nudge. Tapping the card
// selects the bag (sets activeBagId). Action row: Next Portion (frozen bags),
// Edit, New Bag (save-as), Mark as Empty, Delete (refused by storage when
// shots reference the bag — a brief message explains).
Rectangle {
    id: card

    property var bag: ({})

    signal editRequested(var bag)
    signal saveAsRequested(var bag)

    readonly property bool selected: bag && bag.id !== undefined && bag.id === Settings.dye.activeBagId
    readonly property bool hasCanonical: bag && bag.beanBaseId !== undefined && String(bag.beanBaseId).length > 0
    readonly property bool isFrozen: bag && bag.frozenDate !== undefined && String(bag.frozenDate).length > 0
    readonly property string defrostDate: bag && bag.defrostDate !== undefined ? String(bag.defrostDate) : ""

    readonly property var beanBase: {
        if (!bag || !bag.beanBaseData || String(bag.beanBaseData).length === 0) return ({})
        try { return JSON.parse(bag.beanBaseData) } catch (e) { return ({}) }
    }

    // Canonical attribute line: origin · variety · process (only what exists)
    readonly property string attrLine: {
        var parts = []
        if (beanBase.origin) parts.push(String(beanBase.origin))
        if (beanBase.variety) parts.push(String(beanBase.variety))
        if (beanBase.process) parts.push(String(beanBase.process))
        return parts.join(" · ")
    }

    function daysSince(isoDate) {
        if (!isoDate || isoDate.length < 8) return -1
        var d = new Date(isoDate.substring(0, 10) + "T00:00:00")
        if (isNaN(d.getTime())) return -1
        var now = new Date()
        var today = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        var that = new Date(d.getFullYear(), d.getMonth(), d.getDate())
        var days = Math.round((today - that) / 86400000)
        return days >= 0 ? days : -1
    }

    // Roast age · freeze state line (omits anything unknown — no placeholders)
    readonly property string metaLine: {
        var _ = TranslationManager.translationVersion
        var parts = []
        var roastAge = daysSince(bag && bag.roastDate ? String(bag.roastDate) : "")
        if (roastAge >= 0)
            parts.push(TranslationManager.translate("beans.summary.roastedDays", "Roasted %1d").arg(roastAge))
        if (defrostDate.length > 0) {
            var defAge = daysSince(defrostDate)
            if (defAge >= 0)
                parts.push(TranslationManager.translate("beans.summary.defrostDays", "Def %1d").arg(defAge))
        } else if (isFrozen) {
            parts.push(TranslationManager.translate("beans.summary.frozen", "Frozen"))
        }
        return parts.join(" · ")
    }

    readonly property string accessibleSummary: {
        var bits = [(bag && bag.coffeeName) || "", (bag && bag.roasterName) || ""]
            .filter(function(s) { return s.length > 0 })
        if (attrLine.length > 0) bits.push(attrLine)
        if (metaLine.length > 0) bits.push(metaLine)
        if (selected) bits.push(TranslationManager.translate("accessibility.selected", "selected"))
        return bits.join(", ")
    }

    color: Theme.surfaceColor
    radius: Theme.cardRadius
    border.width: selected ? 2 : 1
    border.color: selected ? Theme.primaryColor : Theme.borderColor

    implicitWidth: Theme.scaled(360)
    implicitHeight: cardColumn.implicitHeight + Theme.scaled(24)

    Accessible.ignored: true  // AccessibleMouseArea below carries the card's accessibility

    Timer {
        id: deleteRefusedTimer
        interval: 4000  // UI auto-dismiss (allowed timer use)
        onTriggered: deleteRefusedText.visible = false
    }

    Connections {
        target: MainController.bagStorage
        function onBagDeleted(bagId, success) {
            if (!card.bag || bagId !== card.bag.id || success) return
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

        // Info area — tap to select
        Item {
            id: infoArea
            Layout.fillWidth: true
            implicitHeight: infoColumn.implicitHeight

            ColumnLayout {
                id: infoColumn
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.scaled(2)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)

                    ColoredIcon {
                        visible: card.hasCanonical
                        Layout.alignment: Qt.AlignTop
                        source: "qrc:/icons/tick.svg"
                        iconWidth: Theme.scaled(14)
                        iconHeight: Theme.scaled(14)
                        iconColor: Theme.primaryColor
                        Accessible.ignored: true
                    }

                    Text {
                        Layout.fillWidth: true
                        text: (card.bag && card.bag.coffeeName) || (card.bag && card.bag.roasterName) || ""
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.subtitleFont.pixelSize
                        font.bold: true
                        color: Theme.textColor
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: !!(card.bag && card.bag.coffeeName && card.bag.roasterName)
                    text: (card.bag && card.bag.roasterName) || ""
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }

                // Canonical: one dense attribute line; partial: nothing (no placeholders)
                Text {
                    Layout.fillWidth: true
                    visible: card.attrLine.length > 0
                    text: card.attrLine
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }

                Text {
                    Layout.fillWidth: true
                    visible: card.metaLine.length > 0
                    text: card.metaLine
                    font: Theme.captionFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }

                // Subtle nudge for bags without a canonical link
                Tr {
                    visible: !card.hasCanonical
                    key: "bagcard.findInBeanBase"
                    fallback: "Find in Bean Base"
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                    Accessible.ignored: true
                }
            }

            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: card.accessibleSummary
                accessibleItem: infoArea
                onAccessibleClicked: {
                    if (card.bag && card.bag.id !== undefined)
                        Settings.dye.activeBagId = card.bag.id
                }
            }
        }

        // Delete-refused message (bag has linked shots)
        Tr {
            id: deleteRefusedText
            visible: false
            Layout.fillWidth: true
            key: "bagcard.deleteRefused"
            fallback: "This bag has shots linked to it — use Mark as Empty instead"
            font: Theme.captionFont
            color: Theme.warningColor
            wrapMode: Text.Wrap
        }

        // Action row
        Flow {
            Layout.fillWidth: true
            spacing: Theme.scaled(6)

            AccessibleButton {
                visible: card.isFrozen
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.nextPortion", "Next Portion")
                accessibleName: TranslationManager.translate("bagcard.accessible.nextPortion", "Next portion: mark a new portion defrosted today")
                onClicked: MainController.bagStorage.requestSetDefrostToday(card.bag.id)
            }

            AccessibleButton {
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.markEmpty", "Mark as Empty")
                accessibleName: TranslationManager.translate("bagcard.accessible.markEmpty", "Mark bag as empty and remove it from inventory")
                onClicked: MainController.bagStorage.requestMarkEmpty(card.bag.id)
            }

            StyledIconButton {
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                icon.source: "qrc:/icons/edit.svg"
                accessibleName: TranslationManager.translate("bagcard.accessible.edit", "Edit bag details")
                onClicked: card.editRequested(card.bag)
            }

            StyledIconButton {
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                icon.source: "qrc:/icons/plus.svg"
                accessibleName: TranslationManager.translate("bagcard.accessible.saveAs", "New bag of this coffee")
                onClicked: card.saveAsRequested(card.bag)
            }

            StyledIconButton {
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                icon.source: "qrc:/icons/trash.svg"
                accessibleName: TranslationManager.translate("bagcard.accessible.delete", "Delete bag")
                accessibleDescription: TranslationManager.translate("bagcard.accessible.deleteHint", "Only bags with no linked shots can be deleted")
                onClicked: MainController.bagStorage.requestDeleteBag(card.bag.id)
            }
        }
    }
}
