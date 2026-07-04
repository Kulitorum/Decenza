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

    // The currently selected steam pitcher preset. getSteamPitcherPreset() is Q_INVOKABLE, so the
    // binding must ALSO read steamPitcherPresets to re-run when the selected pitcher is renamed,
    // disabled, or recalibrated without the selection index itself changing.
    readonly property var _preset: {
        void(Settings.brew.steamPitcherPresets)
        return Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
    }
    readonly property bool _presetOff: !!(_preset && _preset.disabled)
    readonly property string _pitcherName: (_preset && _preset.name) ? String(_preset.name) : ""

    // The milk weight measured this session (set on capture / the bell, reset on pitcher change and
    // session end). Mirrored from the window root: seeded on completion / when the window resolves, and
    // refreshed on its sessionMeasuredMilkGChanged signal. (Rename-fragile: if that root property is ever
    // renamed, the ignoreUnknownSignals Connections silently no-ops and this freezes on its last value.)
    readonly property var winRoot: root.Window.window
    property real _sessionMilk: 0
    function _refreshMilk() { root._sessionMilk = winRoot ? (winRoot.sessionMeasuredMilkG || 0) : 0 }
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
    readonly property int _duration: Settings.brew.steamTimeout

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
        if (_milkStr !== "" && _pitcherName !== "" && _durStr !== "")
            return TranslationManager.translate("steamplan.sentence", "Steam %1 of milk, using the %2 pitcher for %3")
                .arg(fmt(_milkStr, true)).arg(fmt(_pitcherName, true)).arg(fmt(_durStr, true))
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
    }, " <font size=\"+1\"><b>·</b></font> ")

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    // Always wrapped by SteamPlanItem / PlanItem, which already expose a StaticText a11y node — so ignore
    // this role-less root to avoid a duplicate nested announcement (ShotPlanText gets the same effect from
    // its role-less root, which needs no explicit flag).
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
