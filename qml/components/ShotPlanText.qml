import QtQuick
import Decenza
import "../"

// The brew summary for the home screen. Content is driven by an ordered item
// list (`itemOrder`) and a `sentence` toggle:
//  - sentence ON: "Brew 36.0g of Espresso, using <profile> at 92°C" — the
//    scaffold consumes doseYield's yield, profile, and temperature (wherever
//    they sit in the order; sentence word order belongs to the translated
//    template), and everything else — including doseYield's dose-in fragment —
//    trails after it in list order. Without the profile item (or a profile
//    name) the sentence has no anchor and rendering falls back to fragments.
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

    // Highlight (espresso-button yellow) on a brew TEMPERATURE override only.
    // Yield is intentionally excluded: the target output is dose × ratio and the
    // measured dose never exactly equals the profile's listed dose, so yield would
    // almost always differ — leaving the plan permanently highlighted.
    readonly property bool _tempOverride:
        tempOverridden && Math.abs(overrideTemp - profileTemp) > 0.1

    // --- Per-item segments (empty string = hidden). ---
    // Dose & yield: the shot's target output, plus dose-in (e.g. "18.0g in"). A DELIBERATE yield
    // override (the hasBrewYieldOverride flag, not raw drift — measured dose never exactly matches
    // the profile's) renders as "36.0 → 40.0g" — the yield counterpart of the temperature-override
    // call-out (which uses a delta tag, not an arrow). yieldTargetOnly suppresses the arrow and
    // shows only the effective target ("40.0g"); with no active override it's a visible no-op.
    readonly property string _yieldStr: {
        if (!(_has("doseYield") && targetWeight > 0)) return ""
        if (!yieldTargetOnly && yieldOverridden && profileYield > 0
                && Math.abs(targetWeight - profileYield) > 0.1)
            return profileYield.toFixed(1) + " → " + targetWeight.toFixed(1) + "g"
        return targetWeight.toFixed(1) + "g"
    }
    readonly property string _doseStr: (_has("doseYield") && dose > 0) ? (dose.toFixed(1) + "g") : ""
    // temperatureDisplay() follows the C/F display unit; its Settings.app.temperatureUnit
    // read is in C++, invisible to QML bindings, so read it here.
    readonly property string _profileStr: (_has("profile") && profileName) ? profileName : ""
    readonly property string _tempStr: {
        void(Settings.app.temperatureUnit)
        if (!(_has("temperature") && profileTemp > 0)) return ""
        return ProfileManager.temperatureDisplay(profileTemp, Settings.brew.hasTemperatureOverride, overrideTemp)
    }
    // Roaster = brand only; Coffee = bean name only; Grind = grinder setting + RPM when recorded.
    // Each item gates exactly its named content so saved widget configs mean what they say.
    readonly property string _roasterStr: (_has("roaster") && roasterBrand) ? roasterBrand : ""
    readonly property string _coffeeStr: (_has("coffee") && coffeeName) ? coffeeName : ""
    readonly property string _grindStr: {
        if (!_has("grind")) return ""
        var parts = []
        if (grindSize.length > 0) parts.push(grindSize)
        if (Settings.dye.dyeGrinderRpm > 0)
            parts.push(TranslationManager.translate("equipment.card.lastRpm", "%1 rpm").arg(Settings.dye.dyeGrinderRpm))
        return parts.join(" · ")
    }
    readonly property string _roastDateStr: (_has("roastDate") && roastDate.length > 0) ? roastDate : ""
    // Beverage word from the profile's beverage_type: "Espresso" (the default type),
    // generic "coffee" for filter/pourover/any other coffee type, "tea" for tea
    // profiles ("tea"/"tea_portafilter"). Cleaning/descale profiles get their own
    // sentence in _build() — no bean/dose tail, plus the do-not-load-coffee warning.
    readonly property string _bevType: ProfileManager.currentProfileBeverageType
    // The cleaning/descale/calibrate no-coffee tier is Profile::isMaintenanceBeverageType
    // in C++ — the same call the shot-history, Visualizer and MCP gates make — so this
    // warning genuinely can't drift from them.
    readonly property bool _isCleaning: ProfileManager.currentProfileIsMaintenance
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
    // `_rich` (display), so they can NEVER drift. fmt(value, live) formats one value: plain %-escapes,
    // rich HTML-escapes and bolds live values. blockSep separates the sentence from its detail tail —
    // the same `sep` normally, a line break when stacked on the DISPLAY path only (the a11y `text`
    // always passes the dot separator so the spoken string stays one sentence).
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
        var order = itemOrder || []

        // Sentence format needs the profile as its anchor; without it (item removed
        // or no profile name) fall through to the fragment list. Word order inside
        // the scaffold belongs to the translated template — the consumed items
        // (doseYield's yield, profile, temperature) ignore their list positions.
        if (sentence && _profileStr !== "") {
            var s
            if (_yieldStr !== "" && _tempStr !== "")
                s = TranslationManager.translate("shotplan.sentence", "Brew %1 of %2, using %3 at %4")
                    .arg(fmt(_yieldStr, true)).arg(fmt(_beverage, false)).arg(fmt(_profileStr, true)).arg(fmt(_tempStr, true))
            else if (_yieldStr === "" && _tempStr !== "")
                s = TranslationManager.translate("shotplan.sentenceNoYield", "Brew %1, using %2 at %3")
                    .arg(fmt(_beverage, false)).arg(fmt(_profileStr, true)).arg(fmt(_tempStr, true))
            else if (_yieldStr !== "")
                s = TranslationManager.translate("shotplan.sentenceNoTemp", "Brew %1 of %2, using %3")
                    .arg(fmt(_yieldStr, true)).arg(fmt(_beverage, false)).arg(fmt(_profileStr, true))
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

        // Fragment format: every present item, in list order.
        var parts = []
        for (var j = 0; j < order.length; j++) {
            switch (order[j]) {
            case "doseYield":
                if (dose !== "") parts.push(dose)
                if (_yieldStr !== "") parts.push(fmt(_yieldStr, true))
                break
            case "profile":     if (_profileStr !== "") parts.push(fmt(_profileStr, true)); break
            case "temperature": if (_tempStr !== "") parts.push(fmt(_tempStr, true)); break
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
    // Rich: same content, live values bolded, all HTML-escaped; the styled bold safe-dot · separator.
    // Stacked breaks the sentence and its detail tail onto separate lines (display only).
    // A cleaning/descale notice is bolded WHOLE — it's a warning, not a plan.
    readonly property string _rich: {
        var r = _build(function(v, live) {
            var e = Theme.escapeHtml(_argSafe(v))
            return live ? ("<b>" + e + "</b>") : e
        }, Theme.bulletSep, root.stacked ? "<br>" : Theme.bulletSep)
        return (_isCleaning && r !== "") ? ("<b>" + r + "</b>") : r
    }

    readonly property color _color: mouseArea.pressed ? Theme.accentColor
        : (_isCleaning ? Theme.errorColor
                       : (_tempOverride ? Theme.highlightColor : Theme.textColor))

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
