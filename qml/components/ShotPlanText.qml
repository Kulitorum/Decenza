import QtQuick
import Decenza
import "../"

// The brew summary for the home screen. Content is driven by an ordered item
// list (`itemOrder`) and a `sentence` toggle:
//  - sentence ON, profile shown: "Brew 36.0g of Espresso, using <profile> at
//    92°C" — the scaffold consumes doseYield's yield, profile, and temperature
//    (wherever they sit in the order; sentence word order belongs to the
//    translated template), and everything else — including doseYield's dose-in
//    fragment — trails after it in list order.
//  - sentence ON, no profile anchor (item removed, or no profile name
//    available): the profile-less "recipe" sentence — "Brew 40.0g of Espresso
//    at 92°C from 18.0g of <Roaster> <Bean>" — anchored on the beverage word.
//    Dose, temperature, roaster and coffee are consumed into it; only
//    grind/roastDate trail.
//  - sentence OFF: every item renders as a separator-joined fragment, in list
//    order.
//  - stacked ON (sentence mode only, display path only): the detail tail
//    renders on its own line(s) below the sentence instead of trailing after a
//    separator. The accessibility string stays one dot-joined sentence.
//    (Idea from PR #1415's "stacked" format.)
// Tapping it opens Brew Settings.
//
// Root is an Item (icon + text) but it preserves the old ShotPlanText API the
// ShotPlanItem wrapper relies on: a readonly `text` (plain sentence, for
// accessibility / visibility), a `clicked()` signal, and implicit sizing.
// When the granted width is narrower than the natural single-line width, the
// text wraps up to `maxLines` lines and elides past that — it never clips.
Item {
    id: root

    signal clicked()

    // Ordered display items. Keys: "doseYield", "profile", "temperature",
    // "roaster", "coffee", "grind", "roastDate". Membership shows the item,
    // position orders it (fragment list / sentence tail).
    property var itemOrder: ["doseYield", "profile", "temperature", "roaster", "coffee", "grind"]
    // Sentence scaffold vs plain fragment list.
    property bool sentence: true
    // Sentence mode only: render the detail tail on its own line(s) below the
    // sentence (display path — the a11y text stays one joined sentence).
    property bool stacked: false
    // Yield rendering: when true, suppress the "profileDefault → target" arrow
    // and show only the effective target — see _yieldStr for the full rule.
    property bool yieldTargetOnly: false
    // Wrap budget before eliding: 2 in the full-size widget, 1 in compact bars.
    property int maxLines: 2

    function _has(key) { return itemOrder && itemOrder.indexOf(key) !== -1 }

    property string profileName: ProfileManager.currentProfileName
    property double profileTemp: ProfileManager.profileTargetTemperature
    property double overrideTemp: Settings.brew.hasTemperatureOverride ? Settings.brew.temperatureOverride : profileTemp
    property string roasterBrand: Settings.dye.dyeBeanBrand || ""
    property string coffeeName: Settings.dye.dyeBeanType || ""
    property string roastDate: Settings.dye.dyeRoastDate
    property string grindSize: Settings.dye.dyeGrinderSetting
    property double dose: Settings.dye.dyeBeanWeight
    property double profileYield: ProfileManager.profileTargetWeight
    property double targetWeight: ProfileManager.targetWeight
    // Override flags, parameterizable so non-live consumers (recipe cards)
    // can render THEIR overrides; defaults preserve the live-widget behavior.
    property bool yieldOverridden: Settings.brew.hasBrewYieldOverride
    property bool tempOverridden: Settings.brew.hasTemperatureOverride

    // The active recipe's own yield/temp, injected by the LIVE idle widget so a
    // recipe reads as the BASELINE, not overrides of the profile
    // (recipe-baseline-not-override, #1485): the yield arrow and the amber
    // highlight measure against these, so a recipe's designed yield shows as a
    // plain target ("40.0g", not "36.0 → 40.0g") and isn't tinted. The
    // temperature STRING is re-anchored too — `_tempStr` shifts the frames to the
    // recipe's own temps (e.g. "81 · 91°C") and drops the profile-relative tag.
    // 0 = off — shot review leaves these unset and keeps its explicit
    // shot-relative behavior; recipe cards use the source + delta properties
    // below instead.
    property double recipeBaselineYield: 0
    property double recipeBaselineTemp: 0

    // Recipe-card "source + delta" presentation (recipe-relative-temp-offset).
    // profileStepTemps: the CARD's own profile's frame temperatures (plain
    // number array) — non-empty routes the temperature string through
    // temperatureDisplayForSteps so a card never renders whatever profile the
    // machine currently holds. recipeTempOffsetC: the recipe's stored offset,
    // rendered as a signed tag AFTER the base temps with the highlight on the
    // tag only ("84 · 94°C −3°") — the card shows where the values come from
    // and how the recipe modifies them, while live surfaces show the recipe's
    // resulting values via recipeBaselineTemp above. Empty/0 = live behavior.
    property var profileStepTemps: []
    property double recipeTempOffsetC: 0
    readonly property double _yieldBaseline: recipeBaselineYield > 0 ? recipeBaselineYield : profileYield
    readonly property double _tempHlBaseline: recipeBaselineTemp > 0 ? recipeBaselineTemp : profileTemp
    // Grind RPM + whether the grinder reports RPM, the beverage word, and the
    // cleaning flag — all default to the live singleton reads so the home
    // widget is unchanged, but a per-shot consumer can override every one with
    // the shot's own frozen snapshot values.
    property int grindRpm: Settings.dye.dyeGrinderRpm
    property bool rpmCapable: Settings.dye.grinderRpmCapable(Settings.dye.dyeGrinderBrand, Settings.dye.dyeGrinderModel)
    property string beverageType: ProfileManager.currentProfileBeverageType
    property bool isCleaning: ProfileManager.currentProfileIsMaintenance

    // Per-item override highlight (recipe-aware-brew-settings): only the
    // overridden segment(s) recolor — the temperature item on a temp override,
    // the yield item on a deliberate yield override — matching the Brew
    // Settings value scheme. Both key off the truthful override flags, never
    // raw drift (measured dose never exactly equals the profile's listed dose).
    readonly property bool _tempOverride:
        tempOverridden && Math.abs(overrideTemp - _tempHlBaseline) > 0.1
    readonly property bool _yieldOverride:
        yieldOverridden && _yieldBaseline > 0 && Math.abs(targetWeight - _yieldBaseline) > 0.1

    // --- Per-item segments (empty string = hidden). ---
    // Dose & yield: the shot's target output, plus dose-in (e.g. "18.0g in"). A DELIBERATE yield
    // override (the hasBrewYieldOverride flag, not raw drift — measured dose never exactly matches
    // the profile's) renders as "36.0 → 40.0g" — the yield counterpart of the temperature-override
    // call-out (which uses a delta tag, not an arrow). yieldTargetOnly suppresses the arrow and
    // shows only the effective target ("40.0g"); with no active override it's a visible no-op.
    readonly property string _yieldStr: {
        if (!(_has("doseYield") && targetWeight > 0)) return ""
        // Arrow shows baseline → target. With a recipe active the baseline is the
        // recipe's own yield, so a recipe sitting at its yield reads "40.0g" (no
        // arrow, no profile reference); the arrow returns only for a per-brew
        // tweak away from the recipe. No recipe → the profile target, as before.
        if (!yieldTargetOnly && yieldOverridden && _yieldBaseline > 0
                && Math.abs(targetWeight - _yieldBaseline) > 0.1)
            return _yieldBaseline.toFixed(1) + " → " + targetWeight.toFixed(1) + "g"
        return targetWeight.toFixed(1) + "g"
    }
    readonly property string _doseStr: (_has("doseYield") && dose > 0) ? (dose.toFixed(1) + "g") : ""
    // temperatureDisplay() follows the C/F display unit; its Settings.app.temperatureUnit
    // read is in C++, invisible to QML bindings, so read it here.
    readonly property string _profileStr: (_has("profile") && profileName) ? profileName : ""
    readonly property string _tempStr: {
        void(Settings.app.temperatureUnit)
        if (!(_has("temperature") && profileTemp > 0)) return ""
        // Recipe cards: the card's OWN profile's frames, unshifted — the
        // recipe's stored offset renders separately as _tempTagStr. A card
        // whose profile didn't resolve has profileTemp 0 and exits above:
        // it never falls back to the loaded profile's frames.
        if (profileStepTemps && profileStepTemps.length > 0)
            return ProfileManager.temperatureDisplayForSteps(profileStepTemps, profileTemp, false, 0, 0)
        // A recipe's temperature is the baseline (a baseline is a baseline): show
        // the recipe's OWN temps (profile frames shifted by the recipe's delta,
        // e.g. "81 · 91°C") anchored on the recipe, so a tag appears only for a
        // per-brew deviation FROM the recipe — never a profile-relative delta. With
        // no recipe, shift 0 and anchor on the profile → unchanged.
        var shift = recipeBaselineTemp > 0 ? (recipeBaselineTemp - profileTemp) : 0
        return ProfileManager.temperatureDisplay(_tempHlBaseline, tempOverridden, overrideTemp, shift)
    }
    // The card's offset tag ("−3°"), highlighted on its own — never the base
    // temps. Follows the °C/°F display unit as a DELTA (×9/5, no +32), the
    // same conversion the wizard's stepper uses.
    readonly property string _tempTagStr: {
        if (_tempStr === "" || Math.abs(recipeTempOffsetC) < 0.05) return ""
        void(Settings.app.temperatureUnit)
        var d = Theme.cDeltaToDisplay(recipeTempOffsetC)
        var a = Math.round(Math.abs(d) * 10) / 10
        var s = a.toFixed(1).replace(/\.0$/, "")
        return (d < 0 ? "-" : "+") + s + "°"
    }
    // Roaster = brand only; Coffee = bean name only; Grind = grinder setting + RPM when recorded.
    // Each item gates exactly its named content so saved widget configs mean what they say.
    readonly property string _roasterStr: (_has("roaster") && roasterBrand) ? roasterBrand : ""
    readonly property string _coffeeStr: (_has("coffee") && coffeeName) ? coffeeName : ""
    readonly property string _grindStr: {
        if (!_has("grind")) return ""
        var parts = []
        if (grindSize.length > 0) parts.push(grindSize)
        if (grindRpm > 0 && rpmCapable)
            parts.push(TranslationManager.translate("equipment.card.lastRpm", "%1 rpm").arg(grindRpm))
        return parts.join(" · ")
    }
    readonly property string _roastDateStr: (_has("roastDate") && roastDate.length > 0) ? roastDate : ""
    // Beverage word from the profile's beverage_type: "Espresso" (the default type),
    // generic "coffee" for filter/pourover/any other coffee type, "tea" for tea
    // profiles ("tea"/"tea_portafilter"). Cleaning/descale profiles get their own
    // sentence in _build() — no bean/dose tail, plus the do-not-load-coffee warning.
    readonly property string _bevType: beverageType
    // The cleaning/descale/calibrate no-coffee tier: by default `isCleaning`
    // reads Profile::isMaintenanceBeverageType (the same C++ call the shot-history,
    // Visualizer and MCP gates make, so the live widget can't drift from them);
    // per-shot consumers override `isCleaning` from the shot's own frozen flag.
    readonly property bool _isCleaning: isCleaning
    readonly property string _beverage: {
        var _ = TranslationManager.translationVersion   // re-evaluate on a live language switch
        if (_bevType === "espresso") return TranslationManager.translate("idle.button.espresso", "Espresso")
        if (_bevType.indexOf("tea") === 0) return TranslationManager.translate("shotplan.beverage.tea", "tea")
        return TranslationManager.translate("shotplan.beverage.coffee", "coffee")
    }

    // Break a "%N" token in a user-supplied value so QString.arg can't substitute a later arg into it
    // (a profile/roaster/bean literally named e.g. "Ramp %2"). The zero-width space is invisible.
    function _argSafe(v) { return String(v).replace(/%(\d)/g, "%\u200B$1") }

    // ONE renderer for both the plain `text` (a11y label + `visible: text !== ""` check) and the bolded
    // `_rich` (display), so they can NEVER drift. fmt(value, live, overridden) formats one value: plain
    // %-escapes (and ignores the override flag — the a11y string never changes), rich HTML-escapes,
    // bolds live values and wraps an overridden value in the highlight color. blockSep separates the
    // sentence from its detail tail — the same `sep` normally, a line break when stacked on the DISPLAY
    // path only (the a11y `text` always passes the dot separator so the spoken string stays one sentence).
    function _build(fmt, sep, blockSep) {
        var _ = TranslationManager.translationVersion
        // Cleaning/descale run — beans are the enemy here. Short sentence, no
        // dose/bean tail, and the warning rides along into the a11y label too.
        // The warning wins over both formats and any item configuration.
        if (_isCleaning) {
            return (profileName)
                ? TranslationManager.translate("shotplan.sentenceCleaning", "Cleaning run with %1 — no coffee in the portafilter!")
                    .arg(fmt(profileName, true))
                : TranslationManager.translate("shotplan.cleaningNoProfile", "Cleaning run — no coffee in the portafilter!")
        }
        var dose = (_doseStr !== "") ? TranslationManager.translate("shotplan.doseIn", "%1 in").arg(fmt(_doseStr, true)) : ""
        // "grind" prefix only when there is a grinder setting; a recorded-RPM-only segment
        // already reads as "90 rpm" and "grind 90 rpm" would be wrong.
        var grind = (_grindStr === "") ? ""
            : (grindSize.length > 0 ? TranslationManager.translate("shotplan.grind", "grind %1").arg(fmt(_grindStr, true))
                                    : fmt(_grindStr, true))
        var roasted = (_roastDateStr !== "") ? TranslationManager.translate("shotplan.roasted", "roasted %1").arg(fmt(_roastDateStr, true)) : ""
        // Temperature item: base temps + the card's offset tag. The tag is
        // formatted as its own overridden fragment so only IT takes the
        // highlight color in rich mode (source + delta; the a11y path reads
        // the same words uncolored).
        var temp = ""
        if (_tempStr !== "") {
            temp = fmt(_tempStr, true, _tempOverride)
            if (_tempTagStr !== "")
                temp += " " + fmt(_tempTagStr, true, true)
        }
        var order = itemOrder || []

        // Sentence format needs the profile as its anchor; without it (item removed
        // or no profile name) fall through to the fragment list. Word order inside
        // the scaffold belongs to the translated template — the consumed items
        // (doseYield's yield, profile, temperature) ignore their list positions.
        if (sentence && _profileStr !== "") {
            var s
            if (_yieldStr !== "" && _tempStr !== "")
                s = TranslationManager.translate("shotplan.sentence", "Brew %1 of %2, using %3 at %4")
                    .arg(fmt(_yieldStr, true, _yieldOverride)).arg(fmt(_beverage, false)).arg(fmt(_profileStr, true)).arg(temp)
            else if (_yieldStr === "" && _tempStr !== "")
                s = TranslationManager.translate("shotplan.sentenceNoYield", "Brew %1, using %2 at %3")
                    .arg(fmt(_beverage, false)).arg(fmt(_profileStr, true)).arg(temp)
            else if (_yieldStr !== "")
                s = TranslationManager.translate("shotplan.sentenceNoTemp", "Brew %1 of %2, using %3")
                    .arg(fmt(_yieldStr, true, _yieldOverride)).arg(fmt(_beverage, false)).arg(fmt(_profileStr, true))
            else
                s = TranslationManager.translate("shotplan.sentenceNoYieldNoTemp", "Brew %1, using %2")
                    .arg(fmt(_beverage, false)).arg(fmt(_profileStr, true))
            var tail = []
            for (var i = 0; i < order.length; i++) {
                switch (order[i]) {
                case "doseYield": if (dose !== "") tail.push(dose); break
                case "roaster":   if (_roasterStr !== "") tail.push(fmt(_roasterStr, true)); break
                case "coffee":    if (_coffeeStr !== "") tail.push(fmt(_coffeeStr, true)); break
                case "grind":     if (grind !== "") tail.push(grind); break
                case "roastDate": if (roasted !== "") tail.push(roasted); break
                }
            }
            return tail.length > 0 ? (s + blockSep + tail.join(sep)) : s
        }

        // Recipe sentence — the profile-less anchor. Reached when Sentence is on
        // but there's no profile anchor (item removed, or no profile name
        // available), so the plan reads as the recipe itself: "Brew 40.0g of
        // Espresso at 92°C from 18.0g of <Roaster> <Bean>". Dose, temperature,
        // roaster and coffee are CONSUMED into the sentence (they don't also
        // trail as fragments); only grind/roastDate trail after. Each piece
        // stays gated by its item's presence via the _xStr getters, so the
        // chips still drive what shows. The beverage word is always present,
        // so this never degrades to a fragment list while Sentence is on.
        // Built by appending translatable clauses (at %/from %) onto the head —
        // English word order; the a11y and rich paths share this builder so
        // they can't drift.
        if (sentence) {
            var beans = ""
            if (_roasterStr !== "" && _coffeeStr !== "")
                beans = fmt(_roasterStr, true) + " " + fmt(_coffeeStr, true)
            else if (_roasterStr !== "")
                beans = fmt(_roasterStr, true)
            else if (_coffeeStr !== "")
                beans = fmt(_coffeeStr, true)

            var r = (_yieldStr !== "")
                ? TranslationManager.translate("shotplan.recipe.head", "Brew %1 of %2")
                    .arg(fmt(_yieldStr, true, _yieldOverride)).arg(fmt(_beverage, false))
                : TranslationManager.translate("shotplan.recipe.headNoYield", "Brew %1")
                    .arg(fmt(_beverage, false))
            if (_tempStr !== "")
                r = TranslationManager.translate("shotplan.recipe.atTemp", "%1 at %2")
                    .arg(r).arg(temp)
            if (_doseStr !== "" && beans !== "")
                r = TranslationManager.translate("shotplan.recipe.fromDoseBeans", "%1 from %2 of %3")
                    .arg(r).arg(fmt(_doseStr, true)).arg(beans)
            else if (_doseStr !== "")
                r = TranslationManager.translate("shotplan.recipe.fromDose", "%1 from %2")
                    .arg(r).arg(fmt(_doseStr, true))
            else if (beans !== "")
                r = TranslationManager.translate("shotplan.recipe.fromBeans", "%1 from %2")
                    .arg(r).arg(beans)

            var rtail = []
            for (var k = 0; k < order.length; k++) {
                switch (order[k]) {
                case "grind":     if (grind !== "") rtail.push(grind); break
                case "roastDate": if (roasted !== "") rtail.push(roasted); break
                }
            }
            return rtail.length > 0 ? (r + blockSep + rtail.join(sep)) : r
        }

        // Fragment format: every present item, in list order.
        var parts = []
        for (var j = 0; j < order.length; j++) {
            switch (order[j]) {
            case "doseYield":
                if (dose !== "") parts.push(dose)
                if (_yieldStr !== "") parts.push(fmt(_yieldStr, true, _yieldOverride))
                break
            case "profile":     if (_profileStr !== "") parts.push(fmt(_profileStr, true)); break
            case "temperature": if (temp !== "") parts.push(temp); break
            case "roaster":     if (_roasterStr !== "") parts.push(fmt(_roasterStr, true)); break
            case "coffee":      if (_coffeeStr !== "") parts.push(fmt(_coffeeStr, true)); break
            case "grind":       if (grind !== "") parts.push(grind); break
            case "roastDate":   if (roasted !== "") parts.push(roasted); break
            }
        }
        return parts.join(sep)
    }

    // Plain: for the accessibility label + `visible: text !== ""`. Always dot-joined —
    // a screen reader hears one clean sentence regardless of the visual format.
    readonly property string text: _build(function(v, live) { return _argSafe(v) }, "  ·  ", "  ·  ")
    // The override-highlight color as a 6-digit hex for the rich <font color> span.
    readonly property string _hlHex: Theme.colorToHex(Theme.highlightColor)

    // Rich: same content, live values bolded, all HTML-escaped; the styled bold safe-dot · separator.
    // An overridden value additionally wraps in the highlight color (per-item, so only the overridden
    // segment lights up — never the whole sentence). Stacked breaks the sentence and its detail tail
    // onto separate lines (display only). A cleaning/descale notice is bolded WHOLE — it's a warning,
    // not a plan.
    readonly property string _rich: {
        var r = _build(function(v, live, overridden) {
            var e = Theme.escapeHtml(_argSafe(v))
            var b = live ? ("<b>" + e + "</b>") : e
            return overridden ? ("<font color=\"" + root._hlHex + "\">" + b + "</font>") : b
        }, Theme.bulletSep, root.stacked ? "<br>" : Theme.bulletSep)
        return (_isCleaning && r !== "") ? ("<b>" + r + "</b>") : r
    }

    readonly property color _color: mouseArea.pressed ? Theme.accentColor
        : (_isCleaning ? Theme.errorColor : Theme.textColor)

    // Natural size = icon + spacing + the text's UNWRAPPED width. Measured on a
    // hidden, width-unconstrained twin (textMeasure): a wrapping/eliding StyledText
    // recomputes its own implicitWidth whenever its width is set, so any binding of
    // planText.width to planText.implicitWidth is a self-referential loop ("Binding
    // loop detected for property width"). The measurer has no width, wrap or elide,
    // so its implicitWidth is layout-independent and the chain stays acyclic.
    // Height follows the text's actual (possibly wrapped) height.
    implicitWidth: planIcon.width + row.spacing + textMeasure.implicitWidth
    implicitHeight: Math.max(planIcon.height, planText.height)

    Text {
        id: textMeasure
        visible: false
        text: root._rich
        textFormat: Text.StyledText
        font: Theme.bodyFont
        Accessible.ignored: true
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingSmall

        ColoredIcon {
            id: planIcon
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/icons/espresso.svg"
            iconWidth: Theme.scaled(20)
            iconHeight: Theme.scaled(20)
            iconColor: root._color
            Accessible.ignored: true
        }

        Text {
            id: planText
            anchors.verticalCenter: parent.verticalCenter
            // Never wider than the granted width leaves room for; never clipped —
            // wrap up to maxLines, then elide. Capped against the measurer's
            // natural width, never this element's own implicitWidth (loop).
            width: Math.max(0, Math.min(textMeasure.implicitWidth,
                root.width - planIcon.width - row.spacing))
            text: root._rich
            textFormat: Text.StyledText
            font: Theme.bodyFont
            color: root._color
            wrapMode: Text.Wrap
            maximumLineCount: root.maxLines
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            Accessible.ignored: true
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        anchors.margins: -Theme.spacingSmall
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
