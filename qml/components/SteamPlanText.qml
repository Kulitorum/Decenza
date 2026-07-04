import QtQuick
import QtQuick.Window
import Decenza
import "../"

// The steam analogue of ShotPlanText: a one-line sentence summarising what the
// next steam will do — "Steam 300g of milk, using the Large pitcher for 30s" — with the live
// values bolded and a leading steam icon. Display-only (no tap target). Renders
// milk weight / pitcher / duration only (no temperature — the app steams by weight
// + duration; the only steam temperature is the boiler setpoint, not a milk target).
Item {
    id: root

    // Mirrors ShotPlanText: "sentence"/"compact" render the same short line; "stacked" lets it wrap.
    // availableWidth (the tile width from ShotPlanItem) caps the text so it never overflows the tile.
    property string format: "sentence"
    property real availableWidth: 0

    // The currently selected steam pitcher preset. getSteamPitcherPreset() is Q_INVOKABLE, so the
    // binding must ALSO read steamPitcherPresets to re-run when the selected pitcher is renamed,
    // disabled, or recalibrated without the selection index itself changing.
    readonly property var _preset: {
        void(Settings.brew.steamPitcherPresets)
        return Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
    }
    readonly property bool _presetOff: !!(_preset && _preset.disabled)
    readonly property string _pitcherName: (_preset && _preset.name) ? String(_preset.name) : ""

    // The milk weight measured this session. Owned by main.qml's sessionMeasuredMilkG —
    // see that property for the full write/reset lifecycle. Mirrored from the window
    // root: seeded on completion / when the window resolves, and refreshed on its
    // sessionMeasuredMilkGChanged signal. (Rename-fragile: if that root property is ever
    // renamed, the ignoreUnknownSignals Connections silently no-ops and this freezes on
    // its last value — hence the one-time warn below to make that failure greppable.)
    readonly property var winRoot: root.Window.window
    property real _sessionMilk: 0
    property bool _warnedMissingMilkProp: false
    function _refreshMilk() {
        if (winRoot && winRoot.sessionMeasuredMilkG === undefined && !_warnedMissingMilkProp) {
            _warnedMissingMilkProp = true
            console.warn("SteamPlanText: window root has no sessionMeasuredMilkG — steam plan milk will not update")
        }
        root._sessionMilk = winRoot ? (winRoot.sessionMeasuredMilkG || 0) : 0
    }
    onWinRootChanged: _refreshMilk()
    Component.onCompleted: _refreshMilk()
    Connections {
        target: root.winRoot
        ignoreUnknownSignals: true
        function onSessionMeasuredMilkGChanged() { root._refreshMilk() }
    }

    // Target milk for the plan: the weight just measured this session (after the bell) if
    // there is one, else the pitcher's calibrated reference, else the last measured milk.
    readonly property real _targetMilk: {
        if (_sessionMilk > 0) return _sessionMilk
        if (_preset && (_preset.calibMilkG || 0) > 0) return _preset.calibMilkG
        return Settings.brew.lastSteamMilkG || 0
    }
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
    readonly property string _rich: _build(function(v, live) {
        var e = Theme.escapeHtml(_argSafe(v))
        return live ? ("<b>" + e + "</b>") : e
    }, Theme.bulletSep)

    // Report the NATURAL width, not row.implicitWidth (which would track the capped text width and
    // ratchet the tile smaller each layout, never re-expanding). See ShotPlanText for the full rationale.
    implicitWidth: Theme.scaled(20) + Theme.spacingSmall + planText.implicitWidth
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
            id: planText
            anchors.verticalCenter: parent.verticalCenter
            // Cap to the available width only when the text would overflow; shorter text keeps its
            // natural width so the centred Row is unaffected.
            width: root.availableWidth > 0
                   ? Math.min(implicitWidth, Math.max(0, root.availableWidth - Theme.scaled(20) - Theme.spacingSmall))
                   : implicitWidth
            text: root._rich
            textFormat: Text.StyledText
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: root.format === "stacked" ? Text.Wrap : Text.NoWrap
            maximumLineCount: root.format === "stacked" ? 3 : 1
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }
}
