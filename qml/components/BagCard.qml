import QtQuick
import QtQuick.Layouts
import Decenza

// Inventory bag card (bean-bag-inventory). Adaptive content: canonical-linked
// bags show a dense attribute line + verified badge; partial bags show only
// what is available plus a subtle "Find in Bean Base" nudge. Tapping the card
// selects the bag (sets activeBagId). Action row: Restock (a new bag of the
// same coffee, prefilled from this one), Thaw (frozen bags), Edit, and ONE
// removal action that follows the bag's life: a trash icon
// while no shot references it (a mistaken creation — deletes the row), then
// "Bag finished" once shots exist (leaves inventory, history kept). Storage
// still refuses deleting a referenced bag — a brief message explains if the
// count was stale.
//
// A finished bag (finishedCard) is shown dimmed; tapping it opens the editor,
// and its actions are Restock, Restore (this bag back into inventory, as Restore
// does for an archived recipe), Edit and details.
Rectangle {
    id: card

    property var bag: ({})
    property bool finishedCard: false

    signal editRequested(var bag)
    // "Find in Bean Base": open the edit dialog with the canonical link
    // search pre-run for this bag.
    signal linkRequested(var bag)
    // Finished card: open the new-bag form prefilled from this bag.
    signal restockRequested(var bag)
    // Bean Search: open your history and the community's for this coffee.
    signal suggestionsRequested(var bag)

    readonly property bool selected: !finishedCard && bag && bag.id !== undefined && bag.id === BagService.activeBagId
    readonly property bool hasCanonical: bag && bag.beanBaseId !== undefined && String(bag.beanBaseId).length > 0
    // Which buttons the card shows: InventoryBag::cardActions, the list the web
    // page renders too.
    readonly property var actions: bag && bag.actions ? bag.actions : []
    function offers(action) { return actions.indexOf(action) >= 0 }

    readonly property var beanBase: {
        if (!bag || !bag.beanBaseData || String(bag.beanBaseData).length === 0) return ({})
        try { return JSON.parse(bag.beanBaseData) } catch (e) {
            // Display can omit a corrupt blob; the host retains its original bytes.
            WebDebugLogger.warn("BeanBase", "BagCard", ["corrupt beanBaseData for bag", bag.id, e].map(String).join(" "))
            return ({})
        }
    }
    // Keep raw bytes for the canonical link usability check.
    readonly property string rawBeanBase: bag && bag.beanBaseData ? String(bag.beanBaseData) : ""
    // A dead link is retained, so "has a link" no longer means "has one worth
    // using". BeanBaseBlob::linkIsUsable is the rule; this reads the same two
    // fields so the binding tracks them.
    readonly property bool linkIsUsable:
        BeanUiService.linkIsUsable(rawBeanBase, String(beanBase.link || ""))
    // Manual bags use bag-<rowid>; the cache identity survives a dead product URL.
    readonly property string linkKey: BeanUiService.bagImageKey(
        bag && bag.id !== undefined ? bag.id : 0, canonicalId)

    readonly property string canonicalId: hasCanonical ? String(bag.beanBaseId) : ""
    readonly property string imageKey: linkKey

    // Link recovery and persistence run once on the host, using its current row.
    function inspectLinks() {
        if (bag && bag.id > 0 && BeanUiService.ready)
            BeanUiService.inspectBagLinks(bag.id)
    }
    Component.onCompleted: inspectLinks()
    onImageKeyChanged: inspectLinks()
    onSelectedChanged: if (selected) inspectLinks()
    Connections {
        target: BeanUiService
        function onStateChanged() { if (BeanUiService.ready) card.inspectLinks() }
    }

    // Canonical attribute line: origin · variety · process (only what exists).
    // Plain join for accessibility; joinWithBullet (styled bold dot, HTML-escaped)
    // for display.
    readonly property bool isTea: !!(bag && String(bag.kind || "") === "tea")
    readonly property var _attrParts: {
        var parts = []
        if (isTea) {
            // Tea attribute line (add-recipe-wizard-tea): type · origin ·
            // brewing summary — the fields that matter for a tea bag.
            if (beanBase.teaType) parts.push(String(beanBase.teaType))
            if (beanBase.origin) parts.push(String(beanBase.origin))
            if (beanBase.brewTempC) parts.push(String(beanBase.brewTempC) + "°C")
            if (beanBase.steepTime) parts.push(String(beanBase.steepTime))
            return parts
        }
        if (beanBase.origin) parts.push(String(beanBase.origin))
        if (beanBase.variety) parts.push(String(beanBase.variety))
        if (beanBase.process) parts.push(String(beanBase.process))
        return parts
    }
    readonly property string attrLine: _attrParts.join("  ·  ")
    readonly property string attrLineRich: Theme.joinWithBullet(_attrParts)

    // Roasted · Thawed · Opened, from the lifecycleParts the inventory carries.
    readonly property var _metaParts: BagLifecycleLabels.describe(bag ? bag.lifecycleParts : [])
    readonly property string metaLine: _metaParts.join("  ·  ")
    readonly property string metaLineRich: Theme.joinWithBullet(_metaParts)

    readonly property string accessibleSummary: {
        var bits = [(bag && bag.coffeeName) || "", (bag && bag.roasterName) || ""]
            .filter(function(s) { return s.length > 0 })
        if (attrLine.length > 0) bits.push(attrLine)
        if (metaLine.length > 0) bits.push(metaLine)
        if (selected) bits.push(TranslationManager.translate("accessibility.selected", "selected"))
        if (finishedCard) bits.push(TranslationManager.translate("bagcard.accessible.finished", "finished"))
        return bits.join(", ")
    }

    color: Theme.cardBackgroundColor
    opacity: finishedCard ? 0.7 : 1.0
    radius: Theme.cardRadius
    border.width: selected ? 2 : 1
    border.color: selected ? Theme.primaryColor : Theme.borderColor

    implicitWidth: Theme.scaled(360)
    implicitHeight: cardColumn.implicitHeight + Theme.scaled(24)

    Accessible.ignored: true  // cardTapArea below carries the card's accessibility

    BeanBaseDetailsPopup {
        id: beanDetailsPopup
        beanBaseJson: (card.bag && card.bag.beanBaseData) || ""
        imageKey: card.imageKey
    }

    DatePickerDialog {
        id: thawDatePicker
        allowFuture: false
        minimumIso: (card.bag && card.bag.frozenDate) || ""
        heading: TranslationManager.translate("bagcard.thawHeading", "When did this portion leave the freezer?")
        onDateSelected: function(dateString) {
            BagService.requestUpdateBag(card.bag.id, { "defrostDate": dateString })
        }
    }

    DatePickerDialog {
        id: freezeDatePicker
        allowFuture: false
        minimumIso: (card.bag && card.bag.roastDate) || ""
        heading: TranslationManager.translate("bagcard.freezeHeading", "When did this bag go into the freezer?")
        onDateSelected: function(dateString) {
            BagService.requestUpdateBag(card.bag.id, { "frozenDate": dateString })
        }
    }

    // Tap anywhere on the card to select the bag (#1798: the tap area used to
    // wrap only the info row, so the action row's whitespace and the card's
    // padding were dead space). The action-row buttons keep their own taps;
    // everything else — text, photo, whitespace — selects.
    CardTapArea {
        id: cardTapArea
        enabled: BagService.ready && !BagService.busy
        accessibleName: card.accessibleSummary
        accessibleItem: card
        onAccessibleClicked: {
            if (!card.bag || card.bag.id === undefined)
                return
            if (card.finishedCard)
                card.editRequested(card.bag)
            else
                BagService.requestActivateBag(card.bag.id)
        }
    }

    Timer {
        id: deleteRefusedTimer
        interval: 4000  // UI auto-dismiss (allowed timer use)
        onTriggered: deleteRefusedText.visible = false
    }

    Connections {
        target: BagService
        function onBagDeleted(bagId, success) {
            if (!card.bag || bagId !== card.bag.id || success) return
            deleteRefusedText.visible = true
            deleteRefusedTimer.restart()
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled)
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

        // Info area — cardTapArea (declared above) selects the bag
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(10)

            // Bag photo — the shared BeanThumbnail cache widget (legacy
            // blob `image` URL as fallback).
            BeanThumbnail {
                Layout.preferredWidth: Theme.scaled(44)
                Layout.preferredHeight: Theme.scaled(44)
                Layout.alignment: Qt.AlignTop
                imageKey: card.imageKey
                fallbackName: (card.bag && card.bag.coffeeName) || ""
                link: card.linkIsUsable ? String(card.beanBase.link) : ""
                legacyImageUrl: card.beanBase.image || ""
            }

            ColumnLayout {
                Layout.fillWidth: true
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
                    text: card.attrLineRich
                    textFormat: Text.StyledText
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
                    text: card.metaLineRich
                    textFormat: Text.StyledText
                    font: Theme.captionFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
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
            enabled: BagService.ready && !BagService.busy
            Layout.fillWidth: true
            spacing: Theme.scaled(6)

            AccessibleButton {
                primary: card.finishedCard
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.restock", "Restock")
                accessibleName: TranslationManager.translate("bagcard.accessible.restock", "Restock: add a new bag of this coffee")
                onClicked: card.restockRequested(card.bag)
            }

            AccessibleButton {
                visible: card.offers("restore")
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.restore", "Restore")
                accessibleName: TranslationManager.translate("bagcard.accessible.restore", "Restore: put this bag back in inventory")
                onClicked: BagService.requestUpdateBag(card.bag.id, { "inInventory": true })
            }

            // Unlinked bag: one tap opens the edit dialog with the Bean Base
            // search already run for this coffee (was a passive hint before).
            AccessibleButton {
                visible: card.offers("findInBeanBase")
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.findInBeanBase", "Find in Bean Base")
                accessibleName: TranslationManager.translate("bagcard.accessible.findInBeanBase", "Find this coffee in Bean Base and link it")
                onClicked: card.linkRequested(card.bag)
            }

            AccessibleButton {
                visible: card.offers("bagFinished")
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.bagFinished", "Bag finished")
                accessibleName: TranslationManager.translate("bagcard.accessible.bagFinished", "Bag finished: remove this bag from inventory; shot history is kept")
                onClicked: BagService.requestMarkEmpty(card.bag.id)
            }

            AccessibleButton {
                visible: !card.finishedCard && !card.isTea
                         && String((card.bag && card.bag.roasterName) || "") !== ""
                         && String((card.bag && card.bag.coffeeName) || "") !== ""
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.suggestions", "Suggestions")
                accessibleName: TranslationManager.translate("bagcard.accessible.suggestions",
                    "Suggestions: your history and the Visualizer community's for this coffee")
                onClicked: card.suggestionsRequested(card.bag)
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
                visible: card.offers("info")
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                icon.source: "qrc:/icons/info.svg"
                accessibleName: TranslationManager.translate("bagcard.accessible.details", "Show all bean details")
                onClicked: beanDetailsPopup.open()
            }

            // "Freeze" marks the bag as kept frozen; it records no thaw, since
            // shots can be ground straight from the freezer.
            AccessibleButton {
                visible: card.offers("freeze")
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.freeze", "Freeze")
                accessibleName: TranslationManager.translate("bagcard.accessible.freeze", "Freeze: pick the date this bag went into the freezer")
                onClicked: freezeDatePicker.openWithDate("")
            }

            // Frozen bag: "Thaw" records the latest portion leaving the
            // freezer — calendar picker, always defaulting to today (pass "" so
            // the picker's default wins over any stored defrostDate).
            AccessibleButton {
                visible: card.offers("thaw")
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.thaw", "Thaw")
                accessibleName: TranslationManager.translate("bagcard.accessible.thaw", "Thaw: pick the date the latest portion left the freezer")
                onClicked: thawDatePicker.openWithDate("")
            }

            // No shots yet: the bag is a mistaken creation — offer delete
            // instead of finishing. Swaps to "Bag finished" above once the
            // first shot lands (inventory refreshes via bagsChanged).
            StyledIconButton {
                visible: card.offers("delete")
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                icon.source: "qrc:/icons/trash.svg"
                accessibleName: TranslationManager.translate("bagcard.accessible.delete", "Delete bag")
                accessibleDescription: TranslationManager.translate("bagcard.accessible.deleteHint", "Deletes this unused bag entirely")
                onClicked: BagService.requestDeleteBag(card.bag.id)
            }
        }
    }
}
