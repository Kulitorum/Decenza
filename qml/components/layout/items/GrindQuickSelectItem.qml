import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

// Layout widget: grinder dial-in quick-select (composable-brew-bar).
// Shows the current dial-in as a pill; tapping opens a two-section value picker.
//
// A variable-RPM grinder's dial-in has TWO components — the burr grind setting
// AND the motor RPM — so the widget shows BOTH rather than toggling between
// them. The pill shows the grind alone for a non-RPM grinder (or when no RPM is
// recorded), and "<grind> · <rpm>" when the grinder is RPM-capable and an RPM is
// set. The picker has a Grind section always and an RPM section when the grinder
// is RPM-capable; picking a row writes only that half (dyeGrinderSetting or
// dyeGrinderRpm), via the same write-through the Brew Settings controls use.
//
// RPM-capability uses the broad grinderRpmCapable() (matching BrewDialog's RPM
// field), not the narrow catalog-confirmed check: pairing always shows grind, so
// the old "don't force an unknown grinder into RPM-only mode" concern is moot.
//
// Both steps are history-derived (grindStepForGrinder / grindRpmStepForGrinder),
// so the picker steps the grinder the way the user actually dials it. There is no
// user-facing step setting — the step is a property of the grinder.
//
// Pure layout widget: no AI / feedback dependencies. The grind section works
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

    // Active grinder identity (both carry NOTIFY, so rpmCapable stays reactive
    // when the user switches equipment — mirrors BrewDialog.equipmentRpmCapable).
    readonly property string grinderBrand: String(Settings.dye.dyeGrinderBrand || "")
    readonly property string grinderModel: String(Settings.dye.dyeGrinderModel || "")

    // Broad RPM-capability gate (unknown/custom grinders count too), matching
    // BrewDialog. When true, the pill shows the RPM half and the picker gains an
    // RPM section. Grind is always shown regardless.
    readonly property bool rpmCapable: Settings.dye.grinderRpmCapable(grinderBrand, grinderModel)

    readonly property string labelText:
        TranslationManager.translate("grind.quickSelect.label", "Grind")

    // The two dial-in halves.
    readonly property string grindSetting: String(Settings.dye.dyeGrinderSetting || "")
    readonly property int rpmValue: Settings.dye.dyeGrinderRpm > 0 ? Settings.dye.dyeGrinderRpm : 0
    readonly property bool showRpm: rpmCapable && rpmValue > 0

    // Combined pill text: "<grind> · <rpm>" when both are present; grind alone,
    // or (grind unset but rpm set) the rpm alone; "—" when neither is set.
    readonly property string valueText: {
        if (grindSetting.length > 0 && showRpm)
            return grindSetting + " · " + rpmValue
        if (grindSetting.length > 0)
            return grindSetting
        if (showRpm)
            return String(rpmValue)
        return TranslationManager.translate("grind.quickSelect.unset", "—")
    }
    // Spoken form for screen readers — announce both halves explicitly.
    readonly property string accessibleValue: {
        var parts = []
        if (grindSetting.length > 0)
            parts.push(grindSetting)
        if (showRpm)
            parts.push(rpmValue + " RPM")
        return parts.length > 0 ? parts.join(", ")
            : TranslationManager.translate("grind.quickSelect.unset", "—")
    }

    // RPM step, derived from the user's own shot history for the active grinder —
    // the typical increment between the RPMs they've actually dialed (shots.rpm),
    // via the same noise-filtered estimator as grindStep. Falls back to 50 when
    // history is too thin, or while the distinct-value cache is still cold
    // (recomputes on _distinctCacheVersion below). 50 suits the ~600–1400 RPM
    // working range, where ±5 steps span ~±250 RPM.
    readonly property int rpmStep: {
        var __ = root._distinctCacheVersion
        var s = MainController.shotHistory
            ? MainController.shotHistory.grindRpmStepForGrinder(root.grinderModel) : 0
        return s > 0 ? Math.round(s) : 50
    }

    // Neutral starting anchor for an RPM grinder whose dyeGrinderRpm is still
    // unset (0): the RPM section centres here so it is adjustable on the first
    // tap. Only a seed for the offered rows — nothing is written until picked.
    readonly property int rpmDefaultAnchor: 1000

    // Per-grinder grind step, derived from the user's own shot history for the
    // ACTIVE grinder by the same noise-filtered estimator the AI dialing context
    // uses — so the picker steps the grinder the way the user actually dials it
    // (e.g. 0.25 on a Niche Zero), not in whole numbers. Falls back to 1.0 when
    // history is too thin, or while the cache is cold. With no grinder selected,
    // grindStepForGrinder("") derives from the full cross-grinder history.
    readonly property double grindStep: {
        var __ = root._distinctCacheVersion
        var s = MainController.shotHistory
            ? MainController.shotHistory.grindStepForGrinder(root.grinderModel) : 0
        return s > 0 ? s : 1.0
    }

    implicitWidth: col.implicitWidth
    implicitHeight: col.implicitHeight

    // --- Stepping algorithm ---------------------------------------------------
    // Return the grind setting `n` steps from `currentString` (n in -5..+5),
    // or "" to skip (out of range / unparseable). Registry grinders route
    // through the catalog first (numeric AND Compound "a+b" rotation dials —
    // Eureka Mignon/Atom/Helios, 1Zpresso — via SettingsDye.stepGrinderSetting,
    // the same parse/format math the AI dialing block uses). For grinders NOT in
    // the registry, or catalog values it can't parse, the JS fallbacks handle:
    //   1. pure numeric      "31"          -> +/- n*step, format by step's decimals
    //   2. number-in-text    "C4" / "4F"   -> step the numeric group, re-wrap
    //   3. pure letters       "F"          -> step the LAST letter ordinal, clamp A..Z
    //   4. unparseable       "medium-fine" -> "" (widget falls back to history)
    function _stepDecimals(step) {
        // Decimal places come from the STEP, not the current value:
        // 1.0 -> 0 ("31"), 0.5 -> 1 ("30.5"), 0.25 -> 2.
        // Sanitize float-dirty steps: an accumulated step like String(0.1 + 0.2)
        // === "0.30000000000000004" would otherwise yield 17 decimals and
        // render/persist a garbage grind label (the picked label string is written
        // straight to dyeGrinderSetting). Bound to 3 decimals and strip trailing
        // zeros so labels + the persisted value stay clean.
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

        // 0. Catalog-first: registry grinders (numeric AND compound rotation
        //    "a+b") step through the notation-aware pipeline, which round-trips
        //    both notations and handles rev/position carry-borrow. Returns "" for
        //    a custom grinder, an unparseable value, or a step below the dial
        //    floor — then the JS branches below take over unchanged.
        var viaCatalog = Settings.dye.stepGrinderSetting(root.grinderBrand, root.grinderModel,
                                                         s, n * step, _stepDecimals(step))
        if (viaCatalog && viaCatalog.length > 0)
            return viaCatalog

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
            if (root.grindSetting.length > 0)
                out.push({ value: root.grindSetting, isCurrent: true })
            return out
        }
        // observed is grinder-sorted ascending. Find the current value's slot
        // and take ~5 below and ~5 above (current always included/highlighted).
        var list = observed.slice()
        var idx = list.indexOf(root.grindSetting)
        if (idx < 0) {
            // Current not in history: prepend it, highlight it, plus first ~10.
            out.push({ value: root.grindSetting, isCurrent: true })
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

    // Bumped when the async distinct-value cache refreshes, so the derived steps
    // and history fallback re-evaluate once shot history finishes loading
    // (grindStepForGrinder / getDistinctGrinderSettingsForGrinder are cache-backed
    // + async, mirroring BrewDialog's _distinctCacheVersion guard).
    property int _distinctCacheVersion: 0
    Connections {
        target: MainController.shotHistory
        function onDistinctCacheReady() { root._distinctCacheVersion++ }
    }

    // RPM rows: current ±5 * rpmStep, clamped at >= 0, de-duplicated. Pure
    // integers (no history fallback — observed history is burr-setting-specific).
    function _rpmRows() {
        // When the grinder's RPM is unset, seed from a neutral anchor so the RPM
        // section is adjustable on the first tap. Nothing is written until the
        // user picks; the current row is only highlighted when a real RPM is set.
        var rpmSet = Settings.dye.dyeGrinderRpm > 0
        var base = rpmSet ? Settings.dye.dyeGrinderRpm : root.rpmDefaultAnchor
        var out = []
        var seen = ({})
        for (var n = -5; n <= 5; n++) {
            var rpm = base + n * root.rpmStep
            if (rpm < 0) continue
            var v = String(rpm)
            if (seen[v]) continue
            seen[v] = true
            out.push({ value: v, isCurrent: rpmSet && n === 0 })
        }
        return out
    }

    // Grind rows (burr section): current ±5 via stepGrind, deduped, with the
    // observed-history fallback when stepping yields too few distinct values.
    readonly property var grindRows: {
        // Reactivity refs: translations, async history cache, live grind setting,
        // and grinder identity (the step values depend on the grinder's notation
        // via stepGrinderSetting, so a grinder switch must re-evaluate).
        var _ = TranslationManager.translationVersion
        var __ = root._distinctCacheVersion
        var ___ = root.grindSetting
        var ____ = root.grinderBrand + " " + root.grinderModel

        var cur = root.grindSetting
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

    // RPM rows (only when the grinder is RPM-capable).
    readonly property var rpmRows: {
        var __ = root._distinctCacheVersion
        var ___ = Settings.dye.dyeGrinderRpm
        var ____ = root.grinderBrand + " " + root.grinderModel
        if (!root.rpmCapable)
            return []
        return root._rpmRows()
    }

    function applyGrind(v) {
        // Plain property write: the SettingsDye setter write-through keeps the
        // active bag + package last-used dial-in current (coffee_bags), matching
        // the Brew Settings and Post-Shot Review write path.
        if (!v || v.length === 0)
            return
        Settings.dye.dyeGrinderSetting = v
    }

    function applyRpm(v) {
        // dyeGrinderRpm is an int property; parse the picked label. Plain
        // assignment (NOT a setter call) so the write-through in the C++ setter
        // fires — the same rule as dyeGrinderSetting.
        if (!v || v.length === 0)
            return
        var rpm = parseInt(v)
        if (rpm > 0)
            Settings.dye.dyeGrinderRpm = rpm
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
            // Over a background image the solid capsule reads as an opaque white
            // chip on the photo; render it transparent so the value sits on the
            // background like the Beans/Milk widgets. Without a background image
            // keep the existing solid pill (zone-color fill, accent text).
            readonly property bool hasBackgroundImage: Settings.theme.backgroundImagePath.length > 0
            color: hasBackgroundImage
                ? "transparent"
                : (grindMa.pressed ? Qt.darker(root.zoneTextColor, 1.15) : root.zoneTextColor)

            Accessible.role: Accessible.Button
            Accessible.name: root.labelText + " " + root.accessibleValue + ". "
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
                // Accent text reads on the solid pill; over a background image the
                // pill is transparent, so the value uses the zone text color to
                // read against the photo (matching Beans/Milk).
                color: parent.hasBackgroundImage ? root.zoneTextColor : Theme.primaryColor
                font.pixelSize: Theme.scaled(20)
                font.bold: true
            }
            MouseArea { id: grindMa; anchors.fill: parent; onClicked: grindDialog.open() }
        }
    }

    GrindPickerDialog {
        id: grindDialog
        grindRows: root.grindRows
        rpmRows: root.rpmRows
        // Finer/Coarser annotation is burr-position semantics.
        finerHint: root.grindStep > 0 && root.grindSetting.length > 0
        onGrindPicked: function(v) { root.applyGrind(v) }
        onRpmPicked: function(v) { root.applyRpm(v) }
    }
}
