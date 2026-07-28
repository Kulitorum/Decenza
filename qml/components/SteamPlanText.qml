import QtQuick
import Decenza
import "../"

// The steam analogue of ShotPlanText: a one-line sentence summarising what the
// next steam will do — "Steam 300g of milk, using the Large pitcher for 30s" — with the live
// values bolded and a leading steam icon. Display-only (no tap target). Renders
// milk weight / pitcher / duration only (no temperature — the app steams by weight
// + duration; the only steam temperature is the boiler setpoint, not a milk target).
Item {
    id: root

    // The currently selected steam pitcher preset. getSteamPitcherPreset() is Q_INVOKABLE, so the
    // binding must ALSO read steamPitcherPresets to re-run when the selected pitcher is renamed,
    // disabled, or recalibrated without the selection index itself changing.
    readonly property var _preset: {
        void(Settings.brew.steamPitcherPresets)
        return Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
    }
    readonly property bool _presetOff: !!(_preset && _preset.disabled)
    readonly property string _pitcherName: (_preset && _preset.name) ? String(_preset.name) : ""

    // Target milk for the plan: the weight just measured this session (after the bell) if
    // there is one, else the last measured milk. (The old per-pitcher calibMilkG is no
    // longer consulted — weight-timing is now a global rate, and calibMilkG has no UI
    // left to edit, so it would freeze the plan on a stale reference weight.)
    //
    // AppShell.sessionMeasuredMilkG is a plain reactive binding. It used to be main.qml's
    // property, reached through `Window.window` — an untyped hop that needed a winRoot
    // mirror, a manual refresh on two events, an `ignoreUnknownSignals` Connections, and
    // a one-time console.warn to make a rename greppable. All of that is gone: a rename
    // now fails the build here instead of silently freezing the plan on a stale value.
    readonly property real _targetMilk:
        AppShell.sessionMeasuredMilkG > 0 ? AppShell.sessionMeasuredMilkG
                                          : (Settings.brew.lastSteamMilkG || 0)
    // The SELECTED preset's effective time (scaled when weight-timing has milk to work
    // with, else its base duration) — not Settings.brew.steamTimeout, which holds
    // whatever the last pill tap computed and can be stale for a fresh selection.
    // effectiveSteamDurationSec() reads preset data + the weight-timing toggle in C++,
    // so re-read both here to keep the binding live.
    readonly property int _duration: {
        void(Settings.brew.steamPitcherPresets)
        void(Settings.brew.milkAutoCaptureEnabled)
        return Settings.brew.effectiveSteamDurationSec(Settings.brew.selectedSteamPitcher, _targetMilk)
    }

    readonly property string _milkStr: _targetMilk > 0 ? (_targetMilk.toFixed(0) + "g") : ""
    readonly property string _durStr: _duration > 0 ? (_duration + "s") : ""

    // Break a "%N" token in a user value so QString.arg can't substitute a later arg into it (a pitcher
    // literally named e.g. "50% off"). The zero-width space is invisible.
    function _argSafe(v) { return String(v).replace(/%(\d)/g, "%\u200B$1") }

    // ONE renderer for both the plain `text` (a11y label + `visible: text !== ""`) and the bolded `_rich`
    // (display), so they can't drift. fmt(value, live) formats one value: plain %-escapes, rich HTML-escapes
    // and bolds live values. Disabled ("Off") preset ⇒ "" ⇒ hidden.
    function _build(fmt, sep) {
        var _ = TranslationManager.translationVersion
        if (_presetOff) return ""
        if (_milkStr !== "" && _pitcherName !== "" && _durStr !== "") {
            // Pills display presets as "Small Pitcher" etc., so users name them that way —
            // don't render "…the Large Pitcher pitcher". Separate full template (not string
            // surgery) so translators control word order in both forms.
            var tpl = _pitcherName.toLowerCase().indexOf("pitcher") >= 0
                ? TranslationManager.translate("steamplan.sentenceNamedPitcher", "Steam %1 of milk, using the %2 for %3")
                : TranslationManager.translate("steamplan.sentence", "Steam %1 of milk, using the %2 pitcher for %3")
            return tpl.arg(fmt(_milkStr, true)).arg(fmt(_pitcherName, true)).arg(fmt(_durStr, true))
        }
        // Degrade gracefully when a piece is missing.
        var parts = []
        if (_milkStr !== "") parts.push(fmt(_milkStr, true))
        if (_pitcherName !== "") parts.push(fmt(_pitcherName, true))
        if (_durStr !== "") parts.push(fmt(_durStr, true))
        if (parts.length === 0) return ""
        return fmt(TranslationManager.translate("steamplan.prefix", "Steam"), false) + " " + parts.join(sep)
    }

    // Plain: for the accessibility label + `visible: text !== ""`.
    readonly property string text: _build(function(v, live) { return _argSafe(v) }, "  ·  ")
    // Rich: same content, live values bolded, all HTML-escaped; styled bold safe-dot · separator.
    // Same treatment as ShotPlanText: escaping makes user text safe as markup but leaves emoji
    // as raw codepoints for the text renderer. Pitcher names are user-supplied.
    readonly property string _rich: Theme.replaceEmojiWithImg(_build(function(v, live) {
        var e = Theme.escapeHtml(_argSafe(v))
        return live ? ("<b>" + e + "</b>") : e
    }, Theme.bulletSep), Theme.bodyFont.pixelSize, true)

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    // Always wrapped by ShotPlanItem, which already exposes the a11y node for the plan.
    // Role-less Items are skipped by the a11y tree anyway (which is why ShotPlanText's
    // root needs no flag); this is deliberate belt-and-braces so adding a role here
    // later can't silently create a duplicate nested announcement.
    Accessible.ignored: true

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingSmall

        ColoredIcon {
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/icons/steam.svg"
            iconWidth: Theme.scaled(20)
            iconHeight: Theme.scaled(20)
            iconColor: Theme.textColor
            Accessible.ignored: true
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root._rich
            textFormat: Text.StyledText
            font: Theme.bodyFont
            color: Theme.textColor
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }
}
