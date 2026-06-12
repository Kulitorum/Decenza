import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Inventory bag card (bean-bag-inventory). Adaptive content: canonical-linked
// bags show a dense attribute line + verified badge; partial bags show only
// what is available plus a subtle "Find in Bean Base" nudge. Tapping the card
// selects the bag (sets activeBagId). Action row: Thaw (frozen bags),
// Edit, and ONE removal action that follows the bag's life: a trash icon
// while no shot references it (a mistaken creation — deletes the row), then
// "Bag finished" once shots exist (leaves inventory, history kept). Storage
// still refuses deleting a referenced bag — a brief message explains if the
// count was stale.
Rectangle {
    id: card

    property var bag: ({})

    signal editRequested(var bag)
    // "Find in Bean Base": open the edit dialog with the canonical link
    // search pre-run for this bag.
    signal linkRequested(var bag)

    readonly property bool selected: bag && bag.id !== undefined && bag.id === Settings.dye.activeBagId
    readonly property bool hasShots: bag && (bag.shotCount ?? 0) > 0
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

    // Roast date as a short, locale-formatted string; falls back to the raw
    // stored text if it isn't a parseable ISO date.
    function formatRoastDate(raw) {
        if (!raw || raw.length < 8) return raw || ""
        var d = new Date(raw.substring(0, 10) + "T00:00:00")
        if (isNaN(d.getTime())) return raw
        return Qt.formatDate(d, Qt.locale().dateFormat(Locale.ShortFormat))
    }

    // Roast date · freeze state line (omits anything unknown — no
    // placeholders). The user freezes beans, so the actual roast date is more
    // meaningful than days-since-roast.
    readonly property string metaLine: {
        var _ = TranslationManager.translationVersion
        var parts = []
        var roast = formatRoastDate(bag && bag.roastDate ? String(bag.roastDate) : "")
        if (roast.length > 0)
            parts.push(TranslationManager.translate("beans.summary.roastedDate", "Roasted %1").arg(roast))
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

    BeanBaseDetailsPopup {
        id: beanDetailsPopup
        beanBaseJson: (card.bag && card.bag.beanBaseData) || ""
    }

    DatePickerDialog {
        id: thawDatePicker
        onDateSelected: function(dateString) {
            MainController.bagStorage.requestUpdateBag(card.bag.id, { "defrostDate": dateString })
        }
    }

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

                // Tasting notes earn a line of their own — the most
                // interesting canonical data. One elided line; the info
                // button opens the full record.
                Text {
                    Layout.fillWidth: true
                    visible: !!(card.beanBase.tastingNotes)
                    text: card.beanBase.tastingNotes || ""
                    font.family: Theme.captionFont.family
                    font.pixelSize: Theme.captionFont.pixelSize
                    font.italic: true
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
            fallback: "This bag has shots linked to it — use Bag finished instead"
            font: Theme.captionFont
            color: Theme.warningColor
            wrapMode: Text.Wrap
        }

        // Action row
        Flow {
            Layout.fillWidth: true
            spacing: Theme.scaled(6)

            // Unlinked bag: one tap opens the edit dialog with the Bean Base
            // search already run for this coffee (was a passive hint before).
            AccessibleButton {
                visible: !card.hasCanonical
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.findInBeanBase", "Find in Bean Base")
                accessibleName: TranslationManager.translate("bagcard.accessible.findInBeanBase", "Find this coffee in Bean Base and link it")
                onClicked: card.linkRequested(card.bag)
            }

            AccessibleButton {
                visible: card.hasShots
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.bagFinished", "Bag finished")
                accessibleName: TranslationManager.translate("bagcard.accessible.bagFinished", "Bag finished: remove this bag from inventory; shot history is kept")
                onClicked: MainController.bagStorage.requestMarkEmpty(card.bag.id)
            }

            StyledIconButton {
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                icon.source: "qrc:/icons/edit.svg"
                accessibleName: TranslationManager.translate("bagcard.accessible.edit", "Edit bag details")
                onClicked: card.editRequested(card.bag)
            }

            // Everything we know about the bean, on demand — the card keeps
            // its dense subset (attrs + tasting notes), the popup shows all.
            StyledIconButton {
                visible: card.hasCanonical
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                icon.source: "qrc:/icons/info.svg"
                accessibleName: TranslationManager.translate("bagcard.accessible.details", "Show all bean details")
                onClicked: beanDetailsPopup.open()
            }

            // Frozen bag: "Thaw" records the latest portion leaving the
            // freezer — calendar picker, defaulting to today.
            AccessibleButton {
                visible: card.isFrozen
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.thaw", "Thaw")
                accessibleName: TranslationManager.translate("bagcard.accessible.thaw", "Thaw: pick the date the latest portion left the freezer")
                onClicked: thawDatePicker.openWithDate(card.defrostDate)
            }

            // No shots yet: the bag is a mistaken creation — offer delete
            // instead of finishing. Swaps to "Bag finished" above once the
            // first shot lands (inventory refreshes via bagsChanged).
            StyledIconButton {
                visible: !card.hasShots
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                icon.source: "qrc:/icons/trash.svg"
                accessibleName: TranslationManager.translate("bagcard.accessible.delete", "Delete bag")
                accessibleDescription: TranslationManager.translate("bagcard.accessible.deleteHint", "Deletes this unused bag entirely")
                onClicked: MainController.bagStorage.requestDeleteBag(card.bag.id)
            }
        }
    }
}
