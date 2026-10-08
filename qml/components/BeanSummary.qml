import QtQuick
import QtQuick.Layouts
import Decenza

// Read-only adaptive one-line bean summary ("show less when more is known"):
//   canonical-linked  ->  "{coffee} · {origin} · {process} · Roasted <date> [· Thawed <date> (Md) | Frozen | Opened <date> (Md)]"
//                         with a small verified badge
//   history only      ->  "{roaster} {coffee} · Roasted <date> [· Thawed <date> (Md) | Frozen | Opened <date> (Md)]"
//                         + "Link to Bean Base" nudge
//   no roast date     ->  roast-date portion silently omitted (no placeholder)
//   no bag selected   ->  "No beans selected"
//
// Roast shows the actual date (the user freezes beans, so days-since-roast is
// misleading); the thaw/open line shows the absolute date plus days-since (a
// real freshness clock for the current portion).
//
// Two data sources: live DYE state (default — brew/idle contexts) or
// explicitly set shot-snapshot properties (useShotData: true — detail pages).
Item {
    id: root

    // false = read live Settings.dye state; true = use the shot-mode properties below
    property bool useShotData: false

    // When true, the "Link to Bean Base" nudge becomes a tappable link that emits
    // linkRequested() — hosts wire it to their bean-linking flow (e.g. the Change
    // Beans dialog). Default false keeps the nudge a passive hint on read-only
    // surfaces (idle/brew, shot detail) where there's no link action to take.
    property bool linkable: false
    signal linkRequested()
    property string roasterName: ""
    property string coffeeName: ""
    property string roastDate: ""
    property string roastLevel: ""
    property string beanBaseData: ""
    property string frozenDate: ""
    property string defrostDate: ""
    // When the current portion was first used; a snapshot can carry it with
    // frozenDate and defrostDate.
    property string openedDate: ""
    // The ISO date ages are measured to: the shot's date in shot mode ("" = today).
    property string referenceDate: ""

    // Effective values for the active mode
    readonly property string effRoaster: useShotData ? roasterName : Settings.dye.dyeBeanBrand
    readonly property string effCoffee: useShotData ? coffeeName : Settings.dye.dyeBeanType
    readonly property string effRoastDate: useShotData ? roastDate : Settings.dye.dyeRoastDate
    readonly property string effBeanBaseData: useShotData ? beanBaseData : Settings.dye.dyeBeanBaseData
    readonly property string effFrozenDate: useShotData ? frozenDate : Settings.dye.activeBagFrozenDate
    readonly property string effDefrostDate: useShotData ? defrostDate : Settings.dye.activeBagDefrostDate
    readonly property string effOpenedDate: useShotData ? openedDate : Settings.dye.activeBagOpenedDate

    readonly property var beanBase: {
        if (!effBeanBaseData || effBeanBaseData.length === 0) return ({})
        try { return JSON.parse(effBeanBaseData) } catch (e) { return ({}) }
    }

    // A non-empty canonical blob (or a live Bean Base link) marks the bean as
    // canonical-linked. Bag blobs carry attributes without an id, shot blobs
    // carry an id — accept either.
    readonly property bool canonical: {
        if (!useShotData && Settings.dye.dyeBeanBaseId.length > 0) return true
        for (let k in beanBase) {
            if (beanBase[k] !== undefined && String(beanBase[k]).length > 0) return true
        }
        return false
    }

    // "No beans": live mode requires no active bag AND empty identity fields;
    // shot mode just checks the snapshot identity.
    readonly property bool hasBeans: {
        if (!useShotData && Settings.dye.activeBagId > 0) return true
        return effRoaster.length > 0 || effCoffee.length > 0
    }

    readonly property var _parts: {
        if (!hasBeans) return []
        var parts = []
        if (canonical) {
            // Dense canonical line: coffee name carries identity, roaster is implied
            if (effCoffee.length > 0) parts.push(effCoffee)
            else if (effRoaster.length > 0) parts.push(effRoaster)
            if (beanBase.origin) parts.push(String(beanBase.origin))
            if (beanBase.process) parts.push(String(beanBase.process))
        } else {
            let name = [effRoaster, effCoffee].filter(function(s) { return s && s.length > 0 }).join(" ")
            if (name.length > 0) parts.push(name)
        }
        // Roasted · Thawed · Opened: the same parts the bag card shows.
        parts = parts.concat(BagLifecycleLabels.describe(MainController.bagStorage.lifecycleParts({
            "roastDate": effRoastDate, "frozenDate": effFrozenDate,
            "defrostDate": effDefrostDate, "openedDate": effOpenedDate },
            useShotData ? referenceDate : "")))
        return parts
    }

    // Plain text — used for the accessibility label (no markup).
    readonly property string summaryText: {
        var _ = TranslationManager.translationVersion
        if (!hasBeans)
            return TranslationManager.translate("beans.summary.noBeans", "No beans selected")
        return _parts.join("  ·  ")
    }

    // Display text — separators rendered as a slightly bigger/bolder bullet so the
    // info sections read as distinct. StyledText; user data is HTML-escaped.
    // Bean names are user-typed and the picker encourages emoji in them. Escaping alone left
    // them as raw codepoints in a StyledText: a colour emoji then reaches the platform text
    // renderer (the macOS path this change exists to avoid) and an unbundled one draws tofu
    // instead of being dropped. allowMarkup is true because the input is markup we just built.
    readonly property string summaryRich:
        Theme.replaceEmojiWithImg(
            hasBeans ? Theme.joinWithBullet(_parts) : Theme.escapeHtml(summaryText),
            Theme.bodyFont.pixelSize, true)

    implicitHeight: contentColumn.implicitHeight
    implicitWidth: contentColumn.implicitWidth

    Accessible.role: Accessible.StaticText
    Accessible.name: canonical && hasBeans
        ? summaryText + ", " + TranslationManager.translate("beans.summary.accessible.verified", "linked to Bean Base")
        : summaryText
    Accessible.focusable: true

    ColumnLayout {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.scaled(2)

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(6)

            // Canonical verified badge
            ColoredIcon {
                visible: root.canonical && root.hasBeans
                source: "qrc:/icons/tick.svg"
                iconWidth: Theme.scaled(14)
                iconHeight: Theme.scaled(14)
                iconColor: Theme.primaryColor
                Accessible.ignored: true
            }

            Text {
                Layout.fillWidth: true
                text: root.summaryRich
                textFormat: Text.StyledText
                font: Theme.bodyFont
                color: root.hasBeans ? Theme.textColor : Theme.textSecondaryColor
                elide: Text.ElideRight
                Accessible.ignored: true
            }
        }

        // Subtle nudge for non-canonical beans. When `linkable`, it reads as a
        // tappable link (primary colour + underline) that emits linkRequested();
        // otherwise it's a passive secondary-colour hint.
        Item {
            id: linkNudge
            visible: root.hasBeans && !root.canonical
            Layout.fillWidth: true
            implicitHeight: nudgeText.implicitHeight
            implicitWidth: nudgeText.implicitWidth

            Tr {
                id: nudgeText
                key: "beans.summary.linkNudge"
                fallback: "Link to Bean Base"
                // Sub-properties assigned individually (font: + font.underline mix
                // is a known QML conflict — see CLAUDE.md QML Gotchas).
                font.family: Theme.captionFont.family
                font.pixelSize: Theme.captionFont.pixelSize
                font.underline: root.linkable
                color: root.linkable ? Theme.primaryColor : Theme.textSecondaryColor
                Accessible.ignored: true
            }

            AccessibleMouseArea {
                anchors.fill: parent
                enabled: root.linkable
                visible: root.linkable
                accessibleName: nudgeText.text
                accessibleItem: linkNudge
                onAccessibleClicked: root.linkRequested()
            }
        }
    }
}
