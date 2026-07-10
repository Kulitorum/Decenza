import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

// Layout widget: grinder-setting quick-select (composable-brew-bar).
// Shows the current grind setting as a pill; tapping opens a value picker of
// grind settings at current ±5 steps (11 entries, current centered). Tapping a
// value writes it to Settings.dye.dyeGrinderSetting (the same path the Brew
// Settings +/- controls use, which write-through to the active bag/package).
//
// RPM-capable grinders (Settings.dye.grinderRpmCapable(brand, model) — the same
// check BrewDialog uses to reveal its dedicated RPM field) dial in via motor
// RPM, not burr position. For those the pill instead steps Settings.dye
// .dyeGrinderRpm (an int) by a fixed RPM increment, so the widget adjusts the
// parameter the user actually turns. Non-RPM grinders step dyeGrinderSetting
// exactly as before.
//
// Pure layout widget: no AI / feedback dependencies, so it can be
// cherry-picked cleanly onto upstream/main. In burr-setting mode it works
// whether the grinder encodes its setting as a NUMBER ("31"), a number embedded
// in text ("C4", "4F"), or pure LETTERS ("F"); anything else falls back to the
// user's observed grind settings for the active grinder from shot history.
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})
    property color zoneTextColor: Theme.textColor
    property bool zoneValueBold: false

    // Active grinder identity (both carry NOTIFY, so isRpmMode stays reactive
    // when the user switches equipment — mirrors BrewDialog.equipmentRpmCapable).
    readonly property string grinderBrand: String(Settings.dye.dyeGrinderBrand || "")
    readonly property string grinderModel: String(Settings.dye.dyeGrinderModel || "")
    // RPM-capable grinders dial in by motor RPM (BrewDialog shows a dedicated RPM
    // field for exactly these). In that case the pill steps dyeGrinderRpm.
    //
    // Also require an RPM to be set: grinderRpmCapable() returns true for ANY
    // grinder not in the registry (deriveRpmCapable: unknown -> true), so a
    // burr-dialed custom grinder would otherwise be forced into RPM mode with a
    // dead blank picker. Gating on dyeGrinderRpm > 0 keeps such grinders on burr
    // stepping until the user actually sets an RPM (in BrewDialog); a genuine RPM
    // grinder with rpm unset couldn't step from blank anyway, so nothing is lost.
    // BrewDialog dodges this by showing BOTH fields; this pill is either/or.
    readonly property bool isRpmMode: Settings.dye.grinderRpmCapable(grinderBrand, grinderModel)
                                      && Settings.dye.dyeGrinderRpm > 0

    readonly property string labelText: isRpmMode
        ? TranslationManager.translate("grind.quickSelect.rpmLabel", "RPM")
        : TranslationManager.translate("grind.quickSelect.label", "Grind")

    // Current dial-in as a STRING, mode-aware:
    //   burr mode — dyeGrinderSetting (numeric, letters, or mixed).
    //   rpm  mode — dyeGrinderRpm (an int; 0 = unset → empty).
    readonly property string currentSetting: isRpmMode
        ? (Settings.dye.dyeGrinderRpm > 0 ? String(Settings.dye.dyeGrinderRpm) : "")
        : String(Settings.dye.dyeGrinderSetting || "")
    readonly property string valueText: currentSetting.length > 0
        ? currentSetting
        : TranslationManager.translate("grind.quickSelect.unset", "—")

    // Fixed RPM step (rpm mode only). RPM-capable grinders (e.g. Turin DF83V/DF64V)
    // run ~600–1400 RPM where ~50 RPM is a meaningful dial-in change, so ±5 steps
    // span ~±250 RPM — a useful picker range. The global grindQuickSelectStep
    // (default 1.0, range 0.1–5.0) is a BURR-POSITION step and would give a
    // useless ~10 RPM span, so RPM mode deliberately ignores it. Tunable.
    readonly property int rpmStep: 50

    // Global configurable step (burr-setting numeric mode only), edited in
    // Settings. Default 1.0.
    readonly property double grindStep: (Settings.brew.grindQuickSelectStep > 0)
        ? Settings.brew.grindQuickSelectStep : 1.0

    implicitWidth: col.implicitWidth
    implicitHeight: col.implicitHeight

    // --- Stepping algorithm ---------------------------------------------------
    // Return the grind setting `n` steps from `currentString` (n in -5..+5),
    // or "" to skip (out of range / unparseable). Handles four cases in order:
    //   1. pure numeric      "31"          -> +/- n*step, format by step's decimals
    //   2. number-in-text    "C4" / "4F"   -> step the numeric group, re-wrap
    //   3. pure letters       "F"          -> step the LAST letter ordinal, clamp A..Z
    //   4. unparseable       "medium-fine" -> "" (widget falls back to history)
    function _stepDecimals(step) {
        // Decimal places come from the STEP, not the current value:
        // 1.0 -> 0 ("31"), 0.5 -> 1 ("30.5"), 0.25 -> 2.
        // Sanitize float-dirty steps: String(0.30) === "0.30000000000000004" would
        // otherwise yield 17 decimals and render/persist a garbage grind label (the
        // picked label string is written straight to dyeGrinderSetting). Bound to 3
        // decimals and strip trailing zeros so labels + the persisted value stay clean.
        var s = Number(step).toFixed(3).replace(/0+$/, "").replace(/\.$/, "")
        var dot = s.indexOf(".")
        return dot < 0 ? 0 : (s.length - dot - 1)
    }

    function _fmtNum(v, step) {
        return v.toFixed(_stepDecimals(step))
    }

    function stepGrind(currentString, n, step) {
        var s = String(currentString == null ? "" : currentString).trim()
        if (s.length === 0)
            return ""

        // 1. Pure numeric.
        if (/^-?\d+(\.\d+)?$/.test(s)) {
            var v = parseFloat(s) + n * step
            if (v < 0) return ""              // below 0 -> skip
            return _fmtNum(v, step)
        }

        // 2. Number embedded in text: optional non-digit prefix + number + suffix.
        var m = s.match(/^(\D*)(\d+(?:\.\d+)?)(\D*)$/)
        if (m) {
            var nv = parseFloat(m[2]) + n * step
            if (nv < 0) nv = 0               // clamp numeric >= 0
            return m[1] + _fmtNum(nv, step) + m[3]
        }

        // 3. Pure letters (1..3 chars): step the LAST character by ordinal,
        //    clamp to A..Z (no wrap), preserve case and any leading chars.
        if (/^[A-Za-z]{1,3}$/.test(s)) {
            var last = s.charAt(s.length - 1)
            var isUpper = last === last.toUpperCase()
            var base = isUpper ? 65 : 97      // 'A' / 'a'
            var ord = last.charCodeAt(0) - base + n
            if (ord < 0) ord = 0
            if (ord > 25) ord = 25
            return s.substring(0, s.length - 1) + String.fromCharCode(base + ord)
        }

        // 4. Unparseable.
        return ""
    }

    // --- List generation ------------------------------------------------------
    // 11 rows for n = -5..+5 via stepGrind, dropping "" and de-duplicating
    // (clamping at the low/letter edge collapses several n's onto one value).
    // Each row: { value, isCurrent }. If <= 2 distinct rows generate, fall back
    // to observed history for the active grinder; if that is empty too, show
    // just the current value.
    function _observedFallback() {
        var out = []
        var model = String(Settings.dye.dyeGrinderModel || "")
        var observed = (MainController.shotHistory && model.length > 0)
            ? MainController.shotHistory.getDistinctGrinderSettingsForGrinder(model)
            : []
        if (!observed || observed.length === 0) {
            if (root.currentSetting.length > 0)
                out.push({ value: root.currentSetting, isCurrent: true })
            return out
        }
        // observed is grinder-sorted ascending. Find the current value's slot
        // and take ~5 below and ~5 above (current always included/highlighted).
        var list = observed.slice()
        var idx = list.indexOf(root.currentSetting)
        if (idx < 0) {
            // Current not in history: prepend it, highlight it, plus first ~10.
            out.push({ value: root.currentSetting, isCurrent: true })
            for (var k = 0; k < list.length && out.length < 11; k++)
                out.push({ value: list[k], isCurrent: false })
            return out
        }
        var lo = Math.max(0, idx - 5)
        var hi = Math.min(list.length - 1, idx + 5)
        for (var i = lo; i <= hi; i++)
            out.push({ value: list[i], isCurrent: i === idx })
        return out
    }

    // Bumped when the async distinct-value cache refreshes, so the fallback
    // list re-evaluates once shot history for this grinder finishes loading
    // (getDistinctGrinderSettingsForGrinder is cache-backed + async, mirroring
    // BrewDialog's _distinctCacheVersion guard).
    property int _distinctCacheVersion: 0
    Connections {
        target: MainController.shotHistory
        function onDistinctCacheReady() { root._distinctCacheVersion++ }
    }

    // RPM rows: current ±5 * rpmStep, clamped at >= 0, de-duplicated. Pure
    // integers (no history fallback — observed history is burr-setting-specific).
    function _rpmRows() {
        var base = Settings.dye.dyeGrinderRpm
        if (base <= 0)
            return [{ value: root.currentSetting, isCurrent: true }]  // unset -> just show "—"/current
        var out = []
        var seen = ({})
        for (var n = -5; n <= 5; n++) {
            var rpm = base + n * root.rpmStep
            if (rpm < 0) continue
            var v = String(rpm)
            if (seen[v]) continue
            seen[v] = true
            out.push({ value: v, isCurrent: n === 0 })
        }
        return out
    }

    readonly property var rows: {
        // Reference for reactivity across setting + translation changes and the
        // async history cache (only consumed by the burr-mode fallback path).
        var _ = TranslationManager.translationVersion
        var __ = root._distinctCacheVersion
        // Track the live dial-in so rows recompute on setting/RPM changes
        // (currentSetting reflects dyeGrinderSetting or dyeGrinderRpm per mode).
        var ___ = root.currentSetting

        if (root.isRpmMode)
            return root._rpmRows()

        var cur = root.currentSetting
        var step = root.grindStep

        var generated = []
        var seen = ({})
        for (var n = -5; n <= 5; n++) {
            var v = root.stepGrind(cur, n, step)
            if (v === "" || v === undefined) continue
            if (seen[v]) continue
            seen[v] = true
            // Highlight the current row by n === 0 (string equality would miss
            // reformatted values, e.g. "30" -> "30.0" at step 0.5).
            generated.push({ value: v, isCurrent: n === 0 })
        }

        if (generated.length <= 2)
            return root._observedFallback()
        return generated
    }

    function applyValue(v) {
        // Plain property write: the SettingsDye setter write-through keeps the
        // active bag + package last-used dial-in current (coffee_bags), matching
        // the Brew Settings and Post-Shot Review write path. No dose/target/temp
        // re-apply (that belongs to Brew Settings, not a grind-only widget).
        if (!v || v.length === 0)
            return
        if (root.isRpmMode) {
            // dyeGrinderRpm is an int property; parse the picked label. Plain
            // assignment (NOT a setter call) so the write-through in the C++
            // setter fires — the same rule as dyeGrinderSetting.
            var rpm = parseInt(v)
            if (rpm > 0)
                Settings.dye.dyeGrinderRpm = rpm
        } else {
            Settings.dye.dyeGrinderSetting = v
        }
    }

    ColumnLayout {
        id: col
        anchors.centerIn: parent
        width: parent.width
        spacing: Theme.scaled(2)

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.labelText
            color: root.zoneTextColor
            font: Theme.labelFont
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            // Brew-bar symmetry: never narrower than a "1:X.X" Ratio Quick-Select pill (same font +
            // padding formula), so the Grind and Ratio pills line up at equal width side by side.
            Layout.preferredWidth: Math.max(grindValue.implicitWidth, grindPillRef.implicitWidth)
                                   + Theme.spacingMedium * 2
            Layout.preferredHeight: Theme.scaled(32)
            radius: height / 2
            color: grindMa.pressed ? Qt.darker(root.zoneTextColor, 1.15) : root.zoneTextColor

            Accessible.role: Accessible.Button
            Accessible.name: root.labelText + " " + root.valueText + ". "
                             + TranslationManager.translate("grind.quickSelect.tapToChange", "Tap to change")
            Accessible.focusable: true
            Accessible.onPressAction: grindMa.clicked(null)

            // Hidden width reference: a representative ratio value in the identical font, measured only
            // (never drawn), so the grind pill is at least as wide as a ratio pill for brew-bar parity.
            Text {
                id: grindPillRef
                visible: false
                text: "1:2.0"
                font.pixelSize: Theme.scaled(20)
                font.bold: true
            }
            Text {
                id: grindValue
                anchors.centerIn: parent
                text: root.valueText
                color: Theme.primaryColor
                font.pixelSize: Theme.scaled(20)
                font.bold: true
            }
            MouseArea { id: grindMa; anchors.fill: parent; onClicked: grindDialog.open() }
        }
    }

    GrindPickerDialog {
        id: grindDialog
        rows: root.rows
        // Finer/Coarser annotation is burr-position semantics; RPM->grind
        // direction is grinder-specific and ambiguous, so suppress it there.
        finerHint: !root.isRpmMode && root.grindStep > 0 && root.currentSetting.length > 0
        onValuePicked: function(v) { root.applyValue(v) }
    }
}
