import QtQuick
import QtQuick.Window
import Decenza
import "../"

// The steam analogue of ShotPlanText: a one-line sentence summarising what the
// next steam will do — "Steam 300g of milk, using Large for 30s" — with the live
// values bolded and a leading steam icon. Display-only (no tap target). Renders
// milk weight / pitcher / duration only (no temperature).
Item {
    id: root

    // The currently selected steam pitcher preset (re-reads when the selection changes).
    readonly property var _preset: Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
    readonly property bool _presetOff: !!(_preset && _preset.disabled)
    readonly property string _pitcherName: (_preset && _preset.name) ? String(_preset.name) : ""

    // The milk weight measured this session (set on capture / the bell, reset on pitcher
    // change and session end). Mirrored imperatively from the window root — reading a
    // sub-property through a var-typed intermediate doesn't register a binding dependency.
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

    // Plain sentence — exposed as `text` for accessibility and the visibility check.
    readonly property string text: {
        var _ = TranslationManager.translationVersion
        if (_presetOff) return ""
        if (_milkStr !== "" && _pitcherName !== "" && _durStr !== "")
            return TranslationManager.translate("steamplan.sentence", "Steam %1 of milk, using the %2 pitcher for %3")
                .arg(_milkStr).arg(_pitcherName).arg(_durStr)
        // Degrade gracefully when a piece is missing.
        var parts = []
        if (_milkStr !== "") parts.push(_milkStr)
        if (_pitcherName !== "") parts.push(_pitcherName)
        if (_durStr !== "") parts.push(_durStr)
        if (parts.length === 0) return ""
        return TranslationManager.translate("steamplan.prefix", "Steam") + " " + parts.join("  •  ")
    }

    // Rich version with the live values bolded (parts HTML-escaped).
    readonly property string _rich: {
        var _ = TranslationManager.translationVersion
        function b(s) { return "<b>" + Theme.escapeHtml(s) + "</b>" }
        if (_presetOff) return ""
        if (_milkStr !== "" && _pitcherName !== "" && _durStr !== "")
            return TranslationManager.translate("steamplan.sentence", "Steam %1 of milk, using the %2 pitcher for %3")
                .arg(b(_milkStr)).arg(b(_pitcherName)).arg(b(_durStr))
        var parts = []
        if (_milkStr !== "") parts.push(_milkStr)
        if (_pitcherName !== "") parts.push(_pitcherName)
        if (_durStr !== "") parts.push(_durStr)
        if (parts.length === 0) return ""
        return Theme.escapeHtml(TranslationManager.translate("steamplan.prefix", "Steam")) + " " + Theme.joinWithBullet(parts)
    }

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    // Always wrapped by SteamPlanItem / PlanItem, which already expose a StaticText
    // a11y node — so ignore this root to avoid a duplicate nested announcement.
    // (ShotPlanText achieves the same effect by being a role-less Item; here the
    // root is an Item with content, so we mark it ignored explicitly.)
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
