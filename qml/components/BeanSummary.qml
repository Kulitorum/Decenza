import QtQuick
import QtQuick.Layouts
import Decenza

// Read-only adaptive one-line bean summary ("show less when more is known"):
//   canonical-linked  ->  "{coffee} · {origin} · {process} · Roasted Nd [· Def Md]"
//                         with a small verified badge
//   history only      ->  "{roaster} {coffee} · Roasted Nd" + "Link to Bean Base" nudge
//   no roast date     ->  roast-age portion silently omitted (no placeholder)
//   no bag selected   ->  "No beans selected"
//
// Two data sources: live DYE state (default — brew/idle contexts) or
// explicitly set shot-snapshot properties (useShotData: true — detail pages).
Item {
    id: root

    // false = read live Settings.dye state; true = use the shot-mode properties below
    property bool useShotData: false
    property string roasterName: ""
    property string coffeeName: ""
    property string roastDate: ""
    property string roastLevel: ""
    property string beanBaseData: ""
    property string frozenDate: ""
    property string defrostDate: ""

    // Effective values for the active mode
    readonly property string effRoaster: useShotData ? roasterName : Settings.dye.dyeBeanBrand
    readonly property string effCoffee: useShotData ? coffeeName : Settings.dye.dyeBeanType
    readonly property string effRoastDate: useShotData ? roastDate : Settings.dye.dyeRoastDate
    readonly property string effBeanBaseData: useShotData ? beanBaseData : Settings.dye.dyeBeanBaseData
    readonly property string effFrozenDate: useShotData ? frozenDate : Settings.dye.activeBagFrozenDate
    readonly property string effDefrostDate: useShotData ? defrostDate : Settings.dye.activeBagDefrostDate

    readonly property var beanBase: {
        if (!effBeanBaseData || effBeanBaseData.length === 0) return ({})
        try { return JSON.parse(effBeanBaseData) } catch (e) { return ({}) }
    }

    // A non-empty canonical blob (or a live Bean Base link) marks the bean as
    // canonical-linked. Bag blobs carry attributes without an id, shot blobs
    // carry an id — accept either.
    readonly property bool canonical: {
        if (!useShotData && Settings.dye.dyeBeanBaseId.length > 0) return true
        for (var k in beanBase) {
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

    readonly property string summaryText: {
        var _ = TranslationManager.translationVersion  // re-evaluate on language change
        if (!hasBeans)
            return TranslationManager.translate("beans.summary.noBeans", "No beans selected")
        var parts = []
        if (canonical) {
            // Dense canonical line: coffee name carries identity, roaster is implied
            if (effCoffee.length > 0) parts.push(effCoffee)
            else if (effRoaster.length > 0) parts.push(effRoaster)
            if (beanBase.origin) parts.push(String(beanBase.origin))
            if (beanBase.process) parts.push(String(beanBase.process))
        } else {
            var name = [effRoaster, effCoffee].filter(function(s) { return s && s.length > 0 }).join(" ")
            if (name.length > 0) parts.push(name)
        }
        var roastAge = daysSince(effRoastDate)
        if (roastAge >= 0)
            parts.push(TranslationManager.translate("beans.summary.roastedDays", "Roasted %1d").arg(roastAge))
        if (effDefrostDate.length > 0) {
            var defAge = daysSince(effDefrostDate)
            if (defAge >= 0)
                parts.push(TranslationManager.translate("beans.summary.defrostDays", "Def %1d").arg(defAge))
        } else if (effFrozenDate.length > 0) {
            parts.push(TranslationManager.translate("beans.summary.frozen", "Frozen"))
        }
        return parts.join(" · ")
    }

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
                text: root.summaryText
                font: Theme.bodyFont
                color: root.hasBeans ? Theme.textColor : Theme.textSecondaryColor
                elide: Text.ElideRight
                Accessible.ignored: true
            }
        }

        // Subtle nudge for non-canonical beans
        Tr {
            visible: root.hasBeans && !root.canonical
            key: "beans.summary.linkNudge"
            fallback: "Link to Bean Base"
            font: Theme.captionFont
            color: Theme.textSecondaryColor
            Accessible.ignored: true
        }
    }
}
