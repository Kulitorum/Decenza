import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "."

// The ONE recipe card (recipes-bag-links-ui-polish, design D4): rendered by
// the Recipes management page and, identically, as the wizard summary's
// WYSIWYG hero — what the user builds is what they will later see in the
// list. Takes plain properties only (a recipe map plus resolved extras), no
// page-specific lookups, so neither host can drift.
//
// Layout, top to bottom:
//   1. Recipe name (anchor) + Active badge
//   2. Drink line: drink-type icon + short label + profile (ALWAYS shown —
//      same-bean twins must be distinguishable) + milk weight when stored
//      (never a bare "milk" word)
//   3. Bean line: bag name + shot count — or, when stale, the "Bag finished —
//      tap to choose beans" re-point affordance
//   4. Shot-plan line — or the vessel snapshot ("200ml · 80°C") for a
//      profile-less hot-water tea, which has no plan
// Every text line wraps rather than elides: the profile is never truncated
// away; added card height is acceptable.
Rectangle {
    id: card

    // The recipe map (storage keys: name, drinkType, profileTitle, steamJson,
    // hotWaterJson, roasterName, coffeeName, doseG, yieldG, tempOffsetC,
    // grindPinned, shotCount…). The wizard hero feeds a synthesized map.
    property var recipe: ({})
    property bool active: false
    // Stale = the linked bag left inventory. showStaleAction turns the bean
    // line into the one-tap re-point affordance (management page only — the
    // wizard hero has its own bag row).
    property bool stale: false
    property bool showStaleAction: false
    // Bean photo: the host passes the cache key (Bean Base id or "bag-<id>")
    // plus the fallbacks ensureBagImage needs. Empty key = icon placeholder.
    property string imageKey: ""
    property string imageLink: ""
    // Profile numbers the plan line needs (the recipe stores only its deltas):
    // the card's OWN profile's scalar temp/yield plus its frame temperatures —
    // never the loaded profile's (recipe-relative-temp-offset). Needed even
    // though the plan line now shows only the resulting baseline value: the
    // shared ShotPlanText's live-anchored baseline path reads the currently
    // loaded profile, which would be wrong here. An unresolved profile leaves
    // all three empty/0 and the plan line omits the temperature segment
    // rather than borrowing the loaded profile's frames.
    property real profileTempC: 0
    property real profileYieldG: 0
    property var profileStepTemps: []

    signal staleActionClicked()
    signal planClicked()

    readonly property var steam: {
        if (!recipe || !recipe.steamJson || String(recipe.steamJson).length === 0) return ({})
        try { return JSON.parse(recipe.steamJson) } catch (e) { return ({}) }
    }
    readonly property var hotWater: {
        if (!recipe || !recipe.hotWaterJson || String(recipe.hotWaterJson).length === 0) return ({})
        try { return JSON.parse(recipe.hotWaterJson) } catch (e) { return ({}) }
    }
    readonly property string drinkType: DrinkType.fromRecipeMap(recipe)
    // Profile-less hot-water tea: no plan line to speak of — the drink line
    // says "Tea · Hot water" and the vessel snapshot takes the plan's place.
    readonly property bool isHotWaterOnly:
        (!recipe.profileTitle || String(recipe.profileTitle).trim() === "") && !!hotWater.hasWater

    Tr { id: trActiveBadge; key: "recipes.list.active"; fallback: "Active"; visible: false }
    Tr { id: trShotsWord; key: "recipes.list.shots"; fallback: "shots"; visible: false }
    Tr { id: trMilkWeight; key: "recipes.list.milkWeight"; fallback: "%1g milk"; visible: false }
    Tr { id: trHotWaterWord; key: "recipes.list.hotWater"; fallback: "Hot water"; visible: false }
    Tr {
        id: trBagFinishedAction
        key: "recipes.list.bagFinished"
        fallback: "Bag finished — tap to choose beans"
        visible: false
    }

    function drinkLine() {
        var parts = []
        if (isHotWaterOnly)
            parts.push(DrinkType.shortLabel("tea") + " · " + trHotWaterWord.text)
        else
            parts.push(DrinkType.shortLabel(drinkType))
        if (recipe.profileTitle && String(recipe.profileTitle).trim() !== "")
            parts.push(recipe.profileTitle)
        if (steam.hasMilk && (steam.milkWeightG || 0) > 0)
            parts.push(trMilkWeight.text.arg(steam.milkWeightG))
        return parts.join(" · ")
    }

    function beanLine() {
        var parts = []
        var bean = ((recipe.roasterName || "") + " " + (recipe.coffeeName || "")).trim()
        if (bean !== "") parts.push(bean)
        if ((recipe.shotCount || 0) > 0)
            parts.push(recipe.shotCount + " " + trShotsWord.text)
        return parts.join(" · ")
    }

    // The hot-water tea's vessel snapshot ("200ml · 80°C" / "200g · 80°C").
    function vesselLine() {
        var parts = []
        if ((hotWater.volume || 0) > 0)
            parts.push(hotWater.volume + (hotWater.mode === "volume" ? "ml" : "g"))
        if ((hotWater.temperatureC || 0) > 0)
            parts.push(Math.round(hotWater.temperatureC) + "°C")
        if (hotWater.vesselName)
            parts.unshift(hotWater.vesselName)
        return parts.join(" · ")
    }

    // Extra rows a host appends under the card content (the management
    // page's action buttons). The wizard hero leaves it empty. Deliberately
    // NOT the default property so plain children (tap areas, Connections)
    // stay ordinary children of the card rectangle.
    property alias footer: footerSlot.data

    implicitHeight: cardColumn.implicitHeight + 2 * Theme.spacingMedium
    radius: Theme.cardRadius
    color: Theme.cardBackgroundColor
    border.color: active ? Theme.accentColor : Theme.borderColor
    border.width: active ? 2 : 1

    ColumnLayout {
        id: cardColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingSmall

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.scaled(10)

        // Bean photo thumbnail — the shared BeanThumbnail cache widget.
        BeanThumbnail {
            Layout.preferredWidth: Theme.scaled(44)
            Layout.preferredHeight: Theme.scaled(44)
            Layout.alignment: Qt.AlignTop
            imageKey: card.imageKey
            fallbackName: card.recipe.coffeeName || ""
            link: card.imageLink
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(2)

            // 1. Name anchor + Active badge.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                Label {
                    text: card.recipe.name || ""
                    font: Theme.subtitleFont
                    color: Theme.textColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    visible: card.active
                    Layout.alignment: Qt.AlignTop
                    text: trActiveBadge.text
                    font: Theme.captionFont
                    color: Theme.accentColor
                }
            }

            // 2. Drink line: icon + short label + profile + milk weight.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(6)
                ColoredIcon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Theme.scaled(1)
                    source: DrinkType.icon(card.drinkType)
                    iconWidth: Theme.scaled(16)
                    iconHeight: Theme.scaled(16)
                    iconColor: Theme.textSecondaryColor
                    Accessible.ignored: true
                }
                Label {
                    Layout.fillWidth: true
                    text: card.drinkLine()
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                    wrapMode: Text.WordWrap
                }
            }

            // 3. Bean line — or the stale re-point affordance.
            Label {
                visible: !(card.stale && card.showStaleAction) && card.beanLine() !== ""
                Layout.fillWidth: true
                text: card.beanLine()
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                wrapMode: Text.WordWrap
            }
            Label {
                visible: card.stale && card.showStaleAction
                Layout.fillWidth: true
                text: trBagFinishedAction.text
                font: Theme.captionFont
                color: Theme.warningColor
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.Button
                Accessible.name: text
                Accessible.focusable: true
                Accessible.onPressAction: card.staleActionClicked()
                TapHandler { onTapped: card.staleActionClicked() }
            }

            // 4. Plan line — or the vessel snapshot for hot-water tea.
            ShotPlanText {
                visible: !card.isHotWaterOnly
                Layout.fillWidth: true
                sentence: false
                maxLines: 3
                itemOrder: ["doseYield", "temperature", "grind"]
                profileName: card.recipe.profileTitle || ""
                profileTemp: card.profileTempC
                // Baseline resolution against the card's OWN profile
                // (recipe-relative-temp-offset): profileStepTemps carries that
                // profile's own frame temperatures so temperatureDisplayForSteps
                // can resolve them explicitly (never the currently loaded
                // profile's), and recipeTempOffsetC is folded into those frames
                // as a shift — the card shows only the resulting value, no tag.
                // The live override inputs are explicitly silenced — a card
                // must never reflect the dial, and has no override to show.
                profileStepTemps: card.profileStepTemps
                recipeTempOffsetC: card.recipe.tempOffsetC || 0
                tempOverridden: false
                dose: card.recipe.doseG || 0
                profileYield: card.profileYieldG
                targetWeight: card.recipe.yieldG > 0 ? card.recipe.yieldG : card.profileYieldG
                // No arrow/highlight: a card is a static recipe definition, not
                // a live per-brew comparison — it shows the resulting yield only.
                yieldOverridden: false
                grindSize: card.recipe.grindPinned || ""
                roasterBrand: ""
                coffeeName: ""
                // Its internal MouseArea sits above the host's tap area —
                // forward so the host can route it (activate on the list).
                onClicked: card.planClicked()
            }
            Label {
                visible: card.isHotWaterOnly && card.vesselLine() !== ""
                Layout.fillWidth: true
                text: card.vesselLine()
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                wrapMode: Text.WordWrap
            }
        }
    }

    ColumnLayout {
        id: footerSlot
        Layout.fillWidth: true
        spacing: Theme.spacingSmall
        visible: children.length > 0
    }
    }
}
