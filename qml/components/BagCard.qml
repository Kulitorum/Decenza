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
    // NOTE: isFrozen means "has EVER been frozen" — a thawed bag keeps its
    // frozenDate, so this stays true after a thaw. That is the intended
    // reading for the "Frozen" badge and the freeze toggle, but NOT for
    // anything asking "is a portion in the freezer right now" — use
    // portionInFreezer for that.
    readonly property bool isFrozen: bag && bag.frozenDate !== undefined && String(bag.frozenDate).length > 0
    readonly property string defrostDate: bag && bag.defrostDate !== undefined ? String(bag.defrostDate) : ""
    // A portion is in the freezer only while the bag is frozen and no thaw has
    // been recorded; once defrostDate is set the current portion is out.
    readonly property bool portionInFreezer: isFrozen && defrostDate.length === 0
    readonly property string openedDate: bag && bag.openedDate !== undefined ? String(bag.openedDate) : ""

    readonly property var beanBase: {
        if (!bag || !bag.beanBaseData || String(bag.beanBaseData).length === 0) return ({})
        try { return JSON.parse(bag.beanBaseData) } catch (e) { return ({}) }
    }

    // Bag photo from the on-disk image cache (canonical entries carry no image
    // — the photo is resolved from the product page's og:image and cached as a
    // file, never stored in the DB). Legacy pre-removal blobs may still carry
    // a CDN `image` URL, used as fallback. Manual bags with a user-entered
    // product URL get the same treatment under a "bag-<rowid>" cache key
    // (add-bag-detail-editing).
    readonly property string canonicalId: hasCanonical ? String(bag.beanBaseId) : ""
    readonly property string imageKey: hasCanonical
        ? canonicalId
        : (bag && bag.id !== undefined && beanBase.link ? "bag-" + bag.id : "")

    // The thumbnail itself (cache resolve/backfill) lives in the shared
    // BeanThumbnail widget below; this card only adds the reorder-URL
    // recovery, wanted even when the image is already cached (a legacy blob
    // whose photo resolved before link backfill existed). Canonical-linked
    // bags only — a manual bag has nothing to recover from.
    function maybeRecoverLink() {
        // A cleared-because-dead link (linkDead) must not be recovered: the
        // canonical API only knows the same dead URL and would re-add it.
        if (hasCanonical && !beanBase.link && !beanBase.linkDead)
            MainController.beanbase.recoverBagLink(canonicalId, (bag && bag.coffeeName) || "")
    }
    // Validate the stored product URL once per bag (pick-time). The persisted
    // linkChecked marker keeps this to a single GET ever — not a per-view probe.
    function maybeValidateLink() {
        if (hasCanonical && beanBase.link && !beanBase.linkChecked)
            MainController.beanbase.validateBagLink(canonicalId, String(beanBase.link))
    }
    Component.onCompleted: { maybeRecoverLink(); maybeValidateLink() }
    onImageKeyChanged: { maybeRecoverLink(); maybeValidateLink() }

    Connections {
        target: MainController.beanbase
        // One-time blob backfill: the image re-search recovered the product
        // URL for a blob linked before `link` was captured. Persist it so the
        // details popup can offer the reorder link (bag row only — shot
        // snapshots stay as recorded, per the propagate default).
        function onBagLinkRecovered(id, link) {
            if (id !== card.canonicalId || !card.bag || card.bag.id === undefined)
                return
            if (card.beanBase.link)
                return
            var blob = card.beanBase
            blob.link = link
            MainController.bagStorage.requestUpdateBag(card.bag.id,
                { "beanBaseData": JSON.stringify(blob) })
            card.maybeValidateLink()  // validate the freshly recovered URL too
        }
        // Pick-time URL validation resolved (possibly via redirect): normalize a
        // stale alias to the durable canonical URL, and stamp linkChecked so the
        // check never re-runs for this bag. No-op when nothing changed.
        function onBagLinkResolved(id, link) {
            if (id !== card.canonicalId || !card.bag || card.bag.id === undefined)
                return
            var blob = card.beanBase
            var changed = false
            if (link && blob.link !== link) { blob.link = link; changed = true }
            if (blob.linkDead !== undefined) { delete blob.linkDead; changed = true }
            if (!blob.linkChecked) { blob.linkChecked = true; changed = true }
            if (changed)
                MainController.bagStorage.requestUpdateBag(card.bag.id,
                    { "beanBaseData": JSON.stringify(blob) })
        }
        // Confirmed 404/410: clear the dead reorder link and mark it so neither
        // validation nor recovery re-adds the same dead URL.
        function onBagLinkDead(id) {
            if (id !== card.canonicalId || !card.bag || card.bag.id === undefined)
                return
            var blob = card.beanBase
            if (blob.link === undefined && blob.linkDead && blob.linkChecked)
                return
            delete blob.link
            blob.linkDead = true
            blob.linkChecked = true
            MainController.bagStorage.requestUpdateBag(card.bag.id,
                { "beanBaseData": JSON.stringify(blob) })
        }
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

    // Roast date · freeze/open state line (omits anything unknown — no
    // placeholders). The user freezes beans, so the actual roast/thaw/open
    // date is more meaningful than a bare day count — show both (the absolute
    // date and the at-a-glance age), matching the roast-date convention.
    readonly property var _metaParts: {
        var _ = TranslationManager.translationVersion
        var parts = []
        var roast = formatRoastDate(bag && bag.roastDate ? String(bag.roastDate) : "")
        if (roast.length > 0)
            parts.push(TranslationManager.translate("beans.summary.roastedDate", "Roasted %1").arg(roast))
        if (defrostDate.length > 0) {
            var defAge = daysSince(defrostDate)
            if (defAge >= 0)
                parts.push(TranslationManager.translate("beans.summary.thawedDate", "Thawed %1 (%2d)")
                    .arg(formatRoastDate(defrostDate)).arg(defAge))
        } else if (isFrozen) {
            parts.push(TranslationManager.translate("beans.summary.frozen", "Frozen"))
        } else if (openedDate.length > 0) {
            // Non-frozen bags: show the opened date/age the same way (only when
            // not frozen — a frozen bag's lifecycle is the thaw date above).
            var openAge = daysSince(openedDate)
            if (openAge >= 0)
                parts.push(TranslationManager.translate("beans.summary.openedDate", "Opened %1 (%2d)")
                    .arg(formatRoastDate(openedDate)).arg(openAge))
        }
        return parts
    }
    readonly property string metaLine: _metaParts.join("  ·  ")
    readonly property string metaLineRich: Theme.joinWithBullet(_metaParts)

    readonly property string accessibleSummary: {
        var bits = [(bag && bag.coffeeName) || "", (bag && bag.roasterName) || ""]
            .filter(function(s) { return s.length > 0 })
        if (attrLine.length > 0) bits.push(attrLine)
        if (metaLine.length > 0) bits.push(metaLine)
        if (selected) bits.push(TranslationManager.translate("accessibility.selected", "selected"))
        return bits.join(", ")
    }

    color: Theme.cardBackgroundColor
    radius: Theme.cardRadius
    border.width: selected ? 2 : 1
    border.color: selected ? Theme.primaryColor : Theme.borderColor

    implicitWidth: Theme.scaled(360)
    implicitHeight: cardColumn.implicitHeight + Theme.scaled(24)

    Accessible.ignored: true  // AccessibleMouseArea below carries the card's accessibility

    BeanBaseDetailsPopup {
        id: beanDetailsPopup
        beanBaseJson: (card.bag && card.bag.beanBaseData) || ""
        imageKey: card.imageKey
    }

    DatePickerDialog {
        id: thawDatePicker
        onDateSelected: function(dateString) {
            MainController.bagStorage.requestUpdateBag(card.bag.id, { "defrostDate": dateString })
        }
    }

    // "Mark Opened" quick action for non-frozen bags (bean-freshness-followup),
    // the non-frozen analogue of thawDatePicker.
    DatePickerDialog {
        id: openedDatePicker
        onDateSelected: function(dateString) {
            MainController.bagStorage.requestUpdateBag(card.bag.id, { "openedDate": dateString })
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
            implicitHeight: infoRow.implicitHeight

            RowLayout {
                id: infoRow
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.scaled(10)

                // Bag photo — the shared BeanThumbnail cache widget (legacy
                // blob `image` URL as fallback).
                BeanThumbnail {
                    Layout.preferredWidth: Theme.scaled(44)
                    Layout.preferredHeight: Theme.scaled(44)
                    Layout.alignment: Qt.AlignTop
                    imageKey: card.imageKey
                    fallbackName: (card.bag && card.bag.coffeeName) || ""
                    link: card.beanBase.link || ""
                    legacyImageUrl: card.beanBase.image || ""
                }

                ColumnLayout {
                    id: infoColumn
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
            // freezer — calendar picker, always defaulting to today (a new
            // thaw event happening today is overwhelmingly the common case;
            // pass "" so the picker's "default to today" branch wins over any
            // stored defrostDate).
            AccessibleButton {
                visible: card.isFrozen
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.thaw", "Thaw")
                accessibleName: TranslationManager.translate("bagcard.accessible.thaw", "Thaw: pick the date the latest portion left the freezer")
                onClicked: thawDatePicker.openWithDate("")
            }

            // "Mark Opened" records when the current portion started being
            // used at room temperature. Shown whenever no portion is in the
            // freezer — i.e. never-frozen bags AND thawed ones, which carry
            // both actions: "Thaw" records a later portion leaving the
            // freezer, "Mark Opened" this portion leaving airtight storage.
            // Same picker pattern as Thaw, always defaulting to today.
            AccessibleButton {
                visible: !card.portionInFreezer
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: TranslationManager.translate("bagcard.markOpened", "Mark Opened")
                accessibleName: TranslationManager.translate("bagcard.accessible.markOpened", "Mark opened: pick the date this bag was opened")
                onClicked: openedDatePicker.openWithDate("")
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
